#pragma once

// Rota de boost DSP para um sink (equivalente, no Pulse, ao filter-chain do
// plano original):
//
//   apps -> [sink virtual "LoudMixer Boost"] -> monitor -> GainProcessor
//        -> stream de reprodução -> [sink físico]
//
// O GainProcessor aplica ganho até 200% com soft-clip e mede o clipping que
// acende o LED da UI. Funciona tanto no PulseAudio quanto no PipeWire
// (module-null-sink é suportado pelo pipewire-pulse).
//
// Todos os métodos exigem o lock do pa_threaded_mainloop e NÃO podem ser
// chamados de dentro da thread do mainloop (start() espera respostas do servidor).

#include <pulse/pulseaudio.h>

#include <memory>
#include <string>
#include <vector>

namespace loudmixer {

class GainProcessor;

class PulseBoostRoute {
public:
    static constexpr const char* kSinkPrefix = "loudmixer_boost_";
    static constexpr const char* kAppId = "org.loudmixer.boost";

    PulseBoostRoute(pa_threaded_mainloop* mainloop,
                    pa_context* context,
                    std::string targetSink,
                    std::string targetDescription,
                    const pa_sample_spec& targetSpec,
                    const pa_channel_map& map,
                    std::shared_ptr<GainProcessor> processor);
    ~PulseBoostRoute();

    PulseBoostRoute(const PulseBoostRoute&) = delete;
    PulseBoostRoute& operator=(const PulseBoostRoute&) = delete;

    bool start(std::string* error);

    // Desmonta streams e descarrega o módulo. Com `serverAlive == false`
    // (conexão perdida) apenas libera os objetos locais.
    void stop(bool serverAlive = true);

    const std::string& targetSink() const { return targetSink_; }
    const std::string& virtualSink() const { return virtualSink_; }

private:
    static void onModuleLoaded(pa_context* c, std::uint32_t index, void* userdata);
    static void onStreamState(pa_stream* s, void* userdata);
    static void onRead(pa_stream* s, std::size_t bytes, void* userdata);

    bool waitStreamReady(pa_stream* s, std::string* error);

    pa_threaded_mainloop* mainloop_;
    pa_context* context_;
    std::string targetSink_;
    std::string targetDescription_;
    std::string virtualSink_;
    pa_sample_spec spec_{};
    pa_channel_map map_{};
    std::shared_ptr<GainProcessor> processor_;

    std::uint32_t module_ = PA_INVALID_INDEX;
    bool moduleCallbackDone_ = false;
    pa_stream* record_ = nullptr;
    pa_stream* playback_ = nullptr;
};

} // namespace loudmixer
