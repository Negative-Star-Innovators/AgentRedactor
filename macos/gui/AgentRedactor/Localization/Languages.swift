import Foundation

// Port of SUPPORTED_LANGUAGES in core/include/constants.h. Keep in sync with
// that list: the Settings combo and the tray Language submenu are built from
// it, and app-language validation on the engine side accepts exactly these
// tags (plus region variants via LanguageMatches).
struct SupportedLanguage: Equatable {
    let tag: String
    let nativeName: String
}

let kSupportedLanguages: [SupportedLanguage] = [
    SupportedLanguage(tag: "en", nativeName: "English"),
    SupportedLanguage(tag: "de", nativeName: "Deutsch"),
    SupportedLanguage(tag: "es", nativeName: "Español"),
    SupportedLanguage(tag: "fr", nativeName: "Français"),
    SupportedLanguage(tag: "pt", nativeName: "Português"),
    SupportedLanguage(tag: "it", nativeName: "Italiano"),
    SupportedLanguage(tag: "da", nativeName: "Dansk"),
    SupportedLanguage(tag: "nl", nativeName: "Nederlands"),
    SupportedLanguage(tag: "sv", nativeName: "Svenska"),
    SupportedLanguage(tag: "lb", nativeName: "Lëtzebuergesch"),
    SupportedLanguage(tag: "nb", nativeName: "Norsk bokmål"),
    SupportedLanguage(tag: "fi", nativeName: "Suomi"),
    SupportedLanguage(tag: "ru", nativeName: "Русский"),
    SupportedLanguage(tag: "hr", nativeName: "Hrvatski"),
    SupportedLanguage(tag: "el", nativeName: "Ελληνικά"),
    SupportedLanguage(tag: "sl", nativeName: "Slovenščina"),
    SupportedLanguage(tag: "sr-Latn", nativeName: "Srpski"),
    SupportedLanguage(tag: "uk", nativeName: "Українська"),
    SupportedLanguage(tag: "sq", nativeName: "Shqip"),
    SupportedLanguage(tag: "lv", nativeName: "Latviešu"),
    SupportedLanguage(tag: "hy", nativeName: "Հայերեն"),
    SupportedLanguage(tag: "cs", nativeName: "Čeština"),
    SupportedLanguage(tag: "et", nativeName: "Eesti"),
    SupportedLanguage(tag: "sk", nativeName: "Slovenčina"),
    SupportedLanguage(tag: "bg", nativeName: "Български"),
    SupportedLanguage(tag: "ka", nativeName: "ქართული"),
    SupportedLanguage(tag: "hu", nativeName: "Magyar"),
    SupportedLanguage(tag: "pl", nativeName: "Polski"),
    SupportedLanguage(tag: "ro", nativeName: "Română"),
    SupportedLanguage(tag: "lt", nativeName: "Lietuvių"),
    SupportedLanguage(tag: "is", nativeName: "Íslenska"),
    SupportedLanguage(tag: "zh-CN", nativeName: "简体中文"),
    SupportedLanguage(tag: "zh-TW", nativeName: "繁體中文"),
    SupportedLanguage(tag: "ja", nativeName: "日本語"),
    SupportedLanguage(tag: "ko", nativeName: "한국어"),
    SupportedLanguage(tag: "mt", nativeName: "Malti"),
    SupportedLanguage(tag: "hi", nativeName: "हिन्दी"),
    SupportedLanguage(tag: "ta", nativeName: "தமிழ்"),
    SupportedLanguage(tag: "vi", nativeName: "Tiếng Việt"),
    SupportedLanguage(tag: "sw", nativeName: "Kiswahili"),
    SupportedLanguage(tag: "af", nativeName: "Afrikaans"),
    SupportedLanguage(tag: "he", nativeName: "עברית"),
    SupportedLanguage(tag: "id", nativeName: "Bahasa Indonesia"),
    SupportedLanguage(tag: "fil", nativeName: "Filipino"),
    SupportedLanguage(tag: "ig-NG", nativeName: "Igbo"),
    SupportedLanguage(tag: "th", nativeName: "ไทย"),
    SupportedLanguage(tag: "tr", nativeName: "Türkçe"),
    SupportedLanguage(tag: "ur", nativeName: "اردو"),
    SupportedLanguage(tag: "ar", nativeName: "العربية"),
    SupportedLanguage(tag: "ms", nativeName: "Bahasa Melayu"),
    SupportedLanguage(tag: "az-Latn", nativeName: "Azərbaycan dili"),
    SupportedLanguage(tag: "kk", nativeName: "Қазақ тілі"),
    SupportedLanguage(tag: "ha-Latn", nativeName: "Hausa"),
]

func isSupportedLanguage(_ tag: String) -> Bool {
    kSupportedLanguages.contains { $0.tag == tag }
}

// Port of IsLanguageRtl in core/include/constants.h (primary-subtag matching).
func isLanguageRtl(_ tag: String) -> Bool {
    let rtlPrefixes = ["ar", "he", "ur", "fa", "ps", "sd", "ug", "dv", "yi"]
    for prefix in rtlPrefixes {
        if tag == prefix { return true }
        if tag.hasPrefix(prefix + "-") { return true }
    }
    return false
}

// Port of LanguageMatches in core/include/constants.h: exact match or one tag
// is a more specific variant of the other ("de-DE" ~ "de").
func languageMatches(_ current: String, _ supportedTag: String) -> Bool {
    if current == supportedTag { return true }
    if current.hasPrefix(supportedTag + "-") { return true }
    if supportedTag.hasPrefix(current + "-") { return true }
    return false
}
