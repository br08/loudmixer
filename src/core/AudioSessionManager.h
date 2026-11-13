#pragma once

// Fachada Qt sobre um IAudioBackend.
//
//  - Leva os eventos do backend (que chegam em threads arbitrárias) para a
//    thread da UI e os agrupa em sinais devicesChanged()/sessionsChanged().
//  - Mantém um snapshot dos dispositivos/sessões para a UI consultar sem
//    bloquear.
//  - Gerencia os GainProcessors das rotas de boost DSP (soft-clip + LED).
//  - Implementa as ações globais: silenciar tudo, resetar, presets e passos
//    de volume para atalhos.

#include "AudioTypes.h"
#include "GainProcessor.h"
#include "IAudioBackend.h"
#include "PresetStore.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <thread>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace loudmixer {

// Traduz uma mensagem de erro vinda de um backend (ver LM_TRANSLATE_NOOP),
// preservando o detalhe técnico final entre colchetes.
QString translateBackendError(const std::string& message);

class AudioSessionManager : public QObject {
    Q_OBJECT
public:
    explicit AudioSessionManager(std::unique_ptr<IAudioBackend> backend, QObject* parent = nullptr);
    ~AudioSessionManager() override;

    bool start(QString* error = nullptr);
    void stop();
    bool isRunning() const { return running_; }

    QString backendName() const;
    IAudioBackend* backend() const { return backend_.get(); }

    const std::vector<AudioDevice>& devices() const { return devices_; }
    const std::vector<AudioSession>& sessions() const { return sessions_; }
    std::optional<AudioDevice> device(const std::string& id) const;
    std::optional<AudioSession> session(const std::string& id) const;
    std::string defaultDeviceId() const;

    double maxGain(const Target& target) const;
    bool setGain(const Target& target, double gain);
    bool setMuted(const Target& target, bool muted);

    // Ações globais.
    void resetAllGains();           // tudo volta a 100%
    void setAllMuted(bool muted);   // silencia todos os dispositivos (e restaura depois)
    bool allMuted() const { return allMuted_; }
    void nudgeDefaultDevice(double delta); // usado pelos atalhos (+/- 5%)
    void toggleMuteDefaultDevice();

    // Rota de boost DSP por dispositivo (soft-clip real + indicador de clipping).
    bool supportsBoostRoute() const;
    bool setBoostRoute(const std::string& deviceId, bool enabled, QString* error = nullptr);
    bool boostRouteActive(const std::string& deviceId) const;
    std::vector<std::string> activeBoostRoutes() const;
    std::optional<ProcessorStats> takeStats(const std::string& deviceId);

    // Boost do sistema (Windows: APO), por dispositivo. Assíncrono: pode pedir
    // permissão de administrador e reiniciar o áudio; o resultado chega por
    // systemBoostFinished. Um pedido por vez (devolve false se já houver um).
    bool requestSystemBoost(const std::string& deviceId, bool enabled);
    // Dispositivo com pedido em andamento (vazio se nenhum).
    std::string systemBoostPending() const { return systemBoostPending_; }

    void setClipCurve(ClipCurve curve);
    ClipCurve clipCurve() const { return curve_; }

    // Presets.
    Preset capturePreset(const QString& name) const;
    void applyPreset(const Preset& preset);

    // Força a releitura do backend (normalmente não é necessário).
    void refresh();

signals:
    void devicesChanged();
    void sessionsChanged();
    void boostRouteChanged(const QString& deviceId, bool active);
    // code: SystemBoostResult::Code
    void systemBoostFinished(const QString& deviceId, bool enabled, int code, const QString& detail);
    void systemBoostPendingChanged();
    // O boost do sistema deixou de estar ativo sem pedido do usuário
    // (aprimoramentos desligados, atualização de driver...).
    void systemBoostLost(const QString& deviceId);
    void errorOccurred(const QString& message);

private:
    void onBackendEvent(const ChangeEvent& event); // thread da UI
    void flushPending();

    std::unique_ptr<IAudioBackend> backend_;
    bool running_ = false;

    std::vector<AudioDevice> devices_;
    std::vector<AudioSession> sessions_;

    std::map<std::string, std::shared_ptr<GainProcessor>> processors_;
    ClipCurve curve_ = ClipCurve::Tanh;

    bool allMuted_ = false;
    std::set<std::string> mutedBeforeMuteAll_;

    QTimer coalesceTimer_;
    bool devicesDirty_ = false;
    bool sessionsDirty_ = false;

    std::string systemBoostPending_;
    std::thread systemBoostThread_;
};

} // namespace loudmixer
