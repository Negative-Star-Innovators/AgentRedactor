#include "hello_unlock.h"

#include <windows.h>
#include <roapi.h>
#include <stdlib.h>
#include <winstring.h>
#include <UserConsentVerifierInterop.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cwctype>
#include <memory>
#include <mutex>
#include <optional>
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

// The blocking engine path must never hang a script: the consent prompt waits
// for the user, so a watchdog guarantees the caller fails fast (default 60 s).
// The GUI async path deliberately has NO watchdog by default — the prompt is a
// lock screen and may wait indefinitely; the only early cancels there are the
// caller's shared cancel flag (window hide/destroy) and the override below.
// AGENTREDACTOR_HELLO_TIMEOUT_MS (when set) cancels any prompt after the delay
// so test harnesses fail fast instead of waiting for a human.
std::optional<std::chrono::milliseconds> HelloTimeoutOverride() {
    const wchar_t* v = _wgetenv(L"AGENTREDACTOR_HELLO_TIMEOUT_MS");
    if (!v) return std::nullopt;
    try {
        const long long ms = _wtoi64(v);
        if (ms > 0 && ms < 3600000) return std::chrono::milliseconds(ms);
    } catch (...) {
    }
    return std::nullopt;
}

std::chrono::milliseconds HelloTimeout() {
    return HelloTimeoutOverride().value_or(std::chrono::milliseconds(60000));
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

// ---------------------------------------------------------------------------
// Window-attached consent prompt (foreground)
// ---------------------------------------------------------------------------
// The interop RequestVerificationForWindowAsync only succeeds when it runs on
// the thread that owns the target window. The engine's control-API handlers
// run on worker threads (and the caller's window belongs to another process
// anyway), so the prompt used to fall back to unowned system UI — which pops
// up in the BACKGROUND. To replicate what the GUI gets from its in-process
// prompt, the engine creates a hidden top-level window on its OWN thread and
// attaches the consent operation to it: the Windows Security dialog then
// comes to the foreground. Falls back to the unowned prompt if the window
// cannot be created. One short-lived thread per prompt; it exits once the
// operation completes (or the caller's watchdog cancels it).

namespace {

struct WindowedRequest {
    std::wstring message;
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;
    bool ok = false;
    UserConsentResultAsync op;
};

const wchar_t* kPromptWindowClass = L"AgentRedactorHelloPromptOwner";

LRESULT CALLBACK PromptWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_APP + 2) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RunWindowedPrompt(std::shared_ptr<WindowedRequest> req) {
    // Every thread must initialize its own apartment (EnsureApartment's
    // call_once only covered the first thread that ran it).
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    static std::once_flag registerOnce;
    std::call_once(registerOnce, []() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &PromptWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kPromptWindowClass;
        RegisterClassExW(&wc);
    });
    HWND wnd = CreateWindowExW(0, kPromptWindowClass, L"", 0, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)> wndGuard(wnd, &DestroyWindow);
    auto fail = [&]() {
        std::lock_guard<std::mutex> lock(req->mtx);
        req->ready = true;
        req->ok = false;
        req->cv.notify_all();
    };
    if (!wnd) {
        fail();
        return;
    }
    // The window is 1x1 and captionless — imperceptible — but VISIBLE and
    // made the active window (best effort): Windows only brings the consent
    // dialog to the foreground when it attaches to the window of the ACTIVE
    // application. When this runs inside the CLI process (the user's
    // terminal), the process IS the active application and the dialog comes
    // forward; inside the engine (a background process) it cannot.
    ::ShowWindow(wnd, SW_SHOWNA);
    ::SetForegroundWindow(wnd);
    try {
        req->op = RequestVerificationWithWindow(wnd, req->message);
    } catch (...) {
        fail();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(req->mtx);
        req->ready = true;
        req->ok = true;
        req->cv.notify_all();
    }
    // Pump until the operation finishes: the worker's watchdog cancels it on
    // timeout, and the Completed handler (fires on the operation's completion,
    // possibly on a threadpool thread) posts WM_APP+2 to exit the pump.
    req->op.Completed([wnd](const auto&, winrt::Windows::Foundation::AsyncStatus) {
        PostMessageW(wnd, WM_APP + 2, 0, 0);
    });
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Returns the window-attached operation, or nullopt when the machinery failed
// (the caller then falls back to the unowned prompt).
std::optional<UserConsentResultAsync> TryWindowedPrompt(const std::wstring& message) {
    auto req = std::make_shared<WindowedRequest>();
    req->message = message;
    std::thread(RunWindowedPrompt, req).detach();
    std::unique_lock<std::mutex> lock(req->mtx);
    if (!req->cv.wait_for(lock, std::chrono::seconds(10), [&] { return req->ready; })) {
        return std::nullopt;
    }
    if (!req->ok) return std::nullopt;
    return req->op;
}

} // namespace

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
        // Best-effort foreground of the caller's window (harmless; the real
        // foregrounding is the window-attached prompt below).
        if (hwnd) {
            if (::IsIconic(hwnd)) ::ShowWindow(hwnd, SW_RESTORE);
            ::ShowWindow(hwnd, SW_SHOW);
            if (!::SetForegroundWindow(hwnd)) {
                FLASHWINFO fi{};
                fi.cbSize = sizeof(fi);
                fi.hwnd = hwnd;
                fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
                fi.uCount = 0;
                ::FlashWindowEx(&fi);
            }
        }
        // Window-attached prompt on an engine-owned window: the Windows
        // Security dialog comes to the FOREGROUND (the unowned fallback would
        // pop up in the background). Falls back to the unowned prompt when the
        // window machinery fails.
        if (auto windowed = TryWindowedPrompt(message)) {
            if (windowed->wait_for(HelloTimeout())
                != winrt::Windows::Foundation::AsyncStatus::Completed) {
                try {
                    windowed->Cancel();
                } catch (...) {
                }
                // The user never answered within the watchdog: distinct from
                // Unavailable (no Hello on the device), so callers can report
                // a timeout instead of a misleading "not available" error.
                return HelloUnlockResult::TimedOut;
            }
            return MapResult(windowed->GetResults());
        }
        auto operation = StartOperation(hwnd, message);
        if (operation.wait_for(HelloTimeout())
            != winrt::Windows::Foundation::AsyncStatus::Completed) {
            try {
                operation.Cancel();
            } catch (...) {
            }
            return HelloUnlockResult::TimedOut;
        }
        return MapResult(operation.GetResults());
    } catch (...) {
        return HelloUnlockResult::Failed;
    }
}

winrt::Windows::Foundation::IAsyncOperation<bool>
RequestHelloUnlockAsync(HWND hwnd, const std::wstring& message,
                        std::shared_ptr<std::atomic<bool>> cancel) {
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

        // No timeout by default: the prompt waits for the user like a lock
        // screen, and the user closing it cancels. Early cancels: the caller's
        // shared flag (the window hid or is being destroyed — an orphaned
        // system dialog must never survive it) and the optional test-harness
        // timeout override. The canceller is a detached std::thread (NOT a
        // coroutine — the earlier fire_and_forget + resume_after watchdog
        // raced its own frame teardown and crashed with a use-after-free at
        // the Cancel path, see the 0x930EE fault) holding the operation and
        // the flag by shared ownership, so the Cancel can never touch freed
        // memory: it either sees done=true (the awaiter already finished) or
        // the operation is still alive.
        const auto timeoutOverride = HelloTimeoutOverride();
        if (cancel || timeoutOverride.has_value()) {
            std::thread([operation, done, cancel, timeoutOverride]() {
                const auto deadline = timeoutOverride.has_value()
                    ? std::chrono::steady_clock::now() + *timeoutOverride
                    : std::chrono::steady_clock::time_point::max();
                while (!done->load(std::memory_order_acquire)) {
                    if (cancel && cancel->load(std::memory_order_acquire)) break;
                    if (std::chrono::steady_clock::now() >= deadline) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (!done->exchange(true)) {
                    try {
                        operation->Cancel();
                    } catch (...) {
                    }
                }
            }).detach();
        }

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
