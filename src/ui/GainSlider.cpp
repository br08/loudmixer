#include "GainSlider.h"

#include "AppIcons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace loudmixer::ui {

namespace {
constexpr qreal kHandleRadius = 8.0;
constexpr qreal kGrooveHeight = 6.0;

QColor zoneColor(int percent)
{
    if (percent <= 100)
        return palette::normal;
    return percent <= 150 ? palette::boost : palette::danger;
}
} // namespace

// ---------------------------------------------------------------------------
// BoostSlider
// ---------------------------------------------------------------------------

BoostSlider::BoostSlider(QWidget* parent)
    : QSlider(Qt::Horizontal, parent)
{
    setRange(0, 200);
    setSingleStep(1);
    setPageStep(10);
    setValue(100);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
}

QSize BoostSlider::sizeHint() const { return {240, 24}; }
QSize BoostSlider::minimumSizeHint() const { return {120, 24}; }

QRectF BoostSlider::grooveRect() const
{
    return QRectF(kHandleRadius, (height() - kGrooveHeight) / 2.0, width() - 2 * kHandleRadius, kGrooveHeight);
}

int BoostSlider::valueAt(qreal x) const
{
    const QRectF g = grooveRect();
    const qreal t = std::clamp((x - g.left()) / g.width(), 0.0, 1.0);
    return minimum() + static_cast<int>(std::lround(t * (maximum() - minimum())));
}

void BoostSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QSlider::mousePressEvent(event);
        return;
    }
    // Clique leva direto à posição (mais natural do que "page step").
    setSliderDown(true);
    setValue(valueAt(event->position().x()));
    event->accept();
}

void BoostSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (isSliderDown()) {
        setValue(valueAt(event->position().x()));
        event->accept();
        return;
    }
    QSlider::mouseMoveEvent(event);
}

void BoostSlider::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isSliderDown()) {
        setSliderDown(false);
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

void BoostSlider::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit resetRequested();
        event->accept();
    }
}

void BoostSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF g = grooveRect();
    const int span = std::max(1, maximum() - minimum());
    auto xFor = [&](int v) { return g.left() + g.width() * (v - minimum()) / span; };
    const bool enabled = isEnabled();
    const QColor track = palette().color(QPalette::Mid);

    // Trilho completo
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(g, kGrooveHeight / 2, kGrooveHeight / 2);

    // Zona de boost (100–200%) sempre levemente visível
    if (maximum() > 100) {
        QColor zone = palette::boost;
        zone.setAlpha(70);
        p.setBrush(zone);
        p.drawRoundedRect(QRectF(QPointF(xFor(100), g.top()), g.bottomRight()), kGrooveHeight / 2, kGrooveHeight / 2);
    }

    // Parte preenchida até o valor atual
    const qreal xv = xFor(value());
    if (value() > minimum()) {
        if (!enabled) {
            p.setBrush(palette().color(QPalette::Disabled, QPalette::WindowText));
            p.drawRoundedRect(QRectF(g.topLeft(), QPointF(xv, g.bottom())), kGrooveHeight / 2, kGrooveHeight / 2);
        } else {
            const qreal x100 = std::min(xv, xFor(100));
            p.setBrush(palette::normal);
            p.drawRoundedRect(QRectF(g.topLeft(), QPointF(x100, g.bottom())), kGrooveHeight / 2, kGrooveHeight / 2);
            if (value() > 100) {
                QLinearGradient grad(QPointF(xFor(100), 0), QPointF(xFor(200), 0));
                grad.setColorAt(0.0, palette::boost);
                grad.setColorAt(1.0, palette::danger);
                p.setBrush(grad);
                p.drawRect(QRectF(QPointF(xFor(100) - 1, g.top()), QPointF(xv, g.bottom())));
            }
        }
    }

    // Marca de 100%
    if (maximum() > 100) {
        p.setPen(QPen(palette().color(QPalette::WindowText), 1.2));
        p.drawLine(QPointF(xFor(100), g.top() - 4), QPointF(xFor(100), g.bottom() + 4));
    }

    // Puxador
    const QPointF c(xv, height() / 2.0);
    QColor ring = enabled ? zoneColor(value()) : track;
    p.setPen(QPen(ring, 2.5));
    p.setBrush(palette().color(QPalette::Base));
    const qreal r = (underMouse() || isSliderDown()) ? kHandleRadius : kHandleRadius - 1;
    p.drawEllipse(c, r, r);
    if (hasFocus()) {
        QColor focus = palette().color(QPalette::Highlight);
        focus.setAlpha(90);
        p.setPen(QPen(focus, 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r + 3, r + 3);
    }
}

// ---------------------------------------------------------------------------
// ClipLed
// ---------------------------------------------------------------------------

ClipLed::ClipLed(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(14, 14);
    setToolTip(tr("Clipping indicator: lights up red while the soft-clipper is limiting peaks"));
}

void ClipLed::setState(State s)
{
    if (s == state_)
        return;
    state_ = s;
    update();
}

void ClipLed::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c;
    switch (state_) {
    case State::Off: c = palette().color(QPalette::Mid); break;
    case State::Active: c = QColor(0x3f, 0xb9, 0x50); break;
    case State::Clipping: c = palette::danger; break;
    }
    if (state_ == State::Clipping) { // brilho
        QColor glow = c;
        glow.setAlpha(90);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(rect());
    }
    p.setPen(QPen(c.darker(140), 1));
    p.setBrush(c);
    p.drawEllipse(QRectF(rect()).adjusted(3, 3, -3, -3));
}

// ---------------------------------------------------------------------------
// ElidedLabel
// ---------------------------------------------------------------------------

ElidedLabel::ElidedLabel(QWidget* parent)
    : QLabel(parent)
{
    setTextFormat(Qt::PlainText);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

void ElidedLabel::setFullText(const QString& text)
{
    full_ = text;
    updateElided();
}

QSize ElidedLabel::minimumSizeHint() const
{
    return {fontMetrics().horizontalAdvance(QStringLiteral("Mmmmm…")), QLabel::minimumSizeHint().height()};
}

void ElidedLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    updateElided();
}

void ElidedLabel::updateElided()
{
    setText(fontMetrics().elidedText(full_, Qt::ElideRight, width()));
}

// ---------------------------------------------------------------------------
// GainSlider
// ---------------------------------------------------------------------------

GainSlider::GainSlider(QWidget* parent)
    : QWidget(parent)
    , icon_(new QLabel(this))
    , label_(new ElidedLabel(this))
    , slider_(new BoostSlider(this))
    , value_(new QLabel(this))
    , led_(new ClipLed(this))
    , reset_(new QToolButton(this))
    , mute_(new QToolButton(this))
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(8);

    icon_->setFixedSize(24, 24);
    label_->setMinimumWidth(110);
    value_->setMinimumWidth(value_->fontMetrics().horizontalAdvance(QStringLiteral("200%")) + 6);
    value_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    reset_->setText(QStringLiteral("⟲"));
    reset_->setToolTip(tr("Reset to 100%"));
    reset_->setAutoRaise(true);
    mute_->setCheckable(true);
    mute_->setAutoRaise(true);

    row->addWidget(icon_);
    row->addWidget(label_, 2);
    row->addWidget(slider_, 3);
    row->addWidget(value_);
    row->addWidget(led_);
    row->addWidget(reset_);
    row->addWidget(mute_);
    // O LED reserva espaço mesmo oculto, para os sliders ficarem alinhados.
    QSizePolicy ledPolicy = led_->sizePolicy();
    ledPolicy.setRetainSizeWhenHidden(true);
    led_->setSizePolicy(ledPolicy);
    led_->setVisible(false);

    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        updateValueLabel();
        emit gainChanged(v / 100.0);
    });
    auto reset = [this] { slider_->setValue(100); };
    connect(reset_, &QToolButton::clicked, this, reset);
    connect(slider_, &BoostSlider::resetRequested, this, reset);
    connect(mute_, &QToolButton::toggled, this, [this](bool m) {
        muted_ = m;
        updateMuteButton();
        emit muteToggled(m);
    });

    updateValueLabel();
    updateMuteButton();
}

void GainSlider::setLabel(const QString& text, const QString& tooltip)
{
    label_->setFullText(text);
    label_->setToolTip(tooltip.isEmpty() ? text : tooltip);
    slider_->setAccessibleName(text);
}

void GainSlider::setLabelBold(bool bold)
{
    QFont f = label_->font();
    if (f.bold() == bold)
        return;
    f.setBold(bold);
    label_->setFont(f);
    label_->setFullText(label_->fullText()); // recalcula as reticências
}

void GainSlider::setIcon(const QIcon& icon)
{
    icon_->setPixmap(icon.pixmap(24, 24));
    icon_->setVisible(!icon.isNull());
}

void GainSlider::setGain(double gain)
{
    if (slider_->isSliderDown())
        return; // não briga com o usuário enquanto ele arrasta
    const QSignalBlocker block(slider_);
    slider_->setValue(static_cast<int>(std::lround(gain * 100.0)));
    updateValueLabel();
    slider_->update();
}

double GainSlider::gain() const { return slider_->value() / 100.0; }

void GainSlider::setMaxGain(double maxGain)
{
    const int max = static_cast<int>(std::lround(maxGain * 100.0));
    if (slider_->maximum() == max)
        return;
    const QSignalBlocker block(slider_);
    slider_->setMaximum(max);
    slider_->setToolTip(max > 100 ? QString() : tr("Boost above 100% is not available for this item right now"));
    updateValueLabel();
}

void GainSlider::setMuted(bool muted)
{
    const QSignalBlocker block(mute_);
    muted_ = muted;
    mute_->setChecked(muted);
    updateMuteButton();
}

void GainSlider::setMuteVisible(bool visible) { mute_->setVisible(visible); }
void GainSlider::setLedVisible(bool visible) { led_->setVisible(visible); }
void GainSlider::setLedState(ClipLed::State state) { led_->setState(state); }
bool GainSlider::isDragging() const { return slider_->isSliderDown(); }

void GainSlider::setExtraWidget(QWidget* widget)
{
    auto* row = static_cast<QHBoxLayout*>(layout());
    row->insertWidget(row->indexOf(led_), widget);
}

void GainSlider::updateValueLabel()
{
    const int v = slider_->value();
    value_->setText(QStringLiteral("%1%").arg(v));
    QPalette pal = value_->palette();
    pal.setColor(QPalette::WindowText, v > 100 ? zoneColor(v) : palette().color(QPalette::WindowText));
    value_->setPalette(pal);
}

void GainSlider::updateMuteButton()
{
    mute_->setIcon(volumeIcon(muted_));
    mute_->setToolTip(muted_ ? tr("Unmute") : tr("Mute"));
    slider_->setEnabled(!muted_);
}

} // namespace loudmixer::ui
