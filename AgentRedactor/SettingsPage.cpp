#include "pch.h"
#include "SettingsPage.h"
#include "SettingsPage.xaml.g.hpp"
#include "SettingsPage.g.cpp"
#include "AppState.h"
#include "settings_manager.h"
#include "constants.h"
#include "localization.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::AgentRedactor::implementation
{
    SettingsPage::SettingsPage()
    {
        InitializeComponent();
        ::AgentRedactor::ApplyCurrentFlowDirection(*this);
        LocalizeStaticUI();
    }

    void SettingsPage::LocalizeStaticUI()
    {
        BtnBack().Content(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_BackButton/Content")));
        Title().Text(::AgentRedactor::LocString(L"SettingsPage_Title/Text").c_str());
        ComboOnnxProvider().Header(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_OnnxProvider_Header/Header")));
        OnnxProviderCPU().Content(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_OnnxProvider_CPU/Content")));
        OnnxProviderCUDA().Content(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_OnnxProvider_CUDA/Content")));
        OnnxProviderDirectML().Content(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_OnnxProvider_DirectML/Content")));
        ComboLanguage().Header(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_Language_Header/Header")));

        // Repopulate the language selector from the single source-of-truth list.
        // Suppress SelectionChanged while we rebuild so we do not recursively change language.
        updatingLanguageCombo_ = true;
        std::wstring selectedTag;
        if (auto selected = ComboLanguage().SelectedItem().try_as<Controls::ComboBoxItem>()) {
            selectedTag = unbox_value<hstring>(selected.Tag());
        }

        ComboLanguage().Items().Clear();
        auto addLangItem = [&](const std::wstring& tag, const std::wstring& displayName) {
            Controls::ComboBoxItem item;
            item.Tag(winrt::box_value(tag));
            item.Content(winrt::box_value(displayName));
            ComboLanguage().Items().Append(item);
        };
        addLangItem(L"", ::AgentRedactor::LocString(L"SettingsPage_Language_SystemDefault/Content"));
        for (const auto& lang : ::AgentRedactor::SUPPORTED_LANGUAGES) {
            addLangItem(lang.tag, lang.nativeName);
        }

        // Restore the previous selection.
        for (uint32_t i = 0; i < ComboLanguage().Items().Size(); ++i) {
            auto item = ComboLanguage().Items().GetAt(i).as<Controls::ComboBoxItem>();
            if (item && unbox_value<hstring>(item.Tag()) == selectedTag) {
                ComboLanguage().SelectedIndex(static_cast<int32_t>(i));
                break;
            }
        }
        updatingLanguageCombo_ = false;

        ToggleStartOnBoot().Header(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_StartOnBoot_Header/Header")));
        ToggleMasterPassword().Header(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_MasterPassword_Header/Header")));
        BtnChangePassword().Content(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_ChangePasswordButton/Content")));
        ToggleEnableLogging().Header(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_EnableLogging_Header/Header")));
        ToggleShowSensitive().Header(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_SensitiveInfo_Header/Header")));
        BtnSave().Content(winrt::box_value(::AgentRedactor::LocString(L"SettingsPage_SaveButton/Content")));
    }

    void SettingsPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&)
    {
        LoadSettings();
    }

    void SettingsPage::LoadSettings()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto settings = app->Settings();

        std::wstring provider = settings->GetOnnxProvider();
        for (uint32_t i = 0; i < ComboOnnxProvider().Items().Size(); ++i) {
            auto item = ComboOnnxProvider().Items().GetAt(i).as<ComboBoxItem>();
            if (item && unbox_value<hstring>(item.Tag()) == provider) {
                ComboOnnxProvider().SelectedIndex(static_cast<int32_t>(i));
                break;
            }
        }

        ToggleStartOnBoot().IsOn(settings->IsStartOnBoot());
        ToggleMasterPassword().IsOn(settings->IsMasterPasswordEnabled());
        BtnChangePassword().IsEnabled(settings->IsMasterPasswordEnabled());
        bool loggingEnabled = settings->IsLoggingEnabled();
        ToggleEnableLogging().IsOn(loggingEnabled);
        ToggleShowSensitive().IsEnabled(loggingEnabled);
        ToggleShowSensitive().IsOn(loggingEnabled && app->Logs()->IsShowSensitive());

        std::wstring lang = settings->GetAppLanguage();
        updatingLanguageCombo_ = true;
        for (uint32_t i = 0; i < ComboLanguage().Items().Size(); ++i) {
            auto item = ComboLanguage().Items().GetAt(i).as<ComboBoxItem>();
            if (item && unbox_value<hstring>(item.Tag()) == lang) {
                ComboLanguage().SelectedIndex(static_cast<int32_t>(i));
                break;
            }
        }
        updatingLanguageCombo_ = false;
    }

    void SettingsPage::SaveSettings_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto settings = app->Settings();

        auto item = ComboOnnxProvider().SelectedItem().as<ComboBoxItem>();
        if (item) {
            settings->SetOnnxProvider(unbox_value<hstring>(item.Tag()).c_str());
        }

        bool startOnBoot = ToggleStartOnBoot().IsOn();
        settings->SetStartOnBoot(startOnBoot);
        if (startOnBoot) {
            RegisterStartupTask();
        } else {
            UnregisterStartupTask();
        }

        settings->SaveSettings();
        app->RestartProxyServers();
    }

    void SettingsPage::ToggleMasterPassword_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto settings = app->Settings();

        bool enabled = ToggleMasterPassword().IsOn();
        if (enabled) {
            // Show a simple content dialog for password
            // For now, just enable and let user set via change password
            // This is a simplified flow
            BtnChangePassword().IsEnabled(true);
        } else {
            settings->DisableMasterPassword();
            BtnChangePassword().IsEnabled(false);
        }
    }

    void SettingsPage::ChangePassword_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // Simplified: show a message that this should be implemented with ContentDialog
        // For initial conversion, we'll skip the full dialog
    }

    void SettingsPage::ComboLanguage_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        if (updatingLanguageCombo_) return;
        auto item = ComboLanguage().SelectedItem().as<ComboBoxItem>();
        if (!item) return;
        auto tag = unbox_value<hstring>(item.Tag());
        auto app = ::AgentRedactor::AppState::Instance();
        if (app) app->SetLanguage(std::wstring(tag.c_str()));
    }

    void SettingsPage::Back_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Frame().GoBack();
    }

    void SettingsPage::ToggleEnableLogging_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto settings = app->Settings();
        bool enabled = ToggleEnableLogging().IsOn();
        settings->SetLoggingEnabled(enabled);
        app->Logs()->SetLoggingEnabled(enabled);
        ToggleShowSensitive().IsEnabled(enabled);
        if (!enabled) {
            ToggleShowSensitive().IsOn(false);
            app->Logs()->SetShowSensitive(false);
        }
    }

    void SettingsPage::ToggleShowSensitive_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        bool enabled = ToggleShowSensitive().IsOn();
        app->Logs()->SetShowSensitive(enabled);
        if (enabled) {
            ContentDialog dialog;
            dialog.XamlRoot(this->XamlRoot());
            dialog.Title(box_value(::AgentRedactor::LocString(L"SettingsPage_SensitiveInfoWarning_Title")));
            dialog.Content(box_value(::AgentRedactor::LocString(L"SettingsPage_SensitiveInfoWarning_Message")));
            dialog.CloseButtonText(::AgentRedactor::LocString(L"Dialog_OKButton"));
            dialog.ShowAsync();
        }
    }
}
