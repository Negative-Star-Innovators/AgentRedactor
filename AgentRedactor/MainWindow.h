#pragma once
#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include <winrt/Windows.UI.ViewManagement.h>

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
        winrt::fire_and_forget ShowQuitConfirmationAsync();
        static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

        HWND hwnd_ = nullptr;
        bool allowClose_ = false;
        Windows::UI::ViewManagement::UISettings uiSettings_;
        event_token colorValuesChangedToken_;
    };
}

namespace winrt::AgentRedactor::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> { };
}
