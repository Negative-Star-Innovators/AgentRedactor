#pragma once

// Windows Hello (UserConsentVerifier) consent prompts for the engine host and
// the WinUI GUI. Kept out of the OS-agnostic core: the CLI talks to the engine
// over the control API, so only the engine process needs WinRT here. The GUI
// compiles this same TU (with PrecompiledHeader NotUsing) so its prompts are
// in-process too. The caller passes the owning HWND (the app's main window, or
// the CLI's console window when the engine prompts on its behalf); the prompt
// is then attached to that window so it reliably comes to the foreground.
//
// Two entry points:
// - RequestHelloUnlock (blocking, watchdog): used by the engine's control-API
//   handlers (CLI requests), which run on worker threads and must block. A
//   watchdog (default 60 s) guarantees a script can never hang on the prompt.
// - RequestHelloUnlockAsync (coroutine): used by the GUI, where the prompt is
//   created on the UI thread (the interop operation is window-bound) and the
//   caller just awaits the outcome without blocking its thread. NO watchdog by
//   default: the prompt waits for the user like a lock screen (the user
//   closing it cancels). The optional shared cancel flag lets the caller
//   dismiss the prompt early — the window hiding or being destroyed — so no
//   orphaned system dialog survives the window.
//
// Test harnesses can force a fail-fast prompt timeout for either entry point
// via AGENTREDACTOR_HELLO_TIMEOUT_MS: when set, the prompt is cancelled after
// the delay; unset keeps the default behaviour above.
//
// AGENTREDACTOR_HELLO_SUPPRESS_PROMPT (test-only): makes every prompt behave
// exactly as if the user cancelled it — no system UI, never Verified. It is
// cancel-equivalent and grants nothing (see the invariant test
// test_hello_suppress_prompt_flag_never_grants_access in tests/cli). Never
// change it to return Verified or to reach the engine's real unlock.

#include <atomic>
#include <memory>
#include <string>

#include <winrt/Windows.Foundation.h>

// Windows handle type without dragging <windows.h> into every consumer.
struct HWND__;
using HWND = HWND__*;

namespace AgentRedactor {

enum class HelloUnlockResult {
    Verified,        // User verified with Windows Hello
    Canceled,        // User dismissed the consent prompt
    RetriesExhausted, // Too many failed verification attempts
    Unavailable,     // Hello not configured / no hardware / device busy
    TimedOut,        // Blocking prompt watchdog fired (no answer in time)
    Failed,          // Unexpected error
};

// True when UserConsentVerifier can show a prompt for the current user.
bool IsWindowsHelloAvailable();

// Shows the Windows Hello consent prompt, owned by `hwnd` when non-null (so
// the system UI comes to the foreground of that window) or as unowned system
// UI when null. Never throws. Blocks up to the watchdog timeout.
HelloUnlockResult RequestHelloUnlock(HWND hwnd, const std::wstring& message);

// Coroutine form for UI callers: the operation is created on the calling
// thread (the window's owning thread for the GUI), so the consent dialog
// stays responsive, and the coroutine resumes there too. Returns true only
// when the user verified with Windows Hello; every other outcome (canceled,
// unavailable, error) is false. Never throws. `cancel`, when non-null, is a
// shared flag the caller may set to dismiss the prompt early (window
// hide/destroy); without it (and without AGENTREDACTOR_HELLO_TIMEOUT_MS) the
// prompt waits for the user indefinitely.
winrt::Windows::Foundation::IAsyncOperation<bool>
RequestHelloUnlockAsync(HWND hwnd, const std::wstring& message,
    std::shared_ptr<std::atomic<bool>> cancel = nullptr);

} // namespace AgentRedactor
