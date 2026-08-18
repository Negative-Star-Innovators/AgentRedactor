#include "pch.h"
#include "App.h"
#include "MainWindow.h"
#include "AppState.h"
#include "localization.h"
#include "settings_manager.h"
#include "utils.h"
#include "constants.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <Microsoft.UI.Xaml.Window.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <cwchar>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

static void DbgLog(const wchar_t* msg)
{
    // Under %APPDATA%\AgentRedactor (writable on every install — the MSIX
    // install dir is read-only, so the old exe-dir debug.log silently did not
    // exist for packaged builds and crash details were lost).
    std::error_code ec;
    auto logDir = ::AgentRedactor::Utils::GetAppDataPath();
    std::filesystem::create_directories(logDir, ec);
    auto logPath = logDir / L"debug.log";
    std::wofstream f(logPath, std::ios::app);
    if (f) f << msg << L"\n";
}

namespace winrt::AgentRedactor::implementation
{
    App::App()
    {
        InitializeComponent();

        UnhandledException([](auto&&, UnhandledExceptionEventArgs const& e) {
            DbgLog(L"!!! XAML UnhandledException !!!");
            auto msg = e.Message();
            DbgLog(msg.c_str());
            e.Handled(true);
        });
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        DbgLog(L"App: OnLaunched");
        bool trayOnly = false;
        if (auto cmd = GetCommandLineW()) {
            if (wcsstr(cmd, L"--tray-only") != nullptr) {
                trayOnly = true;
                DbgLog(L"App: tray-only launch");
            }
        }
        // Re-apply the language override immediately before any XAML is parsed;
        // the ResourceContext used by WinUI 3 XAML may differ from the one used
        // earlier in AppState::Initialize.
        ::AgentRedactor::InitializeLocalization();

        // When the blocking first-run model download is pending the window
        // must appear even for a --tray-only launch: the modal download dialog
        // hosts on it and the app cannot serve traffic until it completes.
        bool modelDownloadBlocking = false;
        if (auto appState = ::AgentRedactor::AppState::Instance()) {
            modelDownloadBlocking = appState->IsModelDownloadRequired();
        }

        window = make<MainWindow>();
        DbgLog(L"App: MainWindow created");
        if (!trayOnly || modelDownloadBlocking) {
            window.Activate();
            DbgLog(L"App: window activated");
        }

        if (::AgentRedactor::AppState::Instance()) {
            try {
                auto windowNative = window.try_as<::IWindowNative>();
                if (windowNative) {
                    HWND hwnd = nullptr;
                    windowNative->get_WindowHandle(&hwnd);
                    DbgLog(L"App: SetMainWindow hwnd");
                    ::AgentRedactor::AppState::Instance()->SetMainWindow(hwnd);
                }
            } catch (...) {}
        }
        DbgLog(L"App: OnLaunched done");
    }
}

LONG WINAPI MyExceptionFilter(PEXCEPTION_POINTERS info)
{
    DbgLog(L"!!! FATAL SEH EXCEPTION CAUGHT !!!");
    if (info) {
        wchar_t buf[512];
        swprintf_s(buf, L"code: 0x%08X at address: 0x%p",
            info->ExceptionRecord->ExceptionCode,
            info->ExceptionRecord->ExceptionAddress);
        DbgLog(buf);
        HMODULE self = GetModuleHandleW(nullptr);
        swprintf_s(buf, L"module base: 0x%p, offset: 0x%llX",
            self, reinterpret_cast<ULONG_PTR>(info->ExceptionRecord->ExceptionAddress)
                - reinterpret_cast<ULONG_PTR>(self));
        DbgLog(buf);
        if (info->ContextRecord) {
#if defined(_M_X64)
            swprintf_s(buf, L"regs: rax=0x%llX rbx=0x%llX rcx=0x%llX rdx=0x%llX rsp=0x%llX rbp=0x%llX rsi=0x%llX rdi=0x%llX rip=0x%llX",
                info->ContextRecord->Rax, info->ContextRecord->Rbx,
                info->ContextRecord->Rcx, info->ContextRecord->Rdx,
                info->ContextRecord->Rsp, info->ContextRecord->Rbp,
                info->ContextRecord->Rsi, info->ContextRecord->Rdi,
                info->ContextRecord->Rip);
            DbgLog(buf);
#elif defined(_M_ARM64)
            swprintf_s(buf, L"regs: x0=0x%llX fp=0x%llX lr=0x%llX sp=0x%llX pc=0x%llX cpsr=0x%08X",
                info->ContextRecord->X[0], info->ContextRecord->Fp,
                info->ContextRecord->Lr, info->ContextRecord->Sp,
                info->ContextRecord->Pc,
                static_cast<DWORD>(info->ContextRecord->Cpsr));
            DbgLog(buf);
#endif
        }
        {
            const unsigned char* p = static_cast<const unsigned char*>(
                info->ExceptionRecord->ExceptionAddress);
            const unsigned char* q = p - 8;
            wchar_t bytes[128] = {};
            int used = 0;
            for (int i = 0; i < 16 && q; i++) {
                used += swprintf_s(bytes + used, 16, L"%02X ", q[i]);
            }
            DbgLog(bytes);
        }
        void* frames[32] = {};
        USHORT n = CaptureStackBackTrace(0, 32, frames, nullptr);
        for (USHORT i = 0; i < n; i++) {
            HMODULE mod = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                static_cast<LPCWSTR>(frames[i]), &mod);
            wchar_t modName[MAX_PATH] = L"?";
            if (mod) GetModuleFileNameW(mod, modName, MAX_PATH);
            swprintf_s(buf, L"  frame %u: 0x%p  %s", i, frames[i], modName);
            DbgLog(buf);
        }
    }
    MessageBoxW(nullptr, ::AgentRedactor::LocString(L"FatalError_Message").c_str(), ::AgentRedactor::LocString(L"FatalError_Caption").c_str(), MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR lpCmdLine, int)
{
    SetUnhandledExceptionFilter(MyExceptionFilter);

    // Headless test hook (both channels): load + migrate + save the settings
    // for the resolved config dir (AGENTREDACTOR_CONFIG_DIR honored), print a
    // machine-readable result, and exit. Deliberately runs before ANY other
    // init — no logging, no mutex, no WinUI/apartment — so pytest can drive
    // settings-migration tests without a GUI session.
    if (lpCmdLine && wcsstr(lpCmdLine, L"--selftest-migrate-settings") != nullptr) {
        try {
            ::AgentRedactor::SettingsManager settings({});
            printf("SETTINGS_MIGRATION_OK\n");
            return 0;
        } catch (const std::exception& e) {
            printf("SETTINGS_MIGRATION_FAIL %s\n", e.what());
            return 1;
        } catch (...) {
            printf("SETTINGS_MIGRATION_FAIL unknown error\n");
            return 1;
        }
    }

#ifdef AGENTREDACTOR_SELFRELEASE
    // Velopack invokes the app with --veloapp-* lifecycle arguments (install,
    // updated, obsolete, uninstall hooks); exit immediately so the installer
    // is never blocked by the single-instance mutex or the UI.
    if (lpCmdLine && wcsstr(lpCmdLine, L"--veloapp-") != nullptr) {
        return 0;
    }
#endif

    {
        // Keep the previous run's crash details: rotate debug.log ->
        // debug.prev.log instead of deleting it (the "Fatal error" dialog
        // points at debug.log, so wiping it on startup hid the crash). The
        // log lives under %APPDATA%\AgentRedactor (writable even for
        // packaged MSIX installs).
        std::error_code ec;
        auto logDir = ::AgentRedactor::Utils::GetAppDataPath();
        std::filesystem::create_directories(logDir, ec);
        std::filesystem::rename(logDir / L"debug.log", logDir / L"debug.prev.log", ec);
    }
    DbgLog(L"wWinMain: started");

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"AgentRedactor_WinUI3_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DbgLog(L"wWinMain: already running");
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 1;
    }
    DbgLog(L"wWinMain: mutex ok");

    std::filesystem::path exePath = ::AgentRedactor::Utils::GetExecutablePath();
    SetCurrentDirectoryW(exePath.c_str());
    DbgLog(L"wWinMain: cwd set");

    ::AgentRedactor::Utils::InitializeLogging({});
    DbgLog(L"wWinMain: logging initialized");

    winrt::init_apartment(winrt::apartment_type::single_threaded);
    DbgLog(L"wWinMain: apartment initialized");

    auto appState = std::make_unique<::AgentRedactor::AppState>();
    DbgLog(L"wWinMain: AppState allocated");
    if (!appState->Initialize({})) {
        DbgLog(L"wWinMain: AppState::Initialize FAILED");
        MessageBoxW(nullptr, ::AgentRedactor::LocString(L"InitializeFailed_Message").c_str(), ::AgentRedactor::LocString(L"InitializeFailed_Caption").c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }
    DbgLog(L"wWinMain: AppState initialized OK");
    g_appState = appState.get();

    using winrt::AgentRedactor::implementation::App;
    Application::Start([](auto&&) { make<App>(); });
    DbgLog(L"wWinMain: Application::Start returned");

    appState->Shutdown();
    g_appState = nullptr;
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    DbgLog(L"wWinMain: exiting cleanly");
    return 0;
}
