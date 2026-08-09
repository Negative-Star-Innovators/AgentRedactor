#include "pch.h"
#include "RegexPage.h"
#include "RegexPage.xaml.g.hpp"
#include "AppState.h"
#include "settings_manager.h"
#include "api_key_profile.h"
#include "localization.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::AgentRedactor::implementation
{
    RegexPage::RegexPage()
    {
        InitializeComponent();
        ::AgentRedactor::ApplyCurrentFlowDirection(*this);
        LocalizeStaticUI();
        RegexList().ContainerContentChanging({ this, &RegexPage::OnContainerContentChanging });
    }

    void RegexPage::OnContainerContentChanging(IInspectable const&, ContainerContentChangingEventArgs const& args)
    {
        if (args.InRecycleQueue()) return;
        auto root = args.ItemContainer().ContentTemplateRoot().try_as<Grid>();
        if (!root) return;
        auto button = root.FindName(L"RemoveButton").try_as<Button>();
        if (button) {
            button.Content(winrt::box_value(::AgentRedactor::LocString(L"RegexPage_RemoveButton/Content")));
        }
    }

    void RegexPage::LocalizeStaticUI()
    {
        Title().Text(::AgentRedactor::LocString(L"RegexPage_Title/Text").c_str());
        EditNewRegex().PlaceholderText(::AgentRedactor::LocString(L"RegexPage_Placeholder/PlaceholderText").c_str());
        AddButton().Content(winrt::box_value(::AgentRedactor::LocString(L"RegexPage_AddButton/Content")));
        // RemoveButton lives inside the DataTemplate and is localized per container in OnContainerContentChanging.
    }

    void RegexPage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        LoadRegex();
    }

    void RegexPage::OnNavigatedFrom(Navigation::NavigationEventArgs const&)
    {
    }

    void RegexPage::LoadRegex()
    {
        RegexList().Items().Clear();
        entries_.clear();
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto profiles = app->Settings()->GetProfiles();
        // For simplicity, edit the first profile's regex
        if (profiles.empty()) return;
        entries_ = profiles[0].regexPatterns;
        for (const auto& e : entries_) {
            RegexList().Items().Append(box_value(e.pattern));
        }
    }

    void RegexPage::AddRegex_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto profiles = app->Settings()->GetProfiles();
        if (profiles.empty()) return;
        auto p = profiles[0];
        ::AgentRedactor::RegexEntry entry;
        entry.pattern = EditNewRegex().Text().c_str();
        entry.enabled = true;
        p.regexPatterns.push_back(entry);
        app->Settings()->UpdateProfile(p);
        app->RestartProxyServers();
        EditNewRegex().Text(L"");
        LoadRegex();
    }

    void RegexPage::EditRegex_Click(IInspectable const&, RoutedEventArgs const&)
    {
    }

    void RegexPage::RemoveRegex_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto btn = sender.as<Button>();
        if (!btn) return;
        auto pattern = unbox_value<hstring>(btn.Tag());
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto profiles = app->Settings()->GetProfiles();
        if (profiles.empty()) return;
        auto p = profiles[0];
        p.regexPatterns.erase(
            std::remove_if(p.regexPatterns.begin(), p.regexPatterns.end(),
                [&](const ::AgentRedactor::RegexEntry& e) { return e.pattern == pattern; }),
            p.regexPatterns.end());
        app->Settings()->UpdateProfile(p);
        app->RestartProxyServers();
        LoadRegex();
    }
}
