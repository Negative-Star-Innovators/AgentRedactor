#include "translator_loader.h"

#include <QApplication>
#include <QLocale>

#include "constants.h" // SUPPORTED_LANGUAGES, IsLanguageRtl

TranslatorLoader::TranslatorLoader(QApplication& app, QObject* parent)
    : QObject(parent), app_(app) {}

void TranslatorLoader::applyLanguage(const QString& tag) {
    app_.removeTranslator(&translator_);

    QString effective = tag;
    if (effective.isEmpty()) effective = QLocale::system().name(); // e.g. de_DE

    // Only exact supported tags load a translation; a system locale like
    // de_DE falls back to its base language (de) when supported.
    const auto isSupported = [](const QString& t) {
        const std::wstring wt = t.toStdWString();
        for (const auto& lang : AgentRedactor::SUPPORTED_LANGUAGES) {
            if (lang.tag == wt) return true;
        }
        return false;
    };
    if (!isSupported(effective)) {
        const QString base = effective.section(QLatin1Char('_'), 0, 0);
        effective = isSupported(base) ? base : QStringLiteral("en");
    }

    if (effective != QLatin1String("en")) {
        // Translation catalogs are embedded under :/i18n, named with Qt
        // locale suffixes (zh_CN, sr_Latn) rather than BCP-47 dashes.
        QString fileTag = effective;
        fileTag.replace(QLatin1Char('-'), QLatin1Char('_'));
        if (translator_.load(QStringLiteral(":/i18n/agentredactor_") + fileTag)) {
            app_.installTranslator(&translator_);
        }
    }

    const bool rtl = AgentRedactor::IsLanguageRtl(effective.toStdWString());
    app_.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
}
