#include "PipeWireBackend.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/dict.h>

#include <algorithm>
#include <cstring>
#include <ctime>

namespace loudmixer {

namespace {

constexpr int kMaxCh = SPA_AUDIO_MAX_CHANNELS;

std::string dictGet(const spa_dict* props, const char* key, const std::string& fallback = {})
{
    const char* v = props ? spa_dict_lookup(props, key) : nullptr;
    return (v && *v) ? std::string(v) : fallback;
}

const char* channelName(std::uint32_t pos)
{
    switch (pos) {
    case SPA_AUDIO_CHANNEL_MONO: return "Mono";
    case SPA_AUDIO_CHANNEL_FL: return "Front Left";
    case SPA_AUDIO_CHANNEL_FR: return "Front Right";
    case SPA_AUDIO_CHANNEL_FC: return "Front Center";
    case SPA_AUDIO_CHANNEL_LFE: return "Subwoofer";
    case SPA_AUDIO_CHANNEL_SL: return "Side Left";
    case SPA_AUDIO_CHANNEL_SR: return "Side Right";
    case SPA_AUDIO_CHANNEL_RL: return "Rear Left";
    case SPA_AUDIO_CHANNEL_RR: return "Rear Right";
    case SPA_AUDIO_CHANNEL_RC: return "Rear Center";
    default: return "Channel";
    }
}

// Extrai "name" de um valor de metadata como {"name":"alsa_output.pci-..."}.
std::string jsonName(const char* value)
{
    if (!value)
        return {};
    const char* k = std::strstr(value, "\"name\"");
    if (!k)
        return {};
    const char* q1 = std::strchr(k + 6, '"');
    if (!q1)
        return {};
    const char* q2 = std::strchr(q1 + 1, '"');
    return q2 ? std::string(q1 + 1, q2) : std::string();
}

class LoopLock {
public:
    explicit LoopLock(pw_thread_loop* l) : l_(l) { if (l_) pw_thread_loop_lock(l_); }
    ~LoopLock() { if (l_) pw_thread_loop_unlock(l_); }
    LoopLock(const LoopLock&) = delete;
    LoopLock& operator=(const LoopLock&) = delete;
private:
    pw_thread_loop* l_;
};

} // namespace

struct PipeWireBackend::Node {
    PipeWireBackend::Impl* impl = nullptr;
    std::uint32_t id = 0;
    bool isSink = false;
    pw_proxy* proxy = nullptr;
    spa_hook nodeListener{};

    // Snapshot (protegido por Impl::dataMutex).
    std::string name;
    std::string description;
    std::string appName;
    std::string iconName;
    std::uint32_t pid = 0;
    std::vector<float> volumes;
    std::vector<std::uint32_t> positions;
    bool muted = false;
    bool hasProps = false;
};

struct PipeWireBackend::Impl {
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    pw_proxy* metadata = nullptr;
    spa_hook coreListener{};
    spa_hook registryListener{};
    spa_hook metadataListener{};
    int syncSeq = 0;
    bool syncDone = false;
    bool connectionLost = false;

    std::map<std::uint32_t, std::unique_ptr<Node>> nodes; // acesso: thread do loop / lock do loop
    mutable std::mutex dataMutex;                          // protege campos de snapshot e os mapas abaixo
    std::map<std::uint32_t, Link> links;
    std::string defaultSinkName;

    std::mutex callbackMutex;
    ChangeCallback callback;

    void notify(ChangeKind k, const std::string& id)
    {
        std::lock_guard l(callbackMutex);
        if (callback)
            callback(ChangeEvent{k, id});
    }

    static std::string nodeKey(const Node& n) { return n.isSink ? n.name : std::to_string(n.id); }

    // ---- Eventos do core -------------------------------------------------
    static void onCoreDone(void* data, std::uint32_t id, int seq)
    {
        auto* self = static_cast<Impl*>(data);
        if (id == PW_ID_CORE && seq == self->syncSeq) {
            self->syncDone = true;
            pw_thread_loop_signal(self->loop, false);
        }
    }

    static void onCoreError(void* data, std::uint32_t id, int, int res, const char*)
    {
        auto* self = static_cast<Impl*>(data);
        if (id == PW_ID_CORE && res == -EPIPE) {
            self->connectionLost = true;
            {
                std::lock_guard l(self->dataMutex);
                self->links.clear();
            }
            self->notify(ChangeKind::BackendReset, {});
            pw_thread_loop_signal(self->loop, false);
        }
    }

    // ---- Eventos de nó ----------------------------------------------------
    static void onNodeInfo(void* data, const pw_node_info* info)
    {
        auto* node = static_cast<Node*>(data);
        Impl* self = node->impl;
        bool first = false;
        {
            std::lock_guard l(self->dataMutex);
            first = node->name.empty() && node->appName.empty();
            if (info->props) {
                node->name = dictGet(info->props, PW_KEY_NODE_NAME, node->name);
                node->description = dictGet(info->props, PW_KEY_NODE_DESCRIPTION,
                                            dictGet(info->props, PW_KEY_NODE_NICK, node->name));
                const std::string binary = dictGet(info->props, PW_KEY_APP_PROCESS_BINARY);
                node->appName = dictGet(info->props, PW_KEY_APP_NAME,
                                        binary.empty() ? dictGet(info->props, PW_KEY_MEDIA_NAME, node->name) : binary);
                node->iconName = dictGet(info->props, PW_KEY_APP_ICON_NAME, binary);
                try {
                    node->pid = static_cast<std::uint32_t>(std::stoul(dictGet(info->props, PW_KEY_APP_PROCESS_ID, "0")));
                } catch (...) {
                    node->pid = 0;
                }
            }
        }
        if (node->isSink)
            self->notify(first ? ChangeKind::DeviceAdded : ChangeKind::DeviceChanged, nodeKey(*node));
        else
            self->notify(first ? ChangeKind::SessionAdded : ChangeKind::SessionChanged, nodeKey(*node));
    }

    static void onNodeParam(void* data, int, std::uint32_t id, std::uint32_t, std::uint32_t, const spa_pod* param)
    {
        auto* node = static_cast<Node*>(data);
        if (id != SPA_PARAM_Props || !param || !spa_pod_is_object(param))
            return;
        Impl* self = node->impl;
        float vols[kMaxCh];
        std::uint32_t pos[kMaxCh];
        std::uint32_t nVols = 0;
        std::uint32_t nPos = 0;
        bool mute = false;
        bool haveMute = false;

        const auto* obj = reinterpret_cast<const spa_pod_object*>(param);
        const spa_pod_prop* prop;
        SPA_POD_OBJECT_FOREACH(obj, prop)
        {
            switch (prop->key) {
            case SPA_PROP_channelVolumes:
                nVols = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, vols, kMaxCh);
                break;
            case SPA_PROP_channelMap:
                nPos = spa_pod_copy_array(&prop->value, SPA_TYPE_Id, pos, kMaxCh);
                break;
            case SPA_PROP_mute:
                haveMute = spa_pod_get_bool(&prop->value, &mute) == 0;
                break;
            default:
                break;
            }
        }
        if (nVols == 0 && !haveMute)
            return;
        {
            std::lock_guard l(self->dataMutex);
            if (nVols > 0)
                node->volumes.assign(vols, vols + nVols);
            if (nPos > 0)
                node->positions.assign(pos, pos + nPos);
            if (haveMute)
                node->muted = mute;
            node->hasProps = true;
        }
        self->notify(node->isSink ? ChangeKind::DeviceChanged : ChangeKind::SessionChanged, nodeKey(*node));
    }

    // ---- Metadata (dispositivo padrão) -----------------------------------
    static int onMetadataProperty(void* data, std::uint32_t subject, const char* key, const char*, const char* value)
    {
        auto* self = static_cast<Impl*>(data);
        if (subject != PW_ID_CORE || !key || std::strcmp(key, "default.audio.sink") != 0)
            return 0;
        const std::string name = jsonName(value);
        {
            std::lock_guard l(self->dataMutex);
            self->defaultSinkName = name;
        }
        self->notify(ChangeKind::DefaultDeviceChanged, name);
        return 0;
    }

    // ---- Registry -----------------------------------------------------------
    static void onGlobal(void* data, std::uint32_t id, std::uint32_t, const char* type, std::uint32_t, const spa_dict* props)
    {
        auto* self = static_cast<Impl*>(data);
        if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            const std::string mediaClass = dictGet(props, PW_KEY_MEDIA_CLASS);
            const bool isSink = mediaClass == "Audio/Sink";
            if (!isSink && mediaClass != "Stream/Output/Audio")
                return;
            auto node = std::make_unique<Node>();
            node->impl = self;
            node->id = id;
            node->isSink = isSink;
            node->name = dictGet(props, PW_KEY_NODE_NAME);
            node->proxy = static_cast<pw_proxy*>(pw_registry_bind(self->registry, id, type, PW_VERSION_NODE, 0));
            if (!node->proxy)
                return;
            static const pw_node_events nodeEvents = [] {
                pw_node_events e{};
                e.version = PW_VERSION_NODE_EVENTS;
                e.info = &Impl::onNodeInfo;
                e.param = &Impl::onNodeParam;
                return e;
            }();
            pw_node_add_listener(reinterpret_cast<pw_node*>(node->proxy), &node->nodeListener, &nodeEvents, node.get());
            std::uint32_t ids[] = {SPA_PARAM_Props};
            pw_node_subscribe_params(reinterpret_cast<pw_node*>(node->proxy), ids, 1);
            self->nodes[id] = std::move(node);
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
            Link link;
            try {
                link.outputNode = static_cast<std::uint32_t>(std::stoul(dictGet(props, PW_KEY_LINK_OUTPUT_NODE, "0")));
                link.inputNode = static_cast<std::uint32_t>(std::stoul(dictGet(props, PW_KEY_LINK_INPUT_NODE, "0")));
            } catch (...) {
                return;
            }
            {
                std::lock_guard l(self->dataMutex);
                self->links[id] = link;
            }
            self->notify(ChangeKind::SessionChanged, std::to_string(link.outputNode));
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 && !self->metadata
                   && dictGet(props, PW_KEY_METADATA_NAME) == "default") {
            self->metadata = static_cast<pw_proxy*>(pw_registry_bind(self->registry, id, type, PW_VERSION_METADATA, 0));
            if (!self->metadata)
                return;
            static const pw_metadata_events metaEvents = [] {
                pw_metadata_events e{};
                e.version = PW_VERSION_METADATA_EVENTS;
                e.property = &Impl::onMetadataProperty;
                return e;
            }();
            pw_metadata_add_listener(reinterpret_cast<pw_metadata*>(self->metadata), &self->metadataListener, &metaEvents, self);
        }
    }

    static void onGlobalRemove(void* data, std::uint32_t id)
    {
        auto* self = static_cast<Impl*>(data);
        if (auto it = self->nodes.find(id); it != self->nodes.end()) {
            std::unique_ptr<Node> node = std::move(it->second);
            self->nodes.erase(it);
            spa_hook_remove(&node->nodeListener);
            pw_proxy_destroy(node->proxy);
            self->notify(node->isSink ? ChangeKind::DeviceRemoved : ChangeKind::SessionRemoved, nodeKey(*node));
            return;
        }
        std::lock_guard l(self->dataMutex);
        self->links.erase(id);
    }

    bool roundtrip()
    {
        syncDone = false;
        syncSeq = pw_core_sync(core, PW_ID_CORE, syncSeq);
        while (!syncDone && !connectionLost) {
            if (pw_thread_loop_timed_wait(loop, 3) != 0)
                return false; // timeout
        }
        return syncDone;
    }

    Node* findSinkByName(const std::string& name)
    {
        for (auto& [id, n] : nodes)
            if (n->isSink && n->name == name)
                return n.get();
        return nullptr;
    }

    // Envia Props com channelVolumes e/ou mute. Exige o lock do loop.
    static bool setProps(Node* node, const std::vector<float>* volumes, const bool* mute)
    {
        alignas(8) std::uint8_t buffer[1024];
        spa_pod_builder b{};
        spa_pod_builder_init(&b, buffer, sizeof(buffer));
        spa_pod_frame f{};
        spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
        if (volumes && !volumes->empty()) {
            spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
            spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, static_cast<std::uint32_t>(volumes->size()),
                                  volumes->data());
        }
        if (mute) {
            spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
            spa_pod_builder_bool(&b, *mute);
        }
        auto* pod = static_cast<spa_pod*>(spa_pod_builder_pop(&b, &f));
        if (!pod)
            return false;
        return pw_node_set_param(reinterpret_cast<pw_node*>(node->proxy), SPA_PARAM_Props, 0, pod) >= 0;
    }
};

PipeWireBackend::PipeWireBackend()
    : d_(std::make_unique<Impl>())
{
}

PipeWireBackend::~PipeWireBackend()
{
    stop();
}

bool PipeWireBackend::start(std::string* error)
{
    if (d_->loop)
        return true;
    pw_init(nullptr, nullptr);

    d_->loop = pw_thread_loop_new("loudmixer-pw", nullptr);
    if (!d_->loop) {
        if (error)
            *error = "pw_thread_loop_new failed";
        return false;
    }
    d_->context = pw_context_new(pw_thread_loop_get_loop(d_->loop), nullptr, 0);
    if (!d_->context || pw_thread_loop_start(d_->loop) < 0) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "Could not start the PipeWire event loop.");
        stop();
        return false;
    }

    pw_thread_loop_lock(d_->loop);
    d_->core = pw_context_connect(d_->context, nullptr, 0);
    if (!d_->core) {
        pw_thread_loop_unlock(d_->loop);
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "Could not connect to the PipeWire daemon.");
        stop();
        return false;
    }

    static const pw_core_events coreEvents = [] {
        pw_core_events e{};
        e.version = PW_VERSION_CORE_EVENTS;
        e.done = &Impl::onCoreDone;
        e.error = &Impl::onCoreError;
        return e;
    }();
    pw_core_add_listener(d_->core, &d_->coreListener, &coreEvents, d_.get());

    static const pw_registry_events registryEvents = [] {
        pw_registry_events e{};
        e.version = PW_VERSION_REGISTRY_EVENTS;
        e.global = &Impl::onGlobal;
        e.global_remove = &Impl::onGlobalRemove;
        return e;
    }();
    d_->registry = pw_core_get_registry(d_->core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(d_->registry, &d_->registryListener, &registryEvents, d_.get());

    // 1ª ida e volta: recebe os globals; 2ª: recebe info/params dos nós.
    const bool ok = d_->roundtrip() && d_->roundtrip();
    pw_thread_loop_unlock(d_->loop);
    if (!ok) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "PipeWire did not respond in time.");
        stop();
        return false;
    }
    return true;
}

void PipeWireBackend::stop()
{
    {
        std::lock_guard l(d_->callbackMutex);
        d_->callback = nullptr;
    }
    if (!d_->loop)
        return;
    pw_thread_loop_lock(d_->loop);
    for (auto& [id, node] : d_->nodes) {
        spa_hook_remove(&node->nodeListener);
        pw_proxy_destroy(node->proxy);
    }
    d_->nodes.clear();
    if (d_->metadata) {
        spa_hook_remove(&d_->metadataListener);
        pw_proxy_destroy(d_->metadata);
        d_->metadata = nullptr;
    }
    if (d_->registry) {
        spa_hook_remove(&d_->registryListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(d_->registry));
        d_->registry = nullptr;
    }
    if (d_->core) {
        spa_hook_remove(&d_->coreListener);
        pw_core_disconnect(d_->core);
        d_->core = nullptr;
    }
    pw_thread_loop_unlock(d_->loop);
    pw_thread_loop_stop(d_->loop);
    if (d_->context) {
        pw_context_destroy(d_->context);
        d_->context = nullptr;
    }
    pw_thread_loop_destroy(d_->loop);
    d_->loop = nullptr;

    std::lock_guard l(d_->dataMutex);
    d_->links.clear();
}

std::vector<AudioDevice> PipeWireBackend::listDevices() const
{
    if (!d_->loop)
        return {};
    LoopLock lock(d_->loop); // protege o mapa de nós
    std::lock_guard l(d_->dataMutex);
    std::vector<AudioDevice> out;
    for (const auto& [id, n] : d_->nodes) {
        if (!n->isSink)
            continue;
        AudioDevice dev;
        dev.id = n->name;
        dev.name = n->name;
        dev.description = n->description.empty() ? n->name : n->description;
        dev.isDefault = n->name == d_->defaultSinkName;
        dev.muted = n->muted;
        double master = 0.0;
        for (std::size_t i = 0; i < n->volumes.size(); ++i) {
            ChannelInfo c;
            c.index = static_cast<int>(i);
            c.name = i < n->positions.size() ? channelName(n->positions[i]) : "Channel";
            c.gain = clampGain(n->volumes[i]);
            master = std::max(master, c.gain);
            dev.channels.push_back(std::move(c));
        }
        dev.gain = n->volumes.empty() ? kUnityGain : master;
        out.push_back(std::move(dev));
    }
    return out;
}

std::vector<AudioSession> PipeWireBackend::listSessions() const
{
    if (!d_->loop)
        return {};
    LoopLock lock(d_->loop);
    std::lock_guard l(d_->dataMutex);
    std::vector<AudioSession> out;
    for (const auto& [id, n] : d_->nodes) {
        if (n->isSink)
            continue;
        AudioSession s;
        s.id = std::to_string(n->id);
        s.appName = n->appName;
        s.iconName = n->iconName;
        s.processId = n->pid;
        s.muted = n->muted;
        double g = 0.0;
        for (float v : n->volumes)
            g = std::max(g, static_cast<double>(v));
        s.gain = n->volumes.empty() ? kUnityGain : clampGain(g);
        for (const auto& [lid, link] : d_->links) {
            if (link.outputNode != n->id)
                continue;
            if (auto sink = d_->nodes.find(link.inputNode); sink != d_->nodes.end() && sink->second->isSink)
                s.deviceId = sink->second->name;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<ChannelInfo> PipeWireBackend::listChannels(const std::string& deviceId) const
{
    for (auto& d : listDevices())
        if (d.id == deviceId)
            return d.channels;
    return {};
}

double PipeWireBackend::maxGain(const Target&) const { return kMaxGain; }

bool PipeWireBackend::setGain(const Target& target, double gain)
{
    if (!d_->loop)
        return false;
    gain = clampGain(gain);
    LoopLock lock(d_->loop);
    Node* node = nullptr;
    if (target.kind == TargetKind::Application) {
        try {
            auto it = d_->nodes.find(static_cast<std::uint32_t>(std::stoul(target.id)));
            if (it != d_->nodes.end() && !it->second->isSink)
                node = it->second.get();
        } catch (...) {
        }
    } else {
        node = d_->findSinkByName(target.id);
    }
    if (!node)
        return false;

    std::vector<float> vols;
    {
        std::lock_guard l(d_->dataMutex);
        vols = node->volumes;
    }
    if (vols.empty())
        vols.assign(2, 1.0f);

    const auto g = static_cast<float>(gain);
    if (target.kind == TargetKind::Channel) {
        if (target.channel < 0 || target.channel >= static_cast<int>(vols.size()))
            return false;
        vols[static_cast<std::size_t>(target.channel)] = g;
    } else if (target.kind == TargetKind::Device) {
        const float m = *std::max_element(vols.begin(), vols.end());
        for (float& v : vols)
            v = m > 0.0f ? v / m * g : g; // preserva o balanço
    } else {
        std::fill(vols.begin(), vols.end(), g);
    }
    return Impl::setProps(node, &vols, nullptr);
}

bool PipeWireBackend::setMuted(const Target& target, bool muted)
{
    if (!d_->loop)
        return false;
    LoopLock lock(d_->loop);
    Node* node = nullptr;
    if (target.kind == TargetKind::Application) {
        try {
            auto it = d_->nodes.find(static_cast<std::uint32_t>(std::stoul(target.id)));
            if (it != d_->nodes.end())
                node = it->second.get();
        } catch (...) {
        }
    } else {
        node = d_->findSinkByName(target.id);
    }
    return node && Impl::setProps(node, nullptr, &muted);
}

void PipeWireBackend::subscribeToChanges(ChangeCallback callback)
{
    std::lock_guard l(d_->callbackMutex);
    d_->callback = std::move(callback);
}

} // namespace loudmixer
