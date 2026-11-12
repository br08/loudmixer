#pragma once

// Atalhos globais de teclado.
//  - Windows: RegisterHotKey (Ctrl+Alt+↑ / Ctrl+Alt+↓ / Ctrl+Alt+M).
//  - Linux: não há API global portátil (Wayland proíbe por design). Em vez
//    disso, o usuário associa `loudmixer --volume-up` etc. a teclas no
//    próprio ambiente gráfico; ver SingleInstance.

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace loudmixer::ui {

class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    enum class Action { VolumeUp = 1, VolumeDown = 2, ToggleMute = 3 };

    explicit GlobalHotkeys(QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    static bool isSupported();
    bool setEnabled(bool enabled); // false se algum atalho já estiver em uso
    bool isEnabled() const { return enabled_; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void triggered(loudmixer::ui::GlobalHotkeys::Action action);

private:
    bool enabled_ = false;
};

} // namespace loudmixer::ui
