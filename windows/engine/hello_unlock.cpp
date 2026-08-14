#include "hello_unlock.h"

#include <windows.h>
#include <roapi.h>
#include <stdlib.h>
#include <winstring.h>
#include <UserConsentVerifierInterop.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <winrt/Windows.Security.Credentials.UI.h>

using namespace winrt::Windows::Security::Credentials::UI;

namespace {

std::once_flag s_apartmentOnce;

void EnsureApartment() {
    std::call_once(s_apartmentOnce, []() {
        try {
            // CheckAvailability(Async) works from any apartment; the consent
            // prompt itself is system UI. A multi-threaded apartment avoids
            // any dependency on a message pump on this thread.
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            // COM/WinRT unavailable; the callers report Unavailable/Failed.
        }
    });
}

using UserConsentResultAsync =
    winrt::Windows::Foundation::IAsyncOperation<UserConsentVerificationResult>;

// The consent prompt waits for the user. A watchdog guarantees the caller can
// never hang: after the timeout the operation is cancelled and the caller gets
// Unavailable. AGENTREDACTOR_HELLO_TIMEOUT_MS lets test harnesses shorten it.
std::chrono::milliseconds HelloTimeout() {
    if (const wchar_t* v = _wgetenv(L"AGENTREDACTOR_HELLO_TIMEOUT_MS")) {
        try {
            const long long ms = _wtoi64(v);
            if (ms > 0 && ms < 3600000) return std::chrono::milliseconds(ms);
        } catch (...) {
        }
    }
    return std::chrono::milliseconds(60000);
}

// Test-only affordance: AGENTREDACTOR_HELLO_SUPPRESS_PROMPT makes every
// consent prompt behave EXACTLY as if the user cancelled it - no system UI
// is ever shown and the result can NEVER be Verified.
//
// SECURITY INVARIANT (tested against the release binary in the CLI suite,
// test_hello_suppress_prompt_flag_never_grants_access): this flag must stay
// cancel-equivalent. It grants nothing - canceling was always available to
// any user - so it cannot be abused. Any change that lets it yield Verified
// or reach the engine's real unlock is a critical vulnerability.
bool HelloPromptSuppressed() {
    const wchar_t* v = _wgetenv(L"AGENTREDACTOR_HELLO_SUPPRESS_PROMPT");
    if (!v) return false;
    const std::wstring s(v);
    if (s == L"1" || s == L"true" || s == L"yes" || s == L"on") return true;
    std::wstring lower = s;
    for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    return lower == L"true" || lower == L"yes" || lower == L"on";
}

// RequestVerificationForWindowAsync attaches the consent prompt to `hwnd` so
// it reliably appears in the foreground of that window. The interop interface
// lives in the desktop SDK (um/UserConsentVerifierInterop.h); C++/WinRT has no
// projected class for it, so we reach the activation factory's interop vtable
// directly. The call must be made on the thread that owns the window (the GUI
// does this via RequestHelloUnlockAsync; the engine falls back to an unowned
// prompt when the interop call fails from a worker thread).
//
// The COM calls are wrapped in SEH (raw COM only — no objects that need
// unwinding between __try and __except): a misbehaving HWND or a broken
// consent service must surface as a normal failure (E_FAIL -> fall back to
// the unowned prompt), never as an access violation that takes the app down.
// Raw-COM core of the windowed prompt, wrapped in SEH (no C++ objects that
// need unwinding, no throw): a misbehaving HWND or a broken consent service
// must surface as a normal HRESULT failure (-> fall back to the unowned
// prompt), never as an access violation that takes the app down.
static HRESULT CreateWindowedVerification(HWND hwnd, const std::wstring& message, ::IUnknown** out) {
    *out = nullptr;
    HRESULT hr = E_FAIL;
    __try {
        ::IUserConsentVerifierInterop* interop = nullptr;
        HSTRING className = nullptr;
        HSTRING msgH = nullptr;
        void* opPtr = nullptr;
        hr = ::WindowsCreateString(L"Windows.Security.Credentials.UI.UserConsentVerifier", 47, &className);
        if (SUCCEEDED(hr)) {
            hr = ::RoGetActivationFactory(className,
                __uuidof(::IUserConsentVerifierInterop),
                reinterpret_cast<void**>(&interop));
        }
        if (className) { ::WindowsDeleteString(className); className = nullptr; }
        if (SUCCEEDED(hr)) {
            hr = ::WindowsCreateString(message.c_str(), static_cast<UINT32>(message.size()), &msgH);
        }
        if (SUCCEEDED(hr)) {
            hr = interop->RequestVerificationForWindowAsync(
                hwnd, msgH, winrt::guid_of<UserConsentResultAsync>(), &opPtr);
        }
        if (msgH) { ::WindowsDeleteString(msgH); msgH = nullptr; }
        if (interop) { interop->Release(); interop = nullptr; }
        if (SUCCEEDED(hr) && opPtr) {
            *out = static_cast<::IUnknown*>(opPtr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
    return hr;
}

UserConsentResultAsync RequestVerificationWithWindow(HWND hwnd, const std::wstring& message) {
    ::IUnknown* raw = nullptr;
    const HRESULT hr = CreateWindowedVerification(hwnd, message, &raw);
    if (FAILED(hr) || !raw) {
        throw winrt::hresult_error(E_FAIL);
    }
    winrt::com_ptr<::IUnknown> opUnknown;
    opUnknown.attach(raw);
    return opUnknown.as<UserConsentResultAsync>();
}

// Starts the consent operation for `hwnd` (window-owned via the interop when
// possible) and the plain API as a fallback.
UserConsentResultAsync StartOperation(HWND hwnd, const std::wstring& message) {
    if (hwnd) {
        try {
            return RequestVerificationWithWindow(hwnd, message);
        } catch (...) {
            // Interop unavailable from this thread/context; fall through to
            // the unowned prompt rather than failing outright.
        }
    }
    return UserConsentVerifier::RequestVerificationAsync(winrt::hstring(message));
}

::AgentRedactor::HelloUnlockResult MapResult(UserConsentVerificationResult r) {
    switch (r) {
    case UserConsentVerificationResult::Verified:
        return ::AgentRedactor::HelloUnlockResult::Verified;
    case UserConsentVerificationResult::Canceled:
        return ::AgentRedactor::HelloUnlockResult::Canceled;
    case UserConsentVerificationResult::RetriesExhausted:
        return ::AgentRedactor::HelloUnlockResult::RetriesExhausted;
    default:
        // DeviceNotPresent, NotConfiguredForUser, DeviceBusy, DisabledByPolicy
        return ::AgentRedactor::HelloUnlockResult::Unavailable;
    }
}

} // namespace

namespace AgentRedactor {

bool IsWindowsHelloAvailable() {
    EnsureApartment();
    try {
        return UserConsentVerifier::CheckAvailabilityAsync().get()
            == UserConsentVerifierAvailability::Available;
    } catch (...) {
        return false;
    }
}

HelloUnlockResult RequestHelloUnlock(HWND hwnd, const std::wstring& message) {
    EnsureApartment();
    // Test hook: never show the prompt, never verify (cancel-equivalent).
    if (HelloPromptSuppressed()) {
        return HelloUnlockResult::Canceled;
    }
    try {
        if (!IsWindowsHelloAvailable()) {
            return HelloUnlockResult::Unavailable;
        }
        auto operation = StartOperation(hwnd, message);
        if (operation.wait_for(HelloTimeout())
            != winrt::Windows::Foundation::AsyncStatus::Completed) {
            try {
                operation.Cancel();
            } catch (...) {
            }
            return HelloUnlockResult::Unavailable;
        }
        return MapResult(operation.GetResults());
    } catch (...) {
        return HelloUnlockResult::Failed;
    }
}

winrt::Windows::Foundation::IAsyncOperation<bool>
RequestHelloUnlockAsync(HWND hwnd, const std::wstring& message) {
    // Test hook: never show the prompt, never verify (cancel-equivalent).
    if (HelloPromptSuppressed()) {
        co_return false;
    }
    try {
        if (!IsWindowsHelloAvailable()) {
            co_return false;
        }

        // Created on the calling (UI) thread so the window-owned prompt is
        // set up where the window's message pump lives.
        auto operation = std::make_shared<UserConsentResultAsync>(StartOperation(hwnd, message));
        auto done = std::make_shared<std::atomic<bool>>(false);

        // Watchdog: cancel the prompt after the timeout. A detached thread
        // (NOT a coroutine — the earlier fire_and_forget + resume_after
        // watchdog raced its own frame teardown and crashed with a
        // use-after-free at the Cancel path, see the 0x930EE fault) holds the
        // operation and the flag by shared ownership, so the Cancel can never
        // touch freed memory: it either sees done=true (the awaiter already
        // finished) or the operation is still alive.
        std::thread([operation, done]() {
            std::this_thread::sleep_for(HelloTimeout());
            if (!done->exchange(true)) {
                try {
                    operation->Cancel();
                } catch (...) {
                }
            }
        }).detach();

        bool verified = false;
        try {
            const auto r = co_await *operation;
            if (!done->exchange(true)) {
                verified = MapResult(r) == ::AgentRedactor::HelloUnlockResult::Verified;
            }
        } catch (const winrt::hresult_canceled&) {
            done->exchange(true);
            verified = false;
        } catch (...) {
            done->exchange(true);
            verified = false;
        }
        co_return verified;
    } catch (...) {
        co_return false;
    }
}

} // namespace AgentRedactor
