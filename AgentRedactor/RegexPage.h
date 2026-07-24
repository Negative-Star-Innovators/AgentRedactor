#pragma once
#include "RegexPage.g.h"
#include "RegexPage.xaml.g.h"
#include "api_key_profile.h"

namespace winrt::AgentRedactor::implementation
{
    struct RegexPage : RegexPageT<RegexPage>
    {
        RegexPage();

        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnNavigatedFrom(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void LocalizeStaticUI();
        void OnContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);

        void AddRegex_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void EditRegex_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void RemoveRegex_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void LoadRegex();
        std::vector<::AgentRedactor::RegexEntry> entries_;
    };
}

namespace winrt::AgentRedactor::factory_implementation
{
    struct RegexPage : RegexPageT<RegexPage, implementation::RegexPage> { };
}
