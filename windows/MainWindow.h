#pragma once
#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include <atomic>
#include <memory>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include "update_manager.h"

namespace winrt::AgentRedactor::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        void ReloadLocalization();
        void RefreshFromEngineSettings();

    private:
        void ApplyTitleBarTheme();
        void OnColorValuesChanged(Windows::UI::ViewManagement::UISettings const& sender, Windows::Foundation::IInspectable const& args);
        void CloseWindow();
        void UpdateModelDownloadDialog();
        winrt::fire_and_forget ShowModelDownloadDialogAsync(winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog);
        void ScheduleModelDialogRetry();
        void RunPendingUpdateUiAction();
        winrt::fire_and_forget ShowQuitConfirmationAsync();
        static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

        // ---- Windows Hello session lock (protection enabled) ----
        // The session is locked whenever the window is not actively in use:
        // on first appearance (app start or tray re-open) and after 10
        // minutes without input. A full-window overlay hides the app content
        // while locked and the Windows Hello prompt is owned by this window.
        void LocalizeLockOverlay();
        void OnUnlockBtnClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnWindowShown();
        void OnWindowHidden();
        void EnsureLockState(bool autoPrompt);
        void UpdateLockState(bool autoPrompt);
        void ShowLockOverlay();
        void HideLockOverlay();
        void LockSession();
        void ResetActivityTimer();
        void OnInactivityTimeout();
        winrt::fire_and_forget PromptUnlockAsync();
        static bool IsActivityMessage(UINT uMsg);

        HWND hwnd_ = nullptr;
        bool allowClose_ = false;
        Windows::UI::ViewManagement::UISettings uiSettings_;
        event_token colorValuesChangedToken_;
        winrt::Microsoft::UI::Xaml::Controls::ContentDialog modelDialog_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBlock modelStatusText_{ nullptr };
        // True while a model-download dialog instance is open or its single
        // ShowAsync call is still in flight; a new dialog may only be created
        // after this clears (never two ContentDialogs at once).
        bool modelDialogShowing_ = false;
        Microsoft::UI::Dispatching::DispatcherQueueTimer modelDialogRetryTimer_{ nullptr };
        // Update-manager prompt deferred while the blocking download dialog
        // is up (WinUI allows only one ContentDialog at a time).
        ::AgentRedactor::UpdateManager::UiAction pendingUpdateUiAction_{ nullptr };

        // The page frame lives inside RootGrid so the lock overlay can sit
        // above every page.
        Microsoft::UI::Xaml::Controls::Frame frame_{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer inactivityTimer_{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer lockStateRetryTimer_{ nullptr };
        bool firstShowHandled_ = false;
        bool hiddenToTray_ = false;
        bool lockOverlayShown_ = false;
        bool unlockPromptInFlight_ = false;
        // Shared cancel flag for the in-flight Windows Hello prompt: the
        // window hide/destroy paths set it so the system dialog is dismissed
        // (no orphaned dialog survives the window). Null while no prompt is
        // in flight.
        std::shared_ptr<std::atomic<bool>> helloPromptCancel_;
        bool lockStatePendingPrompt_ = false;
        bool quitPromptInFlight_ = false;
        // Set in WM_NCDESTROY: guards the dispatcher timers and the async
        // unlock continuation from touching XAML after the window is gone.
        bool windowDestroyed_ = false;
    };
}

namespace winrt::AgentRedactor::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> { };
}
