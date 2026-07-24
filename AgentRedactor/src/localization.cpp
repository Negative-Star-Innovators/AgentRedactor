#include "pch.h"
#include "localization.h"
#include "settings_manager.h"
#include "AppState.h"
#include "logging.h"
#include "constants.h"
#include <winrt/Windows.ApplicationModel.Resources.h>
#include <winrt/Windows.ApplicationModel.Resources.Core.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.System.UserProfile.h>
#include <sstream>

namespace AgentRedactor {

namespace {

winrt::Windows::ApplicationModel::Resources::ResourceLoader GetResourceLoader()
{
    try {
        return winrt::Windows::ApplicationModel::Resources::ResourceLoader::GetForViewIndependentUse();
    } catch (...) {
        return nullptr;
    }
}

std::wstring GetSavedLanguageOverride()
{
    auto app = AppState::Instance();
    if (!app) return L"";
    auto settings = app->Settings();
    if (!settings) return L"";
    return settings->GetAppLanguage();
}

void SaveLanguageOverride(const std::wstring& language)
{
    auto app = AppState::Instance();
    if (!app) return;
    auto settings = app->Settings();
    if (!settings) return;
    settings->SetAppLanguage(language);
}

} // anonymous namespace

void InitializeLocalization()
{
    auto overrideLang = GetSavedLanguageOverride();
    if (!overrideLang.empty()) {
        try {
            // For unpackaged desktop apps, set the MRT language qualifier globally.
            winrt::Windows::ApplicationModel::Resources::Core::ResourceContext::SetGlobalQualifierValue(L"Language", overrideLang);
            LOG(L"[Localization] Global language qualifier set to: " + overrideLang);
        } catch (...) {
            LOG(L"[Localization] Failed to set global language qualifier to: " + overrideLang);
        }

        try {
            winrt::Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride(overrideLang);
            LOG(L"[Localization] PrimaryLanguageOverride set to: " + overrideLang);
        } catch (...) {
            LOG(L"[Localization] PrimaryLanguageOverride failed for: " + overrideLang);
        }
    } else {
        LOG(L"[Localization] No saved language override");
    }
}

std::wstring LocString(std::wstring_view key)
{
    auto loader = GetResourceLoader();
    if (!loader) return std::wstring(key);
    try {
        return std::wstring(loader.GetString(winrt::hstring(key)).c_str());
    } catch (...) {
        return std::wstring(key);
    }
}

std::wstring LocFormat(std::wstring_view key, std::initializer_list<std::wstring_view> args)
{
    std::wstring value = LocString(key);
    size_t index = 0;
    for (const auto& arg : args) {
        std::wostringstream placeholder;
        placeholder << L"{" << index << L"}";
        size_t pos = 0;
        while ((pos = value.find(placeholder.str(), pos)) != std::wstring::npos) {
            value.replace(pos, placeholder.str().length(), arg.data(), arg.length());
            pos += arg.length();
        }
        ++index;
    }
    return value;
}

bool SetLanguageOverride(const std::wstring& language)
{
    try {
        winrt::Windows::ApplicationModel::Resources::Core::ResourceContext::SetGlobalQualifierValue(L"Language", language);
    } catch (...) {
        return false;
    }

    try {
        winrt::Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride(language);
    } catch (...) {
        LOG(L"[Localization] PrimaryLanguageOverride failed for: " + language);
    }

    SaveLanguageOverride(language);
    return true;
}

std::wstring GetCurrentLanguage()
{
    try {
        auto context = winrt::Windows::ApplicationModel::Resources::Core::ResourceManager::Current().DefaultContext();
        auto qualifiers = context.QualifierValues();
        if (qualifiers.HasKey(L"Language")) {
            auto lang = std::wstring(qualifiers.Lookup(L"Language").c_str());
            if (!lang.empty()) return lang;
        }
    } catch (...) {
    }

    try {
        auto languages = winrt::Windows::Globalization::ApplicationLanguages::Languages();
        if (languages.Size() > 0) {
            return std::wstring(languages.GetAt(0).c_str());
        }
    } catch (...) {
    }
    return L"en";
}

std::wstring GetLanguageOverride()
{
    try {
        return std::wstring(winrt::Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride().c_str());
    } catch (...) {
        return L"";
    }
}

bool IsCurrentLanguageRtl()
{
    return ::AgentRedactor::IsLanguageRtl(GetCurrentLanguage());
}

void ApplyCurrentFlowDirection(const winrt::Windows::Foundation::IInspectable& element)
{
    auto fe = element.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
    if (!fe) {
        LOG(L"[Localization] ApplyCurrentFlowDirection called on non-FrameworkElement");
        return;
    }
    using winrt::Microsoft::UI::Xaml::FlowDirection;
    try {
        fe.FlowDirection(IsCurrentLanguageRtl() ? FlowDirection::RightToLeft : FlowDirection::LeftToRight);
    } catch (...) {
        LOG(L"[Localization] Failed to apply flow direction");
    }
}

} // namespace AgentRedactor
