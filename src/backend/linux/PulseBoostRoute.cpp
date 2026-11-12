#include "PulseBoostRoute.h"

#include "core/AudioTypes.h"
#include "core/GainProcessor.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>

namespace loudmixer {

namespace {
std::atomic<unsigned> g_routeCounter{0};

// Remove caracteres que quebrariam o parser de argumentos de módulo.
std::string sanitizeDescription(const std::string& s)
{
    std::string out;
    for (char c : s)
        if (c != '"' && c != '\'' && c != '\\')
            out.push_back(c);
    return out.empty() ? std::string("output") : out;
}

constexpr pa_usec_t kRecordFragmentUs = 10 * 1000;  // 10 ms por fragmento capturado
constexpr pa_usec_t kPlaybackTargetUs = 40 * 1000;  // ~40 ms de buffer de saída
} // namespace

PulseBoostRoute::PulseBoostRoute(pa_threaded_mainloop* mainloop,
                                 pa_context* context,
                                 std::string targetSink,
                                 std::string targetDescription,
                                 const pa_sample_spec& targetSpec,
                                 const pa_channel_map& map,
                                 std::shared_ptr<GainProcessor> processor)
    : mainloop_(mainloop)
    , context_(context)
    , targetSink_(std::move(targetSink))
    , targetDescription_(std::move(targetDescription))
    , map_(map)
    , processor_(std::move(processor))
{
    // Processamos sempre em float32 na taxa/canais do dispositivo alvo; o
    // servidor converte o que for preciso nas pontas.
    spec_.format = PA_SAMPLE_FLOAT32NE;
    spec_.rate = targetSpec.rate ? targetSpec.rate : 48000;
    spec_.channels = targetSpec.channels ? targetSpec.channels : 2;
    if (!pa_channel_map_valid(&map_) || map_.channels != spec_.channels)
        pa_channel_map_init_extend(&map_, spec_.channels, PA_CHANNEL_MAP_DEFAULT);

    virtualSink_ = std::string(kSinkPrefix) + std::to_string(::getpid()) + "_" + std::to_string(g_routeCounter++);
}

PulseBoostRoute::~PulseBoostRoute()
{
    stop(false);
}

void PulseBoostRoute::onModuleLoaded(pa_context*, std::uint32_t index, void* userdata)
{
    auto* self = static_cast<PulseBoostRoute*>(userdata);
    self->module_ = index;
    self->moduleCallbackDone_ = true;
    pa_threaded_mainloop_signal(self->mainloop_, 0);
}

void PulseBoostRoute::onStreamState(pa_stream*, void* userdata)
{
    auto* self = static_cast<PulseBoostRoute*>(userdata);
    pa_threaded_mainloop_signal(self->mainloop_, 0);
}

bool PulseBoostRoute::waitStreamReady(pa_stream* s, std::string* error)
{
    for (;;) {
        const pa_stream_state_t st = pa_stream_get_state(s);
        if (st == PA_STREAM_READY)
            return true;
        if (!PA_STREAM_IS_GOOD(st)) {
            if (error)
                *error = std::string(LM_TRANSLATE_NOOP("backend", "Could not open audio stream")) + " [" + pa_strerror(pa_context_errno(context_)) + "]";
            return false;
        }
        pa_threaded_mainloop_wait(mainloop_);
    }
}

bool PulseBoostRoute::start(std::string* error)
{
    // 1) Sink virtual onde os apps vão tocar.
    char mapStr[PA_CHANNEL_MAP_SNPRINT_MAX];
    pa_channel_map_snprint(mapStr, sizeof(mapStr), &map_);
    const std::string args = "sink_name=" + virtualSink_
        + " sink_properties='device.description=\"LoudMixer Boost (" + sanitizeDescription(targetDescription_) + ")\""
        + " device.icon_name=\"audio-volume-high\"'"
        + " format=float32le rate=" + std::to_string(spec_.rate)
        + " channels=" + std::to_string(spec_.channels)
        + " channel_map=" + mapStr;

    moduleCallbackDone_ = false;
    pa_operation* op = pa_context_load_module(context_, "module-null-sink", args.c_str(), &PulseBoostRoute::onModuleLoaded, this);
    if (!op) {
        if (error)
            *error = std::string(LM_TRANSLATE_NOOP("backend", "Could not load module-null-sink")) + " [" + pa_strerror(pa_context_errno(context_)) + "]";
        return false;
    }
    while (!moduleCallbackDone_ && pa_operation_get_state(op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(mainloop_);
    pa_operation_unref(op);
    if (module_ == PA_INVALID_INDEX) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "The audio server refused to create the virtual device (module-null-sink).");
        return false;
    }

    // 2) Captura do monitor do sink virtual.
    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_ID, kAppId);
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "LoudMixer");
    pa_proplist_sets(props, PA_PROP_MEDIA_ROLE, "production");

    record_ = pa_stream_new_with_proplist(context_, "LoudMixer Boost (capture)", &spec_, &map_, props);
    playback_ = pa_stream_new_with_proplist(context_, "LoudMixer Boost (output)", &spec_, &map_, props);
    pa_proplist_free(props);
    if (!record_ || !playback_) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "Could not create audio streams.");
        stop();
        return false;
    }
    pa_stream_set_state_callback(record_, &PulseBoostRoute::onStreamState, this);
    pa_stream_set_state_callback(playback_, &PulseBoostRoute::onStreamState, this);

    // 3) Reprodução no sink físico (aberta antes da captura para que o
    //    primeiro bloco capturado já tenha para onde ir).
    pa_buffer_attr playAttr;
    playAttr.maxlength = static_cast<std::uint32_t>(-1);
    playAttr.tlength = static_cast<std::uint32_t>(pa_usec_to_bytes(kPlaybackTargetUs, &spec_));
    playAttr.prebuf = static_cast<std::uint32_t>(-1);
    playAttr.minreq = static_cast<std::uint32_t>(-1);
    playAttr.fragsize = static_cast<std::uint32_t>(-1);
    const auto playFlags = static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE);
    if (pa_stream_connect_playback(playback_, targetSink_.c_str(), &playAttr, playFlags, nullptr, nullptr) < 0
        || !waitStreamReady(playback_, error)) {
        stop();
        return false;
    }

    pa_buffer_attr recAttr;
    recAttr.maxlength = static_cast<std::uint32_t>(-1);
    recAttr.tlength = static_cast<std::uint32_t>(-1);
    recAttr.prebuf = static_cast<std::uint32_t>(-1);
    recAttr.minreq = static_cast<std::uint32_t>(-1);
    recAttr.fragsize = static_cast<std::uint32_t>(pa_usec_to_bytes(kRecordFragmentUs, &spec_));
    const std::string monitor = virtualSink_ + ".monitor";
    pa_stream_set_read_callback(record_, &PulseBoostRoute::onRead, this);
    const auto recFlags = static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE);
    if (pa_stream_connect_record(record_, monitor.c_str(), &recAttr, recFlags) < 0 || !waitStreamReady(record_, error)) {
        stop();
        return false;
    }
    processor_->resetRamp();
    return true;
}

void PulseBoostRoute::onRead(pa_stream* s, std::size_t, void* userdata)
{
    auto* self = static_cast<PulseBoostRoute*>(userdata);
    const std::size_t frameBytes = pa_frame_size(&self->spec_);

    while (pa_stream_readable_size(s) > 0) {
        const void* data = nullptr;
        std::size_t n = 0;
        if (pa_stream_peek(s, &data, &n) < 0 || n == 0)
            return;

        // data == nullptr com n > 0 é um "buraco" no buffer: descartamos.
        if (data && self->playback_ && pa_stream_get_state(self->playback_) == PA_STREAM_READY) {
            // Limitamos ao espaço livre na saída: se o relógio do sink físico
            // for mais lento que o do virtual, descartamos o excesso em vez de
            // acumular latência indefinidamente.
            std::size_t bytes = std::min(n, pa_stream_writable_size(self->playback_));
            bytes -= bytes % frameBytes;
            if (bytes > 0) {
                void* dst = nullptr;
                std::size_t dstBytes = bytes;
                if (pa_stream_begin_write(self->playback_, &dst, &dstBytes) == 0 && dst) {
                    dstBytes = std::min(dstBytes, bytes);
                    dstBytes -= dstBytes % frameBytes;
                    self->processor_->process(static_cast<const float*>(data), static_cast<float*>(dst),
                                              dstBytes / frameBytes, self->spec_.channels);
                    pa_stream_write(self->playback_, dst, dstBytes, nullptr, 0, PA_SEEK_RELATIVE);
                }
            }
        }
        pa_stream_drop(s);
    }
}

void PulseBoostRoute::stop(bool serverAlive)
{
    for (pa_stream** sp : {&record_, &playback_}) {
        if (!*sp)
            continue;
        pa_stream_set_read_callback(*sp, nullptr, nullptr);
        pa_stream_set_state_callback(*sp, nullptr, nullptr);
        if (serverAlive)
            pa_stream_disconnect(*sp);
        pa_stream_unref(*sp);
        *sp = nullptr;
    }
    if (module_ != PA_INVALID_INDEX) {
        if (serverAlive) {
            // O Pulse move automaticamente o que ainda estiver no sink virtual
            // para o sink padrão ao descarregar o módulo.
            if (pa_operation* op = pa_context_unload_module(context_, module_, nullptr, nullptr))
                pa_operation_unref(op);
        }
        module_ = PA_INVALID_INDEX;
    }
}

} // namespace loudmixer
