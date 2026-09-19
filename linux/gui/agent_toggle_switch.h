#pragma once

// A Windows ToggleSwitch-style on/off control for the Linux GUI. Qt has no
// stock switch widget, and a stylesheet cannot slide a knob inside a
// QCheckBox::indicator, so this is a QCheckBox whose paintEvent we fully
// replace: a pill track whose knob animates between the off (left) and on
// (right) positions when toggled, plus an optional caption (from text(),
// e.g. tr("On")/tr("Off")) to the right. Colors are chosen by window
// lightness so light and dark modes match the app theme with no image assets.
//
// Deriving from QCheckBox (rather than a bare QAbstractButton) is deliberate:
// the existing AT-SPI helpers locate the master switch as role "check box" by
// its accessible name and read/write its CHECKED state, and a QCheckBox base
// preserves all of that unchanged — nothing about the accessibility layer has
// to move. The first accessible action toggles the checked state exactly as
// before.

#include <QCheckBox>

class QVariantAnimation;

class AgentToggleSwitch : public QCheckBox {
    Q_OBJECT
public:
    explicit AgentToggleSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void animTo(bool on);
    qreal knob_ = 0.0;  // 0 = off (left), 1 = on (right)
    QVariantAnimation* anim_ = nullptr;
};
