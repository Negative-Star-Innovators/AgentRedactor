#include "theme.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QTimer>
#include <QWidget>

namespace {

bool isDark() {
    // The platform theme's explicit color scheme is the source of truth where
    // the Qt build provides it (6.8+; CI and the shipped AppImage use distro
    // Qt 6.4): on early-login autostart the settings portal can resolve
    // seconds after the palette was constructed, so the palette alone can
    // report light while the system is dark. Fall back to the palette only
    // while the scheme is unknown.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown) return scheme == Qt::ColorScheme::Dark;
#endif
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString stylesheet(bool dark) {
    const QString page = dark ? QStringLiteral("#1A1A1A") : QStringLiteral("#F5F5F5");
    const QString card = dark ? QStringLiteral("#1E1E1E") : QStringLiteral("#FFFFFF");
    const QString border = dark ? QStringLiteral("#444444") : QStringLiteral("#E0E0E0");
    const QString sidebar = dark ? QStringLiteral("#2B2B2B") : QStringLiteral("#F3F3F3");
    const QString hint = dark ? QStringLiteral("#B0B0B0") : QStringLiteral("#606060");
    const QString accent = dark ? QStringLiteral("#0067C0") : QStringLiteral("#005FB8");
    const QString accentHover = dark ? QStringLiteral("#1A76C2") : QStringLiteral("#0067C0");
    const QString statusOk = dark ? QStringLiteral("#64FF64") : QStringLiteral("#009600");
    const QString statusErr = QStringLiteral("#DC3545");

    QString qss = QStringLiteral(R"QSS(
QMainWindow, QDialog {
    background: %1;
}
QWidget#sidebar {
    background: %4;
}
QScrollArea {
    border: none;
    background: transparent;
}
QSplitter::handle {
    background: transparent;
}
QSplitter::handle:horizontal {
    width: 1px;
}
QGroupBox {
    background-color: %2;
    border: 1px solid %3;
    border-radius: 8px;
    padding: 20px;
    padding-top: 34px;
}
QGroupBox::title {
    subcontrol-origin: padding;
    subcontrol-position: top left;
    top: 8px;
    left: 12px;
    padding: 0 4px;
}
QLabel#profilesHeader {
    font-size: 16px;
    font-weight: 600;
    background: transparent;
}
QLabel[hint="true"] {
    color: %5;
    font-size: 12px;
    background: transparent;
}
QLabel[statusOk="true"] {
    color: %6;
    background: transparent;
}
QLabel[statusErr="true"] {
    color: %7;
    background: transparent;
}
QPushButton[accent="true"] {
    background-color: %8;
    color: white;
    border: none;
    border-radius: 4px;
    padding: 6px 18px;
}
QPushButton[accent="true"]:hover {
    background-color: %9;
}
QPushButton[danger="true"] {
    color: %7;
    background: transparent;
    border: none;
}
QLabel[colHeader="true"] {
    color: %5;
    font-weight: 600;
    font-size: 12px;
    background: transparent;
}
)QSS").arg(page, card, border, sidebar, hint, statusOk, statusErr, accent, accentHover);

    // Checkbox styling. Dark mode is left entirely to Qt's native checkbox
    // (unchanged). Light mode only: the default box/border on the app's white
    // cards can be too faint to see, so give the indicator a clearly visible
    // box (strong border + white interior) while keeping Qt's own checkmark
    // glyph — no solid fill, so it still reads as a checkbox.
    if (!dark) {
        qss += QStringLiteral(R"QSS(
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 2px solid )") + QStringLiteral("#4A4A4A") + QStringLiteral(R"QSS(;
    border-radius: 3px;
    background: white;
}
QCheckBox::indicator:hover {
    border: 2px solid )") + accentHover + QStringLiteral(R"QSS(;
}
)QSS");
    }

    return qss;
}

// Full application palette matching the stylesheet above. The platform theme
// (gtk3) resolves the system preference asynchronously and can leave the
// palette light while the scheme is already dark — styling from the scheme
// while widgets render from the palette produced a hybrid window. Forcing
// both together makes the window consistent regardless of what state the
// platform theme happened to settle into.
QPalette buildPalette(bool dark) {
    QPalette p;
    if (dark) {
        const QColor window("#1A1A1A"), base("#2B2B2B"), alt("#1E1E1E"),
            text("#E8E8E8"), disabled("#606060"), accent("#0067C0");
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, alt);
        p.setColor(QPalette::ToolTipBase, base);
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, base);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::Link, QColor("#3B9BD8"));
        p.setColor(QPalette::Highlight, accent);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, QColor("#808080"));
        p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        p.setColor(QPalette::Disabled, QPalette::Text, disabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    } else {
        const QColor window("#F5F5F5"), base("#FFFFFF"), text("#1A1A1A"),
            disabled("#A0A0A0"), accent("#005FB8");
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, window);
        p.setColor(QPalette::ToolTipBase, base);
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, window);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::red);
        p.setColor(QPalette::Link, accent);
        p.setColor(QPalette::Highlight, accent);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, QColor("#808080"));
        p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        p.setColor(QPalette::Disabled, QPalette::Text, disabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    }
    return p;
}

// The darkness the current stylesheet/palette were built for, so repeat
// re-checks are no-ops unless the resolved theme actually flipped.
bool g_appliedDark = false;
bool g_appliedOnce = false;

void apply(QApplication& app) {
    const bool dark = isDark();
    if (!g_appliedOnce || dark != g_appliedDark) {
        g_appliedDark = dark;
        g_appliedOnce = true;
        app.setPalette(buildPalette(dark));
        app.setStyleSheet(stylesheet(dark));
        return;
    }
    // Same mode as already applied, but an external palette update (the
    // platform theme settling seconds after login, a live system theme
    // switch) may have clobbered our palette while the stylesheet stayed —
    // that mismatch is exactly the hybrid window. Re-force the palette
    // whenever it drifted. Setting an identical stylesheet is skipped since
    // it would needlessly re-polish every widget.
    const QPalette expected = buildPalette(dark);
    if (app.palette() != expected) app.setPalette(expected);
}

class PaletteChangeFilter : public QObject {
public:
    explicit PaletteChangeFilter(QApplication& app) : QObject(&app), app_(app) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ApplicationPaletteChange) apply(app_);
        // A top-level window opening is a natural re-check point: if the
        // system theme settled while the app sat in the tray without any
        // palette event, the window still appears with the right theme.
        if (event->type() == QEvent::Show && watched->isWidgetType() &&
            static_cast<QWidget*>(watched)->isWindow()) {
            apply(app_);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QApplication& app_;
};

} // namespace

void Theme::Apply(QApplication& app) {
    apply(app);
    // On X11/Wayland the system light/dark preference is resolved asynchronously
    // (color-scheme portal), so the value read right after QApplication
    // construction can still be Unknown/default even though the system is
    // dark — and early-login autostart stretches this further, the portal may
    // not answer for the first seconds of the session. Re-check on a short
    // schedule and, on Qt 6.8+, whenever the scheme changes; apply() only
    // re-styles when the resolved darkness actually changed.
    for (const int delayMs : {0, 500, 2000, 5000})
        QTimer::singleShot(delayMs, &app, [&app] { apply(app); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged,
        &app, [&app] { apply(app); });
#endif
    app.installEventFilter(new PaletteChangeFilter(app));
}
