#include "pch.h"
#include "MainWindow.h"
#include "MainWindow.xaml.g.hpp"
#include "AppState.h"
#include "localization.h"
#include "logging.h"
#include "update_manager.h"
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
#include <algorithm>

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

        auto result = ContentDialogResult::None;
        try {
            result = co_await dialog.ShowAsync();
        } catch (...) {
            // Another ContentDialog (e.g. the blocking first-run download
            // dialog) is open; WinUI allows only one at a time. Never let the
            // failure escape into the XAML framework.
            co_return;
        }
        if (result == ContentDialogResult::Primary) {
            lifetime->allowClose_ = true;
            lifetime->Close();
        }
    }

    // Shows / updates / dismisses the blocking first-run model-download dialog
    // from the current AppState download status. Runs on the UI thread. While
    // AppState reports the download as required the app is blocked (no proxy
    // servers), so the dialog is modal and cannot be dismissed: it closes only
    // after the download and detector initialization succeed.
    //
    // Safety rules (violating these crashed self-release builds with a stowed
    // 0xc000027b inside Microsoft.UI.Xaml.dll):
    //  - NO ProgressBar in the dialog content: instantiating one inside this
    //    ContentDialog fail-fasted XAML during open (combase 0x800F1000 "no
    //    installed components") on Windows 11 24H2 + WindowsAppSDK 1.6
    //    self-contained. Progress is reported as text instead.
    //  - ShowAsync is called exactly once per dialog instance and is always
    //    awaited inside a try/catch; failures are logged, never thrown into
    //    the XAML framework.
    //  - Never two ContentDialogs at once: the update-manager prompt is
    //    deferred while this dialog exists, and this dialog is only (re)built
    //    after the previous instance fully closed.
    //  - Nothing here runs inside the window's Activated callback directly;
    //    callers post onto the dispatcher (Low priority) so activation and
    //    the first frame complete before any dialog work.
    void MainWindow::UpdateModelDownloadDialog()
    {
        try {
            auto app = ::AgentRedactor::AppState::Instance();
            if (!app) return;

            bool required = app->IsModelDownloadRequired();
            bool inProgress = app->IsModelDownloadInProgress();
            bool failed = app->HasModelDownloadFailed();

            if (!required && !inProgress && !failed) {
                // Download finished (or was never needed): close the dialog.
                // The member references are dropped and any deferred update
                // prompt is run from the ShowModelDownloadDialogAsync
                // continuation once the dialog is fully closed.
                if (modelDialog_) {
                    try { modelDialog_.Hide(); } catch (...) {}
                } else {
                    RunPendingUpdateUiAction();
                }
                return;
            }

            // A ContentDialog needs the XamlRoot of a visible window; status
            // changes, the Activated handler and this retry timer re-trigger
            // this once it is shown.
            if (!hwnd_ || !IsWindowVisible(hwnd_)) {
                ScheduleModelDialogRetry();
                return;
            }

            if (!modelDialog_) {
                auto xamlRoot = Content().XamlRoot();
                if (!xamlRoot) {
                    LOG_LIFECYCLE(L"[MainWindow] Model download dialog: XamlRoot not ready; retrying shortly");
                    ScheduleModelDialogRetry();
                    return;
                }
                // Text-only content on purpose: a ProgressBar inside this
                // ContentDialog deterministically crashed the self-release
                // build with a stowed 0xc000027b (combase 0x800F1000) on
                // Windows 11 24H2 + WindowsAppSDK 1.6 self-contained. The
                // status text carries "x MB / y GB (z%)" instead.
                Controls::TextBlock statusText;
                statusText.TextWrapping(TextWrapping::WrapWholeWords);

                Controls::ContentDialog dialog;
                dialog.XamlRoot(xamlRoot);
                dialog.Title(box_value(::AgentRedactor::LocString(L"ModelDownload_Title")));
                dialog.Content(statusText);
                dialog.PrimaryButtonText(::AgentRedactor::LocString(L"ModelDownload_RetryButton"));
                // No close button on purpose: the app is blocked until the model
                // exists, so the dialog must not be dismissible. Retry (primary)
                // is enabled only after a failure.
                dialog.DefaultButton(Controls::ContentDialogButton::Primary);
                dialog.PrimaryButtonClick([](auto&&, auto&&) {
                    if (auto appState = ::AgentRedactor::AppState::Instance()) {
                        appState->RetryModelDownload();
                    }
                });

                modelDialog_ = dialog;
                modelStatusText_ = statusText;
                modelDialogShowing_ = true;
                LOG_LIFECYCLE(L"[MainWindow] Model download dialog: calling ShowAsync");
                ShowModelDownloadDialogAsync(dialog);
            }

            if (inProgress) {
                int percent = app->ModelDownloadPercent();
                modelDialog_.IsPrimaryButtonEnabled(false);
                std::wstring status = app->ModelDownloadStatus();
                if (percent >= 0) {
                    status += L" (" + std::to_wstring(std::clamp(percent, 0, 100)) + L"%)";
                }
                modelStatusText_.Text(status);
            } else if (failed) {
                modelDialog_.IsPrimaryButtonEnabled(true);
                modelStatusText_.Text(::AgentRedactor::LocString(L"ModelDownload_Failed"));
            } else {
                // Required but not started yet (dialog just appeared): kick off the
                // download so the user immediately sees progress.
                modelDialog_.IsPrimaryButtonEnabled(false);
                LOG_LIFECYCLE(L"[MainWindow] Model download dialog shown; starting download");
                app->StartModelDownloadIfNeeded();
            }
        } catch (const winrt::hresult_error& e) {
            LOGF_LIFECYCLE(L"[MainWindow] UpdateModelDownloadDialog failed: %s (0x%08X)",
                e.message().c_str(), static_cast<unsigned int>(e.code().value));
        } catch (...) {
            LOG_LIFECYCLE(L"[MainWindow] UpdateModelDownloadDialog failed with an unknown error");
        }
    }

    // Awaits the one and only ShowAsync call of a model-download dialog
    // instance. A failure (e.g. another ContentDialog managed to open first,
    // or the window was not ready) is caught and logged here instead of
    // escaping into the XAML framework. Once the dialog closes — normally or
    // after a failed show — the member references are dropped so a later
    // status update can cleanly re-create it.
    winrt::fire_and_forget MainWindow::ShowModelDownloadDialogAsync(Controls::ContentDialog dialog)
    {
        auto lifetime = get_strong();
        bool shown = true;
        try {
            co_await dialog.ShowAsync();
            LOG_LIFECYCLE(L"[MainWindow] Model download dialog: ShowAsync completed (dialog closed)");
        } catch (const winrt::hresult_error& e) {
            shown = false;
            LOGF_LIFECYCLE(L"[MainWindow] Model download dialog failed to show: %s (0x%08X)",
                e.message().c_str(), static_cast<unsigned int>(e.code().value));
        } catch (...) {
            shown = false;
            LOG_LIFECYCLE(L"[MainWindow] Model download dialog failed to show (unknown error)");
        }

        lifetime->modelDialogShowing_ = false;
        if (lifetime->modelDialog_ &&
            winrt::get_abi(lifetime->modelDialog_) == winrt::get_abi(dialog)) {
            lifetime->modelDialog_ = nullptr;
            lifetime->modelStatusText_ = nullptr;
        }
        if (!shown) {
            // Re-evaluate shortly: either the state changed (dialog no longer
            // needed) or the retry timer re-shows it once the UI is ready.
            lifetime->ScheduleModelDialogRetry();
        } else {
            lifetime->RunPendingUpdateUiAction();
        }
    }

    // Re-runs UpdateModelDownloadDialog once, shortly, on the UI thread. Used
    // when the dialog could not be shown yet (window not visible / not ready)
    // so the blocking first-run download does not wait for the next user
    // action (the Activated event is not guaranteed to fire again).
    void MainWindow::ScheduleModelDialogRetry()
    {
        if (!modelDialogRetryTimer_) {
            modelDialogRetryTimer_ = DispatcherQueue().CreateTimer();
            modelDialogRetryTimer_.Interval(std::chrono::milliseconds(500));
            modelDialogRetryTimer_.IsRepeating(false);
            modelDialogRetryTimer_.Tick([this](auto&&, auto&&) {
                UpdateModelDownloadDialog();
            });
        }
        if (!modelDialogRetryTimer_.IsRunning()) {
            modelDialogRetryTimer_.Start();
        }
    }

    // Runs the deferred update-manager prompt (if any) now that the blocking
    // model-download dialog is gone; keeps the one-ContentDialog-at-a-time
    // rule intact.
    void MainWindow::RunPendingUpdateUiAction()
    {
        if (!pendingUpdateUiAction_) return;
        if (!hwnd_ || !IsWindowVisible(hwnd_)) return;
        auto action = std::move(pendingUpdateUiAction_);
        pendingUpdateUiAction_ = nullptr;
        try {
            action(Content().XamlRoot());
        } catch (...) {
            LOG_LIFECYCLE(L"[MainWindow] Deferred update prompt failed");
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
            app->SetOnModelDownloadStatus([this]() {
                this->DispatcherQueue().TryEnqueue([this]() {
                    this->UpdateModelDownloadDialog();
                });
            });

            // Self-release channel only (no-ops in the Store/MSIX build): give
            // the update manager a way to show dialogs on the UI thread, then
            // kick the startup check + download.
            ::AgentRedactor::UpdateManager::SetUiDispatch(
                [weak = get_weak()](::AgentRedactor::UpdateManager::UiAction action) {
                    if (auto self = weak.get()) {
                        self->DispatcherQueue().TryEnqueue([self, action = std::move(action)]() mutable {
                            if (!self->hwnd_ || !IsWindowVisible(self->hwnd_)) return;
                            // Never stack the update prompt on top of the
                            // blocking first-run download dialog (WinUI allows
                            // only one ContentDialog at a time): defer it until
                            // that dialog has fully closed.
                            if (self->modelDialog_ || self->modelDialogShowing_) {
                                self->pendingUpdateUiAction_ = std::move(action);
                                return;
                            }
                            action(self->Content().XamlRoot());
                        });
                    }
                });
            ::AgentRedactor::UpdateManager::CheckAndDownloadInBackground();
        }

        // Re-evaluate the model-download dialog whenever the window is shown
        // (a failed download may have completed while hidden in the tray).
        // Never touch a ContentDialog from inside the Activated callback
        // itself: post at Low priority so activation and the first frame
        // complete first (showing a dialog mid-activation is what crashed
        // self-release builds with a stowed 0xc000027b).
        Activated([this](auto&&, auto&&) {
            DispatcherQueue().TryEnqueue(Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [this]() {
                UpdateModelDownloadDialog();
            });
        });

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
