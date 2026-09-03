#include "tray_icon.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>

#include "constants.h" // SUPPORTED_LANGUAGES

TrayIcon::TrayIcon(QObject* parent) : QObject(parent) {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    tray_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/app.png")), this);

    menu_ = new QMenu();
    openAction_ = menu_->addAction(QString());
    connect(openAction_, &QAction::triggered, this, &TrayIcon::openRequested);

    startOnBootAction_ = menu_->addAction(QString());
    startOnBootAction_->setCheckable(true);
    connect(startOnBootAction_, &QAction::toggled,
        this, &TrayIcon::startOnBootToggled);

    // Language submenu: one checkable entry per supported language, labels in
    // the language itself (nativeName needs no translation). Live-switch —
    // the engine persists the tag and the settings poll retranslates the UI.
    languageMenu_ = menu_->addMenu(QString());
    languageGroup_ = new QActionGroup(languageMenu_);
    languageGroup_->setExclusive(true);
    for (const auto& lang : AgentRedactor::SUPPORTED_LANGUAGES) {
        const QString tag = QString::fromStdWString(lang.tag);
        QAction* action = languageMenu_->addAction(QString::fromStdWString(lang.nativeName));
        action->setCheckable(true);
        action->setData(tag);
        languageGroup_->addAction(action);
        connect(action, &QAction::triggered, this,
            [this, tag] { emit languageChangeRequested(tag); });
    }

    menu_->addSeparator();
    quitAction_ = menu_->addAction(QString());
    connect(quitAction_, &QAction::triggered, this, &TrayIcon::quitRequested);

    tray_->setContextMenu(menu_);
    connect(tray_, &QSystemTrayIcon::activated, this,
        [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) emit openRequested();
        });

    retranslate();
}

bool TrayIcon::available() const { return tray_ != nullptr; }

void TrayIcon::showIcon() {
    if (tray_) tray_->show();
}

void TrayIcon::setStartOnBoot(bool enabled) {
    if (startOnBootAction_) startOnBootAction_->setChecked(enabled);
}

void TrayIcon::setCurrentLanguage(const QString& tag) {
    if (!languageMenu_) return;
    for (QAction* action : languageMenu_->actions())
        action->setChecked(action->data().toString() == tag);
}

void TrayIcon::retranslate() {
    if (!tray_) return;
    tray_->setToolTip(tr("Agent Redactor"));
    openAction_->setText(tr("Open Agent Redactor"));
    startOnBootAction_->setText(tr("Start on Boot"));
    languageMenu_->setTitle(tr("Language"));
    quitAction_->setText(tr("Quit"));
}
