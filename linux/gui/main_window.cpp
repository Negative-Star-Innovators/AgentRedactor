#include "main_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedLayout>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <regex>

#include "app_state.h"
#include "autostart.h"
#include "constants.h"
#include "http_server.h"
#include "password_dialog.h"
#include "tray_icon.h"
#include "translator_loader.h"
#include "utils.h"

using namespace AgentRedactor;

namespace {

QString q(const std::wstring& ws) { return QString::fromStdWString(ws); }
std::wstring w(const QString& s) { return s.toStdWString(); }

// Card container helper: titled group box with a vertical layout.
QGroupBox* makeCard(const QString& title, QVBoxLayout*& layoutOut, QWidget* parent) {
    auto* box = new QGroupBox(title, parent);
    layoutOut = new QVBoxLayout(box);
    return box;
}

} // namespace

MainWindow::MainWindow(AppState* appState, TrayIcon* tray, TranslatorLoader* translator,
    bool trayOnly, QWidget* parent)
    : QMainWindow(parent), appState_(appState), tray_(tray), translator_(translator) {
    setWindowTitle(tr("Agent Redactor"));
    setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    resize(1000, 900);

    buildUi();

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuitRequested);

    connect(appState_, &AppState::statusUpdated, this, &MainWindow::onStatusUpdated);
    connect(appState_, &AppState::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(appState_, &AppState::modelDownloadChanged, this, &MainWindow::onModelDownloadChanged);
    connect(appState_, &AppState::connectionLost, this, &MainWindow::onConnectionLost);

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
    auto* sidebarLayout = new QVBoxLayout(sidebar);
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
    auto* scroll = new QScrollArea(splitter);
    scroll->setWidgetResizable(true);
    auto* cards = new QWidget(scroll);
    auto* cardsLayout = new QVBoxLayout(cards);

    auto markDirty = [this] { if (!loading_) dirty_ = true; };

    // -- Profile card --
    QVBoxLayout* profileLayout;
    auto* profileCard = makeCard(QString(), profileLayout, cards); // title set in retranslateUi
    profileCard->setObjectName(QStringLiteral("profileCard"));
    auto* profileForm = new QFormLayout;
    aliasBox_ = new QLineEdit(profileCard);
    portBox_ = new QLineEdit(profileCard);
    urlBox_ = new QLineEdit(profileCard);
    apiKeyBox_ = new QLineEdit(profileCard);
    apiKeyBox_->setEchoMode(QLineEdit::Password);
    profileForm->addRow(QStringLiteral("Name:"), aliasBox_);
    profileForm->addRow(QStringLiteral("Port:"), portBox_);
    profileForm->addRow(QStringLiteral("Upstream URL:"), urlBox_);
    profileForm->addRow(QStringLiteral("API key:"), apiKeyBox_);
    profileLayout->addLayout(profileForm);
    connect(aliasBox_, &QLineEdit::textEdited, this, markDirty);
    connect(portBox_, &QLineEdit::textEdited, this, markDirty);
    connect(urlBox_, &QLineEdit::textEdited, this, markDirty);
    connect(apiKeyBox_, &QLineEdit::textEdited, this, markDirty);

    auto* profileBtns = new QHBoxLayout;
    showKeyCheck_ = new QCheckBox(profileCard);
    connect(showKeyCheck_, &QCheckBox::toggled, this, &MainWindow::onToggleApiKeyVisible);
    copyUrlBtn_ = new QPushButton(profileCard);
    connect(copyUrlBtn_, &QPushButton::clicked, this, &MainWindow::onCopyUrl);
    saveBtn_ = new QPushButton(profileCard);
    connect(saveBtn_, &QPushButton::clicked, this, &MainWindow::onSaveProfile);
    profileBtns->addWidget(showKeyCheck_);
    profileBtns->addStretch();
    profileBtns->addWidget(copyUrlBtn_);
    profileBtns->addWidget(saveBtn_);
    profileLayout->addLayout(profileBtns);
    cardsLayout->addWidget(profileCard);

    // -- Detection card --
    QVBoxLayout* detectionLayout;
    auto* detectionCard = makeCard(QString(), detectionLayout, cards);
    detectionCard->setObjectName(QStringLiteral("detectionCard"));
    auto* detectionForm = new QFormLayout;
    useAiCheck_ = new QCheckBox(detectionCard);
    confidenceBox_ = new QLineEdit(detectionCard);
    detectionForm->addRow(QStringLiteral("Use AI model:"), useAiCheck_);
    detectionForm->addRow(QStringLiteral("Confidence threshold:"), confidenceBox_);
    detectionLayout->addLayout(detectionForm);
    connect(useAiCheck_, &QCheckBox::toggled, this, markDirty);
    connect(confidenceBox_, &QLineEdit::textEdited, this, markDirty);
    auto* piiGrid = new QGridLayout;
    int row = 0, col = 0;
    for (const auto& type : DEFAULT_PII_TYPES) {
        auto* check = new QCheckBox(piiTypeLabel(type), detectionCard);
        connect(check, &QCheckBox::toggled, this, markDirty);
        piiChecks_.emplace_back(type, check);
        piiGrid->addWidget(check, row, col);
        if (++col == 4) { col = 0; ++row; }
    }
    detectionLayout->addLayout(piiGrid);
    cardsLayout->addWidget(detectionCard);

    // -- Regex card --
    QVBoxLayout* regexLayout;
    auto* regexCard = makeCard(QString(), regexLayout, cards);
    regexCard->setObjectName(QStringLiteral("regexCard"));
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
    cardsLayout->addWidget(logsCard);

    // -- Settings card --
    QVBoxLayout* settingsLayout;
    auto* settingsCard = makeCard(QString(), settingsLayout, cards);
    settingsCard->setObjectName(QStringLiteral("settingsCard"));
    startOnBootCheck_ = new QCheckBox(settingsCard);
    connect(startOnBootCheck_, &QCheckBox::toggled, this, &MainWindow::onStartOnBootToggled);
    settingsLayout->addWidget(startOnBootCheck_);
    cardsLayout->addWidget(settingsCard);

    cardsLayout->addStretch();
    scroll->setWidget(cards);
    splitter->addWidget(cards);
    splitter->setStretchFactor(1, 1);

    contentLayout->addWidget(splitter);
    centralStack_->addWidget(content);

    // ---- Page 1: lock overlay (opaque; content page is hidden while shown) ----
    lockOverlay_ = new QWidget(central);
    lockOverlay_->setStyleSheet(QStringLiteral("background-color: #202020; color: white;"));
    auto* overlayOuter = new QVBoxLayout(lockOverlay_);
    overlayOuter->addStretch();
    auto* overlayBox = new QVBoxLayout;
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
    findChild<QGroupBox*>(QStringLiteral("profileCard"))->setTitle(tr("Profile"));
    findChild<QGroupBox*>(QStringLiteral("detectionCard"))->setTitle(tr("Detection"));
    findChild<QGroupBox*>(QStringLiteral("regexCard"))->setTitle(tr("Regex patterns"));
    findChild<QGroupBox*>(QStringLiteral("keywordsCard"))->setTitle(tr("Keywords"));
    findChild<QGroupBox*>(QStringLiteral("passwordCard"))->setTitle(tr("Password"));
    findChild<QGroupBox*>(QStringLiteral("statsCard"))->setTitle(tr("Statistics"));
    findChild<QGroupBox*>(QStringLiteral("matchesCard"))->setTitle(tr("Session redactions"));
    findChild<QGroupBox*>(QStringLiteral("logsCard"))->setTitle(tr("Logs"));
    findChild<QGroupBox*>(QStringLiteral("settingsCard"))->setTitle(tr("Settings"));

    addProfileBtn_->setText(tr("Add"));
    removeProfileBtn_->setText(tr("Remove"));
    showKeyCheck_->setText(tr("Show API key"));
    copyUrlBtn_->setText(tr("Copy proxy URL"));
    saveBtn_->setText(tr("Save"));
    useAiCheck_->setText(tr("Use AI model for PII detection"));
    newKeywordCaseCheck_->setText(tr("Case sensitive"));
    findChild<QPushButton*>(QStringLiteral("addRegexBtn"))->setText(tr("Add"));
    findChild<QPushButton*>(QStringLiteral("addKeywordBtn"))->setText(tr("Add"));
    requirePasswordCheck_->setText(tr("Require master password"));
    findChild<QPushButton*>(QStringLiteral("clearStatsBtn"))->setText(tr("Clear statistics"));
    findChild<QPushButton*>(QStringLiteral("clearMatchesBtn"))->setText(tr("Clear"));
    loggingCheck_->setText(tr("Enable logging"));
    showSensitiveCheck_->setText(tr("Show sensitive information in logs"));
    findChild<QPushButton*>(QStringLiteral("openLogBtn"))->setText(tr("Open log"));
    findChild<QPushButton*>(QStringLiteral("openFolderBtn"))->setText(tr("Open folder"));
    findChild<QPushButton*>(QStringLiteral("clearLogsBtn"))->setText(tr("Clear logs"));
    startOnBootCheck_->setText(tr("Start on boot"));
    unlockBox_->setPlaceholderText(tr("Master password"));
    findChild<QPushButton*>(QStringLiteral("unlockBtn"))->setText(tr("Unlock"));
    if (auto* t = findChild<QLabel*>(QStringLiteral("lockTitle")))
        t->setText(tr("Agent Redactor is locked"));
}

QString MainWindow::piiTypeLabel(const std::wstring& type) {
    // English display labels for the PII grid (Windows: PII_Type_<type> resw
    // keys). Underscores become spaces, first letter capitalized.
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
        const json& stats = (*p)["stats"];
        statsLabel_->setText(tr("Requests: %1   PII: %2   Regex: %3   Keywords: %4")
            .arg(stats.value("total_requests", 0))
            .arg(stats.value("total_pii_detected", 0))
            .arg(stats.value("total_regex_matches", 0))
            .arg(stats.value("total_keyword_matches", 0)));

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
    }
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

void MainWindow::reloadProfiles(bool keepSelection) {
    json profiles;
    if (!appState_->client().GetProfiles(profiles) || !profiles.is_array()) return;

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

    // Rebuild regex rows (each row is a widget so takeAt/delete cleans up).
    while (QLayoutItem* item = regexRows_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto& r : p.value("regex_patterns", json::array())) {
        auto* rowWidget = new QWidget;
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* enabled = new QCheckBox;
        enabled->setChecked(r.value("enabled", true));
        auto* pattern = new QLineEdit(QString::fromStdString(r.value("pattern", std::string())));
        auto* del = new QPushButton(tr("Delete"));
        rowLayout->addWidget(enabled);
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
                QMessageBox::warning(this, tr("Invalid regex"),
                    tr("The pattern is not a valid regular expression."));
                reloadProfiles(true);
                return;
            }
            dirty_ = true;
            onSaveProfile();
        });
        connect(del, &QPushButton::clicked, this, [this, pattern] {
            json* p = selectedProfile();
            if (!p) return;
            const std::string pat = pattern->text().toStdString();
            auto& arr = (*p)["regex_patterns"];
            arr.erase(std::remove_if(arr.begin(), arr.end(), [&](const json& r) {
                return r.value("pattern", std::string()) == pat;
            }), arr.end());
            if (appState_->client().PutProfile(w(selectedProfileId()), *p)) {
                appState_->client().RestartListeners();
                reloadProfiles(true);
            }
        });
    }

    // Rebuild keyword rows.
    while (QLayoutItem* item = keywordRows_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto& k : p.value("keywords", json::array())) {
        auto* rowWidget = new QWidget;
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* enabled = new QCheckBox;
        enabled->setChecked(k.value("enabled", true));
        auto* caseBtn = new QPushButton(k.value("case_sensitive", true)
            ? tr("Case: Yes") : tr("Case: No"));
        caseBtn->setFixedWidth(90);
        auto* text = new QLineEdit(QString::fromStdString(k.value("text", std::string())));
        auto* del = new QPushButton(tr("Delete"));
        rowLayout->addWidget(enabled);
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
        connect(caseBtn, &QPushButton::clicked, this, [this, caseBtn, mutateKeyword] {
            const bool newValue = caseBtn->text() == tr("Case: No");
            mutateKeyword([newValue](json& kw) { kw["case_sensitive"] = newValue; });
            caseBtn->setText(newValue ? tr("Case: Yes") : tr("Case: No"));
        });
        connect(text, &QLineEdit::editingFinished, this, [this] {
            dirty_ = true;
            onSaveProfile();
        });
        connect(del, &QPushButton::clicked, this, [this, text] {
            json* p = selectedProfile();
            if (!p) return;
            const std::string t = text->text().toStdString();
            auto& arr = (*p)["keywords"];
            arr.erase(std::remove_if(arr.begin(), arr.end(), [&](const json& kw) {
                return kw.value("text", std::string()) == t;
            }), arr.end());
            if (appState_->client().PutProfile(w(selectedProfileId()), *p)) {
                appState_->client().RestartListeners();
                reloadProfiles(true);
            }
        });
    }

    loading_ = false;
    dirty_ = false;
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
    p["upstream_url"] = urlBox_->text().toStdString();
    p["api_key"] = apiKeyBox_->text().toStdString();
    p["use_openai_model"] = useAiCheck_->isChecked();
    p["pii_confidence_threshold"] = confidenceBox_->text().toDouble();

    std::vector<std::string> types;
    for (const auto& [type, check] : piiChecks_) {
        if (check->isChecked()) types.push_back(Utils::WideToUtf8(type));
    }
    p["enabled_pii_types"] = types;
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
            error = tr("Another profile already uses this port.");
            return false;
        }
    }

    const QString url = urlBox_->text().trimmed();
    const QUrl parsed(url);
    if (url.isEmpty() ||
        !(url.startsWith(QLatin1String("http://")) || url.startsWith(QLatin1String("https://"))) ||
        parsed.host().isEmpty()) {
        error = tr("Upstream URL must start with http:// or https:// and have a host.");
        return false;
    }
    if (url.startsWith(QLatin1String("http://")) &&
        parsed.host() != QLatin1String("localhost") &&
        !parsed.host().startsWith(QLatin1String("127."))) {
        httpWarning = true;
    }

    const double confidence = confidenceBox_->text().toDouble(&ok);
    if (!ok || confidence < 0.0 || confidence > 1.0) {
        error = tr("Confidence threshold must be between 0 and 1.");
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
        QMessageBox::warning(this, tr("Invalid profile"), error);
        reloadProfiles(true);
        return;
    }
    if (httpWarning) {
        const auto answer = QMessageBox::warning(this, tr("Plain HTTP upstream"),
            tr("The upstream URL uses plain HTTP to a non-local host. Redacted requests "
               "will be readable on the network. Save anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            reloadProfiles(true);
            return;
        }
    }

    const json updated = gatherProfileFromForm();
    if (!appState_->client().PutProfile(w(selectedProfileId()), updated)) {
        QMessageBox::warning(this, tr("Save failed"),
            tr("The engine rejected the profile. Check the engine log for details."));
        return;
    }
    appState_->client().RestartListeners();
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
        QMessageBox::warning(this, tr("Add profile failed"),
            tr("The engine rejected the new profile."));
        return;
    }
    appState_->client().RestartListeners();
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
    const QString alias = profileList_->currentItem()
        ? profileList_->currentItem()->text() : QString();
    const auto answer = QMessageBox::question(this, tr("Remove profile"),
        tr("Remove profile \"%1\"?").arg(alias));
    if (answer != QMessageBox::Yes) return;

    if (appState_->client().DeleteProfile(w(selectedProfileId()))) {
        appState_->client().RestartListeners();
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
        QMessageBox::warning(this, tr("Invalid regex"),
            tr("The pattern is not a valid regular expression."));
        return;
    }

    (*p)["regex_patterns"].push_back(
        {{"pattern", Utils::WideToUtf8(normalized)}, {"enabled", true}});
    if (appState_->client().PutProfile(w(selectedProfileId()), *p)) {
        appState_->client().RestartListeners();
        newRegexBox_->clear();
        reloadProfiles(true);
    }
}

void MainWindow::onAddKeyword() {
    json* p = selectedProfile();
    const QString text = newKeywordBox_->text().trimmed();
    if (!p || text.isEmpty()) return;

    (*p)["keywords"].push_back({{"text", text.toStdString()},
        {"case_sensitive", newKeywordCaseCheck_->isChecked()}, {"enabled", true}});
    if (appState_->client().PutProfile(w(selectedProfileId()), *p)) {
        appState_->client().RestartListeners();
        newKeywordBox_->clear();
        reloadProfiles(true);
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
        for (;;) {
            if (dlg.exec() != QDialog::Accepted) break;
            if (!appState_->client().Unlock(dlg.password().toStdWString())) {
                dlg.setError(tr("Wrong password."));
                continue;
            }
            break;
        }
        if (!appState_->client().DisableMasterPassword()) {
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
    appState_->client().PutProfile(w(selectedProfileId()), *p);
    reloadProfiles(true);
}

void MainWindow::onClearMatches() {
    json* p = selectedProfile();
    if (!p) return;
    if (appState_->client().DeleteMatches(w(selectedProfileId()))) {
        matchesList_->clear();
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
    const auto answer = QMessageBox::question(this, tr("Clear logs"),
        tr("Delete the log files? This cannot be undone."));
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
        Autostart::SetEnabled(checked, QCoreApplication::applicationFilePath().toStdString());
        tray_->setStartOnBoot(checked);
    } else {
        QSignalBlocker b(startOnBootCheck_);
        startOnBootCheck_->setChecked(!checked);
    }
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
        if (modelDialog_) modelDialog_->hide();
        return;
    }

    if (!modelDialog_) {
        modelDialog_ = new QDialog(this);
        modelDialog_->setModal(true);
        // No close button: the app cannot serve traffic until the weights exist.
        modelDialog_->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        auto* layout = new QVBoxLayout(modelDialog_);
        modelStatusLabel_ = new QLabel(modelDialog_);
        modelProgress_ = new QProgressBar(modelDialog_);
        modelProgress_->setRange(0, 100);
        modelRetryBtn_ = new QPushButton(modelDialog_);
        modelRetryBtn_->setObjectName(QStringLiteral("modelRetryBtn"));
        connect(modelRetryBtn_, &QPushButton::clicked, this, [this] {
            appState_->client().DownloadModel();
        });
        layout->addWidget(modelStatusLabel_);
        layout->addWidget(modelProgress_);
        layout->addWidget(modelRetryBtn_, 0, Qt::AlignLeft);
    }
    modelDialog_->setWindowTitle(tr("Downloading language model"));
    modelRetryBtn_->setText(tr("Retry"));

    const int percent = status.value("modelDownloadPercent", 0);
    modelProgress_->setValue(percent);
    modelStatusLabel_->setText(tr("The PII detection model is downloading (%1%).").arg(percent));
    modelRetryBtn_->setEnabled(failed);
    if (failed) {
        modelStatusLabel_->setText(tr("The model download failed. Check your connection and retry."));
    } else if (!inProgress) {
        // Not started yet — kick it off.
        appState_->client().DownloadModel();
    }
    if (!modelDialog_->isVisible()) modelDialog_->show();
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
    ensureLockState(true);
}

void MainWindow::onQuitRequested() {
    const auto answer = QMessageBox::question(this, tr("Quit Agent Redactor"),
        tray_->available() || appState_->engineSpawned()
            ? tr("Quit Agent Redactor? The proxy will stop.")
            : tr("Quit Agent Redactor? The engine keeps running in the background."));
    if (answer != QMessageBox::Yes) return;

    quitting_ = true;
    // The actual shutdown (engine stop/lock) runs from aboutToQuit, so a bare
    // window close in control-panel mode takes the same path.
    QApplication::quit();
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) retranslateUi();
    QMainWindow::changeEvent(event);
}
