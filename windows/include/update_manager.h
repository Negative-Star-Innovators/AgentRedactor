#pragma once

#include <functional>
#include <string>
#include <winrt/Microsoft.UI.Xaml.h>

namespace AgentRedactor {
namespace UpdateManager {

// True only in the self-release (Velopack) channel. Every function below is a
// no-op in the Store/MSIX build, which never defines AGENTREDACTOR_SELFRELEASE,
// so callers never need #ifdefs of their own.
constexpr bool IsSelfReleaseBuild() {
#ifdef AGENTREDACTOR_SELFRELEASE
    return true;
#else
    return false;
#endif
}

// Marshals an action onto the UI thread, handing it the window's XamlRoot so
// it can show a ContentDialog. Registered once by MainWindow; until then (or
// while the window is hidden) dialogs are silently skipped.
using UiAction = std::function<void(const winrt::Microsoft::UI::Xaml::XamlRoot&)>;
using UiDispatch = std::function<void(UiAction)>;
void SetUiDispatch(UiDispatch dispatch);

enum class CheckResult { UpdateReady, UpToDate, Error };

// Startup flow: check the update feed, download any newer release package,
// then prompt "restart now / later". Runs on its own thread; never throws and
// never blocks startup. No-op in the Store build.
void CheckAndDownloadInBackground();

// Settings-page flow: same check + download pipeline, but reports the outcome
// through `completion` (invoked on the UI thread) instead of staying silent.
// When an update was downloaded the restart prompt is shown as usual.
void CheckNowInteractive(std::function<void(CheckResult)> completion);

// Registers a hook run on the UI thread at the start of
// ApplyDownloadedUpdateAndExit, before Update.exe is launched. The GUI uses it
// to stop the engine and wait for its exit: Update.exe's --waitPid only covers
// this process, but the file swap fails while the engine still has the old
// binaries mapped, and a lingering engine teardown delays the mutex release
// that the post-apply relaunch waits on. Optional; never invoked in the Store
// build (which never applies updates this way).
void SetPreApplyHook(std::function<void()> hook);

} // namespace UpdateManager
} // namespace AgentRedactor
