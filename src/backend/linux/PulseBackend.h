#pragma once

// Backend sobre a API do PulseAudio (libpulse).
//
// Funciona tanto com o daemon PulseAudio quanto com o PipeWire através do
// pipewire-pulse (presente em praticamente toda distro com PipeWire). Por
// isso é o backend padrão no Linux; o PipeWireBackend nativo é opcional.
//
// Mapeamento de escopos:
//   App        -> sink-input   (pa_context_set_sink_input_volume)
//   Dispositivo-> sink         (pa_cvolume_scale: preserva o balanço)
//   Canal      -> canal do sink (valor individual do pa_cvolume)
// Ganho acima de 100% é nativo no Pulse/PipeWire. A conversão é feita em
// amplitude linear (pa_sw_volume_from_linear), igual ao GainProcessor:
// 200% aqui = +6 dB (o pavucontrol, que usa escala cúbica, mostraria ~126%).
//
// Opcionalmente, um dispositivo pode receber uma rota de boost DSP
// (PulseBoostRoute) com soft-clip e medição de clipping.

#include "core/IAudioBackend.h"

#include <pulse/pulseaudio.h>

#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace loudmixer {

class PulseBoostRoute;

class PulseBackend : public IAudioBackend {
public:
    PulseBackend();
    ~PulseBackend() override;

    std::string name() const override;
    bool start(std::string* error) override;
    void stop() override;

    std::vector<AudioDevice> listDevices() const override;
    std::vector<AudioSession> listSessions() const override;
    std::vector<ChannelInfo> listChannels(const std::string& deviceId) const override;

    bool setGain(const Target& target, double gain) override;
    bool setMuted(const Target& target, bool muted) override;
    double maxGain(const Target& target) const override;
    void subscribeToChanges(ChangeCallback callback) override;

    bool supportsProcessor(TargetKind kind) const override { return kind == TargetKind::Device; }
    bool attachProcessor(const Target& target, std::shared_ptr<GainProcessor> processor, std::string* error) override;

private:
    struct SinkData {
        std::uint32_t index = PA_INVALID_INDEX;
        std::string name;
        std::string description;
        pa_cvolume volume{};
        pa_channel_map map{};
        pa_sample_spec spec{};
        bool muted = false;
    };
    struct InputData {
        std::uint32_t index = PA_INVALID_INDEX;
        std::uint32_t sink = PA_INVALID_INDEX;
        std::uint32_t client = PA_INVALID_INDEX;
        std::string appName;
        std::string iconName;
        std::string appId;
        std::uint32_t pid = 0;
        pa_cvolume volume{};
        bool muted = false;
    };

    // Callbacks do mainloop (thread interna do Pulse).
    static void onContextState(pa_context* c, void* userdata);
    static void onSubscribe(pa_context* c, pa_subscription_event_type_t t, std::uint32_t idx, void* userdata);
    static void onSinkInfo(pa_context* c, const pa_sink_info* info, int eol, void* userdata);
    static void onSinkInfoInitial(pa_context* c, const pa_sink_info* info, int eol, void* userdata);
    static void onInputInfo(pa_context* c, const pa_sink_input_info* info, int eol, void* userdata);
    static void onInputInfoInitial(pa_context* c, const pa_sink_input_info* info, int eol, void* userdata);
    static void onServerInfo(pa_context* c, const pa_server_info* info, void* userdata);
    static void onServerInfoInitial(pa_context* c, const pa_server_info* info, void* userdata);
    static void onReconnectTimer(pa_mainloop_api* api, pa_time_event* e, const struct timeval* tv, void* userdata);

    void handleSink(const pa_sink_info* info);
    void handleInput(const pa_sink_input_info* info);
    void handleServer(const pa_server_info* info);
    void initialStepDone();

    // Exigem o lock do mainloop.
    bool createContext(std::string* error);
    void destroyContext();
    void handleDisconnect();
    bool ready() const;
    void moveInputs(const std::string& fromSink, const std::string& toSink);

    void notify(ChangeKind kind, const std::string& id);

    pa_threaded_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_time_event* reconnectTimer_ = nullptr;
    bool stopping_ = false;
    int pendingInitial_ = 0;
    bool initialSyncDone_ = false;
    bool everConnected_ = false; // distingue a 1ª conexão de uma reconexão

    // Rotas de boost por nome do sink alvo (acesso com o lock do mainloop).
    std::map<std::string, std::unique_ptr<PulseBoostRoute>> routes_;
    std::string defaultBeforeRoute_;

    // Snapshot (protegido por dataMutex_, lido pela thread da UI).
    mutable std::mutex dataMutex_;
    std::map<std::uint32_t, SinkData> sinks_;
    std::map<std::uint32_t, InputData> inputs_;
    std::string defaultSink_;
    std::string serverName_;
    std::uint32_t ownClient_ = PA_INVALID_INDEX;
    std::map<std::string, std::shared_ptr<GainProcessor>> routedProcessors_; // alvo -> DSP
    std::map<std::string, std::string> virtualToTarget_;                     // sink virtual -> alvo

    std::mutex callbackMutex_;
    ChangeCallback callback_;
};

} // namespace loudmixer
