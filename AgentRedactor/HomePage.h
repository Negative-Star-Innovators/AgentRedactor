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
        void LoadPIIGrid();
        void LoadRegexList();
        void LoadKeywordList();
        void LoadMatchesList();
        void UpdateStats();
        void UpdateProxyStatus();

        winrt::fire_and_forget ShowStartupPasswordDialogAsync();
        void FinishInitialization();
        winrt::fire_and_forget ShowEnablePasswordDialogAsync();
        winrt::fire_and_forget ShowDisablePasswordDialogAsync();
        winrt::fire_and_forget ShowChangePasswordDialogAsync();
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
        void ChangePassword_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        Microsoft::UI::Xaml::Controls::ContentDialog activeDialog_{ nullptr };

        std::vector<::AgentRedactor::ApiKeyProfile> profiles_;
        std::wstring currentProfileId_;
        std::vector<Microsoft::UI::Xaml::Controls::CheckBox> piiCheckBoxes_;
        std::vector<Microsoft::UI::Xaml::Controls::CheckBox> regexCheckBoxes_;
        std::vector<Microsoft::UI::Xaml::Controls::CheckBox> keywordCheckBoxes_;
        std::vector<Microsoft::UI::Xaml::Controls::TextBox> regexTextBoxes_;
        std::vector<Microsoft::UI::Xaml::Controls::Button> keywordCaseButtons_;
        std::vector<Microsoft::UI::Xaml::Controls::TextBox> keywordTextBoxes_;


    };
}

namespace winrt::AgentRedactor::factory_implementation
{
    struct HomePage : HomePageT<HomePage, implementation::HomePage>
    {
    };
}
