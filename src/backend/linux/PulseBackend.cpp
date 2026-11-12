#include "PulseBackend.h"

#include "PulseBoostRoute.h"
#include "core/GainProcessor.h"

#include <sys/time.h>

#include <algorithm>
#include <cstring>

namespace loudmixer {

namespace {
constexpr pa_usec_t kReconnectDelayUs = 2 * 1000 * 1000;

double toLinear(pa_volume_t v) { return clampGain(pa_sw_volume_to_linear(v)); }
pa_volume_t fromLinear(double g) { return pa_sw_volume_from_linear(clampGain(g)); }

std::string propOr(const pa_proplist* p, const char* key, const std::string& fallback = {})
{
    const char* v = p ? pa_proplist_gets(p, key) : nullptr;
    return (v && *v) ? std::string(v) : fallback;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// Trava o mainloop no escopo atual.
class MainloopLock {
public:
    explicit MainloopLock(pa_threaded_mainloop* m) : m_(m) { if (m_) pa_threaded_mainloop_lock(m_); }
    ~MainloopLock() { if (m_) pa_threaded_mainloop_unlock(m_); }
    MainloopLock(const MainloopLock&) = delete;
    MainloopLock& operator=(const MainloopLock&) = delete;
private:
    pa_threaded_mainloop* m_;
};
} // namespace

PulseBackend::PulseBackend() = default;

PulseBackend::~PulseBackend()
{
    stop();
}

std::string PulseBackend::name() const
{
    std::lock_guard lock(dataMutex_);
    return serverName_.empty() ? std::string("PulseAudio") : serverName_;
}

// --------------------------------------------------------------------------
// Ciclo de vida
// --------------------------------------------------------------------------

bool PulseBackend::start(std::string* error)
{
    if (mainloop_)
        return true;
    mainloop_ = pa_threaded_mainloop_new();
    if (!mainloop_) {
        if (error)
            *error = "pa_threaded_mainloop_new failed";
        return false;
    }
    pa_threaded_mainloop_set_name(mainloop_, "loudmixer-pulse");

    pa_threaded_mainloop_lock(mainloop_);
    stopping_ = false;
    if (pa_threaded_mainloop_start(mainloop_) < 0 || !createContext(error)) {
        pa_threaded_mainloop_unlock(mainloop_);
        stop();
        return false;
    }

    // Espera conectar e concluir a primeira leitura completa do estado.
    for (;;) {
        const pa_context_state_t st = pa_context_get_state(context_);
        if (st == PA_CONTEXT_READY && initialSyncDone_)
            break;
        if (!PA_CONTEXT_IS_GOOD(st)) {
            if (error)
                *error = std::string(LM_TRANSLATE_NOOP("backend", "Could not connect to the audio server")) + " ["
                    + pa_strerror(pa_context_errno(context_)) + "]";
            pa_threaded_mainloop_unlock(mainloop_);
            stop();
            return false;
        }
        pa_threaded_mainloop_wait(mainloop_);
    }

    // Remove sinks virtuais órfãos de uma execução anterior que tenha caído
    // sem desmontar a rota (senão o áudio do usuário fica preso neles).
    struct Collect {
        pa_threaded_mainloop* ml;
        std::vector<std::uint32_t> modules;
    } collect{mainloop_, {}};
    if (pa_operation* op = pa_context_get_module_info_list(
            context_,
            [](pa_context*, const pa_module_info* info, int eol, void* ud) {
                auto* c = static_cast<Collect*>(ud);
                if (eol == 0 && info && info->name && info->argument && std::strcmp(info->name, "module-null-sink") == 0
                    && std::strstr(info->argument, PulseBoostRoute::kSinkPrefix))
                    c->modules.push_back(info->index);
                if (eol != 0)
                    pa_threaded_mainloop_signal(c->ml, 0);
            },
            &collect)) {
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
            pa_threaded_mainloop_wait(mainloop_);
        pa_operation_unref(op);
    }
    for (std::uint32_t m : collect.modules)
        if (pa_operation* op = pa_context_unload_module(context_, m, nullptr, nullptr))
            pa_operation_unref(op);
    pa_threaded_mainloop_unlock(mainloop_);
    return true;
}

void PulseBackend::stop()
{
    if (!mainloop_)
        return;
    {
        std::lock_guard lock(callbackMutex_);
        callback_ = nullptr;
    }
    pa_threaded_mainloop_lock(mainloop_);
    stopping_ = true;
    if (context_ && pa_context_get_state(context_) == PA_CONTEXT_READY) {
        // Devolve o áudio aos dispositivos reais antes de sair.
        for (auto& [target, route] : routes_) {
            moveInputs(route->virtualSink(), target);
            route->stop(true);
        }
        if (!defaultBeforeRoute_.empty())
            if (pa_operation* op = pa_context_set_default_sink(context_, defaultBeforeRoute_.c_str(), nullptr, nullptr))
                pa_operation_unref(op);
        // Garante que os comandos acima cheguem ao servidor antes de desconectar.
        if (pa_operation* op = pa_context_drain(context_, [](pa_context*, void* m) {
                pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop*>(m), 0);
            }, mainloop_)) {
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
                pa_threaded_mainloop_wait(mainloop_);
            pa_operation_unref(op);
        }
    }
    routes_.clear();
    defaultBeforeRoute_.clear();
    destroyContext();
    pa_threaded_mainloop_unlock(mainloop_);

    pa_threaded_mainloop_stop(mainloop_);
    pa_threaded_mainloop_free(mainloop_);
    mainloop_ = nullptr;

    std::lock_guard lock(dataMutex_);
    sinks_.clear();
    inputs_.clear();
    routedProcessors_.clear();
    virtualToTarget_.clear();
}

bool PulseBackend::createContext(std::string* error)
{
    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "LoudMixer");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "org.loudmixer.LoudMixer");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ICON_NAME, "loudmixer");
    context_ = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(mainloop_), "LoudMixer", props);
    pa_proplist_free(props);
    if (!context_) {
        if (error)
            *error = "pa_context_new failed";
        return false;
    }
    initialSyncDone_ = false;
    pa_context_set_state_callback(context_, &PulseBackend::onContextState, this);
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        if (error)
            *error = std::string(LM_TRANSLATE_NOOP("backend", "Could not connect to the audio server")) + " [" + pa_strerror(pa_context_errno(context_)) + "]";
        return false;
    }
    return true;
}

void PulseBackend::destroyContext()
{
    if (reconnectTimer_) {
        pa_threaded_mainloop_get_api(mainloop_)->time_free(reconnectTimer_);
        reconnectTimer_ = nullptr;
    }
    if (!context_)
        return;
    pa_context_set_state_callback(context_, nullptr, nullptr);
    pa_context_set_subscribe_callback(context_, nullptr, nullptr);
    pa_context_disconnect(context_);
    pa_context_unref(context_);
    context_ = nullptr;
}

bool PulseBackend::ready() const
{
    return context_ && pa_context_get_state(context_) == PA_CONTEXT_READY;
}

void PulseBackend::onContextState(pa_context* c, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY: {
        {
            std::lock_guard lock(self->dataMutex_);
            self->ownClient_ = pa_context_get_index(c);
        }
        pa_context_set_subscribe_callback(c, &PulseBackend::onSubscribe, self);
        const auto mask = static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SINK_INPUT
                                                              | PA_SUBSCRIPTION_MASK_SERVER);
        if (pa_operation* op = pa_context_subscribe(c, mask, nullptr, nullptr))
            pa_operation_unref(op);

        self->pendingInitial_ = 3;
        if (pa_operation* op = pa_context_get_server_info(c, &PulseBackend::onServerInfoInitial, self))
            pa_operation_unref(op);
        else
            self->initialStepDone();
        if (pa_operation* op = pa_context_get_sink_info_list(c, &PulseBackend::onSinkInfoInitial, self))
            pa_operation_unref(op);
        else
            self->initialStepDone();
        if (pa_operation* op = pa_context_get_sink_input_info_list(c, &PulseBackend::onInputInfoInitial, self))
            pa_operation_unref(op);
        else
            self->initialStepDone();
        break;
    }
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        if (!self->stopping_ && self->everConnected_)
            self->handleDisconnect();
        break;
    default:
        break;
    }
    pa_threaded_mainloop_signal(self->mainloop_, 0);
}

void PulseBackend::initialStepDone()
{
    if (--pendingInitial_ > 0)
        return;
    const bool wasReconnect = everConnected_;
    initialSyncDone_ = true;
    everConnected_ = true;
    pa_threaded_mainloop_signal(mainloop_, 0);
    if (wasReconnect)
        notify(ChangeKind::BackendReset, {});
}

// Conexão perdida (servidor reiniciou, usuário trocou PulseAudio por
// PipeWire...): limpa tudo e tenta de novo a cada 2 s.
void PulseBackend::handleDisconnect()
{
    for (auto& [target, route] : routes_)
        route->stop(false);
    routes_.clear();
    defaultBeforeRoute_.clear();
    {
        std::lock_guard lock(dataMutex_);
        sinks_.clear();
        inputs_.clear();
        routedProcessors_.clear();
        virtualToTarget_.clear();
    }
    notify(ChangeKind::BackendReset, {});

    pa_mainloop_api* api = pa_threaded_mainloop_get_api(mainloop_);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    pa_timeval_add(&tv, kReconnectDelayUs);
    if (reconnectTimer_)
        api->time_free(reconnectTimer_);
    reconnectTimer_ = api->time_new(api, &tv, &PulseBackend::onReconnectTimer, this);
}

void PulseBackend::onReconnectTimer(pa_mainloop_api* api, pa_time_event* e, const struct timeval*, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    api->time_free(e);
    self->reconnectTimer_ = nullptr;
    if (self->stopping_)
        return;
    if (self->context_) {
        pa_context_set_state_callback(self->context_, nullptr, nullptr);
        pa_context_set_subscribe_callback(self->context_, nullptr, nullptr);
        pa_context_disconnect(self->context_);
        pa_context_unref(self->context_);
        self->context_ = nullptr;
    }
    // Se a conexão falhar de forma assíncrona, onContextState(FAILED) chama
    // handleDisconnect(), que agenda uma nova tentativa.
    std::string err;
    if (!self->createContext(&err))
        self->handleDisconnect();
}

// --------------------------------------------------------------------------
// Eventos e leitura de estado (thread do mainloop)
// --------------------------------------------------------------------------

void PulseBackend::onSubscribe(pa_context* c, pa_subscription_event_type_t t, std::uint32_t idx, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    const auto facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    const auto type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
        if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
            std::string removed;
            {
                std::lock_guard lock(self->dataMutex_);
                auto it = self->sinks_.find(idx);
                if (it != self->sinks_.end()) {
                    removed = it->second.name;
                    self->sinks_.erase(it);
                }
            }
            if (!removed.empty()) {
                // Dispositivo físico com rota ativa sumiu: desmonta a rota.
                auto r = self->routes_.find(removed);
                if (r != self->routes_.end()) {
                    r->second->stop(true);
                    std::lock_guard lock(self->dataMutex_);
                    self->routedProcessors_.erase(removed);
                    self->virtualToTarget_.erase(r->second->virtualSink());
                    self->routes_.erase(r);
                }
                self->notify(ChangeKind::DeviceRemoved, removed);
            }
        } else if (pa_operation* op = pa_context_get_sink_info_by_index(c, idx, &PulseBackend::onSinkInfo, self)) {
            pa_operation_unref(op);
        }
    } else if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
        if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
            bool existed = false;
            {
                std::lock_guard lock(self->dataMutex_);
                existed = self->inputs_.erase(idx) > 0;
            }
            if (existed)
                self->notify(ChangeKind::SessionRemoved, std::to_string(idx));
        } else if (pa_operation* op = pa_context_get_sink_input_info(c, idx, &PulseBackend::onInputInfo, self)) {
            pa_operation_unref(op);
        }
    } else if (facility == PA_SUBSCRIPTION_EVENT_SERVER) {
        if (pa_operation* op = pa_context_get_server_info(c, &PulseBackend::onServerInfo, self))
            pa_operation_unref(op);
    }
}

void PulseBackend::onSinkInfo(pa_context*, const pa_sink_info* info, int eol, void* userdata)
{
    if (eol == 0 && info)
        static_cast<PulseBackend*>(userdata)->handleSink(info);
}

void PulseBackend::onSinkInfoInitial(pa_context*, const pa_sink_info* info, int eol, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    if (eol == 0 && info)
        self->handleSink(info);
    else
        self->initialStepDone();
}

void PulseBackend::onInputInfo(pa_context*, const pa_sink_input_info* info, int eol, void* userdata)
{
    if (eol == 0 && info)
        static_cast<PulseBackend*>(userdata)->handleInput(info);
}

void PulseBackend::onInputInfoInitial(pa_context*, const pa_sink_input_info* info, int eol, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    if (eol == 0 && info)
        self->handleInput(info);
    else
        self->initialStepDone();
}

void PulseBackend::onServerInfo(pa_context*, const pa_server_info* info, void* userdata)
{
    if (info)
        static_cast<PulseBackend*>(userdata)->handleServer(info);
}

void PulseBackend::onServerInfoInitial(pa_context*, const pa_server_info* info, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    if (info)
        self->handleServer(info);
    self->initialStepDone();
}

void PulseBackend::handleSink(const pa_sink_info* info)
{
    SinkData d;
    d.index = info->index;
    d.name = info->name ? info->name : "";
    d.description = info->description ? info->description : d.name;
    d.volume = info->volume;
    d.map = info->channel_map;
    d.spec = info->sample_spec;
    d.muted = info->mute != 0;

    bool added = false;
    {
        std::lock_guard lock(dataMutex_);
        added = sinks_.find(info->index) == sinks_.end();
        sinks_[info->index] = d;
    }
    // Sinks virtuais do próprio LoudMixer não aparecem na UI.
    if (!startsWith(d.name, PulseBoostRoute::kSinkPrefix))
        notify(added ? ChangeKind::DeviceAdded : ChangeKind::DeviceChanged, d.name);
}

void PulseBackend::handleInput(const pa_sink_input_info* info)
{
    InputData d;
    d.index = info->index;
    d.sink = info->sink;
    d.client = info->client;
    d.volume = info->volume;
    d.muted = info->mute != 0;
    const std::string binary = propOr(info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
    d.appName = propOr(info->proplist, PA_PROP_APPLICATION_NAME,
                       binary.empty() ? std::string(info->name ? info->name : "?") : binary);
    d.iconName = propOr(info->proplist, PA_PROP_APPLICATION_ICON_NAME, binary);
    d.appId = propOr(info->proplist, PA_PROP_APPLICATION_ID);
    try {
        d.pid = static_cast<std::uint32_t>(std::stoul(propOr(info->proplist, PA_PROP_APPLICATION_PROCESS_ID, "0")));
    } catch (...) {
        d.pid = 0;
    }

    bool added = false;
    {
        std::lock_guard lock(dataMutex_);
        added = inputs_.find(info->index) == inputs_.end();
        inputs_[info->index] = d;
    }
    notify(added ? ChangeKind::SessionAdded : ChangeKind::SessionChanged, std::to_string(d.index));
}

void PulseBackend::handleServer(const pa_server_info* info)
{
    bool changed = false;
    std::string def = info->default_sink_name ? info->default_sink_name : "";
    {
        std::lock_guard lock(dataMutex_);
        if (info->server_name) {
            serverName_ = info->server_name;
            if (info->server_version)
                serverName_ += std::string(" ") + info->server_version;
        }
        changed = def != defaultSink_;
        defaultSink_ = def;
    }
    if (changed)
        notify(ChangeKind::DefaultDeviceChanged, def);
}

void PulseBackend::notify(ChangeKind kind, const std::string& id)
{
    std::lock_guard lock(callbackMutex_);
    if (callback_)
        callback_(ChangeEvent{kind, id});
}

void PulseBackend::subscribeToChanges(ChangeCallback callback)
{
    std::lock_guard lock(callbackMutex_);
    callback_ = std::move(callback);
}

// --------------------------------------------------------------------------
// Snapshots (thread da UI)
// --------------------------------------------------------------------------

std::vector<AudioDevice> PulseBackend::listDevices() const
{
    std::lock_guard lock(dataMutex_);
    // Com uma rota ativa, o sink padrão "real" é o virtual; para o usuário,
    // o padrão continua sendo o dispositivo físico por trás dele.
    std::string def = defaultSink_;
    if (auto v = virtualToTarget_.find(def); v != virtualToTarget_.end())
        def = v->second;

    std::vector<AudioDevice> out;
    for (const auto& [idx, s] : sinks_) {
        if (startsWith(s.name, PulseBoostRoute::kSinkPrefix))
            continue;
        AudioDevice d;
        d.id = s.name;
        d.name = s.name;
        d.description = s.description;
        d.isDefault = s.name == def;
        d.muted = s.muted;
        d.gain = toLinear(pa_cvolume_max(&s.volume));
        for (int i = 0; i < s.volume.channels; ++i) {
            ChannelInfo c;
            c.index = i;
            c.name = i < s.map.channels ? pa_channel_position_to_pretty_string(s.map.map[i]) : "Channel";
            c.gain = toLinear(s.volume.values[i]);
            d.channels.push_back(std::move(c));
        }
        if (auto p = routedProcessors_.find(s.name); p != routedProcessors_.end())
            d.gain = p->second->gain();
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<AudioSession> PulseBackend::listSessions() const
{
    std::lock_guard lock(dataMutex_);
    std::vector<AudioSession> out;
    for (const auto& [idx, in] : inputs_) {
        if (in.client == ownClient_ || in.appId == PulseBoostRoute::kAppId)
            continue; // streams internos da rota de boost
        AudioSession s;
        s.id = std::to_string(in.index);
        s.appName = in.appName;
        s.iconName = in.iconName;
        s.processId = in.pid;
        s.muted = in.muted;
        s.gain = toLinear(pa_cvolume_max(&in.volume));
        if (auto sink = sinks_.find(in.sink); sink != sinks_.end()) {
            s.deviceId = sink->second.name;
            if (auto v = virtualToTarget_.find(s.deviceId); v != virtualToTarget_.end())
                s.deviceId = v->second;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<ChannelInfo> PulseBackend::listChannels(const std::string& deviceId) const
{
    for (auto& d : listDevices())
        if (d.id == deviceId)
            return d.channels;
    return {};
}

double PulseBackend::maxGain(const Target&) const { return kMaxGain; }

// --------------------------------------------------------------------------
// Comandos (thread da UI)
// --------------------------------------------------------------------------

bool PulseBackend::setGain(const Target& target, double gain)
{
    if (!mainloop_)
        return false;
    MainloopLock lock(mainloop_);
    if (!ready())
        return false;

    pa_operation* op = nullptr;
    if (target.kind == TargetKind::Application) {
        std::uint32_t idx = 0;
        pa_cvolume cv{};
        {
            std::lock_guard dl(dataMutex_);
            try {
                idx = static_cast<std::uint32_t>(std::stoul(target.id));
            } catch (...) {
                return false;
            }
            auto it = inputs_.find(idx);
            if (it == inputs_.end() || it->second.volume.channels == 0)
                return false;
            pa_cvolume_set(&cv, it->second.volume.channels, fromLinear(gain));
        }
        op = pa_context_set_sink_input_volume(context_, idx, &cv, nullptr, nullptr);
    } else {
        std::shared_ptr<GainProcessor> proc;
        std::uint32_t idx = PA_INVALID_INDEX;
        pa_cvolume cv{};
        {
            std::lock_guard dl(dataMutex_);
            if (target.kind == TargetKind::Device)
                if (auto p = routedProcessors_.find(target.id); p != routedProcessors_.end())
                    proc = p->second;
            for (const auto& [i, s] : sinks_)
                if (s.name == target.id) {
                    idx = i;
                    cv = s.volume;
                }
        }
        if (proc) { // dispositivo com rota DSP: o mestre é o ganho do processador
            proc->setGain(static_cast<float>(gain));
            notify(ChangeKind::DeviceChanged, target.id);
            return true;
        }
        if (idx == PA_INVALID_INDEX || cv.channels == 0)
            return false;
        if (target.kind == TargetKind::Device) {
            if (pa_cvolume_max(&cv) == PA_VOLUME_MUTED)
                pa_cvolume_set(&cv, cv.channels, fromLinear(gain));
            else
                pa_cvolume_scale(&cv, fromLinear(gain));
        } else {
            if (target.channel < 0 || target.channel >= cv.channels)
                return false;
            cv.values[target.channel] = fromLinear(gain);
        }
        op = pa_context_set_sink_volume_by_index(context_, idx, &cv, nullptr, nullptr);
    }
    if (!op)
        return false;
    pa_operation_unref(op);
    return true;
}

bool PulseBackend::setMuted(const Target& target, bool muted)
{
    if (!mainloop_)
        return false;
    MainloopLock lock(mainloop_);
    if (!ready())
        return false;
    pa_operation* op = nullptr;
    if (target.kind == TargetKind::Application) {
        std::uint32_t idx = 0;
        try {
            idx = static_cast<std::uint32_t>(std::stoul(target.id));
        } catch (...) {
            return false;
        }
        op = pa_context_set_sink_input_mute(context_, idx, muted, nullptr, nullptr);
    } else {
        op = pa_context_set_sink_mute_by_name(context_, target.id.c_str(), muted, nullptr, nullptr);
    }
    if (!op)
        return false;
    pa_operation_unref(op);
    return true;
}

// Move todos os sink-inputs (exceto os nossos) de um sink para outro.
void PulseBackend::moveInputs(const std::string& fromSink, const std::string& toSink)
{
    std::vector<std::uint32_t> toMove;
    {
        std::lock_guard lock(dataMutex_);
        std::uint32_t fromIdx = PA_INVALID_INDEX;
        for (const auto& [i, s] : sinks_)
            if (s.name == fromSink)
                fromIdx = i;
        for (const auto& [i, in] : inputs_)
            if (in.sink == fromIdx && in.client != ownClient_ && in.appId != PulseBoostRoute::kAppId)
                toMove.push_back(i);
    }
    for (std::uint32_t idx : toMove)
        if (pa_operation* op = pa_context_move_sink_input_by_name(context_, idx, toSink.c_str(), nullptr, nullptr))
            pa_operation_unref(op);
}

bool PulseBackend::attachProcessor(const Target& target, std::shared_ptr<GainProcessor> processor, std::string* error)
{
    if (target.kind != TargetKind::Device) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "The boost route can only be applied to devices.");
        return false;
    }
    if (!mainloop_) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "The audio backend is not running.");
        return false;
    }
    MainloopLock lock(mainloop_);
    if (!ready()) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "Not connected to the audio server.");
        return false;
    }

    auto existing = routes_.find(target.id);

    if (!processor) { // desativar
        if (existing == routes_.end())
            return true;
        PulseBoostRoute& route = *existing->second;
        std::string def;
        {
            std::lock_guard dl(dataMutex_);
            def = defaultSink_;
        }
        if (def == route.virtualSink()) {
            const std::string restore = defaultBeforeRoute_.empty() ? target.id : defaultBeforeRoute_;
            if (pa_operation* op = pa_context_set_default_sink(context_, restore.c_str(), nullptr, nullptr))
                pa_operation_unref(op);
            defaultBeforeRoute_.clear();
        }
        moveInputs(route.virtualSink(), target.id);
        route.stop(true);
        {
            std::lock_guard dl(dataMutex_);
            routedProcessors_.erase(target.id);
            virtualToTarget_.erase(route.virtualSink());
        }
        routes_.erase(existing);
        notify(ChangeKind::DeviceChanged, target.id);
        return true;
    }

    if (existing != routes_.end()) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "DSP boost is already active on this device.");
        return false;
    }

    SinkData sink;
    bool found = false;
    std::string def;
    {
        std::lock_guard dl(dataMutex_);
        for (const auto& [i, s] : sinks_)
            if (s.name == target.id) {
                sink = s;
                found = true;
            }
        def = defaultSink_;
    }
    if (!found) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "Device not found.");
        return false;
    }

    auto route = std::make_unique<PulseBoostRoute>(mainloop_, context_, sink.name, sink.description, sink.spec,
                                                   sink.map, processor);
    if (!route->start(error))
        return false;

    {
        std::lock_guard dl(dataMutex_);
        routedProcessors_[target.id] = processor;
        virtualToTarget_[route->virtualSink()] = target.id;
    }
    // O áudio que tocava no físico passa a tocar no virtual, e o virtual vira
    // padrão se o físico era o padrão (apps novos caem direto no boost).
    moveInputs(sink.name, route->virtualSink());
    if (def == sink.name) {
        defaultBeforeRoute_ = def;
        if (pa_operation* op = pa_context_set_default_sink(context_, route->virtualSink().c_str(), nullptr, nullptr))
            pa_operation_unref(op);
    }
    routes_[target.id] = std::move(route);
    notify(ChangeKind::DeviceChanged, target.id);
    return true;
}

} // namespace loudmixer
