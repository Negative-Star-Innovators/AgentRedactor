#include "theme.h"

#include <QApplication>
#include <QEvent>
#include <QPalette>

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

    return QStringLiteral(R"QSS(
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
)QSS").arg(page, card, border, sidebar, hint, statusOk, statusErr, accent, accentHover);
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
    app.installEventFilter(new PaletteChangeFilter(app));
}
