#include "tray_icon.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QTimer>

#include "constants.h" // SUPPORTED_LANGUAGES

namespace {
// How long the startup race with GNOME shell's StatusNotifierWatcher can be:
// 15 tries, 2 s apart (~30 s), before giving up on having a tray icon.
constexpr int kMaxTrayRetries = 15;
} // namespace

TrayIcon::TrayIcon(QObject* parent) : QObject(parent) {
    if (tryCreate()) return;

    // No tray at startup: at session login GNOME shell's StatusNotifierWatcher
    // is often not up yet, and Qt does not retry on its own — the icon would
    // silently never appear (the app itself is healthy). Retry instead.
    retryTimer_ = new QTimer(this);
    retryTimer_->setInterval(2000);
    connect(retryTimer_, &QTimer::timeout, this, [this] {
        if (tryCreate() || ++retryAttempts_ >= kMaxTrayRetries) {
            if (!tray_) qWarning("[TrayIcon] No system tray became available; running without a tray icon");
            retryTimer_->stop();
        }
    });
    retryTimer_->start();
}

bool TrayIcon::tryCreate() {
    if (tray_ || !QSystemTrayIcon::isSystemTrayAvailable()) return tray_ != nullptr;

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
    // When created by the startup retry timer, showIcon() was already called
    // (and no-op'd) before the tray existed — show it ourselves.
    tray_->show();
    return true;
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
