#pragma once

// Ícone permanente na bandeja do sistema.
//  - Clique esquerdo: abre/esconde o mixer.
//  - Clique direito: presets, silenciar tudo, resetar, auto-start,
//    configurações e sair.
// O ícone muda conforme o estado do dispositivo padrão (normal, boost, mudo).

#include <QObject>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace loudmixer {
class AudioSessionManager;
class AutoStartService;
class PresetStore;
} // namespace loudmixer

namespace loudmixer::ui {

class MixerWindow;

class TrayController : public QObject {
    Q_OBJECT
public:
    TrayController(AudioSessionManager* manager, MixerWindow* window, PresetStore* presets, AutoStartService* autostart,
                   QObject* parent = nullptr);
    ~TrayController() override;

    static bool isAvailable();
    void show();
    void showMessage(const QString& title, const QString& text);

private:
    void updateState();
    void rebuildPresetsMenu();

    AudioSessionManager* manager_;
    MixerWindow* window_;
    PresetStore* presets_;
    AutoStartService* autostart_;
    QSystemTrayIcon* tray_;
    std::unique_ptr<QMenu> menu_;
    QMenu* presetsMenu_ = nullptr;
    QAction* muteAll_ = nullptr;
    QAction* autostartAction_ = nullptr;
};

} // namespace loudmixer::ui
