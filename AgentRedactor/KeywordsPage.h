#pragma once
#include "KeywordsPage.g.h"
#include "KeywordsPage.xaml.g.h"
#include "api_key_profile.h"

namespace winrt::AgentRedactor::implementation
{
    struct KeywordsPage : KeywordsPageT<KeywordsPage>
    {
        KeywordsPage();

        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnNavigatedFrom(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void LocalizeStaticUI();
        void OnContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);

        void AddKeyword_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void EditKeyword_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void RemoveKeyword_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void LoadKeywords();
        std::vector<::AgentRedactor::KeywordEntry> entries_;
    };
}

namespace winrt::AgentRedactor::factory_implementation
{
    struct KeywordsPage : KeywordsPageT<KeywordsPage, implementation::KeywordsPage> { };
}
