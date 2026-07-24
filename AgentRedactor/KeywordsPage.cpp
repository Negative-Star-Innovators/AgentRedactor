#include "pch.h"
#include "KeywordsPage.h"
#include "KeywordsPage.xaml.g.hpp"
#include "AppState.h"
#include "settings_manager.h"
#include "api_key_profile.h"
#include "localization.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::AgentRedactor::implementation
{
    KeywordsPage::KeywordsPage()
    {
        InitializeComponent();
        ::AgentRedactor::ApplyCurrentFlowDirection(*this);
        LocalizeStaticUI();
        KeywordList().ContainerContentChanging({ this, &KeywordsPage::OnContainerContentChanging });
    }

    void KeywordsPage::OnContainerContentChanging(IInspectable const&, ContainerContentChangingEventArgs const& args)
    {
        if (args.InRecycleQueue()) return;
        auto root = args.ItemContainer().ContentTemplateRoot().try_as<Grid>();
        if (!root) return;
        auto button = root.FindName(L"RemoveButton").try_as<Button>();
        if (button) {
            button.Content(winrt::box_value(::AgentRedactor::LocString(L"KeywordsPage_RemoveButton/Content")));
        }
    }

    void KeywordsPage::LocalizeStaticUI()
    {
        Title().Text(::AgentRedactor::LocString(L"KeywordsPage_Title/Text").c_str());
        EditNewKeyword().PlaceholderText(::AgentRedactor::LocString(L"KeywordsPage_Placeholder/PlaceholderText").c_str());
        CheckCaseSensitive().Content(winrt::box_value(::AgentRedactor::LocString(L"KeywordsPage_CaseSensitiveCheck/Content")));
        AddButton().Content(winrt::box_value(::AgentRedactor::LocString(L"KeywordsPage_AddButton/Content")));
        // RemoveButton lives inside the DataTemplate and is localized per container in OnContainerContentChanging.
    }

    void KeywordsPage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        LoadKeywords();
    }

    void KeywordsPage::OnNavigatedFrom(Navigation::NavigationEventArgs const&)
    {
    }

    void KeywordsPage::LoadKeywords()
    {
        KeywordList().Items().Clear();
        entries_.clear();
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto profiles = app->Settings()->GetProfiles();
        if (profiles.empty()) return;
        entries_ = profiles[0].keywords;
        for (const auto& e : entries_) {
            std::wstring display = e.text + (e.caseSensitive ? ::AgentRedactor::LocString(L"Keyword_CaseSensitiveSuffix") : L"");
            KeywordList().Items().Append(box_value(display));
        }
    }

    void KeywordsPage::AddKeyword_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto profiles = app->Settings()->GetProfiles();
        if (profiles.empty()) return;
        auto p = profiles[0];
        ::AgentRedactor::KeywordEntry entry;
        entry.text = EditNewKeyword().Text().c_str();
        entry.caseSensitive = CheckCaseSensitive().IsChecked().GetBoolean();
        entry.enabled = true;
        p.keywords.push_back(entry);
        app->Settings()->UpdateProfile(p);
        app->RestartProxyServers();
        EditNewKeyword().Text(L"");
        LoadKeywords();
    }

    void KeywordsPage::EditKeyword_Click(IInspectable const&, RoutedEventArgs const&)
    {
    }

    void KeywordsPage::RemoveKeyword_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto btn = sender.as<Button>();
        if (!btn) return;
        auto tag = unbox_value<hstring>(btn.Tag());
        // Tag contains text + case-sensitive suffix or just text
        std::wstring text = tag.c_str();
        std::wstring suffix = ::AgentRedactor::LocString(L"Keyword_CaseSensitiveSuffix");
        size_t pos = text.find(suffix);
        if (pos != std::wstring::npos) text = text.substr(0, pos);

        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto profiles = app->Settings()->GetProfiles();
        if (profiles.empty()) return;
        auto p = profiles[0];
        p.keywords.erase(
            std::remove_if(p.keywords.begin(), p.keywords.end(),
                [&](const ::AgentRedactor::KeywordEntry& e) { return e.text == text; }),
            p.keywords.end());
        app->Settings()->UpdateProfile(p);
        app->RestartProxyServers();
        LoadKeywords();
    }
}
