#pragma once

// Linha de controle de ganho reutilizada nas três abas:
//
//   [ícone] Nome ............ [====|====] 150%  ● ⟲ 🔇
//
// - Faixa 0–100% em azul, 100–200% em âmbar/vermelho (zona de boost).
// - LED que acende quando o DSP está saturando (soft-clip ativo).
// - Botão de reset (volta a 100%) e duplo clique no slider faz o mesmo.

#include <QLabel>
#include <QSlider>
#include <QWidget>

class QToolButton;
class QTimer;

namespace loudmixer::ui {

class BoostSlider : public QSlider {
    Q_OBJECT
public:
    explicit BoostSlider(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void resetRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRectF grooveRect() const;
    int valueAt(qreal x) const;
};

class ClipLed : public QWidget {
    Q_OBJECT
public:
    enum class State { Off, Active, Clipping };
    explicit ClipLed(QWidget* parent = nullptr);
    void setState(State s);
    State state() const { return state_; }
    QSize sizeHint() const override { return {14, 14}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    State state_ = State::Off;
};

// QLabel que corta o texto com "…" em vez de esconder o final.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent = nullptr);
    void setFullText(const QString& text);
    QString fullText() const { return full_; }
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateElided();
    QString full_;
};

class GainSlider : public QWidget {
    Q_OBJECT
public:
    explicit GainSlider(QWidget* parent = nullptr);

    void setLabel(const QString& text, const QString& tooltip = {});
    void setLabelBold(bool bold);
    void setIcon(const QIcon& icon);
    void setGain(double gain);        // não emite gainChanged
    double gain() const;
    void setMaxGain(double maxGain);  // 1.0 ou 2.0
    void setMuted(bool muted);        // não emite muteToggled
    bool isMuted() const { return muted_; }
    void setMuteVisible(bool visible);
    void setLedVisible(bool visible);
    void setLedState(ClipLed::State state);
    bool isDragging() const;
    void setExtraWidget(QWidget* widget); // ex.: checkbox "Boost DSP"

signals:
    void gainChanged(double gain);
    void muteToggled(bool muted);

private:
    void updateValueLabel();
    void updateMuteButton();

    QLabel* icon_;
    ElidedLabel* label_;
    BoostSlider* slider_;
    QLabel* value_;
    ClipLed* led_;
    QToolButton* reset_;
    QToolButton* mute_;
    bool muted_ = false;
};

} // namespace loudmixer::ui
