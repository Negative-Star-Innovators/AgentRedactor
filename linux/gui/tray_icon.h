#pragma once

// System tray for the Linux GUI (QSystemTrayIcon / StatusNotifierItem).
// Mirrors the Windows tray: left-click opens the window; menu has Open,
// Start on boot (checkable) and Quit with a confirmation dialog. When no
// system tray is available (plain Wayland GNOME without the AppIndicator
// extension), the app runs as a control-panel window instead — see
// MainWindow's close handling.

#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QMenu;

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject* parent = nullptr);

    bool available() const;
    void showIcon();
    void setStartOnBoot(bool enabled);

signals:
    void openRequested();
    void startOnBootToggled(bool enabled);
    void quitRequested();

private:
    QSystemTrayIcon* tray_ = nullptr;
    QAction* startOnBootAction_ = nullptr;
};
