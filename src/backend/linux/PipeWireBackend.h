#pragma once

// Backend PipeWire nativo (EXPERIMENTAL).
//
// Usado quando não há pipewire-pulse (raro) ou por pedido explícito
// (`--backend=pipewire`). Enumera nós de áudio pelo registry e controla
// volume/mute pela propriedade Props.channelVolumes de cada nó (volumes
// lineares; ganho > 1.0 é aceito nativamente).
//
// Limitações conhecidas (v1):
//  - Em sinks de hardware, o WirePlumber guarda o volume "de verdade" no
//    Route do dispositivo; aqui ajustamos o volume do nó (software), que se
//    soma ao do Route. Funciona, mas outros mixers podem mostrar valores
//    diferentes.
//  - Sem rota de boost DSP (use o backend `pulse`, que funciona sobre o
//    PipeWire, para ter soft-clip e indicador de clipping).
//  - Sem reconexão automática se o daemon reiniciar.

#include "core/IAudioBackend.h"

#include <map>
#include <memory>
#include <mutex>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_proxy;
struct spa_hook;
struct spa_pod;
struct spa_dict;

namespace loudmixer {

class PipeWireBackend : public IAudioBackend {
public:
    PipeWireBackend();
    ~PipeWireBackend() override;

    std::string name() const override { return "PipeWire (nativo)"; }
    bool start(std::string* error) override;
    void stop() override;

    std::vector<AudioDevice> listDevices() const override;
    std::vector<AudioSession> listSessions() const override;
    std::vector<ChannelInfo> listChannels(const std::string& deviceId) const override;

    bool setGain(const Target& target, double gain) override;
    bool setMuted(const Target& target, bool muted) override;
    double maxGain(const Target& target) const override;
    void subscribeToChanges(ChangeCallback callback) override;

    // Estado interno exposto para os callbacks C (definidos no .cpp).
    struct Node;
    struct Link {
        std::uint32_t outputNode = 0;
        std::uint32_t inputNode = 0;
    };
    struct Impl;

private:
    std::unique_ptr<Impl> d_;
};

} // namespace loudmixer
