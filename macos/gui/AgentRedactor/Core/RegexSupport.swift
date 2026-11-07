import Foundation

// Port of Utils::NormalizeRegexBraces (core/src/utils.cpp): the "{," shorthand
// is auto-fixed to "{0," only in the exact forms std::regex rejects but
// .NET/PCRE/JS accept ("{,}" / "{,<digits>}"), leaving escaped literals alone.
// Validation uses NSRegularExpression (ICU) as the client-side pre-check; the
// engine's std::regex (ECMAScript grammar) remains the authority and its
// rejections surface as PUT errors.

enum RegexSupport {
    static func normalizeBraces(_ pattern: String) -> String {
        var result = Array(pattern)
        var i = 0
        while i + 1 < result.count {
            if result[i] != "{" || result[i + 1] != "," {
                i += 1
                continue
            }
            if i > 0 && result[i - 1] == "\\" {
                i += 1
                continue
            }
            var j = i + 2
            while j < result.count && result[j].isNumber { j += 1 }
            if j < result.count && result[j] == "}" {
                result.insert("0", at: i + 1)
                i += 1
            }
            i += 1
        }
        return String(result)
    }

    /// Returns nil when the pattern compiles, otherwise the ICU error text
    /// (surfaced via Validation_InvalidRegex "{0}").
    static func validationError(_ pattern: String) -> String? {
        do {
            _ = try NSRegularExpression(pattern: pattern)
            return nil
        } catch {
            return error.localizedDescription
        }
    }
}
