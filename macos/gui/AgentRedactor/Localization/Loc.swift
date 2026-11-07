import Foundation
import SwiftUI

// Manifest-backed localization (mirrors core LocString/LocFormat and the Linux
// TranslatorLoader). The SwiftUI string catalog (Localizable.xcstrings) is the
// compile-time source of truth; strings-manifest.json is the runtime source so
// the UI can switch languages LIVE without a relaunch (AppleLanguages alone
// does not re-resolve String Catalog strings at runtime on macOS).
//
// Fallback semantics mirror linux/gui/translator_loader.cpp: exact tag ->
// base language -> "en" -> the raw key (so a missing key is visible in dev).

final class LocalizationManager: ObservableObject {
    static let shared = LocalizationManager()

    /// The tag UI text is currently rendered in (always a supported tag).
    @Published private(set) var effectiveTag: String = "en"

    private var manifest: [String: [String: String]] = [:]
    private let lock = NSLock()

    private init() {
        loadManifest()
        // Seed from the AppleLanguages override when the app starts with one
        // already applied (e.g. by a previous run).
        if let stored = UserDefaults.standard.array(forKey: "AppleLanguages") as? [String],
           let first = stored.first {
            effectiveTag = LocalizationManager.resolve(first)
        }
    }

    private func loadManifest() {
        guard let url = Bundle.main.url(forResource: "strings-manifest", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let parsed = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            NSLog("[Loc] strings-manifest.json missing or unreadable; falling back to keys")
            return
        }
        var table: [String: [String: String]] = [:]
        for (key, value) in parsed {
            guard let langMap = value as? [String: Any] else { continue }
            var inner: [String: String] = [:]
            for (lang, text) in langMap {
                inner[lang] = text as? String
            }
            table[key] = inner
        }
        manifest = table
    }

    /// TranslatorLoader fallback: exact supported tag, else the base language
    /// ("de-DE" -> "de"), else "en".
    static func resolve(_ tag: String) -> String {
        var effective = tag
        if !isSupportedLanguage(effective) {
            let base = effective.split(separator: "-").first.map(String.init) ?? effective
            effective = isSupportedLanguage(base) ? base : "en"
        }
        return effective
    }

    /// Applies a language override for this session (and future launches via
    /// AppleLanguages, mirroring Windows' PrimaryLanguageOverride persistence
    /// semantics — the engine persists the real setting in settings.json).
    func apply(languageTag: String) {
        let resolved = LocalizationManager.resolve(languageTag)
        if resolved != effectiveTag {
            effectiveTag = resolved
        }
        // Keep the bundle default in step for anything that still resolves
        // through Foundation localization (dialogs, system buttons).
        UserDefaults.standard.set([resolved], forKey: "AppleLanguages")
        NotificationCenter.default.post(name: NSNotification.Name("LocalizationChanged"), object: nil)
    }

    func localized(_ key: String) -> String {
        lock.lock()
        let table = manifest
        lock.unlock()
        guard let entry = table[key] else { return key }
        if let exact = entry[effectiveTag] { return exact }
        if let en = entry["en"] { return en }
        return key
    }

    func localized(_ key: String, _ args: [String]) -> String {
        var result = localized(key)
        for (index, arg) in args.enumerated() {
            // The corpus uses only {0}/{1}-style placeholders; Swift's
            // String(format:) does not substitute them (core LocFormat uses a
            // manual replacer, mirrored here).
            result = result.replacingOccurrences(of: "{\(index)}", with: arg)
        }
        return result
    }

    var layoutDirection: LayoutDirection {
        isLanguageRtl(effectiveTag) ? .rightToLeft : .leftToRight
    }
}

func LocString(_ key: String) -> String {
    LocalizationManager.shared.localized(key)
}

func LocFormat(_ key: String, _ args: String...) -> String {
    LocalizationManager.shared.localized(key, args)
}
