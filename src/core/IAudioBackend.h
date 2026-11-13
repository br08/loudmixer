#pragma once

// Contrato comum entre o núcleo e as implementações específicas de cada SO.
//
// Regras de threading que toda implementação deve respeitar:
//  - Todos os métodos podem ser chamados a partir da thread da UI.
//  - Os métodos list*() devolvem um snapshot e devem ser thread-safe.
//  - O callback de subscribeToChanges() pode ser chamado de QUALQUER thread
//    (normalmente a thread interna do backend). Quem consome é responsável
//    por levar o evento à thread correta (o AudioSessionManager faz isso).
//  - Depois que stop() retorna, nenhum callback pode mais ser disparado.

#include "AudioTypes.h"
#include "GainProcessor.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace loudmixer {

class IAudioBackend {
public:
    using ChangeCallback = std::function<void(const ChangeEvent&)>;

    virtual ~IAudioBackend() = default;

    // Nome legível ("PulseAudio", "PipeWire", "WASAPI", ...).
    virtual std::string name() const = 0;

    // Conecta ao servidor de áudio. Em caso de falha, devolve false e preenche `error`.
    virtual bool start(std::string* error) = 0;
    virtual void stop() = 0;

    virtual std::vector<AudioDevice> listDevices() const = 0;
    virtual std::vector<AudioSession> listSessions() const = 0;
    virtual std::vector<ChannelInfo> listChannels(const std::string& deviceId) const = 0;

    // Ganho linear (0.0 – kMaxGain). Valores acima de maxGain(target) são
    // limitados pelo backend.
    virtual bool setGain(const Target& target, double gain) = 0;
    virtual bool setMuted(const Target& target, bool muted) = 0;

    // Maior ganho que o backend consegue aplicar neste alvo agora. No Linux é
    // sempre kMaxGain; no Windows depende de haver uma rota de boost ativa.
    virtual double maxGain(const Target& target) const = 0;

    virtual void subscribeToChanges(ChangeCallback callback) = 0;

    // Opções específicas de backend (ex.: "boostSource" no WASAPI). Chaves
    // desconhecidas são ignoradas.
    virtual void setOption(const std::string& key, const std::string& value)
    {
        (void)key;
        (void)value;
    }

    // Insere um GainProcessor (DSP com soft-clip) na cadeia de áudio do alvo.
    // Passar nullptr remove o processador. Na v1 apenas alvos do tipo Device
    // são suportados. Enquanto ativo, setGain() no dispositivo passa a
    // controlar o ganho do processador.
    virtual bool supportsProcessor(TargetKind kind) const { (void)kind; return false; }

    // Estatísticas de processamento de um dispositivo cujo boost é feito pelo
    // próprio sistema (AudioDevice::systemBoost). Zera os picos a cada leitura.
    // Pode ser chamada de qualquer thread.
    virtual std::optional<ProcessorStats> takeDeviceStats(const std::string& deviceId)
    {
        (void)deviceId;
        return std::nullopt;
    }
    virtual bool attachProcessor(const Target& target, std::shared_ptr<GainProcessor> processor, std::string* error)
    {
        (void)target;
        (void)processor;
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "This audio backend does not support DSP processing.");
        return false;
    }
};

} // namespace loudmixer
