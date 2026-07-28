#include "pch.h"
#include "App.h"
#include "MainWindow.h"
#include "AppState.h"
#include "localization.h"
#include "utils.h"
#include "constants.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <Microsoft.UI.Xaml.Window.h>
#include <fstream>
#include <filesystem>
#include <cwchar>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

static void DbgLog(const wchar_t* msg)
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    auto logPath = std::filesystem::path(path).parent_path() / L"debug.log";
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

        window = make<MainWindow>();
        DbgLog(L"App: MainWindow created");
        if (!trayOnly) {
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

LONG WINAPI MyExceptionFilter(PEXCEPTION_POINTERS)
{
    DbgLog(L"!!! FATAL SEH EXCEPTION CAUGHT !!!");
    MessageBoxW(nullptr, ::AgentRedactor::LocString(L"FatalError_Message").c_str(), ::AgentRedactor::LocString(L"FatalError_Caption").c_str(), MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetUnhandledExceptionFilter(MyExceptionFilter);

    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        auto logPath = std::filesystem::path(path).parent_path() / L"debug.log";
        // When installed as MSIX the install directory is read-only; never throw here.
        std::error_code ec;
        std::filesystem::remove(logPath, ec);
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
