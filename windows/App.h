#pragma once
#include "App.g.h"
#include "App.xaml.g.h"

namespace winrt::AgentRedactor::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);
    private:
        Microsoft::UI::Xaml::Window window{ nullptr };
    };
}

namespace winrt::AgentRedactor::factory_implementation
{
    struct App : AppT<App, implementation::App> { };
}
