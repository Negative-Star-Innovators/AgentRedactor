#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <winrt/Windows.Foundation.h>

namespace AgentRedactor {

// Initialize the resource loader and apply any saved language override.
// Must be called once during app startup, before UI is shown.
void InitializeLocalization();

// Get a localized string by resource key.
std::wstring LocString(std::wstring_view key);

// Get a localized string and replace {0}, {1}, ... placeholders with args.
std::wstring LocFormat(std::wstring_view key, std::initializer_list<std::wstring_view> args);

// Apply a language override. Pass empty string to revert to Windows default.
// Returns true if the override was applied. A restart is required for full UI update.
bool SetLanguageOverride(const std::wstring& language);

// Get the current effective language (e.g., "en" or "de-DE").
std::wstring GetCurrentLanguage();

// Get the BCP-47 language tag stored in settings, or empty if using system default.
std::wstring GetLanguageOverride();

// Returns true if the current effective UI language is right-to-left
// (e.g. Arabic, Hebrew, Urdu).
bool IsCurrentLanguageRtl();

// Apply RightToLeft/LeftToRight FlowDirection to a FrameworkElement based on
// the current effective UI language. Call after InitializeComponent().
void ApplyCurrentFlowDirection(const winrt::Windows::Foundation::IInspectable& element);

} // namespace AgentRedactor
