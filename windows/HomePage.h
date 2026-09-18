#pragma once
#include "HomePage.g.h"
#include "HomePage.xaml.g.h"
#include "api_key_profile.h"
#include <winrt/Windows.UI.ViewManagement.h>

namespace winrt::AgentRedactor::implementation
{
    struct HomePage : HomePageT<HomePage>
    {
        HomePage();
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnNavigatedFrom(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);
        void OnLoaded(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ClearMatchesBtn_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ClearStatisticsBtn_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ClearLogsBtn_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void EnableLogging_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ShowSensitive_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget ShowSensitiveInfoDialogAsync();
        void OpenLog_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenLogsFolder_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget ShowClearLogsConfirmationAsync();
        winrt::fire_and_forget ShowErrorAsync(std::wstring message);
        static std::wstring ValidateRegex(const std::wstring& pattern);
        void LocalizeStaticUI();
        // Re-read engine-owned settings/profiles (e.g. after a CLI `set`),
        // refreshing all settings-driven controls on the page.
        void RefreshFromEngine();

    private:
        void ApplyTheme();
        bool IsDarkMode() const;
        void OnColorValuesChanged(Windows::UI::ViewManagement::UISettings const& sender, Windows::Foundation::IInspectable const& args);
        void ReloadThemedContent();

        bool isDarkMode_ = true;
        Windows::UI::ViewManagement::UISettings uiSettings_;
        event_token colorValuesChangedToken_;

        void LoadData();
        void LoadProfileList();
        void LoadProfileForm();
        // Commit-style autosave: full validation + UpdateProfile + proxy
        // restart. Returns false when any field is invalid (the validation
        // dialog was already shown and nothing was saved). Used by the
        // manual Save button and every autosave/commit trigger.
        bool SaveProfileFromUi();
        // Full revert of the form + grids from the engine snapshot (Linux
        // reloadProfiles(true) parity) after a failed commit-style autosave.
        void ReloadProfileUi();
        // Confidence-field commit (LostFocus / Enter key): tries to save the
        // whole form, reverting on any invalid field.
        void Confidence_Commit();
        // Re-reads the engine's profile snapshot; reloads the list + form
        // only when the visible fields actually changed (e.g. CLI-driven
        // alias/api-key edits). Stats churn alone never triggers a reload,
        // so in-progress UI edits are never stomped by the 1s poll.
        void RefreshProfilesFromEngine();
        // True when `fresh` (an engine profile snapshot) matches the cached
        // profiles_ on exactly the fields the list/form display.
        bool ProfilesMatch(const std::vector<::AgentRedactor::ApiKeyProfile>& fresh) const;
        // The diff above compares the MASKED api key ("abc...****"), which
        // cannot distinguish two keys sharing their first 3 characters; call
        // this after a "no visible change" diff to still re-fetch the real
        // key for the selected profile and sync the box (Linux's
        // unconditional reloadProfiles picks such changes up; the diff does
        // not).
        void RefreshSelectedProfileApiKey();
        void LoadPIIGrid();
        void LoadRegexList();
        void LoadKeywordList();
        void LoadMatchesList(bool force = false);
        void UpdateStats();
        void UpdateProxyStatus();

        void FinishInitialization();
        winrt::fire_and_forget DisableWithHelloAsync();
        winrt::fire_and_forget ShowEnablePasswordFailedAsync();
        winrt::fire_and_forget ShowRemoveProfileConfirmationAsync();
        winrt::fire_and_forget ShowPortErrorAsync(std::wstring message);
        winrt::fire_and_forget ShowHttpWarningAsync();

        void ProfileList_SelectionChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void AddProfile_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RemoveProfile_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SaveProfile_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ShowKey_Toggled(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AddRegex_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AddKeyword_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RequirePassword_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        Microsoft::UI::Xaml::Controls::ContentDialog activeDialog_{ nullptr };

        std::vector<::AgentRedactor::ApiKeyProfile> profiles_;
        std::wstring currentProfileId_;
        std::vector<Microsoft::UI::Xaml::Controls::CheckBox> piiCheckBoxes_;
        std::vector<Microsoft::UI::Xaml::Controls::CheckBox> regexCheckBoxes_;
        std::vector<Microsoft::UI::Xaml::Controls::CheckBox> keywordCheckBoxes_;
        std::vector<Microsoft::UI::Xaml::Controls::TextBox> regexTextBoxes_;
        std::vector<Microsoft::UI::Xaml::Controls::Button> keywordCaseButtons_;
        std::vector<Microsoft::UI::Xaml::Controls::TextBox> keywordTextBoxes_;

        // Profile id the form was last loaded for; a fresh selection resets
        // the Show Key reveal (privacy), engine-driven reloads of the same
        // profile keep the user's reveal state.
        std::wstring formProfileId_;

        // Change-diff guard for the 1-second stats poll: the session-matches
        // list is rebuilt only when its content actually changed (otherwise
        // the "No Redactions" placeholder visibly flashes every second).
        std::wstring matchesFingerprint_;
        bool matchesLoaded_ = false;
        // Last engine-side profile revision shown in the form; a newer
        // revision (e.g. a CLI `set alias` / `set api-key`) reloads the page.
        unsigned long long profilesRevisionShown_ = 0;

        // Guards the programmatic form population in LoadProfileForm():
        // ToggleSwitch.Toggled fires on IsOn() assignments, and the
        // autosave handler must not echo a programmatic set back to the
        // engine (Linux `loading_` parity).
        bool loadingProfileForm_ = false;

        // True while any of the four profile text fields (alias/port/url/
        // api-key) has uncommitted edits. While set, the engine-driven
        // refreshes (1s poll + profilesRevision tick) defer the whole form
        // rebuild so a pending edit is never wiped (Linux `dirty_` parity);
        // SaveProfileFromUi() or LoadProfileForm() clears it.
        bool formDirty_ = false;

        // Last api-key value the code wrote to the box (LoadProfileForm /
        // RefreshSelectedProfileApiKey) or committed (SaveProfileFromUi).
        // WinUI 3 fires PasswordChanged for programmatic Password() and
        // PasswordRevealMode changes, and the event can arrive
        // asynchronously AFTER loadingProfileForm_ is reset to false —
        // comparing the box against this value keeps those from marking the
        // form dirty; only a genuinely different value is a user edit.
        // Qt's textEdited never fires for programmatic/echo-mode changes, so
        // the Linux GUI has no such contamination (its dirty_ flag is
        // user-input only).
        std::wstring programmaticApiKey_;

    };
}

namespace winrt::AgentRedactor::factory_implementation
{
    struct HomePage : HomePageT<HomePage, implementation::HomePage>
    {
    };
}
