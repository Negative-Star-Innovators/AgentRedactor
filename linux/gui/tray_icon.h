#pragma once

// System tray for the Linux GUI (QSystemTrayIcon / StatusNotifierItem).
// Mirrors the Windows tray: left-click opens the window; menu has Open,
// Start on boot (checkable), a Language submenu (built from the shared
// SUPPORTED_LANGUAGES list, checkmark on the active one) and Quit with a
// confirmation dialog. Unlike Windows the language applies live — no
// restart. When no system tray is available (plain Wayland GNOME without
// the AppIndicator extension), the app runs as a control-panel window
// instead — see MainWindow's close handling.

#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QActionGroup;
class QMenu;

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject* parent = nullptr);

    bool available() const;
    void showIcon();
    void setStartOnBoot(bool enabled);

    // BCP-47 tag from settings; empty/unknown = no checkmark (system default).
    void setCurrentLanguage(const QString& tag);
    // Re-apply tr() to the persistent menu strings after a language change
    // (the menu is built once, so it does not see QEvent::LanguageChange).
    void retranslate();

signals:
    void openRequested();
    void startOnBootToggled(bool enabled);
    // Empty tag = system default is not offered here (window Settings only).
    void languageChangeRequested(const QString& tag);
    void quitRequested();

private:
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* menu_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* startOnBootAction_ = nullptr;
    QMenu* languageMenu_ = nullptr;
    QActionGroup* languageGroup_ = nullptr;
    QAction* quitAction_ = nullptr;
};
