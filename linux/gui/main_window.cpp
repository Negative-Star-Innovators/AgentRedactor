#include "main_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QHideEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedLayout>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <regex>

#include "app_state.h"
#include "autostart.h"
#include "agent_toggle_switch.h"
#include "constants.h"
#include "http_server.h"
#include "password_dialog.h"
#include "tray_icon.h"
#include "translator_loader.h"
#include "update_manager.h"
#include "utils.h"

using namespace AgentRedactor;

namespace {

QString q(const std::wstring& ws) { return QString::fromStdWString(ws); }
std::wstring w(const QString& s) { return s.toStdWString(); }

// Card container helper: titled group box with a vertical layout. Inner
// spacing/margins come from the app stylesheet (theme.cpp).
QGroupBox* makeCard(const QString& title, QVBoxLayout*& layoutOut, QWidget* parent) {
    auto* box = new QGroupBox(title, parent);
    layoutOut = new QVBoxLayout(box);
    layoutOut->setContentsMargins(0, 0, 0, 0);
    layoutOut->setSpacing(8);
    return box;
}

// Dimmed wrapping help text (styled via the hint property in theme.cpp).
QLabel* makeHint(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    label->setProperty("hint", true);
    return label;
}

// Fixed-pitch font for regex/keyword text (Windows uses Consolas 13).
QFont rowFont() {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(13);
    return f;
}

// Row column widths shared by the regex/keyword rows and their headers so the
// header labels sit exactly over their columns (Windows: 70px Enabled column,
// 100px Case column — the case toggle button is 90px here).
constexpr int kEnabledColumnWidth = 70;
constexpr int kCaseColumnWidth = 90;

// Dimmed semi-bold column header label (styled via the colHeader property).
QLabel* makeColHeader(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setProperty("colHeader", true);
    return label;
}

// Modal dialog that cannot be dismissed by the user via Escape, but can be
// hidden programmatically while the main window is closed/minimized to tray.
class NonDismissibleDialog : public QDialog {
public:
    using QDialog::QDialog;

protected:
    void reject() override { /* ignore Escape and any external dismiss request */ }
};

} // namespace

MainWindow::MainWindow(AppState* appState, TrayIcon* tray, TranslatorLoader* translator,
    bool trayOnly, QWidget* parent)
    : QMainWindow(parent), appState_(appState), tray_(tray), translator_(translator) {
    setWindowTitle(tr("Agent Redactor"));
    setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    // Never open larger than the available work area: on Wayland an app
    // cannot reposition its own window, so one that opens taller than the
    // screen (900px on a 1280x800 display) loses its title bar off the top
    // edge with no way to drag it back down.
    const QRect avail = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 1000, 900);
    resize(std::min(1000, avail.width()), std::min(900, avail.height()));

    buildUi();

    connect(appState_, &AppState::statusUpdated, this, &MainWindow::onStatusUpdated);
    connect(appState_, &AppState::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(appState_, &AppState::modelDownloadChanged, this, &MainWindow::onModelDownloadChanged);
    connect(appState_, &AppState::connectionLost, this, &MainWindow::onConnectionLost);
    connect(tray_, &TrayIcon::languageChangeRequested, this, [this](const QString& tag) {
        // Apply immediately so the UI switches without waiting for the
        // settings-poll round-trip; the poll later reconciles the persisted
        // tag (no restart, unlike Windows).
        translator_->applyLanguage(tag);
        syncLanguageSelectors(tag);
        appState_->client().PutSetting(L"appLanguage", tag.toStdString());
    });

    // 10-minute inactivity re-lock, reset by any key/mouse activity (mirrors
    // the Windows message-filter timer).
    inactivityTimer_ = new QTimer(this);
    inactivityTimer_->setSingleShot(true);
    inactivityTimer_->setInterval(10 * 60 * 1000);
    connect(inactivityTimer_, &QTimer::timeout, this, [this] {
        if (isProtected() && isUnlocked()) appState_->client().Lock();
    });
    qApp->installEventFilter(this);

    // The settings snapshot may not have arrived yet when the window first
    // shows; retry the lock enforcement every second until it has (mirrors
    // MainWindow::EnsureLockState's DispatcherQueue retry).
    lockRetryTimer_ = new QTimer(this);
    lockRetryTimer_->setInterval(1000);
    connect(lockRetryTimer_, &QTimer::timeout, this, [this] { ensureLockState(false); });
    lockRetryTimer_->start();

    // Shutdown (engine stop/lock decision) on every quit path: tray/menu Quit,
    // window close in control-panel mode, SIGTERM.
    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        appState_->Shutdown(isProtected());
    });

    // Self-update (Velopack self-release builds only): startup check plus the
    // Settings-card button. Restart prompt mirrors Windows: "later" default.
    if (AppUpdateManager::IsSelfRelease()) {
        updateMgr_ = new AppUpdateManager(this);
        connect(updateMgr_, &AppUpdateManager::updateDownloaded, this,
            [this](QString version, bool) {
                auto* box = new QMessageBox(QMessageBox::Information,
                    tr("Update ready to install"),
                    tr("Agent Redactor %1 has been downloaded. Restart now to apply the update.").arg(version),
                    QMessageBox::NoButton, this);
                QPushButton* now = box->addButton(tr("Restart now"), QMessageBox::AcceptRole);
                box->addButton(tr("Later"), QMessageBox::RejectRole);
                box->setDefaultButton(qobject_cast<QPushButton*>(box->buttons().last()));
                box->exec();
                if (box->clickedButton() == now) updateMgr_->ApplyAndRestart();
                box->deleteLater();
            });
        connect(updateMgr_, &AppUpdateManager::noUpdateFound, this,
            [this](bool userInitiated) {
                if (userInitiated)
                    QMessageBox::information(this, tr("Check for updates"),
                        tr("You're up to date."));
            });
        connect(updateMgr_, &AppUpdateManager::checkFailed, this,
            [this](QString, bool userInitiated) {
                if (userInitiated)
                    QMessageBox::warning(this, tr("Check for updates"),
                        tr("Couldn't check for updates. Try again later."));
            });
        // The Velopack updater is already waiting for this process to exit;
        // skip the quit confirmation and shut down immediately. Stop the engine
        // synchronously so the updater's 60-second wait is never starved, and
        // arm a watchdog that force-exits if Qt's event loop does not wind down
        // promptly (e.g. a stuck signal handler or modal dialog).
        connect(updateMgr_, &AppUpdateManager::restartRequested, this, [this] {
            quitting_ = true;
            qInfo("[MainWindow] Restart requested; stopping engine and quitting");
            // aboutToQuit also calls Shutdown, but do it now so the updater sees
            // the process exit quickly. Pass false: do not leave the engine
            // running/locked during an update restart.
            appState_->Shutdown(false);
            QApplication::quit();
            // If the event loop is stuck and the process is still alive after a
            // short grace period, exit hard so Velopack can swap the AppImage.
            QTimer::singleShot(3000, this, [] {
                qWarning("[MainWindow] Forced exit after update-restart timeout");
                std::_Exit(0);
            });
        });
        QTimer::singleShot(0, this, [this] { updateMgr_->CheckForUpdates(false); });
    }

    if (!trayOnly || !tray_->available()) {
        // Control-panel fallback: without a tray a hidden window would leave
        // the user with no UI at all, so --tray-only is ignored there.
        openWindow();
    }
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    centralStack_ = new QStackedLayout(central);

    // ---- Page 0: content ----
    auto* content = new QWidget(central);
    auto* contentLayout = new QHBoxLayout(content);

    auto* splitter = new QSplitter(content);

    // Sidebar: profile list + add/remove
    auto* sidebar = new QWidget(splitter);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    profilesHeader_ = new QLabel(sidebar);
    profilesHeader_->setObjectName(QStringLiteral("profilesHeader"));
    sidebarLayout->addWidget(profilesHeader_);
    profileList_ = new QListWidget(sidebar);
    connect(profileList_, &QListWidget::currentRowChanged,
        this, &MainWindow::onProfileSelectionChanged);
    sidebarLayout->addWidget(profileList_);
    auto* sideBtns = new QHBoxLayout;
    addProfileBtn_ = new QPushButton(sidebar);
    removeProfileBtn_ = new QPushButton(sidebar);
    connect(addProfileBtn_, &QPushButton::clicked, this, &MainWindow::onAddProfile);
    connect(removeProfileBtn_, &QPushButton::clicked, this, &MainWindow::onRemoveProfile);
    sideBtns->addWidget(addProfileBtn_);
    sideBtns->addWidget(removeProfileBtn_);
    sidebarLayout->addLayout(sideBtns);
    sidebar->setMinimumWidth(220);
    sidebar->setMaximumWidth(280);
    splitter->addWidget(sidebar);

    // Cards in a scroll area
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    // Qt's AT-SPI bridge answers GetChildAtIndex without bounds checks while
    // QAccessibleAbstractScrollArea exposes scrollbar containers only when the
    // scrollbar is visible — a client holding a stale child count (e.g. an
    // AT-SPI test walking the tree during a language switch) crashes the app.
    // Pin scrollbar visibility so the accessible child set never changes.
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* cards = new QWidget(scroll);
    auto* cardsLayout = new QVBoxLayout(cards);
    cardsLayout->setContentsMargins(24, 24, 24, 24);
    cardsLayout->setSpacing(16);

    auto markDirty = [this] { if (!loading_) dirty_ = true; };
    // Controls with immediate effect: persist through the normal save path
    // (validation included) instead of waiting for the Save button. Matches
    // the keyword/regex row toggles, which already PUT on change.
    auto autosave = [this] {
        if (loading_) return;
        dirty_ = true;
        onSaveProfile();
    };

    // -- Quick start card --
    QVBoxLayout* quickStartLayout;
    auto* quickStartCard = makeCard(QString(), quickStartLayout, cards); // title set in retranslateUi
    quickStartCard->setObjectName(QStringLiteral("quickStartCard"));
    quickStep1Label_ = new QLabel(quickStartCard);
    quickStep1Label_->setWordWrap(true);
    quickStep2Label_ = new QLabel(quickStartCard);
    quickStep2Label_->setWordWrap(true);
    quickStep3Label_ = new QLabel(quickStartCard);
    quickStep3Label_->setWordWrap(true);
    quickStartLayout->addWidget(quickStep1Label_);
    quickStartLayout->addWidget(quickStep2Label_);
    quickStartLayout->addWidget(quickStep3Label_);
    cardsLayout->addWidget(quickStartCard);

    // -- API Proxy card (Windows: HomePage_ApiProxy) --
    QVBoxLayout* profileLayout;
    auto* profileCard = makeCard(QString(), profileLayout, cards); // title set in retranslateUi
    profileCard->setObjectName(QStringLiteral("profileCard"));
    auto* profileForm = new QFormLayout;
    aliasLabel_ = new QLabel(profileCard);
    portLabel_ = new QLabel(profileCard); // "Local URL" (labels the port box row)
    urlLabel_ = new QLabel(profileCard);
    apiKeyLabel_ = new QLabel(profileCard);
    aliasBox_ = new QLineEdit(profileCard);
    portBox_ = new QLineEdit(profileCard);
    urlBox_ = new QLineEdit(profileCard);
    apiKeyBox_ = new QLineEdit(profileCard);
    apiKeyBox_->setEchoMode(QLineEdit::Password);
    profileForm->addRow(aliasLabel_, aliasBox_);
    // Local URL row (windows/HomePage.xaml): literal prefix, editable port,
    // literal suffix, Copy button on the same line; hint underneath.
    auto* localUrlRow = new QWidget(profileCard);
    auto* localUrlLayout = new QHBoxLayout(localUrlRow);
    localUrlLayout->setContentsMargins(0, 0, 0, 0);
    localUrlLayout->addWidget(new QLabel(QStringLiteral("http://localhost:"), localUrlRow));
    portBox_->setFixedWidth(72);
    localUrlLayout->addWidget(portBox_);
    localUrlLayout->addWidget(new QLabel(QStringLiteral("/"), localUrlRow));
    copyUrlBtn_ = new QPushButton(localUrlRow);
    connect(copyUrlBtn_, &QPushButton::clicked, this, &MainWindow::onCopyUrl);
    localUrlLayout->addWidget(copyUrlBtn_);
    localUrlLayout->addStretch();
    profileForm->addRow(portLabel_, localUrlRow);
    localUrlHint_ = makeHint(profileCard);
    profileForm->addRow(localUrlHint_);
    profileForm->addRow(urlLabel_, urlBox_);
    forwardToHint_ = makeHint(profileCard);
    profileForm->addRow(forwardToHint_);
    profileForm->addRow(apiKeyLabel_, apiKeyBox_);
    // Windows shows the reveal toggle under the password box, not in the
    // button row.
    showKeyCheck_ = new QCheckBox(profileCard);
    connect(showKeyCheck_, &QCheckBox::toggled, this, &MainWindow::onToggleApiKeyVisible);
    profileForm->addRow(QString(), showKeyCheck_);
    profileLayout->addLayout(profileForm);
    // textChanged (not textEdited): programmatic edits — assistive tech like
    // AT-SPI setTextContents, which never emits textEdited — must also mark
    // the form dirty, or a settings-poll reload can wipe an in-progress edit
    // before it is saved. loadProfileIntoForm's own setText calls are covered
    // by the loading_ guard in markDirty.
    connect(aliasBox_, &QLineEdit::textChanged, this, markDirty);
    connect(portBox_, &QLineEdit::textChanged, this, markDirty);
    connect(urlBox_, &QLineEdit::textChanged, this, markDirty);
    connect(apiKeyBox_, &QLineEdit::textChanged, this, markDirty);

    // Bottom row (Windows Grid.Row=4): port availability status on the left,
    // Save on the right.
    auto* profileBtns = new QHBoxLayout;
    portStatusLabel_ = new QLabel(profileCard);
    portStatusLabel_->setVisible(false);
    saveBtn_ = new QPushButton(profileCard);
    saveBtn_->setProperty("accent", true);
    connect(saveBtn_, &QPushButton::clicked, this, &MainWindow::onSaveProfile);
    profileBtns->addWidget(portStatusLabel_);
    profileBtns->addStretch();
    profileBtns->addWidget(saveBtn_);
    profileLayout->addLayout(profileBtns);
    cardsLayout->addWidget(profileCard);

    // Debounced port availability check (mirrors Windows UpdateProxyStatus).
    portStatusTimer_ = new QTimer(this);
    portStatusTimer_->setSingleShot(true);
    portStatusTimer_->setInterval(300);
    connect(portStatusTimer_, &QTimer::timeout, this, &MainWindow::updatePortStatus);
    connect(portBox_, &QLineEdit::textChanged, this, [this] { portStatusTimer_->start(); });

    // -- Regex card --
    QVBoxLayout* regexLayout;
    auto* regexCard = makeCard(QString(), regexLayout, cards);
    regexCard->setObjectName(QStringLiteral("regexCard"));
    regexDescLabel_ = makeHint(regexCard);
    regexLayout->addWidget(regexDescLabel_);
    // Column headers aligned over the row columns (Windows RegexHeaderGrid);
    // hidden while the list is empty (HomePage::LoadRegexList).
    regexHeader_ = new QWidget(regexCard);
    auto* regexHeaderLayout = new QHBoxLayout(regexHeader_);
    regexHeaderLayout->setContentsMargins(0, 0, 0, 0);
    regexEnabledHeader_ = makeColHeader(regexHeader_);
    regexEnabledHeader_->setFixedWidth(kEnabledColumnWidth);
    regexPatternHeader_ = makeColHeader(regexHeader_);
    regexHeaderLayout->addWidget(regexEnabledHeader_);
    regexHeaderLayout->addWidget(regexPatternHeader_, 1);
    regexHeader_->setVisible(false);
    regexLayout->addWidget(regexHeader_);
    regexRows_ = new QVBoxLayout;
    regexLayout->addLayout(regexRows_);
    auto* newRegexRow = new QHBoxLayout;
    newRegexBox_ = new QLineEdit(regexCard);
    auto* addRegexBtn = new QPushButton(regexCard);
    addRegexBtn->setObjectName(QStringLiteral("addRegexBtn"));
    connect(addRegexBtn, &QPushButton::clicked, this, &MainWindow::onAddRegex);
    connect(newRegexBox_, &QLineEdit::returnPressed, this, &MainWindow::onAddRegex);
    newRegexRow->addWidget(newRegexBox_);
    newRegexRow->addWidget(addRegexBtn);
    regexLayout->addLayout(newRegexRow);
    cardsLayout->addWidget(regexCard);

    // -- Keywords card --
    QVBoxLayout* keywordsLayout;
    auto* keywordsCard = makeCard(QString(), keywordsLayout, cards);
    keywordsCard->setObjectName(QStringLiteral("keywordsCard"));
    keywordsDescLabel_ = makeHint(keywordsCard);
    keywordsLayout->addWidget(keywordsDescLabel_);
    // Column headers aligned over the row columns (Windows KeywordHeaderGrid);
    // hidden while the list is empty (HomePage::LoadKeywordList).
    keywordHeader_ = new QWidget(keywordsCard);
    auto* keywordHeaderLayout = new QHBoxLayout(keywordHeader_);
    keywordHeaderLayout->setContentsMargins(0, 0, 0, 0);
    keywordEnabledHeader_ = makeColHeader(keywordHeader_);
    keywordEnabledHeader_->setFixedWidth(kEnabledColumnWidth);
    keywordCaseHeader_ = makeColHeader(keywordHeader_);
    keywordCaseHeader_->setFixedWidth(kCaseColumnWidth);
    keywordKeywordHeader_ = makeColHeader(keywordHeader_);
    keywordHeaderLayout->addWidget(keywordEnabledHeader_);
    keywordHeaderLayout->addWidget(keywordCaseHeader_);
    keywordHeaderLayout->addWidget(keywordKeywordHeader_, 1);
    keywordHeader_->setVisible(false);
    keywordsLayout->addWidget(keywordHeader_);
    keywordRows_ = new QVBoxLayout;
    keywordsLayout->addLayout(keywordRows_);
    auto* newKeywordRow = new QHBoxLayout;
    newKeywordBox_ = new QLineEdit(keywordsCard);
    newKeywordCaseCheck_ = new QCheckBox(keywordsCard);
    newKeywordCaseCheck_->setObjectName(QStringLiteral("newKeywordCaseCheck"));
    auto* addKeywordBtn = new QPushButton(keywordsCard);
    addKeywordBtn->setObjectName(QStringLiteral("addKeywordBtn"));
    connect(addKeywordBtn, &QPushButton::clicked, this, &MainWindow::onAddKeyword);
    connect(newKeywordBox_, &QLineEdit::returnPressed, this, &MainWindow::onAddKeyword);
    newKeywordRow->addWidget(newKeywordBox_);
    newKeywordRow->addWidget(newKeywordCaseCheck_);
    newKeywordRow->addWidget(addKeywordBtn);
    keywordsLayout->addLayout(newKeywordRow);
    cardsLayout->addWidget(keywordsCard);

    // -- AI Powered Detection Model card (Windows: SecurityCardBorder) --
    QVBoxLayout* detectionLayout;
    auto* detectionCard = makeCard(QString(), detectionLayout, cards);
    detectionCard->setObjectName(QStringLiteral("detectionCard"));
    detectionDescLabel_ = makeHint(detectionCard);
    detectionLayout->addWidget(detectionDescLabel_);
    // Windows ToggleSwitch: a self-drawn sliding switch (AgentToggleSwitch,
    // based on QCheckBox so its accessible role/checked/name contract is
    // unchanged). The visible text is the On/Off state; the accessible name
    // stays fixed so assistive tech can find the control regardless of state.
    useAiCheck_ = new AgentToggleSwitch(detectionCard);
    useAiCheck_->setObjectName(QStringLiteral("useAiSwitch"));
    connect(useAiCheck_, &QCheckBox::toggled, this, autosave);
    connect(useAiCheck_, &QCheckBox::toggled, this, [this](bool on) {
        useAiCheck_->setText(on ? tr("On") : tr("Off"));
    });
    detectionLayout->addWidget(useAiCheck_, 0, Qt::AlignLeft);
    detectionSlowLabel_ = makeHint(detectionCard);
    detectionLayout->addWidget(detectionSlowLabel_);
    auto* detectionForm = new QFormLayout;
    confidenceLabel_ = new QLabel(detectionCard);
    confidenceBox_ = new QLineEdit(detectionCard);
    detectionForm->addRow(confidenceLabel_, confidenceBox_);
    detectionLayout->addLayout(detectionForm);
    connect(confidenceBox_, &QLineEdit::textEdited, this, markDirty);
    // Autosave on commit (focus-out / Enter), not per keystroke: partial
    // input like "0." would fail validation if saved mid-typing.
    connect(confidenceBox_, &QLineEdit::editingFinished, this, autosave);
    auto* piiGrid = new QGridLayout;
    int row = 0, col = 0;
    for (const auto& type : DEFAULT_PII_TYPES) {
        auto* check = new QCheckBox(piiTypeLabel(type), detectionCard);
        connect(check, &QCheckBox::toggled, this, autosave);
        piiChecks_.emplace_back(type, check);
        piiGrid->addWidget(check, row, col);
        if (++col == 4) { col = 0; ++row; }
    }
    detectionLayout->addLayout(piiGrid);
    cardsLayout->addWidget(detectionCard);

    // -- Password card --
    QVBoxLayout* passwordLayout;
    auto* passwordCard = makeCard(QString(), passwordLayout, cards);
    passwordCard->setObjectName(QStringLiteral("passwordCard"));
    requirePasswordCheck_ = new QCheckBox(passwordCard);
    connect(requirePasswordCheck_, &QCheckBox::toggled, this, &MainWindow::onRequirePasswordToggled);
    passwordLayout->addWidget(requirePasswordCheck_);
    cardsLayout->addWidget(passwordCard);

    // -- Statistics card --
    QVBoxLayout* statsLayout;
    auto* statsCard = makeCard(QString(), statsLayout, cards);
    statsCard->setObjectName(QStringLiteral("statsCard"));
    statsLabel_ = new QLabel(statsCard);
    statsLayout->addWidget(statsLabel_);
    auto* clearStatsBtn = new QPushButton(statsCard);
    clearStatsBtn->setObjectName(QStringLiteral("clearStatsBtn"));
    connect(clearStatsBtn, &QPushButton::clicked, this, &MainWindow::onClearStatistics);
    statsLayout->addWidget(clearStatsBtn, 0, Qt::AlignLeft);
    cardsLayout->addWidget(statsCard);

    // -- Session redactions card --
    QVBoxLayout* matchesLayout2;
    auto* matchesCard = makeCard(QString(), matchesLayout2, cards);
    matchesCard->setObjectName(QStringLiteral("matchesCard"));
    matchesDescLabel_ = makeHint(matchesCard);
    matchesLayout2->addWidget(matchesDescLabel_);
    matchesEmptyLabel_ = makeHint(matchesCard);
    matchesLayout2->addWidget(matchesEmptyLabel_);
    matchesList_ = new QListWidget(matchesCard);
    matchesList_->setMinimumHeight(120);
    matchesLayout2->addWidget(matchesList_);
    auto* clearMatchesBtn = new QPushButton(matchesCard);
    clearMatchesBtn->setObjectName(QStringLiteral("clearMatchesBtn"));
    connect(clearMatchesBtn, &QPushButton::clicked, this, &MainWindow::onClearMatches);
    matchesLayout2->addWidget(clearMatchesBtn, 0, Qt::AlignLeft);
    cardsLayout->addWidget(matchesCard);

    // -- Logs card --
    QVBoxLayout* logsLayout;
    auto* logsCard = makeCard(QString(), logsLayout, cards);
    logsCard->setObjectName(QStringLiteral("logsCard"));
    loggingCheck_ = new QCheckBox(logsCard);
    connect(loggingCheck_, &QCheckBox::toggled, this, &MainWindow::onLoggingToggled);
    showSensitiveCheck_ = new QCheckBox(logsCard);
    connect(showSensitiveCheck_, &QCheckBox::toggled, this, &MainWindow::onShowSensitiveToggled);
    logsLayout->addWidget(loggingCheck_);
    logsLayout->addWidget(showSensitiveCheck_);
    auto* logBtns = new QHBoxLayout;
    auto* openLogBtn = new QPushButton(logsCard);
    openLogBtn->setObjectName(QStringLiteral("openLogBtn"));
    connect(openLogBtn, &QPushButton::clicked, this, &MainWindow::onOpenLog);
    auto* openFolderBtn = new QPushButton(logsCard);
    openFolderBtn->setObjectName(QStringLiteral("openFolderBtn"));
    connect(openFolderBtn, &QPushButton::clicked, this, &MainWindow::onOpenLogFolder);
    auto* clearLogsBtn = new QPushButton(logsCard);
    clearLogsBtn->setObjectName(QStringLiteral("clearLogsBtn"));
    connect(clearLogsBtn, &QPushButton::clicked, this, &MainWindow::onClearLogs);
    logBtns->addWidget(openLogBtn);
    logBtns->addWidget(openFolderBtn);
    logBtns->addWidget(clearLogsBtn);
    logBtns->addStretch();
    logsLayout->addLayout(logBtns);
    logsDisclaimerLabel_ = makeHint(logsCard);
    logsLayout->addWidget(logsDisclaimerLabel_);
    cardsLayout->addWidget(logsCard);

    // -- Settings card --
    QVBoxLayout* settingsLayout;
    auto* settingsCard = makeCard(QString(), settingsLayout, cards);
    settingsCard->setObjectName(QStringLiteral("settingsCard"));
    startOnBootCheck_ = new QCheckBox(settingsCard);
    connect(startOnBootCheck_, &QCheckBox::toggled, this, &MainWindow::onStartOnBootToggled);
    settingsLayout->addWidget(startOnBootCheck_);

    // Language selector (Windows: Settings page combo + tray submenu). Qt
    // retranslates live, so no restart is needed here — the settings poll
    // re-applies the tag and retranslateUi rebuilds every string.
    auto* langRow = new QHBoxLayout;
    languageLabel_ = new QLabel(settingsCard);
    languageCombo_ = new QComboBox(settingsCard);
    languageCombo_->addItem(QString(), QString()); // "System default" (retranslateUi)
    for (const auto& lang : SUPPORTED_LANGUAGES) {
        languageCombo_->addItem(QString::fromStdWString(lang.nativeName),
            QString::fromStdWString(lang.tag));
    }
    connect(languageCombo_, &QComboBox::activated, this, &MainWindow::onLanguageSelected);
    langRow->addWidget(languageLabel_);
    langRow->addWidget(languageCombo_, 1);
    settingsLayout->addLayout(langRow);
    if (AppUpdateManager::IsSelfRelease()) {
        checkUpdatesBtn_ = new QPushButton(settingsCard);
        connect(checkUpdatesBtn_, &QPushButton::clicked, this, [this] {
            updateMgr_->CheckForUpdates(true);
        });
        settingsLayout->addWidget(checkUpdatesBtn_, 0, Qt::AlignLeft);
    }
    cardsLayout->addWidget(settingsCard);

    cardsLayout->addStretch();
    scroll->setWidget(cards);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 0); // sidebar keeps its fixed width
    splitter->setStretchFactor(1, 1); // cards take the remaining space

    contentLayout->addWidget(splitter);
    centralStack_->addWidget(content);

    // ---- Page 1: lock overlay (opaque; content page is hidden while shown) ----
    lockOverlay_ = new QWidget(central);
    lockOverlay_->setStyleSheet(QStringLiteral("background-color: #202020; color: white;"));
    auto* overlayOuter = new QVBoxLayout(lockOverlay_);
    overlayOuter->addStretch();
    auto* overlayBox = new QVBoxLayout;
    overlayBox->setSpacing(12);
    auto* lockTitle = new QLabel(lockOverlay_);
    lockTitle->setObjectName(QStringLiteral("lockTitle"));
    lockTitle->setAlignment(Qt::AlignCenter);
    auto f = lockTitle->font();
    f.setPointSize(16);
    f.setBold(true);
    lockTitle->setFont(f);
    overlayBox->addWidget(lockTitle);
    unlockBox_ = new QLineEdit(lockOverlay_);
    unlockBox_->setEchoMode(QLineEdit::Password);
    unlockBox_->setMaximumWidth(300);
    connect(unlockBox_, &QLineEdit::returnPressed, this, &MainWindow::onUnlockClicked);
    overlayBox->addWidget(unlockBox_, 0, Qt::AlignHCenter);
    unlockError_ = new QLabel(lockOverlay_);
    unlockError_->setStyleSheet(QStringLiteral("color: #ff8080"));
    unlockError_->setAlignment(Qt::AlignCenter);
    unlockError_->setVisible(false);
    overlayBox->addWidget(unlockError_);
    auto* unlockBtn = new QPushButton(lockOverlay_);
    unlockBtn->setObjectName(QStringLiteral("unlockBtn"));
    unlockBtn->setMaximumWidth(300);
    connect(unlockBtn, &QPushButton::clicked, this, &MainWindow::onUnlockClicked);
    overlayBox->addWidget(unlockBtn, 0, Qt::AlignHCenter);
    overlayOuter->addLayout(overlayBox);
    overlayOuter->addStretch();
    centralStack_->addWidget(lockOverlay_);

    setCentralWidget(central);
    centralStack_->setCurrentIndex(0);

    retranslateUi();
}

void MainWindow::retranslateUi() {
    setWindowTitle(tr("Agent Redactor"));
    findChild<QGroupBox*>(QStringLiteral("quickStartCard"))->setTitle(tr("How to use Agent Redactor"));
    findChild<QGroupBox*>(QStringLiteral("profileCard"))->setTitle(tr("API Proxy"));
    findChild<QGroupBox*>(QStringLiteral("detectionCard"))->setTitle(tr("AI Powered Detection Model"));
    findChild<QGroupBox*>(QStringLiteral("regexCard"))->setTitle(tr("Regex Patterns"));
    findChild<QGroupBox*>(QStringLiteral("keywordsCard"))->setTitle(tr("Keywords"));
    findChild<QGroupBox*>(QStringLiteral("passwordCard"))->setTitle(tr("Password"));
    findChild<QGroupBox*>(QStringLiteral("statsCard"))->setTitle(tr("Statistics"));
    findChild<QGroupBox*>(QStringLiteral("matchesCard"))->setTitle(tr("Session Redactions"));
    findChild<QGroupBox*>(QStringLiteral("logsCard"))->setTitle(tr("Logs"));
    findChild<QGroupBox*>(QStringLiteral("settingsCard"))->setTitle(tr("Settings"));

    aliasLabel_->setText(tr("Name:"));
    portLabel_->setText(tr("Local URL"));
    urlLabel_->setText(tr("Forward To"));
    apiKeyLabel_->setText(tr("API Key"));
    confidenceLabel_->setText(tr("Confidence threshold:"));

    // Help text reused verbatim from the Windows HomePage resw strings so the
    // existing per-language catalogs translate them for free.
    profilesHeader_->setText(tr("Profiles"));
    quickStep1Label_->setText(tr("1. Configure your profile below (or use the default)"));
    quickStep2Label_->setText(tr("2. Point your LLM client (Claude Code, OpenClaw, etc.) at the Local URL shown below"));
    quickStep3Label_->setText(tr("3. We sit between your client and real API. Everything stays on your machine. Sensitive data is redacted locally before any request leaves your computer ensuring your data never touches our server"));
    localUrlHint_->setText(tr("The address your LLM client points at"));
    forwardToHint_->setText(tr("The real API endpoint that receives your redacted requests"));
    detectionDescLabel_->setText(tr("AI-powered detection runs locally as an additional layer of defense. May miss data or over-redact. Use Regex Patterns and Keywords below for deterministic redaction."));
    detectionSlowLabel_->setText(tr("Expect slightly slower responses when enabled. The model scans every message locally."));
    regexDescLabel_->setText(tr("Text matching these patterns will be redacted before sending to the API"));
    keywordsDescLabel_->setText(tr("Messages containing these words will be flagged for redaction"));
    matchesDescLabel_->setText(tr("Actual redactions detected in the current session."));
    matchesEmptyLabel_->setText(tr("No redactions in current session."));
    logsDisclaimerLabel_->setText(tr("Logs are stored on this PC. Redacted logs may still contain sensitive data that detection missed."));

    // Hint text reused from the Windows HomePage placeholders so the existing
    // per-language catalogs translate them for free.
    aliasBox_->setPlaceholderText(tr("e.g., Work OpenAI"));
    urlBox_->setPlaceholderText(tr("e.g. https://openrouter.ai/api/v1"));
    newRegexBox_->setPlaceholderText(tr("e.g. sk-[a-zA-Z0-9]{20,}"));
    newKeywordBox_->setPlaceholderText(tr("e.g. password"));

    addProfileBtn_->setText(tr("Add"));
    removeProfileBtn_->setText(tr("Remove"));
    showKeyCheck_->setText(tr("Show API key"));
    copyUrlBtn_->setText(tr("Copy"));
    saveBtn_->setText(tr("Save"));
    // The master detection toggle is a switch: its visible text is the On/Off
    // state, its accessible name identifies the control.
    useAiCheck_->setText(useAiCheck_->isChecked() ? tr("On") : tr("Off"));
    useAiCheck_->setAccessibleName(tr("Use AI model for PII detection"));
    newKeywordCaseCheck_->setText(tr("Case sensitive"));
    findChild<QPushButton*>(QStringLiteral("addRegexBtn"))->setText(tr("Add"));
    findChild<QPushButton*>(QStringLiteral("addKeywordBtn"))->setText(tr("Add"));
    requirePasswordCheck_->setText(tr("Require master password"));
    findChild<QPushButton*>(QStringLiteral("clearStatsBtn"))->setText(tr("Clear statistics"));
    findChild<QPushButton*>(QStringLiteral("clearMatchesBtn"))->setText(tr("Clear"));
    loggingCheck_->setText(tr("Enable logging"));
    showSensitiveCheck_->setText(tr("Show sensitive information in logs"));
    findChild<QPushButton*>(QStringLiteral("openLogBtn"))->setText(tr("Open log file"));
    findChild<QPushButton*>(QStringLiteral("openFolderBtn"))->setText(tr("Open folder"));
    findChild<QPushButton*>(QStringLiteral("clearLogsBtn"))->setText(tr("Delete all logs"));
    startOnBootCheck_->setText(tr("Start on Boot"));
    languageLabel_->setText(tr("Language"));
    languageCombo_->setItemText(0, tr("System default"));
    if (checkUpdatesBtn_) checkUpdatesBtn_->setText(tr("Check for updates"));
    unlockBox_->setPlaceholderText(tr("Master Password"));
    findChild<QPushButton*>(QStringLiteral("unlockBtn"))->setText(tr("Unlock"));
    if (auto* t = findChild<QLabel*>(QStringLiteral("lockTitle")))
        t->setText(tr("Agent Redactor is locked"));

    // Accessible names for widgets whose accessible label would otherwise be
    // empty or fall back to the enclosing card title (the four Profile form
    // boxes all reported "Profile" without these).
    profileList_->setAccessibleName(tr("Profiles"));
    aliasBox_->setAccessibleName(tr("Profile name"));
    portBox_->setAccessibleName(tr("Proxy port"));
    urlBox_->setAccessibleName(tr("Forward To URL"));
    apiKeyBox_->setAccessibleName(tr("API key"));
    confidenceBox_->setAccessibleName(tr("Confidence threshold"));
    newRegexBox_->setAccessibleName(tr("New regex pattern"));
    newKeywordBox_->setAccessibleName(tr("New keyword"));
    matchesList_->setAccessibleName(tr("Session redactions"));
    unlockBox_->setAccessibleName(tr("Master password"));

    // PII grid labels are translated too (Windows PII_Type_* strings).
    for (auto& [type, check] : piiChecks_) check->setText(piiTypeLabel(type));

    // Regex/keyword column headers (the rows themselves are rebuilt by
    // reloadProfiles on language change).
    regexEnabledHeader_->setText(tr("Enabled"));
    regexPatternHeader_->setText(tr("Regex Pattern"));
    keywordEnabledHeader_->setText(tr("Enabled"));
    keywordCaseHeader_->setText(tr("Case"));
    keywordKeywordHeader_->setText(tr("Keyword"));

    // Re-render the port status in the new language.
    updatePortStatus();

    // The Statistics card content holds localized "Requests: ..." text; render
    // it again so it follows tr() without waiting for the next status tick.
    refreshStats();
}

QString MainWindow::piiTypeLabel(const std::wstring& type) {
    // English sources match the Windows PII_Type_<type> resw values, so the
    // existing per-language catalogs translate these for free.
    if (type == L"account_number")  return tr("Account number");
    if (type == L"private_address") return tr("Address");
    if (type == L"private_date")    return tr("Date");
    if (type == L"private_email")   return tr("Email");
    if (type == L"private_person")  return tr("Person");
    if (type == L"private_phone")   return tr("Phone");
    if (type == L"private_url")     return tr("URL");
    if (type == L"secret")          return tr("Secret");
    // Unknown future type: raw type name, humanized (English fallback).
    QString s = QString::fromStdWString(type);
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

// ---------------------------------------------------------------------------
// Poll-driven refresh
// ---------------------------------------------------------------------------

void MainWindow::onStatusUpdated() {
    statusBar()->clearMessage();

    const json& status = appState_->lastStatus();
    const bool unlocked = status.value("unlocked", true);

    // Lock state: show the overlay when protection is on and the session is
    // locked; the poll also lifts it after an external CLI unlock path.
    if (isProtected() && !unlocked) {
        showLockOverlay();
    } else if (centralStack_->currentIndex() == 1 && (!isProtected() || unlocked)) {
        hideLockOverlay();
    }
    if (unlocked) inactivityTimer_->start();

    // Stats + session matches refresh every tick (Windows: UpdateStats +
    // LoadMatchesList on the same cadence).
    if (json* p = selectedProfile(); p && !dirty_) {
        // Stats change on every proxied request without bumping
        // profilesRevision, so the cached profile json goes stale between
        // reloads; refresh just the stats from the engine.
        json live;
        if (appState_->client().GetProfiles(live) && live.is_array()) {
            const std::string id = (*p).value("id", std::string());
            for (const auto& lp : live) {
                if (lp.value("id", std::string()) == id) {
                    (*p)["stats"] = lp.value("stats", json::object());
                    break;
                }
            }
        }
        refreshStats();

        const std::wstring id = w(QString::fromStdString((*p)["id"].get<std::string>()));
        json matches;
        if (appState_->client().GetMatches(id, matches) && matches.is_array()) {
            matchesList_->clear();
            for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
                const QString line = QStringLiteral("[%1] %2 (%3): %4")
                    .arg(QString::fromStdString(it->value("timestamp", std::string())))
                    .arg(QString::fromStdString(it->value("type", std::string())))
                    .arg(QString::fromStdString(it->value("detail", std::string())))
                    .arg(QString::fromStdString(it->value("matchedText", std::string())));
                matchesList_->addItem(line);
            }
        }
        matchesEmptyLabel_->setVisible(matchesList_->count() == 0);
    }

    // The port verdict depends on the profiles snapshot (cross-profile
    // conflicts) and on proxyRunning, both of which change under the feet of
    // a debounced textChanged evaluation; recompute every poll so the label
    // never stays stale (Windows: UpdateProxyStatus on the same cadence).
    updatePortStatus();
}

void MainWindow::onSettingsChanged() {
    const json& settings = appState_->lastSettings();

    const bool startOnBoot = settings.value("startOnBoot", false);
    {
        QSignalBlocker b(startOnBootCheck_);
        startOnBootCheck_->setChecked(startOnBoot);
    }
    tray_->setStartOnBoot(startOnBoot);

    {
        QSignalBlocker b1(loggingCheck_);
        QSignalBlocker b2(showSensitiveCheck_);
        loggingCheck_->setChecked(settings.value("loggingEnabled", false));
        showSensitiveCheck_->setChecked(settings.value("showSensitive", false));
    }

    translator_->applyLanguage(
        QString::fromStdString(settings.value("appLanguage", std::string())));

    // Language controls: the engine reports the effective tag (empty/system
    // is already resolved to the OS locale), so an unrecognized tag means
    // "System default" — nothing is checked/selected beyond index 0.
    const QString langTag = QString::fromStdString(settings.value("appLanguage", std::string()));
    {
        QSignalBlocker b(languageCombo_);
        const int idx = languageCombo_->findData(langTag);
        languageCombo_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    tray_->setCurrentLanguage(langTag);

    {
        QSignalBlocker b(requirePasswordCheck_);
        requirePasswordCheck_->setChecked(isProtected());
    }

    // Cheap full-reload trigger: profile mutations bump profilesRevision.
    const uint64_t revision = settings.value("profilesRevision", uint64_t{0});
    if (revision != prevProfilesRevision_) {
        prevProfilesRevision_ = revision;
        if (!dirty_) reloadProfiles(true);
    }

    ensureLockState(false);
}

void MainWindow::onModelDownloadChanged() {
    updateModelDownloadDialog();
}

void MainWindow::onConnectionLost() {
    statusBar()->showMessage(tr("Engine is not running — retrying…"));
}

// ---------------------------------------------------------------------------
// Profiles: load / select / save
// ---------------------------------------------------------------------------

// Snapshot the four profile text fields while the form has uncommitted edits
// (dirty_). reloadProfiles() unconditionally rebuilds the whole form from the
// engine snapshot, which would discard those edits; the row-mutation handlers
// snapshot before their reload and restore afterwards (Windows HomePage
// formDirty_ parity).
std::array<QString, 4> MainWindow::savePendingFormText() const {
    std::array<QString, 4> fields{};
    if (dirty_) {
        fields[0] = aliasBox_->text();
        fields[1] = portBox_->text();
        fields[2] = urlBox_->text();
        fields[3] = apiKeyBox_->text();
    }
    return fields;
}

void MainWindow::restorePendingFormText(const std::array<QString, 4>& fields,
    bool wasDirty) {
    if (!wasDirty) return;
    // Re-apply pending edits over the freshly reloaded values. Only set a
    // field that actually differs, so the textChanged dirty-tracking does not
    // re-mark the form dirty for fields that were never edited.
    if (aliasBox_->text() != fields[0]) aliasBox_->setText(fields[0]);
    if (portBox_->text() != fields[1]) portBox_->setText(fields[1]);
    if (urlBox_->text() != fields[2]) urlBox_->setText(fields[2]);
    if (apiKeyBox_->text() != fields[3]) apiKeyBox_->setText(fields[3]);
}

void MainWindow::reloadProfiles(bool keepSelection) {
    if (reloadingProfiles_) {
        // A reload is already on the stack (e.g. the poll fired inside a
        // nested event loop); run one more pass when it finishes.
        reloadPending_ = true;
        return;
    }
    reloadingProfiles_ = true;
    struct ReloadGuard {
        bool& flag;
        ~ReloadGuard() { flag = false; }
    } guard{reloadingProfiles_};

    json profiles;
    if (!appState_->client().GetProfiles(profiles) || !profiles.is_array()) {
        qWarning("[MainWindow] reloadProfiles: GetProfiles failed");
        return;
    }

    if (profiles.empty()) {
        // Mirror the Windows GUI (HomePage::LoadProfileList): seed a default
        // profile on first run so the user never lands on an empty form.
        const int port = FindAvailablePort(8080, {});
        json profile = {
            {"alias", tr("Default").toStdString()},
            {"upstream_url", ""},
            {"api_key", ""},
            {"port", port > 0 ? port : 8080},
            {"use_openai_model", true},
            {"protocol_mode", "none"},
            {"enabled_pii_types", json::array()},
            {"pii_confidence_threshold", 0.9},
            {"regex_patterns", json::array()},
            {"keywords", json::array()},
            {"stats", {{"total_requests", 0}, {"total_pii_detected", 0},
                       {"total_regex_matches", 0}, {"total_keyword_matches", 0},
                       {"pii_type_breakdown", json::object()}}},
            {"enabled", true},
        };
        for (const auto& t : DEFAULT_PII_TYPES)
            profile["enabled_pii_types"].push_back(Utils::WideToUtf8(t));
        std::wstring id;
        if (appState_->client().PostProfile(profile, id)) {
            // POST /profiles already restarts the listeners engine-side.
            // The re-entry guard defers this to a follow-up pass below.
            reloadProfiles(keepSelection);
        }
    } else {
        const QString previousId = keepSelection ? selectedProfileId() : QString();
        profiles_ = profiles;

        loading_ = true;
        profileList_->clear();
        int selectRow = 0;
        for (size_t i = 0; i < profiles_.size(); ++i) {
            const auto& p = profiles_[i];
            profileList_->addItem(QString::fromStdString(p.value("alias", std::string())));
            if (!previousId.isEmpty() &&
                previousId == QString::fromStdString(p.value("id", std::string()))) {
                selectRow = static_cast<int>(i);
            }
        }
        if (!profiles_.empty()) {
            profileList_->setCurrentRow(selectRow);
            loadProfileIntoForm(selectRow);
        }
        removeProfileBtn_->setEnabled(profiles_.size() > 1);
        loading_ = false;
        dirty_ = false;
    }

    // A nested reload request arrived while this pass was running; do one
    // follow-up pass with fresh data once the guard releases the flag.
    if (reloadPending_) {
        reloadPending_ = false;
        reloadingProfiles_ = false;
        reloadProfiles(keepSelection);
    }
}

void MainWindow::loadProfileIntoForm(int index) {
    if (index < 0 || index >= static_cast<int>(profiles_.size())) return;
    loading_ = true;
    const json& p = profiles_[index];

    aliasBox_->setText(QString::fromStdString(p.value("alias", std::string())));
    portBox_->setText(QString::number(p.value("port", 0)));
    urlBox_->setText(QString::fromStdString(p.value("upstream_url", std::string())));

    // The profiles list only ever serves the masked key; fetch the real one
    // (403 while locked — then keep the masked placeholder).
    const std::wstring id = w(QString::fromStdString(p.value("id", std::string())));
    std::wstring key;
    if (appState_->client().GetApiKey(id, key)) {
        apiKeyBox_->setText(q(key));
    } else {
        apiKeyBox_->setText(QString::fromStdString(p.value("api_key", std::string())));
    }
    apiKeyBox_->setEchoMode(QLineEdit::Password);
    {
        QSignalBlocker b(showKeyCheck_);
        showKeyCheck_->setChecked(false);
    }

    useAiCheck_->setChecked(p.value("use_openai_model", true));
    confidenceBox_->setText(QString::number(p.value("pii_confidence_threshold", 0.9)));

    const std::vector<std::string> enabledTypes =
        p.value("enabled_pii_types", std::vector<std::string>{});
    for (auto& [type, check] : piiChecks_) {
        QSignalBlocker b(check);
        check->setChecked(std::find(enabledTypes.begin(), enabledTypes.end(),
            Utils::WideToUtf8(type)) != enabledTypes.end());
    }

    // Rebuild regex rows (each row is a widget so takeAt cleans up).
    const auto regexPatterns = p.value("regex_patterns", json::array());
    regexHeader_->setVisible(!regexPatterns.empty());
    while (QLayoutItem* item = regexRows_->takeAt(0)) {
        // deleteLater, not delete: the row being rebuilt may contain the
        // widget whose signal handler triggered this reload (e.g. the Delete
        // button mid-mouse-release) — destroying it synchronously returns
        // into Qt event code on a deleted object (general protection fault
        // in libQt6Widgets). The deferred delete runs once the handler
        // returns to the event loop.
        if (QWidget* row = item->widget()) row->deleteLater();
        delete item;
    }
    for (const auto& r : regexPatterns) {
        auto* rowWidget = new QWidget;
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* enabled = new QCheckBox;
        enabled->setChecked(r.value("enabled", true));
        enabled->setAccessibleName(tr("Enable pattern"));
        // Fixed-width column so the rows line up under the header labels.
        auto* enabledCol = new QWidget(rowWidget);
        auto* enabledColLayout = new QHBoxLayout(enabledCol);
        enabledColLayout->setContentsMargins(0, 0, 0, 0);
        enabledColLayout->addWidget(enabled);
        enabledCol->setFixedWidth(kEnabledColumnWidth);
        auto* pattern = new QLineEdit(QString::fromStdString(r.value("pattern", std::string())));
        pattern->setAccessibleName(tr("Regex pattern"));
        pattern->setFont(rowFont());
        auto* del = new QPushButton(QString::fromUtf8("✕"));
        del->setProperty("danger", true);
        del->setAccessibleName(tr("Delete"));
        rowLayout->addWidget(enabledCol);
        rowLayout->addWidget(pattern, 1);
        rowLayout->addWidget(del);
        regexRows_->addWidget(rowWidget);

        connect(enabled, &QCheckBox::toggled, this, [this, pattern, enabled] {
            json* p = selectedProfile();
            if (!p) return;
            const std::string pat = pattern->text().toStdString();
            for (auto& r : (*p)["regex_patterns"]) {
                if (r.value("pattern", std::string()) == pat) r["enabled"] = enabled->isChecked();
            }
            appState_->client().PutProfile(w(selectedProfileId()), *p);
        });
        connect(pattern, &QLineEdit::editingFinished, this, [this, pattern] {
            json* p = selectedProfile();
            if (!p) return;
            // Validate before applying (mirrors Windows LostFocus validation).
            const std::wstring normalized =
                Utils::NormalizeRegexBraces(pattern->text().toStdWString());
            try {
                std::regex re(Utils::WideToUtf8(normalized), std::regex_constants::ECMAScript);
            } catch (const std::regex_error&) {
                QMessageBox::warning(this, tr("Validation Error"),
                    tr("Invalid regex syntax."));
                // The revert reloads the whole form; keep pending profile
                // text edits across it (Windows formDirty_ parity).
                const bool wasDirty = dirty_;
                const std::array<QString, 4> fields = savePendingFormText();
                reloadProfiles(true);
                restorePendingFormText(fields, wasDirty);
                return;
            }
            dirty_ = true;
            onSaveProfile();
        });
        // Programmatic edits (AT-SPI) never fire editingFinished; without
        // textChanged a poll-triggered reload can wipe an unsaved row edit.
        connect(pattern, &QLineEdit::textChanged, this,
                [this] { if (!loading_) dirty_ = true; });
        connect(del, &QPushButton::clicked, this, [this, pattern] {
            json* p = selectedProfile();
            if (!p) return;
            const std::string pat = pattern->text().toStdString();
            auto& arr = (*p)["regex_patterns"];
            arr.erase(std::remove_if(arr.begin(), arr.end(), [&](const json& r) {
                return r.value("pattern", std::string()) == pat;
            }), arr.end());
            const bool wasDirty = dirty_;
            const std::array<QString, 4> fields = savePendingFormText();
            if (appState_->client().PutProfile(w(selectedProfileId()), *p)) {
                reloadProfiles(true);
                restorePendingFormText(fields, wasDirty);
            }
        });
    }

    // Rebuild keyword rows. Deferred deletion for the same reason as the
    // regex rows above: the triggering widget may be inside a row.
    const auto keywords = p.value("keywords", json::array());
    keywordHeader_->setVisible(!keywords.empty());
    while (QLayoutItem* item = keywordRows_->takeAt(0)) {
        if (QWidget* row = item->widget()) row->deleteLater();
        delete item;
    }
    for (const auto& k : keywords) {
        auto* rowWidget = new QWidget;
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* enabled = new QCheckBox;
        enabled->setChecked(k.value("enabled", true));
        enabled->setAccessibleName(tr("Enable keyword"));
        // Fixed-width columns so the rows line up under the header labels.
        auto* enabledCol = new QWidget(rowWidget);
        auto* enabledColLayout = new QHBoxLayout(enabledCol);
        enabledColLayout->setContentsMargins(0, 0, 0, 0);
        enabledColLayout->addWidget(enabled);
        enabledCol->setFixedWidth(kEnabledColumnWidth);
        // Windows uses a borderless Yes/No toggle button in the Case column.
        auto* caseBtn = new QPushButton(k.value("case_sensitive", true)
            ? tr("Yes") : tr("No"));
        caseBtn->setFixedWidth(kCaseColumnWidth);
        auto* text = new QLineEdit(QString::fromStdString(k.value("text", std::string())));
        text->setAccessibleName(tr("Keyword text"));
        text->setFont(rowFont());
        auto* del = new QPushButton(QString::fromUtf8("✕"));
        del->setProperty("danger", true);
        del->setAccessibleName(tr("Delete"));
        rowLayout->addWidget(enabledCol);
        rowLayout->addWidget(caseBtn);
        rowLayout->addWidget(text, 1);
        rowLayout->addWidget(del);
        keywordRows_->addWidget(rowWidget);

        auto mutateKeyword = [this, text](std::function<void(json&)> mutate) {
            json* p = selectedProfile();
            if (!p) return;
            const std::string t = text->text().toStdString();
            for (auto& kw : (*p)["keywords"]) {
                if (kw.value("text", std::string()) == t) mutate(kw);
            }
            appState_->client().PutProfile(w(selectedProfileId()), *p);
        };
        connect(enabled, &QCheckBox::toggled, this, [this, mutateKeyword](bool on) {
            mutateKeyword([on](json& kw) { kw["enabled"] = on; });
        });
        // The Case column is a Yes/No toggle with the real value in the label
        // (Windows: keywordCaseButtons_ content = Common_Yes / Common_No).
        connect(caseBtn, &QPushButton::clicked, this, [this, caseBtn, mutateKeyword] {
            const bool newValue = caseBtn->text() != tr("Yes"); // was No -> now Yes
            mutateKeyword([newValue](json& kw) { kw["case_sensitive"] = newValue; });
            caseBtn->setText(newValue ? tr("Yes") : tr("No"));
        });
        connect(text, &QLineEdit::editingFinished, this, [this] {
            dirty_ = true;
            onSaveProfile();
        });
        // Programmatic edits (AT-SPI) never fire editingFinished; without
        // textChanged a poll-triggered reload can wipe an unsaved row edit.
        connect(text, &QLineEdit::textChanged, this,
                [this] { if (!loading_) dirty_ = true; });
        connect(del, &QPushButton::clicked, this, [this, text] {
            json* p = selectedProfile();
            if (!p) return;
            const std::string t = text->text().toStdString();
            auto& arr = (*p)["keywords"];
            arr.erase(std::remove_if(arr.begin(), arr.end(), [&](const json& kw) {
                return kw.value("text", std::string()) == t;
            }), arr.end());
            const bool wasDirty = dirty_;
            const std::array<QString, 4> fields = savePendingFormText();
            if (appState_->client().PutProfile(w(selectedProfileId()), *p)) {
                reloadProfiles(true);
                restorePendingFormText(fields, wasDirty);
            }
        });
    }

    loading_ = false;
    dirty_ = false;
    updatePortStatus();
}

QString MainWindow::selectedProfileId() const {
    const int row = profileList_->currentRow();
    if (row < 0 || row >= static_cast<int>(profiles_.size())) return {};
    return QString::fromStdString(profiles_[row].value("id", std::string()));
}

json* MainWindow::selectedProfile() {
    const int row = profileList_->currentRow();
    if (row < 0 || row >= static_cast<int>(profiles_.size())) return nullptr;
    return &profiles_[row];
}

void MainWindow::onProfileSelectionChanged() {
    if (!loading_) loadProfileIntoForm(profileList_->currentRow());
}

json MainWindow::gatherProfileFromForm() {
    json* base = selectedProfile();
    json p = base ? *base : json::object();
    p["alias"] = aliasBox_->text().toStdString();
    p["port"] = portBox_->text().toInt();
    // Strip accidental leading/trailing whitespace so a pasted URL or key
    // with trailing spaces does not get used verbatim by the engine.
    p["upstream_url"] = urlBox_->text().trimmed().toStdString();
    p["api_key"] = apiKeyBox_->text().trimmed().toStdString();
    p["use_openai_model"] = useAiCheck_->isChecked();
    p["pii_confidence_threshold"] = confidenceBox_->text().toDouble();

    std::vector<std::string> types;
    for (const auto& [type, check] : piiChecks_) {
        if (check->isChecked()) types.push_back(Utils::WideToUtf8(type));
    }
    p["enabled_pii_types"] = types;

    // Row edits only mark the form dirty; the cached profile json still holds
    // the pre-edit keywords/regexes. Rebuild both arrays from the row widgets
    // so a save after editing a row text actually persists the new value.
    json regexes = json::array();
    for (int i = 0; i < regexRows_->count(); ++i) {
        auto* rowWidget = regexRows_->itemAt(i)->widget();
        if (!rowWidget) continue;
        auto* enabled = rowWidget->findChild<QCheckBox*>();
        auto* pattern = rowWidget->findChild<QLineEdit*>();
        if (!enabled || !pattern) continue;
        const std::wstring normalized =
            Utils::NormalizeRegexBraces(pattern->text().toStdWString());
        regexes.push_back({{"pattern", Utils::WideToUtf8(normalized)},
            {"enabled", enabled->isChecked()}});
    }
    p["regex_patterns"] = regexes;

    json keywords = json::array();
    for (int i = 0; i < keywordRows_->count(); ++i) {
        auto* rowWidget = keywordRows_->itemAt(i)->widget();
        if (!rowWidget) continue;
        auto* enabled = rowWidget->findChild<QCheckBox*>();
        auto* caseBtn = rowWidget->findChild<QPushButton*>();
        auto* text = rowWidget->findChild<QLineEdit*>();
        if (!enabled || !caseBtn || !text) continue;
        keywords.push_back({{"text", text->text().toStdString()},
            {"case_sensitive", caseBtn->text() == tr("Yes")},
            {"enabled", enabled->isChecked()}});
    }
    p["keywords"] = keywords;
    return p;
}

bool MainWindow::validateForm(QString& error, bool& httpWarning) {
    httpWarning = false;

    bool ok = false;
    const int port = portBox_->text().toInt(&ok);
    if (!ok || port < 1024 || port > 65535) {
        error = tr("Port must be between 1024 and 65535.");
        return false;
    }
    for (size_t i = 0; i < profiles_.size(); ++i) {
        if (static_cast<int>(i) == profileList_->currentRow()) continue;
        if (profiles_[i].value("port", 0) == port) {
            error = tr("Port %1 is already used by profile '%2'.")
                .arg(port)
                .arg(QString::fromStdString(profiles_[i].value("alias", std::string())));
            return false;
        }
    }

    const QString url = urlBox_->text().trimmed();
    const QUrl parsed(url);
    if (url.isEmpty() ||
        !(url.startsWith(QLatin1String("http://")) || url.startsWith(QLatin1String("https://"))) ||
        parsed.host().isEmpty()) {
        error = tr("Forward To URL must start with http:// or https://.");
        return false;
    }
    if (url.startsWith(QLatin1String("http://")) &&
        parsed.host() != QLatin1String("localhost") &&
        !parsed.host().startsWith(QLatin1String("127."))) {
        httpWarning = true;
    }

    const double confidence = confidenceBox_->text().toDouble(&ok);
    if (!ok || confidence < 0.0 || confidence > 1.0) {
        error = tr("Confidence threshold must be between 0.0 and 1.0.");
        return false;
    }
    return true;
}

void MainWindow::onSaveProfile() {
    json* p = selectedProfile();
    if (!p) return;

    QString error;
    bool httpWarning = false;
    if (!validateForm(error, httpWarning)) {
        QMessageBox::warning(this, tr("Validation Error"), error);
        reloadProfiles(true);
        return;
    }
    if (httpWarning) {
        const auto answer = QMessageBox::warning(this, tr("Security Warning"),
            tr("You are using an HTTP (unencrypted) upstream URL. Your API key will be "
               "sent in plaintext over the network."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            reloadProfiles(true);
            return;
        }
    }

    const json updated = gatherProfileFromForm();
    if (!appState_->client().PutProfile(w(selectedProfileId()), updated)) {
        QMessageBox::warning(this, tr("Error"),
            tr("The engine rejected the profile. Check the engine log for details."));
        return;
    }
    // PUT /profiles already restarts the listeners engine-side.
    dirty_ = false;
    reloadProfiles(true);
}

void MainWindow::onAddProfile() {
    // First free port from 8080, excluding ports used by other profiles.
    std::vector<int> exclude;
    for (const auto& p : profiles_) exclude.push_back(p.value("port", 0));
    const int port = FindAvailablePort(8080, exclude);

    json profile = {
        {"alias", tr("Profile %1").arg(profiles_.size() + 1).toStdString()},
        {"upstream_url", ""},
        {"api_key", ""},
        {"port", port},
        {"use_openai_model", true},
        {"protocol_mode", "none"},
        {"enabled_pii_types", json::array()},
        {"pii_confidence_threshold", 0.9},
        {"regex_patterns", json::array()},
        {"keywords", json::array()},
        {"stats", {{"total_requests", 0}, {"total_pii_detected", 0},
                   {"total_regex_matches", 0}, {"total_keyword_matches", 0},
                   {"pii_type_breakdown", json::object()}}},
        {"enabled", true},
    };
    for (const auto& t : DEFAULT_PII_TYPES) profile["enabled_pii_types"].push_back(Utils::WideToUtf8(t));

    std::wstring id;
    if (!appState_->client().PostProfile(profile, id)) {
        QMessageBox::warning(this, tr("Error"),
            tr("The engine rejected the new profile."));
        return;
    }
    // POST /profiles already restarts the listeners engine-side.
    dirty_ = false;
    reloadProfiles(false);
    // Select the new profile.
    for (int i = 0; i < profileList_->count(); ++i) {
        if (QString::fromStdString(profiles_[i].value("id", std::string())) == q(id)) {
            profileList_->setCurrentRow(i);
            break;
        }
    }
}

void MainWindow::onRemoveProfile() {
    if (profiles_.size() <= 1) return; // the last profile cannot be deleted
    const auto answer = QMessageBox::question(this, tr("Remove Profile"),
        tr("Are you sure? This operation is permanent."));
    if (answer != QMessageBox::Yes) return;

    if (appState_->client().DeleteProfile(w(selectedProfileId()))) {
        dirty_ = false;
        reloadProfiles(false);
    }
}

void MainWindow::onCopyUrl() {
    QGuiApplication::clipboard()->setText(
        QStringLiteral("http://localhost:%1/").arg(portBox_->text()));
    statusBar()->showMessage(tr("Proxy URL copied to clipboard"), 3000);
}

void MainWindow::onToggleApiKeyVisible(bool visible) {
    apiKeyBox_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
}

void MainWindow::updatePortStatus() {
    bool ok = false;
    const int port = portBox_->text().toInt(&ok);
    if (!ok || port < 1024 || port > 65535) {
        portStatusLabel_->setVisible(false);
        return;
    }
    // A port owned by another saved profile is always shown as taken,
    // mirroring Windows HomePage::UpdateProxyStatus. This must be checked
    // before the running/availability branch (an external listener would not
    // be caught by the profile loop, and another profile's running proxy would
    // otherwise be reported as "available").
    const int currentRow = profileList_->currentRow();
    for (int i = 0; i < static_cast<int>(profiles_.size()); ++i) {
        if (i == currentRow) continue;
        if (profiles_[i].value("port", 0) == port) {
            portStatusLabel_->setText(tr("Port %1 is already used by profile '%2'.")
                .arg(port)
                .arg(QString::fromStdString(profiles_[i].value("alias", std::string()))));
            portStatusLabel_->setProperty("statusOk", false);
            portStatusLabel_->setProperty("statusErr", true);
            portStatusLabel_->style()->unpolish(portStatusLabel_);
            portStatusLabel_->style()->polish(portStatusLabel_);
            portStatusLabel_->setVisible(true);
            return;
        }
    }
    // A port the engine already listens on (this profile's running proxy)
    // counts as available, mirroring Windows HomePage::UpdateProxyStatus.
    bool running = false;
    for (const auto& sp : appState_->lastStatus().value("profiles", json::array())) {
        if (sp.value("port", 0) == port && sp.value("proxyRunning", false)) {
            running = true;
            break;
        }
    }
    const bool available = running || IsPortAvailable(port);
    portStatusLabel_->setText(available
        ? tr("Port %1 is available").arg(port)
        : tr("Port %1 is already in use").arg(port));
    portStatusLabel_->setProperty("statusOk", available);
    portStatusLabel_->setProperty("statusErr", !available);
    portStatusLabel_->style()->unpolish(portStatusLabel_);
    portStatusLabel_->style()->polish(portStatusLabel_);
    portStatusLabel_->setVisible(true);
}

// ---------------------------------------------------------------------------
// Regex / keyword add
// ---------------------------------------------------------------------------

void MainWindow::onAddRegex() {
    json* p = selectedProfile();
    const QString pattern = newRegexBox_->text().trimmed();
    if (!p || pattern.isEmpty()) return;

    const std::wstring normalized = Utils::NormalizeRegexBraces(pattern.toStdWString());
    try {
        std::regex re(Utils::WideToUtf8(normalized), std::regex_constants::ECMAScript);
    } catch (const std::regex_error&) {
        QMessageBox::warning(this, tr("Validation Error"),
            tr("Invalid regex syntax."));
        return;
    }

    const std::string pat8 = Utils::WideToUtf8(normalized);
    for (const auto& r : (*p)["regex_patterns"]) {
        if (r.value("pattern", std::string()) == pat8) {
            QMessageBox::warning(this, tr("Validation Error"),
                tr("This entry already exists."));
            return;
        }
    }

    (*p)["regex_patterns"].push_back(
        {{"pattern", pat8}, {"enabled", true}});
    const bool wasDirty = dirty_;
    const std::array<QString, 4> fields = savePendingFormText();
    if (appState_->client().PutProfile(w(selectedProfileId()), *p)) {
        newRegexBox_->clear();
        reloadProfiles(true);
        restorePendingFormText(fields, wasDirty);
    }
}

void MainWindow::onAddKeyword() {
    json* p = selectedProfile();
    const QString text = newKeywordBox_->text().trimmed();
    if (!p || text.isEmpty()) return;

    const std::string text8 = text.toStdString();
    const bool caseSensitive = newKeywordCaseCheck_->isChecked();
    for (const auto& k : (*p)["keywords"]) {
        if (k.value("text", std::string()) == text8
            && k.value("case_sensitive", true) == caseSensitive) {
            QMessageBox::warning(this, tr("Validation Error"),
                tr("This entry already exists."));
            return;
        }
    }

    (*p)["keywords"].push_back({{"text", text8},
        {"case_sensitive", caseSensitive}, {"enabled", true}});
    const bool wasDirty = dirty_;
    const std::array<QString, 4> fields = savePendingFormText();
    if (appState_->client().PutProfile(w(selectedProfileId()), *p)) {
        newKeywordBox_->clear();
        reloadProfiles(true);
        restorePendingFormText(fields, wasDirty);
    }
}

// ---------------------------------------------------------------------------
// Password card + lock overlay
// ---------------------------------------------------------------------------

bool MainWindow::isProtected() const {
    return appState_->lastSettings().value("masterPasswordEnabled", false);
}

bool MainWindow::isUnlocked() const {
    return appState_->lastStatus().value("unlocked", true);
}

void MainWindow::onRequirePasswordToggled(bool checked) {
    if (loading_) return;
    if (checked) {
        PasswordEnableDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted ||
            !appState_->client().EnableMasterPassword(dlg.password().toStdWString())) {
            QSignalBlocker b(requirePasswordCheck_);
            requirePasswordCheck_->setChecked(false);
            return;
        }
        // The session stays unlocked after enabling (Windows parity); the
        // poll picks up masterPasswordEnabled and refreshes the card.
    } else {
        // Disabling strips all protection: unlock first (the engine's disable
        // endpoint requires an unlocked session), then disable.
        PasswordUnlockDialog dlg(this);
        bool unlocked = false;
        for (;;) {
            if (dlg.exec() != QDialog::Accepted) break;
            if (!appState_->client().Unlock(dlg.password().toStdWString())) {
                dlg.setError(tr("Wrong password."));
                continue;
            }
            unlocked = true;
            break;
        }
        if (!unlocked || !appState_->client().DisableMasterPassword()) {
            QSignalBlocker b(requirePasswordCheck_);
            requirePasswordCheck_->setChecked(true);
            return;
        }
    }
}

void MainWindow::ensureLockState(bool allowPrompt) {
    if (appState_->lastSettings().empty()) return; // snapshot not ready; retry timer runs
    lockRetryTimer_->stop();

    if (isProtected() && !isUnlocked()) {
        showLockOverlay();
        if (allowPrompt) unlockBox_->setFocus();
    }
}

void MainWindow::showLockOverlay() {
    if (centralStack_->currentIndex() != 1) {
        unlockBox_->clear();
        unlockError_->setVisible(false);
        centralStack_->setCurrentIndex(1);
    }
}

void MainWindow::hideLockOverlay() {
    centralStack_->setCurrentIndex(0);
    inactivityTimer_->start();
}

void MainWindow::onUnlockClicked() {
    const QString password = unlockBox_->text();
    if (password.isEmpty()) return;
    if (appState_->client().Unlock(password.toStdWString())) {
        hideLockOverlay();
    } else {
        unlockError_->setText(tr("Wrong password."));
        unlockError_->setVisible(true);
        unlockBox_->selectAll();
        unlockBox_->setFocus();
    }
}

// ---------------------------------------------------------------------------
// Statistics / matches / logs cards
// ---------------------------------------------------------------------------

void MainWindow::onClearStatistics() {
    json* p = selectedProfile();
    if (!p) return;
    (*p)["stats"] = {{"total_requests", 0}, {"total_pii_detected", 0},
        {"total_regex_matches", 0}, {"total_keyword_matches", 0},
        {"pii_type_breakdown", json::object()}};
    // The stats reset reloads the whole form; keep pending profile text
    // edits across it (Windows formDirty_ parity).
    const bool wasDirty = dirty_;
    const std::array<QString, 4> fields = savePendingFormText();
    appState_->client().PutProfile(w(selectedProfileId()), *p);
    reloadProfiles(true);
    restorePendingFormText(fields, wasDirty);
}

void MainWindow::onClearMatches() {
    json* p = selectedProfile();
    if (!p) return;
    if (appState_->client().DeleteMatches(w(selectedProfileId()))) {
        matchesList_->clear();
        matchesEmptyLabel_->setVisible(true);
    }
}

void MainWindow::onLoggingToggled(bool checked) {
    if (loading_) return;
    if (!appState_->client().PutSetting(L"loggingEnabled", checked)) {
        QSignalBlocker b(loggingCheck_);
        loggingCheck_->setChecked(!checked);
    }
}

void MainWindow::onShowSensitiveToggled(bool checked) {
    if (loading_) return;
    if (checked) {
        const auto answer = QMessageBox::warning(this, tr("Show sensitive information"),
            tr("Sensitive logging writes raw, unredacted values (including API keys) to the "
               "log. Only enable it while debugging."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            QSignalBlocker b(showSensitiveCheck_);
            showSensitiveCheck_->setChecked(false);
            return;
        }
    }
    if (!appState_->client().PutSetting(L"showSensitive", checked)) {
        // The engine refuses to arm sensitive logging while logging is off.
        QSignalBlocker b(showSensitiveCheck_);
        showSensitiveCheck_->setChecked(false);
        statusBar()->showMessage(tr("Enable logging first."), 3000);
    }
}

void MainWindow::onOpenLog() {
    const auto logPath = appState_->configDir() / "agent_redactor.log";
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(logPath.string())));
}

void MainWindow::onOpenLogFolder() {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromStdString(appState_->configDir().string())));
}

void MainWindow::onClearLogs() {
    const auto answer = QMessageBox::question(this, tr("Delete all logs?"),
        tr("This will permanently delete the current log file and all archived session "
           "logs. This cannot be undone."));
    if (answer != QMessageBox::Yes) return;

    // Same files the Windows GUI deletes directly on disk.
    std::error_code ec;
    std::filesystem::remove(appState_->configDir() / "agent_redactor.log", ec);
    const auto sessions = appState_->configDir() / "sessions";
    if (std::filesystem::exists(sessions)) {
        for (const auto& entry : std::filesystem::directory_iterator(sessions)) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

// ---------------------------------------------------------------------------
// Settings card / tray
// ---------------------------------------------------------------------------

void MainWindow::onStartOnBootToggled(bool checked) {
    if (loading_) return;
    if (appState_->client().PutSetting(L"startOnBoot", checked)) {
        Autostart::SetEnabled(checked);
        tray_->setStartOnBoot(checked);
    } else {
        QSignalBlocker b(startOnBootCheck_);
        startOnBootCheck_->setChecked(!checked);
    }
}

void MainWindow::onLanguageSelected(int index) {
    if (loading_) return;
    // Empty data = "System default"; the engine resolves it from the OS
    // locale. Apply immediately so the UI switches without waiting for the
    // settings-poll round-trip; the poll later re-applies the persisted tag.
    const QString tag = languageCombo_->itemData(index).toString();
    translator_->applyLanguage(tag);
    syncLanguageSelectors(tag);
    appState_->client().PutSetting(L"appLanguage", tag.toStdString());
}

void MainWindow::refreshStats() {
    // Mirrors Windows HomePage::UpdateStats: localize "Requests: ..." with
    // the selected profile's stats. The status poll refreshes (*p)["stats"]
    // just before calling this; a language change re-renders the cached
    // numbers so the format string follows tr() even if no new tick arrives.
    json* p = selectedProfile();
    if (!p) return;
    const json& st = (*p)["stats"];
    statsLabel_->setText(tr("Requests: %1   PII: %2   Regex: %3   Keywords: %4")
        .arg(st.value("total_requests", 0))
        .arg(st.value("total_pii_detected", 0))
        .arg(st.value("total_regex_matches", 0))
        .arg(st.value("total_keyword_matches", 0)));
}

void MainWindow::syncLanguageSelectors(const QString& tag) {
    // Keep the Settings-combo and the tray submenu agreed on the effective
    // tag. The combo entry index 0 carries an empty data tag for "System
    // default" (the engine resolves the OS locale); anything unrecognized
    // maps back to index 0, mirroring onSettingsChanged.
    {
        QSignalBlocker b(languageCombo_);
        const int idx = languageCombo_->findData(tag);
        languageCombo_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    tray_->setCurrentLanguage(tag);
}

// ---------------------------------------------------------------------------
// Model download dialog (blocking, non-dismissible)
// ---------------------------------------------------------------------------

void MainWindow::updateModelDownloadDialog() {
    const json& status = appState_->lastStatus();
    const bool required = status.value("modelDownloadRequired", false);
    const bool inProgress = status.value("modelDownloadInProgress", false);
    const bool failed = status.value("modelDownloadFailed", false);

    if (!required) {
        if (modelDialog_) {
            // Download finished (or was never needed): close the dialog, re-enable
            // the main window, and let a future required download recreate it.
            QDialog* d = modelDialog_;
            modelDialog_ = nullptr;
            modelStatusLabel_ = nullptr;
            modelProgress_ = nullptr;
            modelRetryBtn_ = nullptr;
            if (auto* cw = centralWidget()) cw->setEnabled(true);
            d->hide();
            d->deleteLater();
        }
        return;
    }

    if (!modelDialog_) {
        modelDialog_ = new NonDismissibleDialog(this);
        // Non-modal: the window manager must still allow minimizing/closing the
        // main window. We disable the central widget below to block interaction
        // with the rest of the UI while the model downloads.
        modelDialog_->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        auto* layout = new QVBoxLayout(modelDialog_);
        auto* titleLabel = new QLabel(tr("Downloading AI model"), modelDialog_);
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleLabel->setFont(titleFont);
        titleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(titleLabel);
        modelStatusLabel_ = new QLabel(modelDialog_);
        modelStatusLabel_->setAlignment(Qt::AlignCenter);
        modelProgress_ = new QProgressBar(modelDialog_);
        modelProgress_->setRange(0, 100);
        modelRetryBtn_ = new QPushButton(modelDialog_);
        modelRetryBtn_->setObjectName(QStringLiteral("modelRetryBtn"));
        connect(modelRetryBtn_, &QPushButton::clicked, this, [this] {
            appState_->client().DownloadModel();
        });
        layout->addWidget(modelStatusLabel_);
        layout->addWidget(modelProgress_);
        layout->addWidget(modelRetryBtn_, 0, Qt::AlignCenter);
    }
    modelRetryBtn_->setText(tr("Retry"));

    const int percent = status.value("modelDownloadPercent", 0);
    modelProgress_->setValue(percent);
    modelStatusLabel_->setText(tr("The PII detection model is downloading (%1%).").arg(percent));
    modelRetryBtn_->setEnabled(failed || status.value("modelDownloadWaitingToRetry", false));
    if (failed) {
        modelStatusLabel_->setText(tr("The model download failed. Check your internet "
            "connection, then retry. PII detection is unavailable until the download completes."));
    } else if (!inProgress) {
        // Not started yet — kick it off.
        appState_->client().DownloadModel();
    }

    if (!modelDialog_->isVisible() && isVisible() && !isMinimized()) {
        // Block interaction with the main window (like Windows' ContentDialog)
        // until the weights are present. Using show() instead of exec() lets the
        // user minimize or close-to-tray the main window while downloading.
        // Do not show the dialog while minimized: Qt still reports isVisible()
        // for minimized windows, and showing the child dialog can restore the
        // parent. showEvent re-runs this once the window is restored.
        modelDialog_->setMinimumWidth(360);
        modelDialog_->adjustSize();
        if (auto* parent = qobject_cast<QWidget*>(modelDialog_->parent())) {
            modelDialog_->move(parent->frameGeometry().center() - modelDialog_->rect().center());
        }
        if (auto* cw = centralWidget()) cw->setEnabled(false);
        modelDialog_->show();
        modelDialog_->raise();
        modelDialog_->activateWindow();
    }
}

// ---------------------------------------------------------------------------
// Close / quit / inactivity
// ---------------------------------------------------------------------------

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        if (isUnlocked()) inactivityTimer_->start();
        break;
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Hide the modal download dialog while the main window is closed/minimized
    // to tray; it will be re-shown when the window is reopened.
    if (modelDialog_) {
        modelDialog_->hide();
    }
    if (quitting_ || !tray_->available()) {
        // Real quit (control-panel mode: closing the window exits the GUI;
        // the engine keeps running).
        event->accept();
        return;
    }
    // Close-to-tray; re-lock so the next open must authenticate.
    if (isProtected() && isUnlocked()) appState_->client().Lock();
    hide();
    event->ignore();
}

void MainWindow::openWindow() {
    showNormal();
    raise();
    activateWindow();
    updateModelDownloadDialog();
    ensureLockState(true);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (modelDialog_) {
        updateModelDownloadDialog();
    }
}

void MainWindow::hideEvent(QHideEvent* event) {
    if (modelDialog_) {
        modelDialog_->hide();
    }
    QMainWindow::hideEvent(event);
}

void MainWindow::onQuitRequested() {
    const auto answer = QMessageBox::question(this, tr("Are you sure you want to quit?"),
        tray_->available() || appState_->engineSpawned()
            ? tr("If you quit, Agent Redactor will no longer monitor and redact API traffic.")
            : tr("Quit Agent Redactor? The engine keeps running in the background."));
    if (answer != QMessageBox::Yes) return;

    quitting_ = true;
    // The actual shutdown (engine stop/lock) runs from aboutToQuit, so a bare
    // window close in control-panel mode takes the same path.
    QApplication::quit();
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
        // Persistent/transient strings outside retranslateUi: the tray menu
        // (built once) and the regex/keyword rows (rebuilt on profile load).
        tray_->retranslate();
        if (!dirty_ && !profiles_.empty()) reloadProfiles(true);
    }
    QMainWindow::changeEvent(event);
}
