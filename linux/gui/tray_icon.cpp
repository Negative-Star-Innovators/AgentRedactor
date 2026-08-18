#include "tray_icon.h"

#include <QAction>
#include <QIcon>
#include <QMenu>

TrayIcon::TrayIcon(QObject* parent) : QObject(parent) {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    tray_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/app.png")), this);
    tray_->setToolTip(tr("Agent Redactor"));

    auto* menu = new QMenu();
    auto* openAction = menu->addAction(tr("Open"));
    connect(openAction, &QAction::triggered, this, &TrayIcon::openRequested);

    startOnBootAction_ = menu->addAction(tr("Start on boot"));
    startOnBootAction_->setCheckable(true);
    connect(startOnBootAction_, &QAction::toggled,
        this, &TrayIcon::startOnBootToggled);

    menu->addSeparator();
    auto* quitAction = menu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this,
        [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) emit openRequested();
        });
}

bool TrayIcon::available() const { return tray_ != nullptr; }

void TrayIcon::showIcon() {
    if (tray_) tray_->show();
}

void TrayIcon::setStartOnBoot(bool enabled) {
    if (startOnBootAction_) startOnBootAction_->setChecked(enabled);
}
