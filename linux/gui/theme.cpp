#include "theme.h"

#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QTimer>

namespace {

bool isDark(const QPalette& palette) {
    return palette.color(QPalette::Window).lightness() < 128;
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

void apply(QApplication& app) {
    app.setStyleSheet(stylesheet(isDark(app.palette())));
}

class PaletteChangeFilter : public QObject {
public:
    explicit PaletteChangeFilter(QApplication& app) : QObject(&app), app_(app) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ApplicationPaletteChange) apply(app_);
        return QObject::eventFilter(watched, event);
    }

private:
    QApplication& app_;
};

} // namespace

void Theme::Apply(QApplication& app) {
    apply(app);
    // On X11/Wayland the system light/dark preference is resolved asynchronously
    // (GTK3 platform theme / color-scheme portal), so the palette read right
    // after QApplication construction can still be the default (light) even
    // though the system is dark — and Qt does NOT emit ApplicationPaletteChange
    // for that initial resolution, so only the constructed snapshot would stick.
    // Re-apply once the event loop has started to pick up a late-settled theme.
    // Applying the same stylesheet is idempotent, so this is a no-op when the
    // palette already matched.
    QTimer::singleShot(0, [&app] { apply(app); });
    app.installEventFilter(new PaletteChangeFilter(app));
}
