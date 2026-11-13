#include "WasapiBackend.h"

#include "ApoRouteMath.h"
#include "ComUtil.h"
#include "LoopbackCapture.h"
#include "SessionEnumerator.h"
#include "apo/ApoRegistry.h"
#include "apo/ApoShared.h"
#include "core/GainProcessor.h"

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propsys.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <future>

namespace loudmixer {

using win::ComPtr;
using win::CoTaskString;
using win::toUtf8;

namespace {

// PKEY_Device_FriendlyName (functiondiscoverykeys_devpkey.h), definido aqui
// para não depender de headers que variam entre SDK e MinGW.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

constexpr auto kPollInterval = std::chrono::milliseconds(750);

std::string channelName(UINT count, UINT index)
{
    static const char* stereo[] = {"Front Left", "Front Right"};
    static const char* surround[] = {"Front Left", "Front Right", "Front Center", "Subwoofer",
                                     "Rear Left",  "Rear Right",  "Side Left",    "Side Right"};
    if (count == 1)
        return "Mono";
    if (count == 2 && index < 2)
        return stereo[index];
    if ((count == 6 || count == 8) && index < 8)
        return surround[index];
    return "Channel " + std::to_string(index + 1);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Recebe hot-plug e troca de dispositivo padrão do Windows.
// Interfaces COM não têm destrutor virtual por design; o objeto é final e se
// destrói pelo próprio Release(), então o aviso -Wnon-virtual-dtor não se aplica.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
class NotificationClient final : public IMMNotificationClient {
public:
    explicit NotificationClient(std::function<void()> onChange) : onChange_(std::move(onChange)) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --refs_;
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return fire(); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return fire(); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return fire(); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        return (flow == eRender && role == eConsole) ? fire() : S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    void disable() { enabled_ = false; }

private:
    HRESULT fire()
    {
        if (enabled_)
            onChange_();
        return S_OK;
    }
    std::atomic<ULONG> refs_{1};
    std::atomic<bool> enabled_{true};
    std::function<void()> onChange_;
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace

struct WasapiBackend::Impl {
    struct DeviceEntry {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioEndpointVolume> volume;
    };

    ComPtr<IMMDeviceEnumerator> enumerator;
    NotificationClient* notifier = nullptr;
    std::map<std::string, DeviceEntry> devices;           // id -> COM
    std::map<std::string, win::SessionEntry> sessions;    // id -> COM
    std::map<std::string, std::string> sessionDevice;     // sessão -> endpoint

    // Rota de boost
    std::unique_ptr<win::LoopbackCapture> capture;
    std::shared_ptr<GainProcessor> processor;
    std::string sourceHint;
    double routeDeviceGain = 1.0;
    std::vector<double> routeChannelGains;
    std::map<std::string, double> appDesired; // sessões no cabo virtual
    double appBoost = 1.0;                    // B

    // Boost do sistema (APO), por dispositivo
    struct ApoDevice {
        std::wstring guid;
        std::shared_ptr<apo::SharedView> view; // aberta quando o APO existe
        double deviceFactor = 1.0;             // 1..2
        std::vector<double> channelFactors;    // 1..2
        std::map<std::string, double> appDesired;
        double appBoost = 1.0; // B
        bool seeding = true;   // primeira varredura: sessões já atenuadas por nós
        bool dirty = false;    // salvar no registro
        bool cannotPersist = false;
    };
    std::map<std::string, ApoDevice> apo; // id do endpoint -> estado

    // Lido pela thread da UI (LED) via takeDeviceStats.
    struct StatsSource {
        std::shared_ptr<apo::SharedView> view;
        std::uint64_t lastClipped = 0;
        std::uint64_t lastProcessed = 0;
    };
    std::mutex statsMutex;
    std::map<std::string, StatsSource> stats;
};

WasapiBackend::WasapiBackend()
    : d_(std::make_unique<Impl>())
{
}

WasapiBackend::~WasapiBackend()
{
    stop();
}

void WasapiBackend::setOption(const std::string& key, const std::string& value)
{
    if (key == "boostSource")
        post([this, value] { d_->sourceHint = value; });
}

// --------------------------------------------------------------------------
// Thread COM
// --------------------------------------------------------------------------

bool WasapiBackend::start(std::string* error)
{
    if (worker_.joinable())
        return true;
    quit_ = false;
    std::string startError;
    int startState = 0;
    worker_ = std::thread([this, &startError, &startState] { workerLoop(&startError, &startState); });
    // workerLoop sinaliza via startState; esperamos com polling curto.
    while (true) {
        {
            std::lock_guard lock(queueMutex_);
            if (startState != 0)
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (startState < 0) {
        worker_.join();
        if (error)
            *error = startError;
        return false;
    }
    return true;
}

void WasapiBackend::stop()
{
    {
        std::lock_guard lock(callbackMutex_);
        callback_ = nullptr;
    }
    if (!worker_.joinable())
        return;
    {
        std::lock_guard lock(queueMutex_);
        quit_ = true;
    }
    queueCv_.notify_all();
    worker_.join();
}

void WasapiBackend::post(std::function<void()> fn)
{
    {
        std::lock_guard lock(queueMutex_);
        if (quit_)
            return;
        queue_.push_back(std::move(fn));
    }
    queueCv_.notify_one();
}

bool WasapiBackend::runSync(std::function<bool()> fn)
{
    if (!worker_.joinable())
        return false;
    auto task = std::make_shared<std::packaged_task<bool()>>(std::move(fn));
    auto result = task->get_future();
    post([task] { (*task)(); });
    // Sem timeout de propósito: a tarefa captura variáveis do chamador por
    // referência, então não podemos voltar antes de ela terminar.
    return result.get();
}

void WasapiBackend::workerLoop(std::string* startError, int* startState)
{
    win::ComApartment com;
    auto signalStart = [&](int state, std::string err = {}) {
        std::lock_guard lock(queueMutex_);
        *startError = std::move(err);
        *startState = state;
    };
    if (!com.ok())
        return signalStart(-1, "CoInitializeEx failed");

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  d_->enumerator.putVoid());
    if (FAILED(hr))
        return signalStart(-1, LM_TRANSLATE_NOOP("backend", "Could not access the Windows audio devices (IMMDeviceEnumerator)."));

    d_->notifier = new NotificationClient([this] { post([this] { refresh(); }); });
    d_->enumerator->RegisterEndpointNotificationCallback(d_->notifier);

    refresh();
    signalStart(1);

    auto nextPoll = std::chrono::steady_clock::now() + kPollInterval;
    for (;;) {
        std::deque<std::function<void()>> tasks;
        {
            std::unique_lock lock(queueMutex_);
            queueCv_.wait_until(lock, nextPoll, [this] { return quit_ || !queue_.empty(); });
            if (quit_)
                break;
            tasks.swap(queue_);
        }
        for (auto& t : tasks)
            t();
        if (std::chrono::steady_clock::now() >= nextPoll) {
            refresh();
            nextPoll = std::chrono::steady_clock::now() + kPollInterval;
        }
    }

    // Encerramento: salva o boost do sistema, desmonta a rota e libera COM
    // nesta mesma thread. (O APO segue aplicando o último ganho com o app
    // fechado; as sessões continuam atenuadas de forma coerente com ele.)
    flushApoState();
    if (d_->capture) {
        d_->capture->stop();
        d_->capture.reset();
    }
    d_->notifier->disable();
    d_->enumerator->UnregisterEndpointNotificationCallback(d_->notifier);
    d_->notifier->Release();
    d_->notifier = nullptr;
    d_->sessions.clear();
    d_->devices.clear();
    d_->enumerator.reset();
    std::lock_guard lock(queueMutex_);
    queue_.clear();
}

// --------------------------------------------------------------------------
// Varredura de estado (thread COM)
// --------------------------------------------------------------------------

void WasapiBackend::refresh()
{
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(d_->enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.put())))
        return;

    std::string defaultId;
    {
        ComPtr<IMMDevice> def;
        CoTaskString id;
        if (SUCCEEDED(d_->enumerator->GetDefaultAudioEndpoint(eRender, eConsole, def.put())) && SUCCEEDED(def->GetId(id.put())))
            defaultId = toUtf8(id.p);
    }

    std::string routeTarget, routeSource;
    {
        std::lock_guard lock(dataMutex_);
        routeTarget = routeTarget_;
        routeSource = routeSource_;
    }

    UINT count = 0;
    collection->GetCount(&count);
    std::map<std::string, Impl::DeviceEntry> entries;
    std::map<std::string, win::SessionEntry> sessionEntries;
    std::map<std::string, std::string> sessionDevice;
    std::vector<AudioDevice> devices;
    std::vector<AudioSession> sessions;
    bool needApply = false;
    std::vector<std::string> apoToApply;

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        CoTaskString rawId;
        if (FAILED(collection->Item(i, dev.put())) || FAILED(dev->GetId(rawId.put())))
            continue;
        const std::string id = toUtf8(rawId.p);

        Impl::DeviceEntry entry;
        entry.device = dev;
        if (auto old = d_->devices.find(id); old != d_->devices.end())
            entry.volume = old->second.volume;
        if (!entry.volume)
            dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, entry.volume.putVoid());

        AudioDevice d;
        d.id = id;
        d.isDefault = id == defaultId;
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, props.put()))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR)
                d.description = toUtf8(pv.pwszVal);
            PropVariantClear(&pv);
        }
        d.name = d.description;
        if (entry.volume) {
            float master = 1.0f;
            BOOL mute = FALSE;
            UINT channels = 0;
            entry.volume->GetMasterVolumeLevelScalar(&master);
            entry.volume->GetMute(&mute);
            entry.volume->GetChannelCount(&channels);
            d.gain = master;
            d.muted = mute != FALSE;
            for (UINT c = 0; c < channels; ++c) {
                float level = 1.0f;
                entry.volume->GetChannelVolumeLevelScalar(c, &level);
                d.channels.push_back({static_cast<int>(c), channelName(channels, c), level});
            }
        }
        if (id == routeTarget) { // com rota: valores desejados (podem passar de 100%)
            d.gain = d_->routeDeviceGain;
            for (std::size_t c = 0; c < d.channels.size() && c < d_->routeChannelGains.size(); ++c)
                d.channels[c].gain = d_->routeChannelGains[c];
        }

        // Boost do sistema: o APO do LoudMixer está na cadeia deste dispositivo?
        Impl::ApoDevice* apoDev = nullptr;
        if (const std::wstring guid = apo::normalizeEndpointGuid(win::toWide(id)); apo::isAttached(guid)) {
            auto [it, isNew] = d_->apo.try_emplace(id);
            apoDev = &it->second;
            if (!apoDev->view || !*apoDev->view) {
                auto view = std::make_shared<apo::SharedView>();
                if (view->open(guid)) {
                    apoDev->view = view;
                    std::lock_guard lock(d_->statsMutex);
                    d_->stats[id] = Impl::StatsSource{view, view->get()->clippedSamples.load(),
                                                      view->get()->processedSamples.load()};
                    if (!isNew)
                        apoToApply.push_back(id); // APO recriado: garante o estado atual
                }
            }
            if (isNew) {
                // Estado salvo (memória compartilhada, mais exata, ou registro).
                const apo::Persisted saved = apo::readPersisted(guid);
                double master = saved.found ? saved.master : 1.0;
                std::vector<float> chans = saved.channels;
                if (apoDev->view && *apoDev->view) {
                    auto* sh = apoDev->view->get();
                    master = sh->gain.load();
                    chans.clear();
                    for (std::size_t c = 0; c < d.channels.size() && c < apo::kSharedMaxChannels; ++c)
                        chans.push_back(sh->channelGains[c].load());
                }
                apoDev->guid = guid;
                apoDev->appBoost = saved.appBoost;
                apoDev->deviceFactor = apoRoute::deviceFactorFrom(master, saved.appBoost);
                apoDev->channelFactors.assign(d.channels.size(), 1.0);
                for (std::size_t c = 0; c < apoDev->channelFactors.size() && c < chans.size(); ++c)
                    apoDev->channelFactors[c] = std::clamp(static_cast<double>(chans[c]), 1.0, apoRoute::kMax);
            }
            apoDev->channelFactors.resize(d.channels.size(), 1.0);
            d.systemBoost = true;
            d.gain = apoRoute::combine(d.gain, apoDev->deviceFactor);
            for (std::size_t c = 0; c < d.channels.size(); ++c)
                d.channels[c].gain = apoRoute::combine(d.channels[c].gain, apoDev->channelFactors[c]);
        } else if (d_->apo.count(id)) {
            detachApo(id); // removido por fora (driver atualizado, uninstall...)
        }
        devices.push_back(d);

        for (auto& s : win::enumerateSessions(dev.get())) {
            AudioSession as;
            as.id = s.id;
            as.appName = s.appName;
            as.iconName = s.iconPath;
            as.processId = s.pid;
            as.deviceId = id;
            as.muted = s.muted;
            as.gain = s.volume;
            if (apoDev) {
                auto want = apoDev->appDesired.find(s.id);
                if (want == apoDev->appDesired.end()) {
                    // Na primeira varredura, as sessões já existentes foram
                    // atenuadas por nós (nativo × B). Sessões novas começam no
                    // próprio volume nativo: nunca mais alto que o pedido.
                    const double desired =
                        apoDev->seeding ? apoRoute::clampGain(s.volume * apoDev->appBoost) : s.volume;
                    apoDev->appDesired[s.id] = desired;
                    as.gain = desired;
                    if (!apoDev->seeding && apoDev->appBoost > 1.0)
                        apoToApply.push_back(id);
                } else {
                    const double observed = apoRoute::observeSession(s.volume, want->second, apoDev->appBoost);
                    if (std::fabs(observed - want->second) > 1e-9) {
                        want->second = observed; // mudou pelo mixer do Windows
                        apoToApply.push_back(id);
                    }
                    as.gain = want->second;
                }
            } else if (id == routeSource && !routeTarget.empty()) {
                auto want = d_->appDesired.find(s.id);
                if (want == d_->appDesired.end()) {
                    // Sessão nova no cabo: começa em 100% efetivo.
                    d_->appDesired[s.id] = 1.0;
                    as.gain = 1.0;
                    needApply = true;
                } else if (std::fabs(s.volume - want->second / d_->appBoost) < 0.01) {
                    as.gain = want->second;
                } else {
                    // Alguém mudou o volume pelo mixer do Windows: respeita.
                    want->second = clampGain(s.volume * d_->appBoost);
                    as.gain = want->second;
                    needApply = true;
                }
            }
            sessionDevice[s.id] = id;
            sessions.push_back(std::move(as));
            sessionEntries[s.id] = std::move(s);
        }
        entries[id] = std::move(entry);
        if (apoDev)
            apoDev->seeding = false;
    }

    d_->devices = std::move(entries);
    d_->sessions = std::move(sessionEntries);
    d_->sessionDevice = std::move(sessionDevice);
    std::erase_if(d_->appDesired, [&](const auto& kv) { return d_->sessions.count(kv.first) == 0; });
    for (auto& [devId, a] : d_->apo)
        std::erase_if(a.appDesired, [&](const auto& kv) { return d_->sessions.count(kv.first) == 0; });
    // Dispositivo com APO que sumiu (desconectado): esquece o estado em memória.
    for (auto it = d_->apo.begin(); it != d_->apo.end();) {
        if (d_->devices.count(it->first)) {
            ++it;
            continue;
        }
        std::lock_guard lock(d_->statsMutex);
        d_->stats.erase(it->first);
        it = d_->apo.erase(it);
    }

    // Rota cujo dispositivo sumiu (fone desconectado): desmonta.
    if (!routeTarget.empty() && (!d_->devices.count(routeTarget) || !d_->devices.count(routeSource))) {
        if (d_->capture)
            d_->capture->stop();
        d_->capture.reset();
        d_->processor.reset();
        d_->appDesired.clear();
        std::lock_guard lock(dataMutex_);
        routeTarget_.clear();
        routeSource_.clear();
    }

    // Diferenças -> eventos.
    std::vector<ChangeEvent> events;
    {
        std::lock_guard lock(dataMutex_);
        auto diff = [&](const auto& before, const auto& after, ChangeKind added, ChangeKind removed, ChangeKind changed) {
            for (const auto& a : after) {
                auto it = std::find_if(before.begin(), before.end(), [&](const auto& b) { return b.id == a.id; });
                if (it == before.end())
                    events.push_back({added, a.id});
                else if (!(*it == a))
                    events.push_back({changed, a.id});
            }
            for (const auto& b : before)
                if (std::none_of(after.begin(), after.end(), [&](const auto& a) { return a.id == b.id; }))
                    events.push_back({removed, b.id});
        };
        diff(devices_, devices, ChangeKind::DeviceAdded, ChangeKind::DeviceRemoved, ChangeKind::DeviceChanged);
        diff(sessions_, sessions, ChangeKind::SessionAdded, ChangeKind::SessionRemoved, ChangeKind::SessionChanged);
        devices_ = std::move(devices);
        sessions_ = std::move(sessions);
    }
    for (const auto& ev : events)
        notify(ev.kind, ev.id);
    if (needApply)
        applyRouteGains();
    std::sort(apoToApply.begin(), apoToApply.end());
    apoToApply.erase(std::unique(apoToApply.begin(), apoToApply.end()), apoToApply.end());
    for (const auto& id : apoToApply)
        applyApoGains(id);
    flushApoState();
}

// Boost do sistema: distribui o ganho entre as sessões (pré-atenuação) e o
// APO, e publica na memória compartilhada (ver ApoRouteMath.h).
void WasapiBackend::applyApoGains(const std::string& deviceId)
{
    auto it = d_->apo.find(deviceId);
    if (it == d_->apo.end())
        return;
    auto& a = it->second;
    a.appBoost = apoRoute::appBoost(a.appDesired);
    for (const auto& [sid, want] : a.appDesired) {
        auto s = d_->sessions.find(sid);
        if (s == d_->sessions.end() || !s->second.control)
            continue;
        const double native = apoRoute::sessionNative(want, a.appBoost);
        if (std::fabs(native - s->second.volume) > 0.001) {
            s->second.control->SetMasterVolume(static_cast<float>(native), nullptr);
            s->second.volume = static_cast<float>(native);
        }
    }
    if (a.view && *a.view) {
        auto* sh = a.view->get();
        sh->gain.store(static_cast<float>(apoRoute::apoMaster(a.deviceFactor, a.appBoost)));
        for (std::size_t c = 0; c < a.channelFactors.size() && c < apo::kSharedMaxChannels; ++c)
            sh->channelGains[c].store(static_cast<float>(a.channelFactors[c]));
    }
    a.dirty = true;
}

// O APO saiu da cadeia do dispositivo: devolve as sessões ao volume nativo
// equivalente (até 100%) e esquece o estado.
void WasapiBackend::detachApo(const std::string& deviceId)
{
    auto it = d_->apo.find(deviceId);
    if (it == d_->apo.end())
        return;
    for (const auto& [sid, want] : it->second.appDesired) {
        auto s = d_->sessions.find(sid);
        if (s != d_->sessions.end() && s->second.control)
            s->second.control->SetMasterVolume(static_cast<float>(std::min(want, 1.0)), nullptr);
    }
    {
        std::lock_guard lock(d_->statsMutex);
        d_->stats.erase(deviceId);
    }
    d_->apo.erase(it);
}

// Salva o estado efetivo no registro para o APO reaplicar quando o Windows o
// recriar (reboot, reconexão), mesmo com o app fechado. Em lote: no máximo
// uma vez por varredura, não a cada movimento do slider.
void WasapiBackend::flushApoState()
{
    for (auto& [id, a] : d_->apo) {
        if (!a.dirty || a.cannotPersist)
            continue;
        a.dirty = false;
        std::vector<float> chans(a.channelFactors.begin(), a.channelFactors.end());
        if (!apo::writePersisted(a.guid, static_cast<float>(apoRoute::apoMaster(a.deviceFactor, a.appBoost)),
                                 static_cast<float>(a.appBoost), chans))
            a.cannotPersist = true; // instalação sem permissão para usuários: só não persiste
    }
}

std::optional<ProcessorStats> WasapiBackend::takeDeviceStats(const std::string& deviceId)
{
    std::lock_guard lock(d_->statsMutex);
    auto it = d_->stats.find(deviceId);
    if (it == d_->stats.end() || !it->second.view || !*it->second.view)
        return std::nullopt;
    auto* sh = it->second.view->get();
    ProcessorStats st;
    st.peakIn = sh->peakIn.exchange(0.0f);
    st.peakOut = sh->peakOut.exchange(0.0f);
    const std::uint64_t clipped = sh->clippedSamples.load();
    const std::uint64_t processed = sh->processedSamples.load();
    st.clippedSamples = clipped >= it->second.lastClipped ? clipped - it->second.lastClipped : 0;
    st.processedSamples = processed >= it->second.lastProcessed ? processed - it->second.lastProcessed : 0;
    it->second.lastClipped = clipped;
    it->second.lastProcessed = processed;
    return st;
}

// Distribui o ganho entre pré-atenuação das sessões e o DSP (ver header).
void WasapiBackend::applyRouteGains()
{
    if (!d_->processor)
        return;
    std::string target, source;
    {
        std::lock_guard lock(dataMutex_);
        target = routeTarget_;
        source = routeSource_;
    }
    double boost = 1.0;
    for (const auto& [id, want] : d_->appDesired)
        boost = std::max(boost, want);
    d_->appBoost = boost;
    for (const auto& [id, want] : d_->appDesired) {
        auto s = d_->sessions.find(id);
        if (s != d_->sessions.end() && s->second.control)
            s->second.control->SetMasterVolume(static_cast<float>(want / boost), nullptr);
    }
    d_->processor->setGain(static_cast<float>(d_->routeDeviceGain * boost));

    auto dev = d_->devices.find(target);
    for (std::size_t c = 0; c < d_->routeChannelGains.size(); ++c) {
        const double g = d_->routeChannelGains[c];
        if (dev != d_->devices.end() && dev->second.volume)
            dev->second.volume->SetChannelVolumeLevelScalar(static_cast<UINT>(c), static_cast<float>(std::min(g, 1.0)), nullptr);
        d_->processor->setChannelGain(static_cast<int>(c), static_cast<float>(std::max(g, 1.0)));
    }
}

void WasapiBackend::notify(ChangeKind kind, const std::string& id)
{
    std::lock_guard lock(callbackMutex_);
    if (callback_)
        callback_(ChangeEvent{kind, id});
}

void WasapiBackend::subscribeToChanges(ChangeCallback callback)
{
    std::lock_guard lock(callbackMutex_);
    callback_ = std::move(callback);
}

// --------------------------------------------------------------------------
// API pública
// --------------------------------------------------------------------------

std::vector<AudioDevice> WasapiBackend::listDevices() const
{
    std::lock_guard lock(dataMutex_);
    return devices_;
}

std::vector<AudioSession> WasapiBackend::listSessions() const
{
    std::lock_guard lock(dataMutex_);
    return sessions_;
}

std::vector<ChannelInfo> WasapiBackend::listChannels(const std::string& deviceId) const
{
    std::lock_guard lock(dataMutex_);
    for (const auto& d : devices_)
        if (d.id == deviceId)
            return d.channels;
    return {};
}

double WasapiBackend::maxGain(const Target& target) const
{
    std::lock_guard lock(dataMutex_);
    // Boost do sistema (APO): dispositivo, canais e apps desse dispositivo.
    auto systemBoost = [&](const std::string& deviceId) {
        return std::any_of(devices_.begin(), devices_.end(),
                           [&](const AudioDevice& d) { return d.id == deviceId && d.systemBoost; });
    };
    if (target.kind == TargetKind::Application) {
        for (const auto& s : sessions_)
            if (s.id == target.id && systemBoost(s.deviceId))
                return kMaxGain;
    } else if (systemBoost(target.id)) {
        return kMaxGain;
    }
    if (routeTarget_.empty())
        return kUnityGain;
    if (target.kind == TargetKind::Application) {
        for (const auto& s : sessions_)
            if (s.id == target.id)
                return s.deviceId == routeSource_ ? kMaxGain : kUnityGain;
        return kUnityGain;
    }
    return target.id == routeTarget_ ? kMaxGain : kUnityGain;
}

bool WasapiBackend::setGain(const Target& target, double gain)
{
    gain = std::min(clampGain(gain), maxGain(target));
    const bool ok = runSync([this, target, gain] {
        std::string routeTarget;
        {
            std::lock_guard lock(dataMutex_);
            routeTarget = routeTarget_;
        }
        if (target.kind == TargetKind::Application) {
            auto s = d_->sessions.find(target.id);
            if (s == d_->sessions.end() || !s->second.control)
                return false;
            if (auto dev = d_->sessionDevice.find(target.id); dev != d_->sessionDevice.end()) {
                if (auto a = d_->apo.find(dev->second); a != d_->apo.end()) {
                    a->second.appDesired[target.id] = gain;
                    applyApoGains(dev->second);
                    return true;
                }
            }
            if (d_->appDesired.count(target.id)) {
                d_->appDesired[target.id] = gain;
                applyRouteGains();
                return true;
            }
            return SUCCEEDED(s->second.control->SetMasterVolume(static_cast<float>(gain), nullptr));
        }
        auto dev = d_->devices.find(target.id);
        if (dev == d_->devices.end() || !dev->second.volume)
            return false;
        if (auto a = d_->apo.find(target.id); a != d_->apo.end()) {
            // Até 100% no volume do Windows, acima disso no APO.
            const auto part = apoRoute::split(gain);
            HRESULT hr;
            if (target.kind == TargetKind::Device) {
                hr = dev->second.volume->SetMasterVolumeLevelScalar(static_cast<float>(part.native), nullptr);
                a->second.deviceFactor = part.factor;
            } else {
                if (target.channel < 0 || target.channel >= static_cast<int>(a->second.channelFactors.size()))
                    return false;
                hr = dev->second.volume->SetChannelVolumeLevelScalar(static_cast<UINT>(target.channel),
                                                                     static_cast<float>(part.native), nullptr);
                a->second.channelFactors[static_cast<std::size_t>(target.channel)] = part.factor;
            }
            applyApoGains(target.id);
            return SUCCEEDED(hr);
        }
        if (target.kind == TargetKind::Device) {
            if (target.id == routeTarget) {
                d_->routeDeviceGain = gain;
                applyRouteGains();
                return true;
            }
            return SUCCEEDED(dev->second.volume->SetMasterVolumeLevelScalar(static_cast<float>(gain), nullptr));
        }
        if (target.id == routeTarget) {
            if (target.channel < 0 || target.channel >= static_cast<int>(d_->routeChannelGains.size()))
                return false;
            d_->routeChannelGains[static_cast<std::size_t>(target.channel)] = gain;
            applyRouteGains();
            return true;
        }
        return SUCCEEDED(dev->second.volume->SetChannelVolumeLevelScalar(static_cast<UINT>(target.channel),
                                                                          static_cast<float>(gain), nullptr));
    });
    if (ok) { // atualiza o snapshot sem esperar a próxima varredura
        std::lock_guard lock(dataMutex_);
        if (target.kind == TargetKind::Application) {
            for (auto& s : sessions_)
                if (s.id == target.id)
                    s.gain = gain;
        } else {
            for (auto& d : devices_) {
                if (d.id != target.id)
                    continue;
                if (target.kind == TargetKind::Device)
                    d.gain = gain;
                else if (target.channel >= 0 && target.channel < static_cast<int>(d.channels.size()))
                    d.channels[static_cast<std::size_t>(target.channel)].gain = gain;
            }
        }
    }
    return ok;
}

bool WasapiBackend::setMuted(const Target& target, bool muted)
{
    return runSync([this, target, muted] {
        if (target.kind == TargetKind::Application) {
            auto s = d_->sessions.find(target.id);
            return s != d_->sessions.end() && s->second.control
                && SUCCEEDED(s->second.control->SetMute(muted ? TRUE : FALSE, nullptr));
        }
        auto dev = d_->devices.find(target.id);
        return dev != d_->devices.end() && dev->second.volume
            && SUCCEEDED(dev->second.volume->SetMute(muted ? TRUE : FALSE, nullptr));
    });
}

bool WasapiBackend::attachProcessor(const Target& target, std::shared_ptr<GainProcessor> processor, std::string* error)
{
    if (target.kind != TargetKind::Device) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "The boost route can only be applied to devices.");
        return false;
    }
    std::string err;
    const bool ok = runSync([&] {
        std::string currentTarget;
        {
            std::lock_guard lock(dataMutex_);
            currentTarget = routeTarget_;
        }

        if (!processor) { // desativar
            if (currentTarget != target.id)
                return true;
            if (d_->capture)
                d_->capture->stop();
            d_->capture.reset();
            // Devolve as sessões ao volume nativo equivalente (máx. 100%).
            for (const auto& [id, want] : d_->appDesired) {
                auto s = d_->sessions.find(id);
                if (s != d_->sessions.end() && s->second.control)
                    s->second.control->SetMasterVolume(static_cast<float>(std::min(want, 1.0)), nullptr);
            }
            d_->appDesired.clear();
            d_->processor.reset();
            {
                std::lock_guard lock(dataMutex_);
                routeTarget_.clear();
                routeSource_.clear();
            }
            refresh();
            return true;
        }

        if (!currentTarget.empty()) {
            err = LM_TRANSLATE_NOOP("backend", "DSP boost is already active on another device (v1 supports one at a time).");
            return false;
        }
        if (d_->apo.count(target.id)) {
            err = LM_TRANSLATE_NOOP("backend", "This device already boosts above 100% through the LoudMixer system effect.");
            return false;
        }

        // Escolhe o cabo virtual de origem.
        std::vector<AudioDevice> devices;
        {
            std::lock_guard lock(dataMutex_);
            devices = devices_;
        }
        std::vector<std::string> hints;
        if (!d_->sourceHint.empty())
            hints.push_back(lower(d_->sourceHint));
        else
            hints = {"cable input", "vb-audio", "voicemeeter input", "virtual"};
        std::string source;
        for (const auto& h : hints) {
            for (const auto& d : devices)
                if (d.id != target.id && lower(d.description).find(h) != std::string::npos) {
                    source = d.id;
                    break;
                }
            if (!source.empty())
                break;
        }
        if (source.empty()) {
            err = LM_TRANSLATE_NOOP("backend", "No virtual output device found. Install VB-Audio Virtual Cable (free), set \"CABLE Input\" as the Windows default output and enable DSP boost again.");
            return false;
        }

        auto capture = std::make_unique<win::LoopbackCapture>();
        if (!capture->start(source, target.id, processor, &err))
            return false;

        d_->capture = std::move(capture);
        d_->processor = processor;
        auto dev = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.id == target.id; });
        d_->routeDeviceGain = processor->gain();
        d_->routeChannelGains.clear();
        if (dev != devices.end())
            for (const auto& c : dev->channels)
                d_->routeChannelGains.push_back(c.gain);
        d_->appDesired.clear();
        for (const auto& [id, devId] : d_->sessionDevice)
            if (devId == source)
                d_->appDesired[id] = d_->sessions[id].volume;
        {
            std::lock_guard lock(dataMutex_);
            routeTarget_ = target.id;
            routeSource_ = source;
        }
        applyRouteGains();
        refresh();
        return true;
    });
    if (!ok && error)
        *error = err.empty() ? std::string(LM_TRANSLATE_NOOP("backend", "The audio backend is not running.")) : err;
    return ok;
}

} // namespace loudmixer
