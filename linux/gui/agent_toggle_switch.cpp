#include "agent_toggle_switch.h"

#include <QPainter>
#include <QPaintEvent>
#include <QVariantAnimation>

namespace {

// Capsule geometry (device-independent pixels).
constexpr int kTrackWidth = 44;
constexpr int kTrackHeight = 24;
constexpr int kKnobDiameter = 18;
constexpr int kKnobInset = (kTrackHeight - kKnobDiameter) / 2;
constexpr int kTextSpacing = 8;
constexpr int kAnimationMs = 140;

} // namespace

AgentToggleSwitch::AgentToggleSwitch(QWidget* parent)
    : QCheckBox(parent), knob_(isChecked() ? 1.0 : 0.0) {
    setCursor(Qt::PointingHandCursor);
    anim_ = new QVariantAnimation(this);
    anim_->setDuration(kAnimationMs);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        knob_ = v.toReal();
        update();
    });
    connect(this, &QCheckBox::toggled, this, [this](bool on) { animTo(on); });
}

QSize AgentToggleSwitch::sizeHint() const {
    const int trackH = kTrackHeight;
    const int extra = text().isEmpty()
        ? 0
        : kTextSpacing + fontMetrics().horizontalAdvance(text());
    return QSize(kTrackWidth + extra, qMax(trackH, fontMetrics().height()));
}

QSize AgentToggleSwitch::minimumSizeHint() const {
    return sizeHint();
}

void AgentToggleSwitch::animTo(bool on) {
    const qreal end = on ? 1.0 : 0.0;
    anim_->stop();
    if (!isVisible()) {
        // Not realized yet (widget construction / programmatic loads while the
        // window is hidden): snap so the control renders its true state on the
        // first paint instead of sliding from a stale position.
        knob_ = end;
        update();
        return;
    }
    anim_->setStartValue(knob_);
    anim_->setEndValue(end);
    anim_->start();
}

void AgentToggleSwitch::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Follow the app theme like theme.cpp: pick tones from window lightness.
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor track = isChecked()
        ? (dark ? QColor("#3B9BD8") : QColor("#0078D4"))
        : (dark ? QColor("#5A5A5A") : QColor("#C9C9C9"));
    const QColor border = isChecked()
        ? (dark ? QColor("#3B9BD8") : QColor("#0078D4"))
        : (dark ? QColor("#7A7A7A") : QColor("#A6A6A6"));
    const QColor knobColor = QColor("#FFFFFF");
    const QColor textColor = palette().color(QPalette::WindowText);

    const int y = (height() - kTrackHeight) / 2;
    const QRectF trackRect(0, y, kTrackWidth, kTrackHeight);
    p.setPen(QPen(border, 1));
    p.setBrush(track);
    p.drawRoundedRect(trackRect, trackRect.height() / 2, trackRect.height() / 2);

    // Knob slides between the two ends of the capsule.
    const qreal leftX = kKnobInset;
    const qreal rightX = kTrackWidth - kKnobInset - kKnobDiameter;
    const qreal x = leftX + (rightX - leftX) * knob_;
    const QRectF knob(x, y + kKnobInset, kKnobDiameter, kKnobDiameter);
    p.setPen(Qt::NoPen);
    p.setBrush(knobColor);
    p.drawEllipse(knob);

    if (!text().isEmpty()) {
        p.setPen(QPen(textColor));
        p.drawText(QRect(kTrackWidth + kTextSpacing, y,
                         width() - kTrackWidth - kTextSpacing, kTrackHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}
