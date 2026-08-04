#pragma once
#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include "update_manager.h"

namespace winrt::AgentRedactor::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        void ReloadLocalization();

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
    };
}

namespace winrt::AgentRedactor::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> { };
}
