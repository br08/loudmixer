#pragma once

// Backend em memória, sem áudio real. Usado nos testes e para desenvolver a
// UI sem servidor de áudio (`loudmixer --backend=mock`).

#include "core/IAudioBackend.h"

#include <map>
#include <mutex>

namespace loudmixer {

class MockBackend : public IAudioBackend {
public:
    // Se `withDemoData`, já começa com dois dispositivos e três aplicativos.
    explicit MockBackend(bool withDemoData = true);

    std::string name() const override { return "Mock"; }
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

    // --- Utilidades para testes (simulam hot-plug e apps abrindo/fechando) ---
    void addDevice(const AudioDevice& device);
    void removeDevice(const std::string& id);
    void addSession(const AudioSession& session);
    void removeSession(const std::string& id);
    void setDefaultDevice(const std::string& id);
    std::shared_ptr<GainProcessor> processorFor(const std::string& deviceId) const;
    bool started() const;

private:
    void emit(ChangeKind kind, const std::string& id);
    static void rescaleDevice(AudioDevice& d, double newMaster);

    mutable std::mutex mutex_;
    bool started_ = false;
    std::vector<AudioDevice> devices_;
    std::vector<AudioSession> sessions_;
    std::map<std::string, std::shared_ptr<GainProcessor>> processors_;
    ChangeCallback callback_;
};

} // namespace loudmixer
