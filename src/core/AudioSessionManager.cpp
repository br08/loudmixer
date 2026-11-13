#include "AudioSessionManager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>

namespace loudmixer {

QString translateBackendError(const std::string& message)
{
    const auto pos = message.find(" [");
    const std::string head = pos == std::string::npos ? message : message.substr(0, pos);
    QString out = QCoreApplication::translate("backend", head.c_str());
    if (pos != std::string::npos)
        out += QString::fromStdString(message.substr(pos));
    return out;
}

AudioSessionManager::AudioSessionManager(std::unique_ptr<IAudioBackend> backend, QObject* parent)
    : QObject(parent)
    , backend_(std::move(backend))
{
    // Agrupa rajadas de eventos (ex.: arrastar um slider no pavucontrol gera
    // dezenas por segundo) em uma única releitura.
    coalesceTimer_.setSingleShot(true);
    coalesceTimer_.setInterval(25);
    connect(&coalesceTimer_, &QTimer::timeout, this, &AudioSessionManager::flushPending);
}

AudioSessionManager::~AudioSessionManager()
{
    // Um pedido de boost do sistema em andamento usa o backend: espera ele
    // terminar (no pior caso, o usuário ainda está diante do UAC).
    if (systemBoostThread_.joinable())
        systemBoostThread_.join();
    stop();
}

bool AudioSessionManager::start(QString* error)
{
    if (running_)
        return true;
    if (!backend_) {
        if (error)
            *error = tr("No audio backend available.");
        return false;
    }

    // O backend chama este callback na thread dele. QPointer + invokeMethod
    // com contexto garante entrega na thread da UI e descarte seguro caso o
    // manager já tenha sido destruído.
    QPointer<AudioSessionManager> self(this);
    backend_->subscribeToChanges([self](const ChangeEvent& ev) {
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(), [self, ev] { if (self) self->onBackendEvent(ev); }, Qt::QueuedConnection);
    });

    std::string err;
    if (!backend_->start(&err)) {
        if (error)
            *error = translateBackendError(err);
        return false;
    }
    running_ = true;
    devices_ = backend_->listDevices();
    sessions_ = backend_->listSessions();
    emit devicesChanged();
    emit sessionsChanged();
    return true;
}

void AudioSessionManager::stop()
{
    if (!running_)
        return;
    // Desfaz rotas de boost antes de desconectar, para não deixar o áudio
    // do usuário preso num dispositivo virtual.
    for (const auto& id : activeBoostRoutes())
        backend_->attachProcessor(Target::device(id), nullptr, nullptr);
    processors_.clear();
    backend_->stop();
    running_ = false;
}

QString AudioSessionManager::backendName() const
{
    return backend_ ? QString::fromStdString(backend_->name()) : QString();
}

std::optional<AudioDevice> AudioSessionManager::device(const std::string& id) const
{
    for (const auto& d : devices_)
        if (d.id == id)
            return d;
    return std::nullopt;
}

std::optional<AudioSession> AudioSessionManager::session(const std::string& id) const
{
    for (const auto& s : sessions_)
        if (s.id == id)
            return s;
    return std::nullopt;
}

std::string AudioSessionManager::defaultDeviceId() const
{
    for (const auto& d : devices_)
        if (d.isDefault)
            return d.id;
    return devices_.empty() ? std::string() : devices_.front().id;
}

double AudioSessionManager::maxGain(const Target& target) const
{
    return backend_ && running_ ? backend_->maxGain(target) : kMaxGain;
}

bool AudioSessionManager::setGain(const Target& target, double gain)
{
    if (!running_)
        return false;
    gain = std::min(clampGain(gain), maxGain(target));
    const bool ok = backend_->setGain(target, gain);

    // Atualização otimista do snapshot: a UI não "pula" enquanto o evento
    // de confirmação do servidor não chega.
    if (ok && target.kind == TargetKind::Application) {
        for (auto& s : sessions_)
            if (s.id == target.id)
                s.gain = gain;
    } else if (ok) {
        for (auto& d : devices_) {
            if (d.id != target.id)
                continue;
            if (target.kind == TargetKind::Channel && target.channel >= 0
                && target.channel < static_cast<int>(d.channels.size())) {
                d.channels[static_cast<std::size_t>(target.channel)].gain = gain;
            } else if (target.kind == TargetKind::Device) {
                d.gain = gain;
            }
        }
    }
    return ok;
}

bool AudioSessionManager::setMuted(const Target& target, bool muted)
{
    if (!running_)
        return false;
    const bool ok = backend_->setMuted(target, muted);
    if (ok && target.kind == TargetKind::Application) {
        for (auto& s : sessions_)
            if (s.id == target.id)
                s.muted = muted;
    } else if (ok) {
        for (auto& d : devices_)
            if (d.id == target.id)
                d.muted = muted;
    }
    return ok;
}

void AudioSessionManager::resetAllGains()
{
    // Cópias: setGain altera os snapshots.
    const auto devices = devices_;
    const auto sessions = sessions_;
    for (const auto& d : devices) {
        // Zera o balanço primeiro (canal a canal), depois garante o mestre.
        for (const auto& c : d.channels)
            setGain(Target::channelOf(d.id, c.index), kUnityGain);
        setGain(Target::device(d.id), kUnityGain);
    }
    for (const auto& s : sessions)
        setGain(Target::app(s.id), kUnityGain);
}

void AudioSessionManager::setAllMuted(bool muted)
{
    if (muted == allMuted_)
        return;
    if (muted) {
        mutedBeforeMuteAll_.clear();
        for (const auto& d : devices_)
            if (d.muted)
                mutedBeforeMuteAll_.insert(d.id);
        const auto devices = devices_;
        for (const auto& d : devices)
            setMuted(Target::device(d.id), true);
    } else {
        const auto devices = devices_;
        for (const auto& d : devices)
            if (!mutedBeforeMuteAll_.count(d.id))
                setMuted(Target::device(d.id), false);
        mutedBeforeMuteAll_.clear();
    }
    allMuted_ = muted;
}

void AudioSessionManager::nudgeDefaultDevice(double delta)
{
    const std::string id = defaultDeviceId();
    if (auto d = device(id))
        setGain(Target::device(id), d->gain + delta);
}

void AudioSessionManager::toggleMuteDefaultDevice()
{
    const std::string id = defaultDeviceId();
    if (auto d = device(id))
        setMuted(Target::device(id), !d->muted);
}

bool AudioSessionManager::supportsBoostRoute() const
{
    return backend_ && backend_->supportsProcessor(TargetKind::Device);
}

bool AudioSessionManager::setBoostRoute(const std::string& deviceId, bool enabled, QString* error)
{
    if (!running_ || !supportsBoostRoute()) {
        if (error)
            *error = tr("This audio backend does not support DSP boost.");
        return false;
    }
    if (enabled == boostRouteActive(deviceId))
        return true;

    std::string err;
    if (enabled) {
        // O processador começa no ganho atual do dispositivo: ativar a rota
        // não deve mudar o volume percebido.
        const auto dev = device(deviceId);
        const float startGain = dev ? static_cast<float>(dev->gain) : 1.0f;
        auto proc = std::make_shared<GainProcessor>(startGain, curve_);
        if (!backend_->attachProcessor(Target::device(deviceId), proc, &err)) {
            if (error)
                *error = translateBackendError(err);
            return false;
        }
        processors_[deviceId] = std::move(proc);
    } else {
        if (!backend_->attachProcessor(Target::device(deviceId), nullptr, &err)) {
            if (error)
                *error = translateBackendError(err);
            return false;
        }
        processors_.erase(deviceId);
    }
    emit boostRouteChanged(QString::fromStdString(deviceId), enabled);
    refresh();
    return true;
}

bool AudioSessionManager::boostRouteActive(const std::string& deviceId) const
{
    return processors_.count(deviceId) > 0;
}

std::vector<std::string> AudioSessionManager::activeBoostRoutes() const
{
    std::vector<std::string> ids;
    for (const auto& [id, proc] : processors_)
        ids.push_back(id);
    return ids;
}

std::optional<ProcessorStats> AudioSessionManager::takeStats(const std::string& deviceId)
{
    auto it = processors_.find(deviceId);
    if (it != processors_.end())
        return it->second->takeStats();
    return backend_ && running_ ? backend_->takeDeviceStats(deviceId) : std::nullopt;
}

bool AudioSessionManager::requestSystemBoost(const std::string& deviceId, bool enabled)
{
    if (!running_ || !systemBoostPending_.empty())
        return false;
    if (systemBoostThread_.joinable())
        systemBoostThread_.join(); // pedido anterior já terminou
    systemBoostPending_ = deviceId;
    emit systemBoostPendingChanged();
    QPointer<AudioSessionManager> self(this);
    IAudioBackend* backend = backend_.get();
    systemBoostThread_ = std::thread([self, backend, deviceId, enabled] {
        const SystemBoostResult r = backend->setSystemBoost(deviceId, enabled);
        QMetaObject::invokeMethod(
            self,
            [self, deviceId, enabled, r] {
                if (!self)
                    return;
                self->systemBoostPending_.clear();
                self->refresh();
                emit self->systemBoostPendingChanged();
                emit self->systemBoostFinished(QString::fromStdString(deviceId), enabled, static_cast<int>(r.code),
                                               QString::fromStdString(r.detail));
            },
            Qt::QueuedConnection);
    });
    return true;
}

void AudioSessionManager::setClipCurve(ClipCurve curve)
{
    curve_ = curve;
    for (auto& [id, proc] : processors_)
        proc->setCurve(curve);
}

Preset AudioSessionManager::capturePreset(const QString& name) const
{
    Preset p;
    p.name = name;
    for (const auto& s : sessions_)
        p.appGains.insert(QString::fromStdString(s.appName), s.gain);
    for (const auto& d : devices_) {
        const QString id = QString::fromStdString(d.id);
        p.deviceGains.insert(id, d.gain);
        QList<double> ch;
        for (const auto& c : d.channels)
            ch.append(c.gain);
        if (!ch.isEmpty())
            p.channelGains.insert(id, ch);
    }
    return p;
}

void AudioSessionManager::applyPreset(const Preset& preset)
{
    const auto devices = devices_;
    for (const auto& d : devices) {
        const QString id = QString::fromStdString(d.id);
        const bool boosted = boostRouteActive(d.id);
        const auto ch = preset.channelGains.constFind(id);
        // Canais definem o mestre (mestre = maior canal), exceto quando há uma
        // rota DSP: aí o mestre é o ganho do processador e precisa ser aplicado.
        if (ch != preset.channelGains.constEnd() && static_cast<std::size_t>(ch->size()) == d.channels.size()) {
            for (int i = 0; i < ch->size(); ++i)
                setGain(Target::channelOf(d.id, i), ch->at(i));
            if (boosted && preset.deviceGains.contains(id))
                setGain(Target::device(d.id), preset.deviceGains.value(id));
        } else if (preset.deviceGains.contains(id)) {
            setGain(Target::device(d.id), preset.deviceGains.value(id));
        }
    }
    const auto sessions = sessions_;
    for (const auto& s : sessions) {
        const auto it = preset.appGains.constFind(QString::fromStdString(s.appName));
        if (it != preset.appGains.constEnd())
            setGain(Target::app(s.id), it.value());
    }
}

void AudioSessionManager::refresh()
{
    devicesDirty_ = sessionsDirty_ = true;
    flushPending();
}

void AudioSessionManager::onBackendEvent(const ChangeEvent& event)
{
    switch (event.kind) {
    case ChangeKind::DeviceRemoved:
        // Dispositivo sumiu (fone desconectado): a rota DSP dele não existe mais.
        if (processors_.erase(event.id))
            emit boostRouteChanged(QString::fromStdString(event.id), false);
        devicesDirty_ = true;
        break;
    case ChangeKind::DeviceAdded:
    case ChangeKind::DeviceChanged:
    case ChangeKind::DefaultDeviceChanged:
        devicesDirty_ = true;
        break;
    case ChangeKind::SessionAdded:
    case ChangeKind::SessionRemoved:
    case ChangeKind::SessionChanged:
        sessionsDirty_ = true;
        break;
    case ChangeKind::BackendReset:
        for (const auto& id : activeBoostRoutes())
            emit boostRouteChanged(QString::fromStdString(id), false);
        processors_.clear();
        devicesDirty_ = sessionsDirty_ = true;
        break;
    }
    if (!coalesceTimer_.isActive())
        coalesceTimer_.start();
}

void AudioSessionManager::flushPending()
{
    if (!running_)
        return;
    if (devicesDirty_) {
        devicesDirty_ = false;
        auto fresh = backend_->listDevices();
        if (fresh != devices_) {
            // Boost do sistema que caiu por fora (não por pedido do usuário).
            std::vector<std::string> lost;
            for (const auto& d : fresh) {
                const auto old = std::find_if(devices_.begin(), devices_.end(), [&](const auto& o) { return o.id == d.id; });
                if (old != devices_.end() && old->systemBoostState == SystemBoostState::Active
                    && (d.systemBoostState == SystemBoostState::EnhancementsOff
                        || d.systemBoostState == SystemBoostState::RemovedExternally)
                    && systemBoostPending_ != d.id)
                    lost.push_back(d.id);
            }
            devices_ = std::move(fresh);
            emit devicesChanged();
            for (const auto& id : lost)
                emit systemBoostLost(QString::fromStdString(id));
        }
    }
    if (sessionsDirty_) {
        sessionsDirty_ = false;
        auto fresh = backend_->listSessions();
        if (fresh != sessions_) {
            sessions_ = std::move(fresh);
            emit sessionsChanged();
        }
    }
}

} // namespace loudmixer
