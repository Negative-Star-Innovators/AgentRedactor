#include "pch.h"
#include "HomePage.h"
#include "HomePage.xaml.g.hpp"
#include "AppState.h"
#include "settings_manager.h"
#include "localization.h"
#include "api_key_profile.h"
#include "constants.h"
#include "utils.h"
#include "http_server.h"
#include "engine/hello_unlock.h"
#include <winhttp.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <regex>
#include <shellapi.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Automation;

namespace winrt::AgentRedactor::implementation
{
    HomePage::HomePage()
    {
        InitializeComponent();
        ::AgentRedactor::ApplyCurrentFlowDirection(*this);

        this->KeyDown([this](IInspectable const&, KeyRoutedEventArgs const& e) {
            if (e.Key() == Windows::System::VirtualKey::PageDown) {
                auto scroll = MainScrollViewer();
                if (scroll.ScrollableHeight() > 0 && scroll.VerticalOffset() >= scroll.ScrollableHeight() - 1.0) {
                    e.Handled(true);
                }
            } else if (e.Key() == Windows::System::VirtualKey::PageUp) {
                auto scroll = MainScrollViewer();
                if (scroll.ScrollableHeight() > 0 && scroll.VerticalOffset() <= 1.0) {
                    e.Handled(true);
                }
            }
        });

        SaveProfileBtn().Click({ this, &HomePage::SaveProfile_Click });
        ShowKeyCheck().Click({ this, &HomePage::ShowKey_Toggled });
        AddRegexBtn().Click({ this, &HomePage::AddRegex_Click });
        AddKeywordBtn().Click({ this, &HomePage::AddKeyword_Click });

        CopyUrlBtn().Click([this](IInspectable const&, RoutedEventArgs const&) {
            auto port = PortBox().Text();
            std::wstring url = L"http://localhost:" + std::wstring(port.c_str()) + L"/";
            Windows::ApplicationModel::DataTransfer::DataPackage dataPackage;
            dataPackage.SetText(url);
            Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(dataPackage);
        });

        NewRegexBox().KeyDown([this](IInspectable const&, KeyRoutedEventArgs const& e) {
            if (e.Key() == Windows::System::VirtualKey::Enter) {
                AddRegex_Click(nullptr, nullptr);
            }
        });
        NewKeywordBox().KeyDown([this](IInspectable const&, KeyRoutedEventArgs const& e) {
            if (e.Key() == Windows::System::VirtualKey::Enter) {
                AddKeyword_Click(nullptr, nullptr);
            }
        });
        AddProfileBtn().Click({ this, &HomePage::AddProfile_Click });
        RemoveProfileBtn().Click({ this, &HomePage::RemoveProfile_Click });
        ProfileList().SelectionChanged({ this, &HomePage::ProfileList_SelectionChanged });
        RequirePasswordCheck().Click({ this, &HomePage::RequirePassword_Click });
        EnableLoggingCheck().Click({ this, &HomePage::EnableLogging_Click });
        ShowSensitiveCheck().Click({ this, &HomePage::ShowSensitive_Click });
        OpenLogBtn().Click({ this, &HomePage::OpenLog_Click });
        OpenFolderBtn().Click({ this, &HomePage::OpenLogsFolder_Click });
        PortBox().BeforeTextChanging([this](IInspectable const&, TextBoxBeforeTextChangingEventArgs const& e) {
            for (wchar_t c : e.NewText()) {
                if (c < L'0' || c > L'9') {
                    e.Cancel(true);
                    return;
                }
            }
        });
        PortBox().TextChanged([this](IInspectable const&, TextChangedEventArgs const&) {
            UpdateProxyStatus();
        });

        Loaded({ this, &HomePage::OnLoaded });

        LocalizeStaticUI();
    }

    void HomePage::LocalizeStaticUI()
    {
        ProfilesHeader().Text(::AgentRedactor::LocString(L"HomePage_ProfilesHeader/Text").c_str());
        AddProfileBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_AddButton/Content")));
        RemoveProfileBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_RemoveButton/Content")));
        QuickStartTitle().Text(::AgentRedactor::LocString(L"HomePage_QuickStart_Title/Text").c_str());
        QuickStartStep1().Text(::AgentRedactor::LocString(L"HomePage_QuickStart_Step1/Text").c_str());
        QuickStartStep2().Text(::AgentRedactor::LocString(L"HomePage_QuickStart_Step2/Text").c_str());
        QuickStartStep3().Text(::AgentRedactor::LocString(L"HomePage_QuickStart_Step3/Text").c_str());
        ApiProxyTitle().Text(::AgentRedactor::LocString(L"HomePage_ApiProxy_Title/Text").c_str());
        ProfileNameLabel().Text(::AgentRedactor::LocString(L"HomePage_ProfileName_Label/Text").c_str());
        ProfileNameBox().PlaceholderText(::AgentRedactor::LocString(L"HomePage_ProfileName_Placeholder/PlaceholderText").c_str());
        LocalUrlLabel().Text(::AgentRedactor::LocString(L"HomePage_LocalUrl_Label/Text").c_str());
        CopyUrlBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_CopyButton/Content")));
        LocalUrlHint().Text(::AgentRedactor::LocString(L"HomePage_LocalUrl_Hint/Text").c_str());
        ForwardToLabel().Text(::AgentRedactor::LocString(L"HomePage_ForwardTo_Label/Text").c_str());
        UrlBox().PlaceholderText(::AgentRedactor::LocString(L"HomePage_ForwardTo_Placeholder/PlaceholderText").c_str());
        ForwardToHint().Text(::AgentRedactor::LocString(L"HomePage_ForwardTo_Hint/Text").c_str());
        ApiKeyLabel().Text(::AgentRedactor::LocString(L"HomePage_ApiKey_Label/Text").c_str());
        ShowKeyCheck().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_ShowKeyCheck/Content")));
        SaveProfileBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_SaveAllSettingsButton/Content")));
        RegexTitle().Text(::AgentRedactor::LocString(L"HomePage_Regex_Title/Text").c_str());
        RegexDescription().Text(::AgentRedactor::LocString(L"HomePage_Regex_Description/Text").c_str());
        RegexEnabledHeader().Text(::AgentRedactor::LocString(L"HomePage_Regex_EnabledHeader/Text").c_str());
        RegexPatternHeader().Text(::AgentRedactor::LocString(L"HomePage_Regex_PatternHeader/Text").c_str());
        NewRegexBox().PlaceholderText(::AgentRedactor::LocString(L"HomePage_Regex_Placeholder/PlaceholderText").c_str());
        AddRegexBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Regex_AddButton/Content")));
        KeywordsTitle().Text(::AgentRedactor::LocString(L"HomePage_Keywords_Title/Text").c_str());
        KeywordsDescription().Text(::AgentRedactor::LocString(L"HomePage_Keywords_Description/Text").c_str());
        KeywordsEnabledHeader().Text(::AgentRedactor::LocString(L"HomePage_Keywords_EnabledHeader/Text").c_str());
        KeywordsCaseHeader().Text(::AgentRedactor::LocString(L"HomePage_Keywords_CaseHeader/Text").c_str());
        KeywordsKeywordHeader().Text(::AgentRedactor::LocString(L"HomePage_Keywords_KeywordHeader/Text").c_str());
        NewKeywordBox().PlaceholderText(::AgentRedactor::LocString(L"HomePage_Keywords_Placeholder/PlaceholderText").c_str());
        AddKeywordBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Keywords_AddButton/Content")));
        CaseSensitiveCheck().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Keywords_CaseSensitiveCheck/Content")));
        DetectionTitle().Text(::AgentRedactor::LocString(L"HomePage_Detection_Title/Text").c_str());
        DetectionDescription().Text(::AgentRedactor::LocString(L"HomePage_Detection_Description/Text").c_str());
        DetectionSlowWarning().Text(::AgentRedactor::LocString(L"HomePage_Detection_SlowWarning/Text").c_str());
        DetectionConfidenceLabel().Text(::AgentRedactor::LocString(L"HomePage_Detection_ConfidenceLabel/Text").c_str());
        PasswordTitle().Text(::AgentRedactor::LocString(L"HomePage_Password_Title/Text").c_str());
        RequirePasswordCheck().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_RequirePasswordCheck/Content")));
        StatisticsTitle().Text(::AgentRedactor::LocString(L"HomePage_Statistics_Title/Text").c_str());
        ClearStatisticsBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Statistics_ClearButton/Content")));
        SessionRedactionsTitle().Text(::AgentRedactor::LocString(L"HomePage_SessionRedactions_Title/Text").c_str());
        ClearMatchesBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_SessionRedactions_ClearButton/Content")));
        SessionRedactionsDescription().Text(::AgentRedactor::LocString(L"HomePage_SessionRedactions_Description/Text").c_str());
        LogsTitle().Text(::AgentRedactor::LocString(L"HomePage_Logs_Title/Text").c_str());
        EnableLoggingCheck().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Logs_EnableLoggingCheck/Content")));
        ShowSensitiveCheck().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Logs_SensitiveCheck/Content")));
        LogsDisclaimer().Text(::AgentRedactor::LocString(L"HomePage_Logs_Disclaimer/Text").c_str());
        OpenLogBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Logs_OpenLogButton/Content")));
        OpenFolderBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Logs_OpenFolderButton/Content")));
        ClearLogsBtn().Content(winrt::box_value(::AgentRedactor::LocString(L"HomePage_Logs_ClearButton/Content")));
    }

    void HomePage::OnLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        // The Windows Hello session lock is owned by MainWindow (overlay +
        // prompt on window show / tray re-open / inactivity timeout); this
        // page just initializes underneath it.
        FinishInitialization();
    }

    bool HomePage::IsDarkMode() const
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

    static void ThemeTextBox(TextBox const& box, bool dark)
    {
        auto resources = box.Resources();
        if (dark) {
            auto darkBg = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 45, 45, 45));
            auto whiteFg = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255));
            resources.Insert(box_value(L"TextControlBackgroundFocused"), darkBg);
            resources.Insert(box_value(L"TextControlBackgroundPointerOver"), darkBg);
            resources.Insert(box_value(L"TextControlForegroundFocused"), whiteFg);
            resources.Insert(box_value(L"TextControlForegroundPointerOver"), whiteFg);
        } else {
            try { resources.Remove(box_value(L"TextControlBackgroundFocused")); } catch (...) {}
            try { resources.Remove(box_value(L"TextControlBackgroundPointerOver")); } catch (...) {}
            try { resources.Remove(box_value(L"TextControlForegroundFocused")); } catch (...) {}
            try { resources.Remove(box_value(L"TextControlForegroundPointerOver")); } catch (...) {}
        }
    }

    static void ThemePasswordBox(PasswordBox const& box, bool dark)
    {
        auto resources = box.Resources();
        if (dark) {
            auto darkBg = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 45, 45, 45));
            auto whiteFg = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255));
            resources.Insert(box_value(L"TextControlBackgroundFocused"), darkBg);
            resources.Insert(box_value(L"TextControlBackgroundPointerOver"), darkBg);
            resources.Insert(box_value(L"TextControlForegroundFocused"), whiteFg);
            resources.Insert(box_value(L"TextControlForegroundPointerOver"), whiteFg);
        } else {
            try { resources.Remove(box_value(L"TextControlBackgroundFocused")); } catch (...) {}
            try { resources.Remove(box_value(L"TextControlBackgroundPointerOver")); } catch (...) {}
            try { resources.Remove(box_value(L"TextControlForegroundFocused")); } catch (...) {}
            try { resources.Remove(box_value(L"TextControlForegroundPointerOver")); } catch (...) {}
        }
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

    void HomePage::ApplyTheme()
    {
        bool dark = IsDarkMode();
        isDarkMode_ = dark;
        auto sidebarBg = dark
            ? Windows::UI::ColorHelper::FromArgb(255, 43, 43, 43)
            : Windows::UI::ColorHelper::FromArgb(255, 243, 243, 243);
        auto cardBg = dark
            ? Windows::UI::ColorHelper::FromArgb(255, 30, 30, 30)
            : Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255);
        auto cardBorder = dark
            ? Windows::UI::ColorHelper::FromArgb(255, 68, 68, 68)
            : Windows::UI::ColorHelper::FromArgb(255, 224, 224, 224);
        auto pageBg = dark
            ? Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26)
            : Windows::UI::ColorHelper::FromArgb(255, 245, 245, 245);
        auto textBrush = dark
            ? SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
            : SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26));
        auto errorBrush = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 100, 100));

        LeftPanel().Background(SolidColorBrush(sidebarBg));
        RootGrid().Background(SolidColorBrush(pageBg));

        auto borders = {
            QuickStartBorder(), ProfileCardBorder(), RegexCardBorder(), KeywordCardBorder(),
            SecurityCardBorder(), PasswordCardBorder(), StatusCardBorder(), LogsCardBorder(), MatchesCardBorder()
        };
        for (auto& b : borders) {
            b.Background(SolidColorBrush(cardBg));
            b.BorderBrush(SolidColorBrush(cardBorder));
        }
        // Theme static checkboxes that always exist
        auto checkBrush = dark
            ? SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
            : SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26));
        RequirePasswordCheck().Foreground(checkBrush);
        CaseSensitiveCheck().Foreground(checkBrush);
        ShowKeyCheck().Foreground(checkBrush);
        EnableLoggingCheck().Foreground(checkBrush);
        ShowSensitiveCheck().Foreground(checkBrush);
        // Theme static text boxes
        ThemeTextBox(ProfileNameBox(), dark);
        ThemeTextBox(PortBox(), dark);
        ThemeTextBox(UrlBox(), dark);
        ThemePasswordBox(ApiKeyBox(), dark);
        ThemeTextBox(ConfidenceThresholdBox(), dark);
        ThemeTextBox(NewRegexBox(), dark);
        ThemeTextBox(NewKeywordBox(), dark);

        // Update dynamic control foregrounds without recreating them
        for (auto& cb : piiCheckBoxes_) { cb.Foreground(checkBrush); }
        for (auto& cb : regexCheckBoxes_) { cb.Foreground(checkBrush); }
        for (auto& cb : keywordCheckBoxes_) { cb.Foreground(checkBrush); }
        for (auto& tb : regexTextBoxes_) {
            tb.Foreground(textBrush);
            tb.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
            ThemeTextBox(tb, dark);
        }
        for (auto& btn : keywordCaseButtons_) {
            btn.Foreground(textBrush);
            btn.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
        }
        for (auto& tb : keywordTextBoxes_) {
            tb.Foreground(textBrush);
            tb.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
            ThemeTextBox(tb, dark);
        }
    }

    void HomePage::OnColorValuesChanged(Windows::UI::ViewManagement::UISettings const&, Windows::Foundation::IInspectable const&)
    {
        DispatcherQueue().TryEnqueue([this]() {
            ApplyTheme();
            if (activeDialog_) {
                bool dark = IsDarkMode();
                ThemeContentDialog(activeDialog_, dark);
                if (auto content = activeDialog_.Content()) {
                    if (auto panel = content.try_as<StackPanel>()) {
                        for (auto child : panel.Children()) {
                            if (auto pb = child.try_as<PasswordBox>()) {
                                ThemePasswordBox(pb, dark);
                            }
                        }
                    }
                }
            }
        });
    }

    void HomePage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        colorValuesChangedToken_ = uiSettings_.ColorValuesChanged({ this, &HomePage::OnColorValuesChanged });
    }

    void HomePage::FinishInitialization()
    {
        LoadData();
        auto app = ::AgentRedactor::AppState::Instance();
        if (app) {
            app->SetOnStatsUpdated([this]() {
                DispatcherQueue().TryEnqueue([this]() {
                    // CLI-side profile mutations (aliases, api keys) bump a
                    // revision in the engine; reload everything when it moved
                    // so the GUI reflects them without a restart. Plain
                    // statistics ticks only refresh the stats + matches list.
                    auto app = ::AgentRedactor::AppState::Instance();
                    json snap;
                    unsigned long long revision = 0;
                    if (app && app->GetSettingsSnapshot(snap)) {
                        revision = snap.value("profilesRevision", 0ULL);
                    }
                    if (revision != profilesRevisionShown_) {
                        profilesRevisionShown_ = revision;
                        RefreshProfilesFromEngine();
                        return;
                    }
                    UpdateStats();
                    LoadMatchesList();
                });
            });
            // Sync logging controls with persisted / runtime state.
            bool loggingEnabled = app->Settings()->IsLoggingEnabled();
            EnableLoggingCheck().IsChecked(loggingEnabled);
            ShowSensitiveCheck().IsEnabled(loggingEnabled);
            ShowSensitiveCheck().IsChecked(loggingEnabled && app->Logs()->IsShowSensitive());
        }
    }

    void HomePage::RefreshFromEngine()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        // Diff-aware refresh: rebuild the profile UI only when the engine's
        // profiles actually changed on the fields the list/form display. A
        // profilesRevision bump caused by the GUI's OWN Add/Save then becomes
        // a no-op, so in-progress (unsaved) form edits are never stomped by
        // the 1-second poll; genuinely external changes (e.g. a CLI `set
        // alias` / `set port`) still reload. Stats churn never reloads.
        auto fresh = app->Settings()->GetProfiles();
        if (fresh.empty() && profiles_.empty()) {
            // First run: bootstrap the default profile.
            LoadData();
        } else if (!ProfilesMatch(fresh)) {
            std::wstring prevId = currentProfileId_;
            profiles_ = std::move(fresh);
            bool keep = false;
            for (const auto& p : profiles_) {
                if (p.id == prevId) { keep = true; break; }
            }
            currentProfileId_ = keep ? prevId : (profiles_.empty() ? L"" : profiles_[0].id);
            ApplyTheme();
            LoadProfileList();
            LoadProfileForm();
            LoadPIIGrid();
            LoadRegexList();
            LoadKeywordList();
            UpdateStats();
        }
        bool loggingEnabled = app->Settings()->IsLoggingEnabled();
        EnableLoggingCheck().IsChecked(loggingEnabled);
        ShowSensitiveCheck().IsEnabled(loggingEnabled);
        ShowSensitiveCheck().IsChecked(loggingEnabled && app->Logs()->IsShowSensitive());
        RequirePasswordCheck().IsChecked(app->Settings()->IsMasterPasswordEnabled());
    }

    void HomePage::OnNavigatedFrom(Navigation::NavigationEventArgs const&)
    {
        uiSettings_.ColorValuesChanged(colorValuesChangedToken_);
        auto app = ::AgentRedactor::AppState::Instance();
        if (app) {
            app->SetOnStatsUpdated(nullptr);
        }
    }

    void HomePage::LoadData()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        std::wstring prevId = currentProfileId_;
        profiles_ = app->Settings()->GetProfiles();
        if (profiles_.empty()) {
            ::AgentRedactor::ApiKeyProfile defaultProfile(::AgentRedactor::LocString(L"Profile_DefaultName"));
            std::vector<int> emptyPorts;
            int port = ::AgentRedactor::FindAvailablePort(8080, emptyPorts);
            defaultProfile.port = (port > 0) ? port : 8080;
            app->Settings()->AddProfile(defaultProfile);
            profiles_ = app->Settings()->GetProfiles();
            currentProfileId_ = defaultProfile.id;
        } else {
            // Keep the current selection (by id) across engine-driven
            // reloads; fall back to the first profile only when the selected
            // one no longer exists.
            bool keep = false;
            for (const auto& p : profiles_) {
                if (p.id == prevId) { keep = true; break; }
            }
            currentProfileId_ = keep ? prevId : profiles_[0].id;
        }
        ApplyTheme();
        LoadProfileList();
        LoadProfileForm();
        LoadPIIGrid();
        LoadRegexList();
        LoadKeywordList();
        LoadMatchesList();
        UpdateStats();

        auto settings = app->Settings();
        RequirePasswordCheck().IsChecked(settings->IsMasterPasswordEnabled());
    }

    void HomePage::LoadProfileList()
    {
        auto list = ProfileList();
        list.Items().Clear();
        for (const auto& p : profiles_) {
            auto item = TextBlock();
            item.Text(p.alias.empty() ? ::AgentRedactor::LocString(L"Profile_Unnamed") : p.alias);
            list.Items().Append(item);
        }
        for (uint32_t i = 0; i < profiles_.size(); ++i) {
            if (profiles_[i].id == currentProfileId_) {
                list.SelectedIndex(i);
                break;
            }
        }
    }

    void HomePage::ProfileList_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        auto idx = ProfileList().SelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(profiles_.size())) return;
        auto newId = profiles_[idx].id;
        if (newId == currentProfileId_) return;
        currentProfileId_ = newId;
        LoadProfileForm();
        LoadPIIGrid();
        LoadRegexList();
        LoadKeywordList();
        UpdateStats();
    }

    void HomePage::AddProfile_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        ::AgentRedactor::ApiKeyProfile newProfile(::AgentRedactor::LocString(L"Profile_NewName"));
        std::vector<int> existingPorts;
        for (const auto& p : profiles_) existingPorts.push_back(p.port);
        int availablePort = ::AgentRedactor::FindAvailablePort(8080, existingPorts);
        newProfile.port = (availablePort > 0) ? availablePort : 8080;
        app->Settings()->AddProfile(newProfile);
        profiles_ = app->Settings()->GetProfiles();
        currentProfileId_ = newProfile.id;
        app->RestartProxyServers();
        LoadProfileList();
        LoadProfileForm();
        LoadPIIGrid();
        LoadRegexList();
        LoadKeywordList();
        UpdateStats();
    }

    void HomePage::RemoveProfile_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (profiles_.size() <= 1) return;
        ShowRemoveProfileConfirmationAsync();
    }

    winrt::fire_and_forget HomePage::ShowRemoveProfileConfirmationAsync()
    {
        auto lifetime = get_strong();
        try
        {
            bool dark = lifetime->IsDarkMode();

            ContentDialog dialog;
            dialog.XamlRoot(lifetime->XamlRoot());
            ThemeContentDialog(dialog, dark);
            dialog.Title(box_value(::AgentRedactor::LocString(L"Dialog_RemoveProfile_Title")));
            dialog.Content(box_value(::AgentRedactor::LocString(L"Dialog_RemoveProfile_Message")));
            dialog.PrimaryButtonText(::AgentRedactor::LocString(L"Dialog_RemoveButton"));
            dialog.CloseButtonText(::AgentRedactor::LocString(L"Dialog_CancelButton"));
            dialog.DefaultButton(ContentDialogButton::Close);

            lifetime->activeDialog_ = dialog;
            auto result = co_await dialog.ShowAsync();
            lifetime->activeDialog_ = nullptr;
            if (result != ContentDialogResult::Primary) co_return;

            auto app = ::AgentRedactor::AppState::Instance();
            if (!app) co_return;
            app->Settings()->RemoveProfile(lifetime->currentProfileId_);
            lifetime->profiles_ = app->Settings()->GetProfiles();
            if (lifetime->profiles_.empty()) {
                app->RestartProxyServers();
                co_return;
            }
            lifetime->currentProfileId_ = lifetime->profiles_[0].id;
            lifetime->LoadProfileList();
            lifetime->LoadProfileForm();
            lifetime->LoadPIIGrid();
            lifetime->LoadRegexList();
            lifetime->LoadKeywordList();
            lifetime->UpdateStats();
            app->RestartProxyServers();
        }
        catch (const winrt::hresult_error& e)
        {
            LOGF(L"[HomePage] ShowRemoveProfileConfirmationAsync hresult error: %s (0x%08X)", e.message().c_str(), e.code());
            lifetime->activeDialog_ = nullptr;
        }
        catch (const std::exception& e)
        {
            LOGF(L"[HomePage] ShowRemoveProfileConfirmationAsync std exception: %s", ::AgentRedactor::Utils::Utf8ToWide(e.what()).c_str());
            lifetime->activeDialog_ = nullptr;
        }
        catch (...)
        {
            LOGF(L"[HomePage] ShowRemoveProfileConfirmationAsync unknown exception");
            lifetime->activeDialog_ = nullptr;
        }
    }

    bool HomePage::ProfilesMatch(const std::vector<::AgentRedactor::ApiKeyProfile>& fresh) const
    {
        // Compare only the fields the list/form display; request/redaction
        // stats change on every request and must never force a reload.
        bool same = fresh.size() == profiles_.size();
        if (same) {
            for (size_t i = 0; i < fresh.size() && same; ++i) {
                const auto& f = fresh[i];
                const auto& o = profiles_[i];
                same = f.id == o.id && f.alias == o.alias && f.port == o.port
                    && f.upstreamUrl == o.upstreamUrl && f.apiKey == o.apiKey
                    && f.useOpenAIModel == o.useOpenAIModel
                    && f.enabledPIITypes == o.enabledPIITypes
                    && f.piiConfidenceThreshold == o.piiConfidenceThreshold
                    && f.regexPatterns.size() == o.regexPatterns.size()
                    && f.keywords.size() == o.keywords.size();
                if (!same) break;
                for (size_t k = 0; k < f.regexPatterns.size(); ++k) {
                    if (f.regexPatterns[k].pattern != o.regexPatterns[k].pattern
                        || f.regexPatterns[k].enabled != o.regexPatterns[k].enabled) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    for (size_t k = 0; k < f.keywords.size(); ++k) {
                        if (f.keywords[k].text != o.keywords[k].text
                            || f.keywords[k].caseSensitive != o.keywords[k].caseSensitive
                            || f.keywords[k].enabled != o.keywords[k].enabled) {
                            same = false;
                            break;
                        }
                    }
                }
            }
        }
        return same;
    }

    void HomePage::RefreshProfilesFromEngine()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto fresh = app->Settings()->GetProfiles();
        if (ProfilesMatch(fresh)) return;

        std::wstring prevId = currentProfileId_;
        profiles_ = std::move(fresh);
        bool keep = false;
        for (const auto& p : profiles_) {
            if (p.id == prevId) { keep = true; break; }
        }
        currentProfileId_ = keep ? prevId : (profiles_.empty() ? L"" : profiles_[0].id);
        LoadProfileList();
        LoadProfileForm();
        UpdateStats();
        LoadMatchesList();
    }

    void HomePage::LoadProfileForm()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;
        auto& p = *opt;
        ProfileNameBox().Text(p.alias);
        PortBox().Text(std::to_wstring(p.port));
        UrlBox().Text(p.upstreamUrl);
        // The engine's profile list only ever serves the masked key; fetch
        // the real one so the box always shows the actual key (including
        // keys set through the CLI) instead of a stale "abc...****" mask.
        std::wstring apiKey = app->Settings()->GetProfileApiKey(currentProfileId_);
        if (apiKey.empty()) apiKey = p.apiKey;
        ApiKeyBox().Password(apiKey);
        // Keep the reveal state across engine-driven reloads: a poll refresh
        // landing while the user has the key shown must not hide it again.
        // Switching profiles still resets it (privacy).
        bool switching = formProfileId_ != currentProfileId_;
        formProfileId_ = currentProfileId_;
        bool wasShown = switching ? false : ShowKeyCheck().IsChecked().GetBoolean();
        ApiKeyBox().PasswordRevealMode(wasShown ? PasswordRevealMode::Visible : PasswordRevealMode::Hidden);
        ShowKeyCheck().IsChecked(wasShown);
        UseOpenAISwitch().IsOn(p.useOpenAIModel);
        ConfidenceThresholdBox().Text(::AgentRedactor::Utils::FormatLocalizedFloat(p.piiConfidenceThreshold, 2));
        UpdateProxyStatus();
    }



    void HomePage::LoadPIIGrid()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;
        auto& enabledTypes = opt->enabledPIITypes;

        auto grid = PiiGrid();
        grid.Children().Clear();
        piiCheckBoxes_.clear();

        int col = 0, row = 0;
        auto checkBrush = isDarkMode_
            ? SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
            : SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26));
        for (const auto& type : ::AgentRedactor::DEFAULT_PII_TYPES) {
            auto cb = CheckBox();
            cb.Content(box_value(::AgentRedactor::LocString(L"PII_Type_" + type)));
            cb.Foreground(checkBrush);
            bool checked = std::find(enabledTypes.begin(), enabledTypes.end(), type) != enabledTypes.end();
            cb.IsChecked(checked);
            Grid::SetColumn(cb, col);
            Grid::SetRow(cb, row);
            grid.Children().Append(cb);
            piiCheckBoxes_.push_back(cb);

            col++;
            if (col >= 4) { col = 0; row++; }
        }
        while (grid.RowDefinitions().Size() <= static_cast<uint32_t>(row)) {
            grid.RowDefinitions().Append(RowDefinition());
        }
    }

    void HomePage::LoadRegexList()
    {
        auto checkBrush = isDarkMode_
            ? SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
            : SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26));
        auto textBrush = isDarkMode_
            ? SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
            : SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26));
        auto delBrush = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 220, 53, 69));
        auto panel = RegexListPanel();
        panel.Children().Clear();
        regexCheckBoxes_.clear();
        regexTextBoxes_.clear();

        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;

        RegexHeaderGrid().Visibility(opt->regexPatterns.empty() ? Visibility::Collapsed : Visibility::Visible);

        for (size_t idx = 0; idx < opt->regexPatterns.size(); ++idx) {
            const auto& rp = opt->regexPatterns[idx];
            auto row = Grid();
            auto c0 = ColumnDefinition(); c0.Width(GridLength{ 70, GridUnitType::Pixel }); row.ColumnDefinitions().Append(c0);
            auto c1 = ColumnDefinition(); c1.Width(GridLength{ 1, GridUnitType::Star }); row.ColumnDefinitions().Append(c1);
            auto c2 = ColumnDefinition(); c2.Width(GridLength{ 1, GridUnitType::Auto }); row.ColumnDefinitions().Append(c2);
            row.Padding(Thickness{ 0, 4, 8, 4 });

            auto cb = CheckBox();
            cb.IsChecked(rp.enabled);
            cb.HorizontalAlignment(HorizontalAlignment::Left);
            cb.VerticalAlignment(VerticalAlignment::Center);
            cb.MinWidth(32);
            cb.MinHeight(0);
            cb.Height(32);
            cb.Padding(Thickness{ 0 });
            cb.Foreground(checkBrush);
            AutomationProperties::SetAutomationId(cb, winrt::hstring(L"RegexCheckBox_" + std::to_wstring(idx)));
            Grid::SetColumn(cb, 0);
            row.Children().Append(cb);
            regexCheckBoxes_.push_back(cb);

            auto tbox = TextBox();
            tbox.Text(rp.pattern);
            tbox.FontFamily(Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
            tbox.FontSize(13);
            tbox.Height(32);
            tbox.VerticalAlignment(VerticalAlignment::Center);
            tbox.Foreground(textBrush);
            tbox.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
            tbox.BorderThickness(Thickness{ 0 });
            tbox.Padding(Thickness{ 0, 8, 0, 0 });
            {
                std::wstring regexAutoId = L"RegexTextBox_" + std::to_wstring(idx) + L"_" + rp.pattern;
                AutomationProperties::SetAutomationId(tbox, winrt::hstring(regexAutoId));
            }
            Grid::SetColumn(tbox, 1);
            row.Children().Append(tbox);
            regexTextBoxes_.push_back(tbox);
            ThemeTextBox(tbox, isDarkMode_);

            tbox.LostFocus([this, idx, tbox, originalPattern = rp.pattern](IInspectable const&, RoutedEventArgs const&) {
                auto text = tbox.Text();
                auto err = ValidateRegex(text.c_str());
                if (!err.empty()) {
                    ShowErrorAsync(err);
                    tbox.Text(originalPattern);
                    return;
                }
                auto app = ::AgentRedactor::AppState::Instance();
                if (!app) return;
                auto opt = app->Settings()->GetProfileById(currentProfileId_);
                if (!opt) return;
                auto p = *opt;
                if (idx >= p.regexPatterns.size()) return;
                auto newPattern = std::wstring(text);
                if (p.regexPatterns[idx].pattern == newPattern) return;
                p.regexPatterns[idx].pattern = newPattern;
                app->Settings()->UpdateProfile(p);
                LOG(L"RegexLostFocus idx=" + std::to_wstring(idx) + L" pattern='" + newPattern + L"'");
            });

            auto delBtn = Button();
            auto icon = FontIcon();
            icon.Glyph(L"\uE74D");
            icon.FontSize(14);
            icon.Foreground(delBrush);
            delBtn.Content(icon);
            delBtn.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
            delBtn.BorderThickness(Thickness{ 0 });
            delBtn.Padding(Thickness{ 4 });
            delBtn.Width(32);
            delBtn.Height(32);
            delBtn.VerticalAlignment(VerticalAlignment::Center);
            AutomationProperties::SetAutomationId(delBtn, winrt::hstring(L"RegexDeleteButton_" + std::to_wstring(idx)));
            Grid::SetColumn(delBtn, 2);
            delBtn.Click([this, idx](IInspectable const&, RoutedEventArgs const&) {
                auto app = ::AgentRedactor::AppState::Instance();
                if (!app) return;
                auto opt = app->Settings()->GetProfileById(currentProfileId_);
                if (!opt) return;
                auto p = *opt;
                if (idx < p.regexPatterns.size()) {
                    p.regexPatterns.erase(p.regexPatterns.begin() + idx);
                    app->Settings()->UpdateProfile(p);
                    LoadRegexList();
                }
            });
            row.Children().Append(delBtn);

            panel.Children().Append(row);
        }
    }

    void HomePage::LoadKeywordList()
    {
        auto checkBrush = isDarkMode_
            ? SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
            : SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26));
        auto textBrush = isDarkMode_
            ? SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255))
            : SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 26, 26, 26));
        auto delBrush = SolidColorBrush(Windows::UI::ColorHelper::FromArgb(255, 220, 53, 69));
        auto panel = KeywordListPanel();
        panel.Children().Clear();
        keywordCheckBoxes_.clear();
        keywordCaseButtons_.clear();
        keywordTextBoxes_.clear();

        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;

        KeywordHeaderGrid().Visibility(opt->keywords.empty() ? Visibility::Collapsed : Visibility::Visible);

        for (size_t idx = 0; idx < opt->keywords.size(); ++idx) {
            const auto& kw = opt->keywords[idx];
            auto row = Grid();
            auto c0 = ColumnDefinition(); c0.Width(GridLength{ 70, GridUnitType::Pixel }); row.ColumnDefinitions().Append(c0);
            auto c1 = ColumnDefinition(); c1.Width(GridLength{ 100, GridUnitType::Pixel }); row.ColumnDefinitions().Append(c1);
            auto c2 = ColumnDefinition(); c2.Width(GridLength{ 1, GridUnitType::Star }); row.ColumnDefinitions().Append(c2);
            auto c3 = ColumnDefinition(); c3.Width(GridLength{ 1, GridUnitType::Auto }); row.ColumnDefinitions().Append(c3);
            row.Padding(Thickness{ 0, 4, 8, 4 });

            auto cb = CheckBox();
            cb.IsChecked(kw.enabled);
            cb.HorizontalAlignment(HorizontalAlignment::Left);
            cb.VerticalAlignment(VerticalAlignment::Center);
            cb.MinWidth(32);
            cb.MinHeight(0);
            cb.Height(32);
            cb.Padding(Thickness{ 0 });
            cb.Foreground(checkBrush);
            AutomationProperties::SetAutomationId(cb, winrt::hstring(L"KeywordCheckBox_" + std::to_wstring(idx)));
            Grid::SetColumn(cb, 0);
            row.Children().Append(cb);
            keywordCheckBoxes_.push_back(cb);

            auto caseBtn = Button();
            AutomationProperties::SetAutomationId(caseBtn, winrt::hstring(L"KeywordCaseButton_" + std::to_wstring(idx)));
            caseBtn.Content(box_value(kw.caseSensitive ? ::AgentRedactor::LocString(L"Common_Yes") : ::AgentRedactor::LocString(L"Common_No")));
            caseBtn.FontSize(13);
            caseBtn.Height(32);
            caseBtn.VerticalAlignment(VerticalAlignment::Center);
            caseBtn.VerticalContentAlignment(VerticalAlignment::Center);
            caseBtn.Foreground(textBrush);
            caseBtn.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
            caseBtn.BorderThickness(Thickness{ 0 });
            caseBtn.Padding(Thickness{ 0 });
            Grid::SetColumn(caseBtn, 1);
            row.Children().Append(caseBtn);
            keywordCaseButtons_.push_back(caseBtn);

            caseBtn.Click([caseBtn](IInspectable const&, RoutedEventArgs const&) {
                auto txt = unbox_value<hstring>(caseBtn.Content());
                auto yes = ::AgentRedactor::LocString(L"Common_Yes");
                caseBtn.Content(box_value(txt == yes ? ::AgentRedactor::LocString(L"Common_No") : yes));
            });

            auto kwBox = TextBox();
            kwBox.Text(kw.text);
            kwBox.FontSize(13);
            kwBox.Height(32);
            kwBox.VerticalAlignment(VerticalAlignment::Center);
            kwBox.Foreground(textBrush);
            kwBox.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
            kwBox.BorderThickness(Thickness{ 0 });
            kwBox.Padding(Thickness{ 0, 8, 0, 0 });
            {
                std::wstring kwAutoId = L"KeywordTextBox_" + std::to_wstring(idx) + L"_" + kw.text;
                AutomationProperties::SetAutomationId(kwBox, winrt::hstring(kwAutoId));
            }
            kwBox.LostFocus([this, idx, kwBox](IInspectable const&, RoutedEventArgs const&) {
                auto app = ::AgentRedactor::AppState::Instance();
                if (!app) return;
                auto opt = app->Settings()->GetProfileById(currentProfileId_);
                if (!opt) return;
                auto p = *opt;
                if (idx < p.keywords.size()) {
                    auto newText = std::wstring(kwBox.Text());
                    LOG(L"KeywordLostFocus idx=" + std::to_wstring(idx) + L" oldProfileText='" + p.keywords[idx].text + L"' textBoxText='" + newText + L"'");
                    p.keywords[idx].text = newText;
                    app->Settings()->UpdateProfile(p);
                }
            });
            Grid::SetColumn(kwBox, 2);
            row.Children().Append(kwBox);
            keywordTextBoxes_.push_back(kwBox);
            ThemeTextBox(kwBox, isDarkMode_);

            auto delBtn = Button();
            AutomationProperties::SetAutomationId(delBtn, winrt::hstring(L"KeywordDeleteButton_" + std::to_wstring(idx)));
            auto icon = FontIcon();
            icon.Glyph(L"\uE74D");
            icon.FontSize(14);
            icon.Foreground(delBrush);
            delBtn.Content(icon);
            delBtn.Background(SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
            delBtn.BorderThickness(Thickness{ 0 });
            delBtn.Padding(Thickness{ 4 });
            delBtn.Width(32);
            delBtn.Height(32);
            delBtn.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(delBtn, 3);
            delBtn.Click([this, idx](IInspectable const&, RoutedEventArgs const&) {
                auto app = ::AgentRedactor::AppState::Instance();
                if (!app) return;
                auto opt = app->Settings()->GetProfileById(currentProfileId_);
                if (!opt) return;
                auto p = *opt;
                if (idx < p.keywords.size()) {
                    p.keywords.erase(p.keywords.begin() + idx);
                    app->Settings()->UpdateProfile(p);
                    LoadKeywordList();
                }
            });
            row.Children().Append(delBtn);

            panel.Children().Append(row);
        }
    }

    void HomePage::UpdateStats()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;
        auto& s = opt->stats;
        std::wstring txt = ::AgentRedactor::LocString(L"Stats_Requests") + L": " + std::to_wstring(s.totalRequests)
            + L"  |  " + ::AgentRedactor::LocString(L"Stats_PII") + L": " + std::to_wstring(s.totalPIIDetected)
            + L"  |  " + ::AgentRedactor::LocString(L"Stats_Regex") + L": " + std::to_wstring(s.totalRegexMatches)
            + L"  |  " + ::AgentRedactor::LocString(L"Stats_Keywords") + L": " + std::to_wstring(s.totalKeywordMatches);
        StatsBlock().Text(txt);
        UpdateProxyStatus();
    }

    void HomePage::LoadMatchesList()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto list = MatchesList();
        auto matches = app->Proxy()->GetSessionMatches(currentProfileId_);
        // The 1-second stats poll calls this constantly; only rebuild when
        // the content actually changed, or the empty placeholder below would
        // clear + re-append every second (visibly flashing "No Redactions").
        std::wstring fingerprint;
        for (const auto& m : matches) {
            fingerprint += m.timestamp + L"|" + m.type + L"|" + m.detail + L"|" + m.matchedText + L"\n";
        }
        if (matchesLoaded_ && fingerprint == matchesFingerprint_) return;
        matchesFingerprint_ = std::move(fingerprint);
        matchesLoaded_ = true;

        list.Items().Clear();
        if (matches.empty()) {
            list.Items().Append(box_value(::AgentRedactor::LocString(L"HomePage_SessionRedactions_Empty")));
            return;
        }
        for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
            // The engine serves match types via an English-only shim (it cannot
            // link MRT localization); map them back to the localized strings.
            std::wstring type = it->type;
            if (type == L"PII") type = ::AgentRedactor::LocString(L"MatchType_PII");
            else if (type == L"Regex") type = ::AgentRedactor::LocString(L"MatchType_Regex");
            else if (type == L"Keyword") type = ::AgentRedactor::LocString(L"MatchType_Keyword");
            std::wstring line = L"[" + it->timestamp + L"] " + type;
            if (!it->detail.empty()) line += L" (" + it->detail + L")";
            line += L": " + it->matchedText;
            list.Items().Append(box_value(line));
        }
    }

    void HomePage::ClearMatchesBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        app->Proxy()->ClearSessionMatches(currentProfileId_);
        LoadMatchesList();
    }

    void HomePage::UpdateProxyStatus()
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        int port = _wtoi(PortBox().Text().c_str());
        if (port < 1024 || port > 65535) {
            ProxyStatusBlock().Text(L"");
            return;
        }
        std::wstring otherProfileName;
        for (const auto& other : profiles_) {
            if (other.id != currentProfileId_ && other.port == port) {
                otherProfileName = other.alias;
                break;
            }
        }
        auto statusBlock = ProxyStatusBlock();
        if (!otherProfileName.empty()) {
            statusBlock.Text(::AgentRedactor::LocFormat(L"HomePage_ProxyStatus_PortUsedByProfile", { std::to_wstring(port), otherProfileName }));
            auto red = Windows::UI::ColorHelper::FromArgb(255, 220, 53, 69);
            statusBlock.Foreground(SolidColorBrush(red));
        } else {
            bool running = app->IsProxyRunning(port);
            bool available = ::AgentRedactor::IsPortAvailable(port);
            if (running || available) {
                statusBlock.Text(::AgentRedactor::LocFormat(L"HomePage_ProxyStatus_PortAvailable", { std::to_wstring(port) }));
                auto green = isDarkMode_
                    ? Windows::UI::ColorHelper::FromArgb(255, 100, 255, 100)
                    : Windows::UI::ColorHelper::FromArgb(255, 0, 150, 0);
                statusBlock.Foreground(SolidColorBrush(green));
            } else {
                statusBlock.Text(::AgentRedactor::LocFormat(L"HomePage_ProxyStatus_PortInUse", { std::to_wstring(port) }));
                auto red = Windows::UI::ColorHelper::FromArgb(255, 220, 53, 69);
                statusBlock.Foreground(SolidColorBrush(red));
            }
        }
    }

    void HomePage::ShowKey_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        bool show = ShowKeyCheck().IsChecked().GetBoolean();
        ApiKeyBox().PasswordRevealMode(show ? PasswordRevealMode::Visible : PasswordRevealMode::Hidden);
    }

    void HomePage::SaveProfile_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;
        auto p = *opt;
        p.alias = ProfileNameBox().Text().c_str();
        p.port = _wtoi(PortBox().Text().c_str());

        // Port validation
        if (p.port < 1024 || p.port > 65535) {
            ShowPortErrorAsync(::AgentRedactor::LocString(L"Validation_PortRange"));
            return;
        }
        for (const auto& other : profiles_) {
            if (other.id != p.id && other.port == p.port) {
                ShowPortErrorAsync(::AgentRedactor::LocFormat(L"Validation_PortUsed", { std::to_wstring(p.port), other.alias }));
                return;
            }
        }

        std::wstring url = UrlBox().Text().c_str();
        std::wstring lowerUrl = ::AgentRedactor::Utils::ToLower(url);
        if (url.empty()) {
            ShowPortErrorAsync(::AgentRedactor::LocString(L"Validation_UrlEmpty"));
            return;
        }
        if (!::AgentRedactor::Utils::StartsWith(lowerUrl, L"http://") && !::AgentRedactor::Utils::StartsWith(lowerUrl, L"https://")) {
            ShowPortErrorAsync(::AgentRedactor::LocString(L"Validation_UrlProtocol"));
            return;
        }
        std::wstring urlHost;
        {
            URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
            urlComp.dwSchemeLength = (DWORD)-1;
            urlComp.dwHostNameLength = (DWORD)-1;
            urlComp.dwUrlPathLength = (DWORD)-1;
            urlComp.dwExtraInfoLength = (DWORD)-1;
            if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComp)) {
                ShowPortErrorAsync(::AgentRedactor::LocString(L"Validation_UrlInvalid"));
                return;
            }
            if (urlComp.lpszHostName && urlComp.dwHostNameLength > 0) {
                urlHost.assign(urlComp.lpszHostName, urlComp.dwHostNameLength);
            }
        }
        p.upstreamUrl = url;
        if (::AgentRedactor::Utils::StartsWith(lowerUrl, L"http://") &&
            urlHost != L"localhost" &&
            urlHost != L"127.0.0.1" &&
            urlHost != L"[::1]") {
            ShowHttpWarningAsync();
        }
        p.useOpenAIModel = UseOpenAISwitch().IsOn();
        p.protocolMode = ::AgentRedactor::ProtocolMode::None;

        // Confidence threshold validation
        float confThreshold = static_cast<float>(::AgentRedactor::Utils::ParseLocalizedFloat(ConfidenceThresholdBox().Text().c_str()));
        if (confThreshold < 0.0f || confThreshold > 1.0f) {
            ShowPortErrorAsync(::AgentRedactor::LocString(L"Validation_ConfidenceRange"));
            return;
        }
        p.piiConfidenceThreshold = confThreshold;

        // Save inline regex edits
        for (size_t i = 0; i < regexCheckBoxes_.size() && i < p.regexPatterns.size(); ++i) {
            auto pattern = std::wstring(regexTextBoxes_[i].Text());
            auto err = ValidateRegex(pattern);
            if (!err.empty()) {
                ShowErrorAsync(err);
                return;
            }
            p.regexPatterns[i].enabled = regexCheckBoxes_[i].IsChecked().GetBoolean();
            p.regexPatterns[i].pattern = pattern;
        }

        // Save inline keyword edits
        for (size_t i = 0; i < keywordCheckBoxes_.size() && i < p.keywords.size(); ++i) {
            p.keywords[i].enabled = keywordCheckBoxes_[i].IsChecked().GetBoolean();
            auto tbText = std::wstring(keywordTextBoxes_[i].Text());
            p.keywords[i].text = tbText;
            p.keywords[i].caseSensitive = unbox_value<hstring>(keywordCaseButtons_[i].Content()) == ::AgentRedactor::LocString(L"Common_Yes");
        }

        p.apiKey = ApiKeyBox().Password().c_str();

        p.enabledPIITypes.clear();
        for (uint32_t i = 0; i < piiCheckBoxes_.size() && i < ::AgentRedactor::DEFAULT_PII_TYPES.size(); ++i) {
            if (piiCheckBoxes_[i].IsChecked().GetBoolean()) {
                p.enabledPIITypes.push_back(::AgentRedactor::DEFAULT_PII_TYPES[i]);
            }
        }

        app->Settings()->UpdateProfile(p);
        app->RestartProxyServers();

        profiles_ = app->Settings()->GetProfiles();
        LoadProfileList();
        UpdateProxyStatus();
    }

    void HomePage::AddRegex_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto txt = NewRegexBox().Text();
        if (txt.empty()) return;
        auto err = ValidateRegex(txt.c_str());
        if (!err.empty()) {
            ShowErrorAsync(err);
            return;
        }
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;
        auto p = *opt;
        p.regexPatterns.push_back({ txt.c_str(), true });
        app->Settings()->UpdateProfile(p);
        LoadRegexList();
        NewRegexBox().Text(L"");
    }

    void HomePage::AddKeyword_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto txt = NewKeywordBox().Text();
        if (txt.empty()) return;
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;
        auto p = *opt;
        p.keywords.push_back({ txt.c_str(), CaseSensitiveCheck().IsChecked().GetBoolean(), true });
        app->Settings()->UpdateProfile(p);
        LoadKeywordList();
        NewKeywordBox().Text(L"");
    }

    // -------------------------------------------------------------------------
    // Windows Hello protection handlers (checkbox-based)
    // -------------------------------------------------------------------------
    void HomePage::RequirePassword_Click(IInspectable const&, RoutedEventArgs const&)
    {
        // Guard against re-entry: a second click while a dialog is in flight
        // would throw (only one ContentDialog can be open) and, inside the
        // fire_and_forget coroutine, terminate the process.
        if (activeDialog_) return;
        bool checked = RequirePasswordCheck().IsChecked().GetBoolean();

        if (checked) {
            // Enabling needs no consent and no confirmation: turn protection
            // on directly (the engine persists a random AES key wrapped in
            // DPAPI — there is no typed password to set).
            auto app = ::AgentRedactor::AppState::Instance();
            if (!app || !app->Settings()->EnableMasterPassword()) {
                RequirePasswordCheck().IsChecked(false);
                ShowEnablePasswordFailedAsync();
            }
        } else {
            // Disabling DOES require a fresh Windows Hello verification
            // before protection is turned off; revert the checkbox until
            // that verification has succeeded.
            RequirePasswordCheck().IsChecked(true);
            DisableWithHelloAsync();
        }
    }

    winrt::fire_and_forget HomePage::ShowEnablePasswordFailedAsync()
    {
        auto lifetime = get_strong();
        ContentDialog err;
        err.XamlRoot(lifetime->XamlRoot());
        ThemeContentDialog(err, lifetime->IsDarkMode());
        err.Title(box_value(::AgentRedactor::LocString(L"Dialog_EnableMasterPasswordFailed_Title")));
        err.Content(box_value(::AgentRedactor::LocString(L"Dialog_EnableMasterPasswordFailed_Message")));
        err.CloseButtonText(::AgentRedactor::LocString(L"Dialog_OKButton"));
        lifetime->activeDialog_ = err;
        co_await err.ShowAsync();
        lifetime->activeDialog_ = nullptr;
    }

    winrt::fire_and_forget HomePage::DisableWithHelloAsync()
    {
        auto lifetime = get_strong();
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) {
            lifetime->RequirePasswordCheck().IsChecked(true);
            co_return;
        }
        // The consent prompt runs in-process (GUI-side system UI owned by the
        // main window, so it stays in front) via the async coroutine, which
        // keeps the UI thread pumping. Cover the window with the opaque
        // padlock overlay for the whole flow: the Windows Security dialog
        // floats over the app, and the regex/keywords/PII content must never
        // be visible behind it.
        app->SetSessionLockOverlay(true);
        LOG(L"[HomePage] Disable flow: padlock overlay shown, prompt starting");
        auto message = ::AgentRedactor::LocString(L"Dialog_DisableHello_Message");
        bool verified = false;
        try {
            verified = co_await ::AgentRedactor::RequestHelloUnlockAsync(app->MainWindow(), message);
        } catch (...) {
            verified = false;
        }
        if (!verified) {
            ContentDialog err;
            err.XamlRoot(lifetime->XamlRoot());
            ThemeContentDialog(err, lifetime->IsDarkMode());
            err.Title(box_value(::AgentRedactor::LocString(L"Dialog_HelloFailed_Title")));
            err.Content(box_value(::AgentRedactor::LocString(L"Dialog_DisableHelloFailed_Message")));
            err.CloseButtonText(::AgentRedactor::LocString(L"Dialog_OKButton"));
            lifetime->activeDialog_ = err;
            co_await err.ShowAsync();
            lifetime->activeDialog_ = nullptr;
            app->SetSessionLockOverlay(false);
            co_return;
        }
        app->Settings()->DisableMasterPassword();
        app->SetSessionLockOverlay(false);
        lifetime->RequirePasswordCheck().IsChecked(false);
    }

    void HomePage::ClearStatisticsBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        auto opt = app->Settings()->GetProfileById(currentProfileId_);
        if (!opt) return;
        auto p = *opt;
        p.stats = {};
        app->Settings()->UpdateProfile(p);
        UpdateStats();
    }

    void HomePage::ClearLogsBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShowClearLogsConfirmationAsync();
    }

    winrt::fire_and_forget HomePage::ShowClearLogsConfirmationAsync()
    {
        auto lifetime = get_strong();
        try
        {
            bool dark = lifetime->IsDarkMode();

            ContentDialog dialog;
            dialog.XamlRoot(lifetime->XamlRoot());
            ThemeContentDialog(dialog, dark);
            dialog.Title(box_value(::AgentRedactor::LocString(L"Dialog_DeleteAllLogs_Title")));
            dialog.Content(box_value(::AgentRedactor::LocString(L"Dialog_DeleteAllLogs_Message")));
            dialog.PrimaryButtonText(::AgentRedactor::LocString(L"Dialog_DeleteAllLogs_Button"));
            dialog.CloseButtonText(::AgentRedactor::LocString(L"Dialog_CancelButton"));
            dialog.DefaultButton(ContentDialogButton::Close);

            lifetime->activeDialog_ = dialog;
            auto result = co_await dialog.ShowAsync();
            lifetime->activeDialog_ = nullptr;

            if (result == ContentDialogResult::Primary) {
                auto logDir = ::AgentRedactor::Utils::GetAppDataPath();
                auto logFile = logDir / L"agent_redactor.log";
                auto sessionsDir = logDir / L"sessions";
                try {
                    std::filesystem::remove(logFile);
                    if (std::filesystem::exists(sessionsDir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(sessionsDir)) {
                            std::filesystem::remove(entry.path());
                        }
                    }
                } catch (...) {}
            }
        }
        catch (const winrt::hresult_error& e)
        {
            LOGF(L"[HomePage] ShowClearLogsConfirmationAsync hresult error: %s (0x%08X)", e.message().c_str(), e.code());
            lifetime->activeDialog_ = nullptr;
        }
        catch (const std::exception& e)
        {
            LOGF(L"[HomePage] ShowClearLogsConfirmationAsync std exception: %s", ::AgentRedactor::Utils::Utf8ToWide(e.what()).c_str());
            lifetime->activeDialog_ = nullptr;
        }
        catch (...)
        {
            LOGF(L"[HomePage] ShowClearLogsConfirmationAsync unknown exception");
            lifetime->activeDialog_ = nullptr;
        }
    }

    void HomePage::EnableLogging_Click(IInspectable const&, RoutedEventArgs const&)
    {
        bool checked = EnableLoggingCheck().IsChecked().GetBoolean();
        auto app = ::AgentRedactor::AppState::Instance();
        if (!app) return;
        app->Settings()->SetLoggingEnabled(checked);
        app->Logs()->SetLoggingEnabled(checked);
        ShowSensitiveCheck().IsEnabled(checked);
        if (!checked) {
            ShowSensitiveCheck().IsChecked(false);
            app->Logs()->SetShowSensitive(false);
        }
    }

    void HomePage::ShowSensitive_Click(IInspectable const&, RoutedEventArgs const&)
    {
        bool checked = ShowSensitiveCheck().IsChecked().GetBoolean();
        if (checked) {
            ShowSensitiveInfoDialogAsync();
        } else {
            auto app = ::AgentRedactor::AppState::Instance();
            if (app) {
                app->Logs()->SetShowSensitive(false);
            }
        }
    }

    winrt::fire_and_forget HomePage::ShowSensitiveInfoDialogAsync()
    {
        auto lifetime = get_strong();
        try
        {
            bool dark = lifetime->IsDarkMode();

            ContentDialog dialog;
            dialog.XamlRoot(lifetime->XamlRoot());
            ThemeContentDialog(dialog, dark);
            dialog.Title(box_value(::AgentRedactor::LocString(L"Dialog_EnableSensitiveInfo_Title")));
            dialog.Content(box_value(::AgentRedactor::LocString(L"Dialog_EnableSensitiveInfo_Message")));
            dialog.PrimaryButtonText(::AgentRedactor::LocString(L"Dialog_EnableButton"));
            dialog.CloseButtonText(::AgentRedactor::LocString(L"Dialog_CancelButton"));
            dialog.DefaultButton(ContentDialogButton::Primary);

            lifetime->activeDialog_ = dialog;
            auto result = co_await dialog.ShowAsync();
            lifetime->activeDialog_ = nullptr;

            if (result == ContentDialogResult::Primary) {
                auto app = ::AgentRedactor::AppState::Instance();
                if (app) {
                    app->Logs()->SetShowSensitive(true);
                }
                lifetime->ShowSensitiveCheck().IsChecked(true);
            } else {
                lifetime->ShowSensitiveCheck().IsChecked(false);
            }
        }
        catch (const winrt::hresult_error& e)
        {
            LOGF(L"[HomePage] ShowSensitiveInfoDialogAsync hresult error: %s (0x%08X)", e.message().c_str(), e.code());
            lifetime->activeDialog_ = nullptr;
        }
        catch (const std::exception& e)
        {
            LOGF(L"[HomePage] ShowSensitiveInfoDialogAsync std exception: %s", ::AgentRedactor::Utils::Utf8ToWide(e.what()).c_str());
            lifetime->activeDialog_ = nullptr;
        }
        catch (...)
        {
            LOGF(L"[HomePage] ShowSensitiveInfoDialogAsync unknown exception");
            lifetime->activeDialog_ = nullptr;
        }
    }

    void HomePage::OpenLog_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto path = ::AgentRedactor::Utils::GetCurrentLogFilePath();
        if (!path.empty() && ::AgentRedactor::Utils::FileExists(path)) {
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    void HomePage::OpenLogsFolder_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto path = ::AgentRedactor::Utils::GetAppDataPath();
        if (!path.empty() && ::AgentRedactor::Utils::FileExists(path)) {
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    std::wstring HomePage::ValidateRegex(const std::wstring& pattern)
    {
        try {
            std::wregex re(pattern, std::regex_constants::ECMAScript);
        } catch (const std::regex_error& e) {
            return ::AgentRedactor::LocFormat(L"Validation_InvalidRegex", { ::AgentRedactor::Utils::Utf8ToWide(e.what()) });
        } catch (...) {
            return ::AgentRedactor::LocString(L"Validation_InvalidRegexGeneric");
        }
        return L"";
    }

    winrt::fire_and_forget HomePage::ShowErrorAsync(std::wstring message)
    {
        auto lifetime = get_strong();
        bool dark = lifetime->IsDarkMode();

        ContentDialog dialog;
        dialog.XamlRoot(lifetime->XamlRoot());
        ThemeContentDialog(dialog, dark);
        dialog.Title(box_value(::AgentRedactor::LocString(L"Dialog_Error_Title")));
        dialog.Content(box_value(message));
        dialog.CloseButtonText(::AgentRedactor::LocString(L"Dialog_OKButton"));

        lifetime->activeDialog_ = dialog;
        co_await dialog.ShowAsync();
        lifetime->activeDialog_ = nullptr;
    }

    winrt::fire_and_forget HomePage::ShowPortErrorAsync(std::wstring message)
    {
        auto lifetime = get_strong();
        bool dark = lifetime->IsDarkMode();

        ContentDialog dialog;
        dialog.XamlRoot(lifetime->XamlRoot());
        ThemeContentDialog(dialog, dark);
        dialog.Title(box_value(::AgentRedactor::LocString(L"Dialog_ValidationError_Title")));
        dialog.Content(box_value(message));
        dialog.CloseButtonText(::AgentRedactor::LocString(L"Dialog_OKButton"));

        lifetime->activeDialog_ = dialog;
        co_await dialog.ShowAsync();
        lifetime->activeDialog_ = nullptr;
    }

    winrt::fire_and_forget HomePage::ShowHttpWarningAsync()
    {
        auto lifetime = get_strong();
        if (lifetime->activeDialog_) {
            co_return;
        }
        bool dark = lifetime->IsDarkMode();

        ContentDialog dialog;
        dialog.XamlRoot(lifetime->XamlRoot());
        ThemeContentDialog(dialog, dark);
        dialog.Title(box_value(::AgentRedactor::LocString(L"Dialog_SecurityWarning_Title")));
        dialog.Content(box_value(::AgentRedactor::LocString(L"Dialog_HttpWarning_Message")));
        dialog.CloseButtonText(::AgentRedactor::LocString(L"Dialog_OKButton"));

        lifetime->activeDialog_ = dialog;
        co_await dialog.ShowAsync();
        lifetime->activeDialog_ = nullptr;
    }
}
