#include "pch.h"
#include "MainWindow.h"
#include "MainWindow.xaml.g.hpp"
#include "AppState.h"
#include "localization.h"
#include "logging.h"
#include "HomePage.h"
#include "SettingsPage.h"
#include "RegexPage.h"
#include "KeywordsPage.h"
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <Microsoft.UI.Xaml.Window.h>
#include <dwmapi.h>
#include <commctrl.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Navigation;

namespace winrt::AgentRedactor::implementation
{
    static bool IsSystemDarkMode()
    {
        HKEY hKey;
        DWORD value = 1; // default to light mode
        DWORD size = sizeof(value);
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(&value), &size);
            RegCloseKey(hKey);
        }
        return value == 0; // 0 = dark mode, 1 = light mode
    }

    static void ThemeContentDialog(ContentDialog const& dialog, bool dark)
    {
        auto bg = dark
            ? Windows::UI::ColorHelper::FromArgb(255, 30, 30, 30)
            : Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255);
        auto fg = dark
            ? Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255)
            : Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26);
        auto border = dark
            ? Windows::UI::ColorHelper::FromArgb(255, 68, 68, 68)
            : Windows::UI::ColorHelper::FromArgb(255, 224, 224, 224);
        dialog.Background(SolidColorBrush(bg));
        dialog.Foreground(SolidColorBrush(fg));
        dialog.BorderBrush(SolidColorBrush(border));
    }

    LRESULT CALLBACK MainWindow::SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        auto* self = reinterpret_cast<MainWindow*>(dwRefData);
        if (uMsg == WM_CLOSE && self && !self->allowClose_) {
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }
        if (uMsg == WM_NCDESTROY && self) {
            RemoveWindowSubclass(hWnd, &SubclassProc, uIdSubclass);
        }
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    void MainWindow::CloseWindow()
    {
        allowClose_ = true;
        this->Close();
    }

    winrt::fire_and_forget MainWindow::ShowQuitConfirmationAsync()
    {
        auto lifetime = get_strong();
        bool dark = IsSystemDarkMode();

        if (lifetime->hwnd_) {
            if (IsIconic(lifetime->hwnd_)) {
                ShowWindow(lifetime->hwnd_, SW_RESTORE);
            } else {
                ShowWindow(lifetime->hwnd_, SW_SHOW);
            }
            SetForegroundWindow(lifetime->hwnd_);
        }

        ContentDialog dialog;
        dialog.XamlRoot(lifetime->Content().XamlRoot());
        ThemeContentDialog(dialog, dark);
        dialog.Title(box_value(::AgentRedactor::LocString(L"Dialog_Quit_Title")));
        dialog.PrimaryButtonText(::AgentRedactor::LocString(L"Dialog_QuitButton"));
        dialog.CloseButtonText(::AgentRedactor::LocString(L"Dialog_CancelButton"));
        dialog.DefaultButton(ContentDialogButton::Close);

        auto fg = dark
            ? Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255)
            : Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26);

        TextBlock msg;
        msg.Text(::AgentRedactor::LocString(L"Dialog_Quit_Message"));
        msg.TextWrapping(TextWrapping::WrapWholeWords);
        msg.Foreground(SolidColorBrush(fg));
        dialog.Content(msg);

        auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::Primary) {
            lifetime->allowClose_ = true;
            lifetime->Close();
        }
    }

    void MainWindow::ApplyTitleBarTheme()
    {
        if (!hwnd_) return;

        auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(
            Microsoft::UI::WindowId{ reinterpret_cast<uint64_t>(hwnd_) });
        if (!appWindow) return;

        auto titleBar = appWindow.TitleBar();
        bool dark = IsSystemDarkMode();

        auto darkBg = Windows::UI::ColorHelper::FromArgb(255, 32, 32, 32);
        auto darkBtnHover = Windows::UI::ColorHelper::FromArgb(255, 50, 50, 50);
        auto darkBtnPressed = Windows::UI::ColorHelper::FromArgb(255, 60, 60, 60);
        auto lightBg = Windows::UI::ColorHelper::FromArgb(255, 243, 243, 243);
        auto lightBtnHover = Windows::UI::ColorHelper::FromArgb(255, 220, 220, 220);
        auto lightBtnPressed = Windows::UI::ColorHelper::FromArgb(255, 200, 200, 200);

        if (dark) {
            titleBar.BackgroundColor(darkBg);
            titleBar.ForegroundColor(Windows::UI::Colors::White());
            titleBar.InactiveBackgroundColor(darkBg);
            titleBar.InactiveForegroundColor(Windows::UI::Colors::Gray());
            titleBar.ButtonBackgroundColor(darkBg);
            titleBar.ButtonForegroundColor(Windows::UI::Colors::White());
            titleBar.ButtonHoverBackgroundColor(darkBtnHover);
            titleBar.ButtonHoverForegroundColor(Windows::UI::Colors::White());
            titleBar.ButtonPressedBackgroundColor(darkBtnPressed);
            titleBar.ButtonPressedForegroundColor(Windows::UI::Colors::White());
            titleBar.ButtonInactiveBackgroundColor(darkBg);
            titleBar.ButtonInactiveForegroundColor(Windows::UI::Colors::Gray());
        }
        else {
            titleBar.BackgroundColor(lightBg);
            titleBar.ForegroundColor(Windows::UI::Colors::Black());
            titleBar.InactiveBackgroundColor(lightBg);
            titleBar.InactiveForegroundColor(Windows::UI::Colors::Gray());
            titleBar.ButtonBackgroundColor(lightBg);
            titleBar.ButtonForegroundColor(Windows::UI::Colors::Black());
            titleBar.ButtonHoverBackgroundColor(lightBtnHover);
            titleBar.ButtonHoverForegroundColor(Windows::UI::Colors::Black());
            titleBar.ButtonPressedBackgroundColor(lightBtnPressed);
            titleBar.ButtonPressedForegroundColor(Windows::UI::Colors::Black());
            titleBar.ButtonInactiveBackgroundColor(lightBg);
            titleBar.ButtonInactiveForegroundColor(Windows::UI::Colors::Gray());
        }

        // Remove the border line under the title bar by matching it to title bar color
        COLORREF borderColor = dark
            ? RGB(darkBg.R, darkBg.G, darkBg.B)
            : RGB(lightBg.R, lightBg.G, lightBg.B);
        DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

        // Tell DWM to use immersive dark mode for the window frame
        BOOL useDarkMode = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
    }

    void MainWindow::OnColorValuesChanged(Windows::UI::ViewManagement::UISettings const&, Windows::Foundation::IInspectable const&)
    {
        DispatcherQueue().TryEnqueue([this]() {
            ApplyTitleBarTheme();
        });
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();

        try {
            auto windowNative = this->try_as<::IWindowNative>();
            if (windowNative) {
                windowNative->get_WindowHandle(&hwnd_);
            }
        }
        catch (...) {}

        if (hwnd_) {
            // Set fox icon
            HICON hIcon = static_cast<HICON>(LoadImageW(
                nullptr, L"app.ico", IMAGE_ICON, 32, 32,
                LR_LOADFROMFILE));
            if (!hIcon) {
                hIcon = static_cast<HICON>(LoadImageW(
                    nullptr, L"resources/app.ico", IMAGE_ICON, 32, 32,
                    LR_LOADFROMFILE));
            }
            if (hIcon) {
                SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
                SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
            }

            auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(
                Microsoft::UI::WindowId{ reinterpret_cast<uint64_t>(hwnd_) });

            if (appWindow) {
                int dpi = GetDpiForWindow(hwnd_);
                if (dpi == 0) dpi = 96;
                int width = MulDiv(1000, dpi, 96);
                int height = MulDiv(900, dpi, 96);
                appWindow.Resize({ width, height });
                appWindow.Title(::AgentRedactor::LocString(L"AppDisplayName"));
                ApplyTitleBarTheme();

                auto displayArea = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
                    Microsoft::UI::WindowId{ reinterpret_cast<uint64_t>(hwnd_) },
                    Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
                if (displayArea) {
                    auto work = displayArea.WorkArea();
                    int32_t x = work.X + (work.Width - width) / 2;
                    int32_t y = work.Y + (work.Height - height) / 2;
                    if (y + height > work.Y + work.Height) y = work.Y + work.Height - height;
                    if (y < work.Y) y = work.Y;
                    appWindow.Move({ x, y });
                }
            }

            SetWindowSubclass(hwnd_, &SubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
        }

        auto app = ::AgentRedactor::AppState::Instance();
        if (app) {
            app->SetMainWindowCloseCallback([this, app]() {
                if (app->IsRestarting()) {
                    this->DispatcherQueue().TryEnqueue([this]() {
                        this->CloseWindow();
                    });
                    return;
                }
                this->DispatcherQueue().TryEnqueue([this]() {
                    this->ShowQuitConfirmationAsync();
                });
            });
            app->SetLocalizationReloadCallback([this]() {
                LOG(L"[MainWindow] Localization reload callback invoked");
                this->DispatcherQueue().TryEnqueue([this]() {
                    this->ReloadLocalization();
                });
            });
        }

        colorValuesChangedToken_ = uiSettings_.ColorValuesChanged({ this, &MainWindow::OnColorValuesChanged });

        auto frame = Frame();
        frame.Padding(Thickness{ 0 });
        ::AgentRedactor::ApplyCurrentFlowDirection(frame);
        frame.NavigationFailed([](IInspectable const&, Navigation::NavigationFailedEventArgs const& args) {
            try {
                auto hr = args.Exception();
                ::AgentRedactor::Utils::LogMessage(L"NavigationFailed: hresult=" + std::to_wstring(static_cast<int32_t>(hr)));
            } catch (...) {
                ::AgentRedactor::Utils::LogMessage(L"NavigationFailed: unknown error");
            }
        });
        Content(frame);
        frame.Navigate(xaml_typename<AgentRedactor::HomePage>());
    }

    void MainWindow::ReloadLocalization()
    {
        LOG(L"[MainWindow] ReloadLocalization started");

        // Update the window title.
        auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(
            Microsoft::UI::WindowId{ reinterpret_cast<uint64_t>(hwnd_) });
        if (appWindow) {
            appWindow.Title(::AgentRedactor::LocString(L"AppDisplayName"));
            LOG(L"[MainWindow] Window title updated");
        } else {
            LOG(L"[MainWindow] AppWindow was null");
        }

        // Retrieve the Frame that was set as the window content.
        auto frame = Content().try_as<Microsoft::UI::Xaml::Controls::Frame>();
        if (!frame) {
            LOG(L"[MainWindow] No Frame found in window content");
            return;
        }

        // Re-localize whichever page is currently in the frame.
        if (auto content = frame.Content()) {
            if (auto home = content.try_as<winrt::AgentRedactor::HomePage>()) {
                winrt::get_self<winrt::AgentRedactor::implementation::HomePage>(home)->LocalizeStaticUI();
                LOG(L"[MainWindow] HomePage localized");
            } else if (auto settings = content.try_as<winrt::AgentRedactor::SettingsPage>()) {
                winrt::get_self<winrt::AgentRedactor::implementation::SettingsPage>(settings)->LocalizeStaticUI();
                LOG(L"[MainWindow] SettingsPage localized");
            } else if (auto regex = content.try_as<winrt::AgentRedactor::RegexPage>()) {
                winrt::get_self<winrt::AgentRedactor::implementation::RegexPage>(regex)->LocalizeStaticUI();
                LOG(L"[MainWindow] RegexPage localized");
            } else if (auto keywords = content.try_as<winrt::AgentRedactor::KeywordsPage>()) {
                winrt::get_self<winrt::AgentRedactor::implementation::KeywordsPage>(keywords)->LocalizeStaticUI();
                LOG(L"[MainWindow] KeywordsPage localized");
            } else {
                LOG(L"[MainWindow] Unknown page type in frame");
            }
            ::AgentRedactor::ApplyCurrentFlowDirection(content);
        } else {
            LOG(L"[MainWindow] Frame content was null");
        }
        ::AgentRedactor::ApplyCurrentFlowDirection(frame);

        LOG(L"[MainWindow] ReloadLocalization complete");
    }
}
