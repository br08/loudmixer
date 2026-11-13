#pragma once

// Tipos de dados compartilhados entre o núcleo, os backends e a UI.
// Sem dependência de Qt: os backends (inclusive o do Windows) só usam a STL.

#include <cstdint>
#include <string>
#include <vector>

// Marca mensagens dos backends (C++ puro, sem Qt) para tradução. O lupdate
// as encontra via `-tr-function-alias QT_TRANSLATE_NOOP+=LM_TRANSLATE_NOOP`
// e o AudioSessionManager as traduz ao exibir (ver translateBackendError).
// Convenção: detalhes técnicos vão no fim, entre colchetes: "Mensagem [detalhe]".
#define LM_TRANSLATE_NOOP(context, text) text

namespace loudmixer {

// Ganho linear de amplitude. 1.0 = 100% (0 dB), 2.0 = 200% (+6 dB).
constexpr double kMinGain = 0.0;
constexpr double kUnityGain = 1.0;
constexpr double kMaxGain = 2.0;

inline double clampGain(double g) noexcept
{
    if (!(g >= kMinGain)) // também captura NaN
        return kMinGain;
    return g > kMaxGain ? kMaxGain : g;
}

enum class TargetKind { Application, Device, Channel };

// Alvo de uma operação de ganho/mute.
//  - Application: id = id da sessão de áudio
//  - Device:      id = id estável do dispositivo de saída
//  - Channel:     id = id do dispositivo, channel = índice do canal
struct Target {
    TargetKind kind = TargetKind::Device;
    std::string id;
    int channel = -1;

    static Target app(std::string sessionId) { return {TargetKind::Application, std::move(sessionId), -1}; }
    static Target device(std::string deviceId) { return {TargetKind::Device, std::move(deviceId), -1}; }
    static Target channelOf(std::string deviceId, int index) { return {TargetKind::Channel, std::move(deviceId), index}; }

    bool operator==(const Target&) const = default;
};

struct ChannelInfo {
    int index = 0;
    std::string name;   // "Front Left", "Front Right", ...
    double gain = kUnityGain; // ganho absoluto do canal

    bool operator==(const ChannelInfo&) const = default;
};

// Dispositivo de saída físico (ou lógico). O ganho mestre segue a convenção
// do PulseAudio/WASAPI: mestre = maior ganho entre os canais; alterar o mestre
// escala todos os canais preservando o balanço.
struct AudioDevice {
    std::string id;
    std::string name;         // nome técnico (ex.: alsa_output.pci-...)
    std::string description;  // nome amigável
    bool isDefault = false;
    bool muted = false;
    double gain = kUnityGain;
    std::vector<ChannelInfo> channels;
    // O caminho de áudio do sistema já aplica boost acima de 100% com
    // soft-clip (APO do LoudMixer no Windows): não precisa de rota DSP própria.
    bool systemBoost = false;

    bool operator==(const AudioDevice&) const = default;
};

// Sessão de áudio de um aplicativo (sink-input no Pulse, nó Stream no
// PipeWire, IAudioSessionControl no Windows).
struct AudioSession {
    std::string id;
    std::string appName;
    std::string iconName;   // nome de ícone freedesktop ou caminho do executável
    std::string deviceId;   // dispositivo onde a sessão está tocando (pode ser vazio)
    std::uint32_t processId = 0;
    bool muted = false;
    double gain = kUnityGain;

    bool operator==(const AudioSession&) const = default;
};

enum class ChangeKind {
    DeviceAdded,
    DeviceRemoved,
    DeviceChanged,
    SessionAdded,
    SessionRemoved,
    SessionChanged,
    DefaultDeviceChanged,
    BackendReset, // conexão perdida/restabelecida: recarregar tudo
};

struct ChangeEvent {
    ChangeKind kind = ChangeKind::BackendReset;
    std::string id;
};

} // namespace loudmixer
