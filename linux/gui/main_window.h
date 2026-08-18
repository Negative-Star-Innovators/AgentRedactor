#pragma once

// Linux mirror of windows/MainWindow + HomePage: a single window with a
// profile sidebar and the settings cards (profile, regex, keywords,
// detection, password, statistics, session redactions, logs), plus the lock
// overlay, the blocking model-download dialog, close-to-tray and the
// inactivity re-lock. All strings go through tr(); retranslateUi() applies
// language changes live (TranslatorLoader drives QEvent::LanguageChange).

#include <QMainWindow>

#include "engine_client.h"

class AppState;
class TrayIcon;
class TranslatorLoader;

class QCheckBox;
class QCloseEvent;
class QDialog;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedLayout;
class QTimer;
class QVBoxLayout;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(AppState* appState, TrayIcon* tray, TranslatorLoader* translator,
        bool trayOnly, QWidget* parent = nullptr);

    // Show/raise the window and enforce the lock state (tray Open, launch).
    void openWindow();

public slots:
    // Tray menu entry points.
    void onStartOnBootToggled(bool checked);
    void onQuitRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onStatusUpdated();
    void onSettingsChanged();
    void onModelDownloadChanged();
    void onConnectionLost();

    void onProfileSelectionChanged();
    void onAddProfile();
    void onRemoveProfile();
    void onSaveProfile();
    void onCopyUrl();
    void onToggleApiKeyVisible(bool visible);

    void onAddRegex();
    void onAddKeyword();

    void onRequirePasswordToggled(bool checked);
    void onUnlockClicked();

    void onClearStatistics();
    void onClearMatches();

    void onLoggingToggled(bool checked);
    void onShowSensitiveToggled(bool checked);
    void onOpenLog();
    void onOpenLogFolder();
    void onClearLogs();

private:
    // Loading/saving
    void reloadProfiles(bool keepSelection);
    void loadProfileIntoForm(int index);
    json gatherProfileFromForm();
    bool validateForm(QString& error, bool& httpWarning);
    QString selectedProfileId() const;
    json* selectedProfile();

    // Lock overlay
    void ensureLockState(bool allowPrompt);
    void showLockOverlay();
    void hideLockOverlay();
    bool isProtected() const;   // settings.masterPasswordEnabled
    bool isUnlocked() const;    // status.unlocked

    // Model download
    void updateModelDownloadDialog();

    // UI construction
    void buildUi();
    void retranslateUi();
    void setCardsEnabled(bool enabled);

    // PII type display label (English; PII_Type_<type> keys in Windows resw).
    static QString piiTypeLabel(const std::wstring& type);

    AppState* appState_ = nullptr;
    TrayIcon* tray_ = nullptr;
    TranslatorLoader* translator_ = nullptr;

    // Central stack: page 0 = content, page 1 = lock overlay.
    QStackedLayout* centralStack_ = nullptr;

    // Sidebar
    QListWidget* profileList_ = nullptr;
    QPushButton* addProfileBtn_ = nullptr;
    QPushButton* removeProfileBtn_ = nullptr;

    // Profile card
    QLineEdit* aliasBox_ = nullptr;
    QLineEdit* portBox_ = nullptr;
    QLineEdit* urlBox_ = nullptr;
    QLineEdit* apiKeyBox_ = nullptr;
    QCheckBox* showKeyCheck_ = nullptr;
    QPushButton* copyUrlBtn_ = nullptr;
    QPushButton* saveBtn_ = nullptr;

    // Detection card
    QCheckBox* useAiCheck_ = nullptr;
    QLineEdit* confidenceBox_ = nullptr;
    std::vector<std::pair<std::wstring, QCheckBox*>> piiChecks_;

    // Regex / keywords cards (rows owned by layout)
    QVBoxLayout* regexRows_ = nullptr;
    QVBoxLayout* keywordRows_ = nullptr;
    QLineEdit* newRegexBox_ = nullptr;
    QLineEdit* newKeywordBox_ = nullptr;
    QCheckBox* newKeywordCaseCheck_ = nullptr;

    // Password card
    QCheckBox* requirePasswordCheck_ = nullptr;

    // Statistics card
    QLabel* statsLabel_ = nullptr;

    // Session redactions card
    QListWidget* matchesList_ = nullptr;

    // Logs card
    QCheckBox* loggingCheck_ = nullptr;
    QCheckBox* showSensitiveCheck_ = nullptr;

    // Settings card
    QCheckBox* startOnBootCheck_ = nullptr;

    // Lock overlay widgets
    QWidget* lockOverlay_ = nullptr;
    QLineEdit* unlockBox_ = nullptr;
    QLabel* unlockError_ = nullptr;

    // Model download dialog widgets (a non-dismissible QDialog)
    QDialog* modelDialog_ = nullptr;
    QLabel* modelStatusLabel_ = nullptr;
    QProgressBar* modelProgress_ = nullptr;
    QPushButton* modelRetryBtn_ = nullptr;

    json profiles_ = json::array();
    uint64_t prevProfilesRevision_ = 0;
    bool loading_ = false;  // suppress dirty-tracking while populating
    bool dirty_ = false;    // form edited since last load/save
    bool quitting_ = false; // real quit in progress (vs close-to-tray)
    bool lockEnforcedOnce_ = false;

    QTimer* inactivityTimer_ = nullptr;
    QTimer* lockRetryTimer_ = nullptr;
};
