#pragma once

// Live UI translation for the Linux GUI. All user-visible strings are
// written in English via tr(); this loader installs a QTranslator for
// `agentredactor_<tag>.qm` when the app's appLanguage setting names another
// supported language, and flips the layout direction for RTL languages.
// No .qm files ship yet — the structure exists so converting the Windows
// Strings/<tag>/Resources.resw files to .ts/.qm later needs no code changes.

#include <QObject>
#include <QString>
#include <QTranslator>

class QApplication;

class TranslatorLoader : public QObject {
    Q_OBJECT
public:
    explicit TranslatorLoader(QApplication& app, QObject* parent = nullptr);

public slots:
    // tag: BCP-47 code from settings; empty = system locale. Unknown tags or
    // missing .qm files fall back to English source strings (tr() defaults).
    void applyLanguage(const QString& tag);

private:
    QApplication& app_;
    QTranslator translator_; // kept alive while installed
};
