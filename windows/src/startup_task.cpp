// Windows logon-startup registration.
//
// Two builds, two mechanisms:
//   - Packaged (MSIX identity): the package manifest declares a
//     windows.startupTask ("AgentRedactorStartup", Enabled="true" in
//     windows/Package.appxmanifest), so the OS launches AgentRedactorUI.exe at
//     logon through the startup-task machinery. The HKCU Run key does not
//     drive boot startup there — only the StartupTask API (or the user's
//     Task Manager / Settings -> Startup toggle) controls it, and writing the
//     Run key while ignoring the task is exactly the bug where unchecking
//     "start on boot" in the tray changed the setting but the app still
//     started at logon.
//   - Unpackaged (Velopack): no manifest task; the HKCU Run value is the
//     mechanism (value "<exe>" --tray-only under
//     HKCU\Software\Microsoft\Windows\CurrentVersion\Run).
//
// Keep the WinRT calls out of inline header code: blocking .get() on the
// async StartupTask API cannot be used in header-inline functions compiled
// into many TUs (C3779), and this TU is GUI-build-only — the engine never
// calls RegisterStartupTask/UnregisterStartupTask.

#include "constants.h"

#include <appmodel.h>
// Windows.ApplicationModel.h only pulls the impl/*.2.h declaration chain; the
// blocking IAsyncOperation::get()/wait_for() bodies live in this umbrella (the
// same include that hello_unlock.h relies on). Without it these TUs fail with
// C3779 ("auto cannot be used before it is defined").
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>

#include <string>

namespace {

bool HasPackageIdentity() {
    UINT32 len = 0;
    return GetCurrentPackageFullName(&len, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

void SetStartupTaskEnabled(bool enabled) {
    if (HasPackageIdentity()) {
        // Packaged build: drive the manifest startup task. RequestEnableAsync
        // shows no consent dialog for packaged desktop apps, and per the API
        // contract it cannot override a task the user disabled via Task
        // Manager / Settings -> Startup (a user-triggered disable stays
        // disabled). GetAsync throws when the TaskId does not match the
        // manifest; that is a packaging bug, not something to paper over.
        try {
            auto task = winrt::Windows::ApplicationModel::StartupTask::GetAsync(
                L"AgentRedactorStartup").get();
            if (enabled) {
                task.RequestEnableAsync().get();
            } else {
                task.Disable();
            }
        } catch (...) {
        }
        return;
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enabled) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring command = std::wstring(L"\"") + path + L"\" --tray-only";
            RegSetValueExW(hKey, AgentRedactor::APP_NAME, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, AgentRedactor::APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

} // namespace

void RegisterStartupTask() { SetStartupTaskEnabled(true); }
void UnregisterStartupTask() { SetStartupTaskEnabled(false); }