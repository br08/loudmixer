#include "MockBackend.h"

#include "core/GainProcessor.h"

#include <algorithm>

namespace loudmixer {

namespace {
AudioDevice makeStereo(std::string id, std::string desc, bool isDefault)
{
    AudioDevice d;
    d.id = id;
    d.name = std::move(id);
    d.description = std::move(desc);
    d.isDefault = isDefault;
    d.channels = {{0, "Front Left", 1.0}, {1, "Front Right", 1.0}};
    return d;
}
} // namespace

MockBackend::MockBackend(bool withDemoData)
{
    if (!withDemoData)
        return;
    devices_.push_back(makeStereo("mock.speakers", "Alto-falantes (Mock)", true));
    devices_.push_back(makeStereo("mock.headphones", "Fone de ouvido (Mock)", false));
    sessions_.push_back({"1", "Firefox", "firefox", "mock.speakers", 1001, false, 1.0});
    sessions_.push_back({"2", "Spotify", "spotify-client", "mock.speakers", 1002, false, 0.8});
    sessions_.push_back({"3", "Discord", "discord", "mock.headphones", 1003, false, 1.2});
}

bool MockBackend::start(std::string*)
{
    std::lock_guard lock(mutex_);
    started_ = true;
    return true;
}

void MockBackend::stop()
{
    std::lock_guard lock(mutex_);
    started_ = false;
    callback_ = nullptr;
}

bool MockBackend::started() const
{
    std::lock_guard lock(mutex_);
    return started_;
}

std::vector<AudioDevice> MockBackend::listDevices() const
{
    std::lock_guard lock(mutex_);
    auto out = devices_;
    for (auto& d : out) {
        auto it = processors_.find(d.id);
        if (it != processors_.end() && it->second)
            d.gain = it->second->gain();
    }
    return out;
}

std::vector<AudioSession> MockBackend::listSessions() const
{
    std::lock_guard lock(mutex_);
    return sessions_;
}

std::vector<ChannelInfo> MockBackend::listChannels(const std::string& deviceId) const
{
    std::lock_guard lock(mutex_);
    for (const auto& d : devices_)
        if (d.id == deviceId)
            return d.channels;
    return {};
}

void MockBackend::rescaleDevice(AudioDevice& d, double newMaster)
{
    double oldMax = 0.0;
    for (const auto& c : d.channels)
        oldMax = std::max(oldMax, c.gain);
    for (auto& c : d.channels)
        c.gain = oldMax > 0.0 ? clampGain(c.gain / oldMax * newMaster) : newMaster;
    d.gain = newMaster;
}

bool MockBackend::setGain(const Target& target, double gain)
{
    gain = clampGain(gain);
    ChangeKind kind{};
    {
        std::lock_guard lock(mutex_);
        if (target.kind == TargetKind::Application) {
            auto it = std::find_if(sessions_.begin(), sessions_.end(), [&](auto& s) { return s.id == target.id; });
            if (it == sessions_.end())
                return false;
            it->gain = gain;
            kind = ChangeKind::SessionChanged;
        } else {
            auto it = std::find_if(devices_.begin(), devices_.end(), [&](auto& d) { return d.id == target.id; });
            if (it == devices_.end())
                return false;
            if (target.kind == TargetKind::Device) {
                auto proc = processors_.find(target.id);
                if (proc != processors_.end() && proc->second)
                    proc->second->setGain(static_cast<float>(gain));
                else
                    rescaleDevice(*it, gain);
            } else {
                if (target.channel < 0 || target.channel >= static_cast<int>(it->channels.size()))
                    return false;
                it->channels[static_cast<std::size_t>(target.channel)].gain = gain;
                double m = 0.0;
                for (const auto& c : it->channels)
                    m = std::max(m, c.gain);
                it->gain = m;
            }
            kind = ChangeKind::DeviceChanged;
        }
    }
    emit(kind, target.id);
    return true;
}

bool MockBackend::setMuted(const Target& target, bool muted)
{
    ChangeKind kind{};
    {
        std::lock_guard lock(mutex_);
        if (target.kind == TargetKind::Application) {
            auto it = std::find_if(sessions_.begin(), sessions_.end(), [&](auto& s) { return s.id == target.id; });
            if (it == sessions_.end())
                return false;
            it->muted = muted;
            kind = ChangeKind::SessionChanged;
        } else {
            auto it = std::find_if(devices_.begin(), devices_.end(), [&](auto& d) { return d.id == target.id; });
            if (it == devices_.end())
                return false;
            it->muted = muted;
            kind = ChangeKind::DeviceChanged;
        }
    }
    emit(kind, target.id);
    return true;
}

double MockBackend::maxGain(const Target&) const { return kMaxGain; }

void MockBackend::subscribeToChanges(ChangeCallback callback)
{
    std::lock_guard lock(mutex_);
    callback_ = std::move(callback);
}

bool MockBackend::attachProcessor(const Target& target, std::shared_ptr<GainProcessor> processor, std::string* error)
{
    if (target.kind != TargetKind::Device) {
        if (error)
            *error = LM_TRANSLATE_NOOP("backend", "The boost route can only be applied to devices.");
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        auto it = std::find_if(devices_.begin(), devices_.end(), [&](auto& d) { return d.id == target.id; });
        if (it == devices_.end()) {
            if (error)
                *error = LM_TRANSLATE_NOOP("backend", "Device not found.");
            return false;
        }
        if (processor)
            processors_[target.id] = std::move(processor);
        else
            processors_.erase(target.id);
    }
    emit(ChangeKind::DeviceChanged, target.id);
    return true;
}

std::shared_ptr<GainProcessor> MockBackend::processorFor(const std::string& deviceId) const
{
    std::lock_guard lock(mutex_);
    auto it = processors_.find(deviceId);
    return it == processors_.end() ? nullptr : it->second;
}

void MockBackend::addDevice(const AudioDevice& device)
{
    {
        std::lock_guard lock(mutex_);
        devices_.push_back(device);
    }
    emit(ChangeKind::DeviceAdded, device.id);
}

void MockBackend::removeDevice(const std::string& id)
{
    {
        std::lock_guard lock(mutex_);
        std::erase_if(devices_, [&](auto& d) { return d.id == id; });
        processors_.erase(id);
    }
    emit(ChangeKind::DeviceRemoved, id);
}

void MockBackend::addSession(const AudioSession& session)
{
    {
        std::lock_guard lock(mutex_);
        sessions_.push_back(session);
    }
    emit(ChangeKind::SessionAdded, session.id);
}

void MockBackend::removeSession(const std::string& id)
{
    {
        std::lock_guard lock(mutex_);
        std::erase_if(sessions_, [&](auto& s) { return s.id == id; });
    }
    emit(ChangeKind::SessionRemoved, id);
}

void MockBackend::setSystemBoost(const std::string& deviceId, bool enabled, ProcessorStats stats)
{
    {
        std::lock_guard lock(mutex_);
        for (auto& d : devices_)
            if (d.id == deviceId)
                d.systemBoost = enabled;
        if (enabled)
            systemStats_[deviceId] = stats;
        else
            systemStats_.erase(deviceId);
    }
    emit(ChangeKind::DeviceChanged, deviceId);
}

std::optional<ProcessorStats> MockBackend::takeDeviceStats(const std::string& deviceId)
{
    std::lock_guard lock(mutex_);
    auto it = systemStats_.find(deviceId);
    if (it == systemStats_.end())
        return std::nullopt;
    ProcessorStats out = it->second;
    it->second = {}; // como o APO: a leitura zera
    return out;
}

void MockBackend::setDefaultDevice(const std::string& id)
{
    {
        std::lock_guard lock(mutex_);
        for (auto& d : devices_)
            d.isDefault = (d.id == id);
    }
    emit(ChangeKind::DefaultDeviceChanged, id);
}

void MockBackend::emit(ChangeKind kind, const std::string& id)
{
    ChangeCallback cb;
    {
        std::lock_guard lock(mutex_);
        if (!started_)
            return;
        cb = callback_;
    }
    if (cb)
        cb(ChangeEvent{kind, id});
}

} // namespace loudmixer
