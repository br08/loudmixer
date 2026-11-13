#pragma once

// Backend Windows sobre WASAPI / Core Audio.
//
// Escopos (ver seção 4 do plano):
//   App         -> ISimpleAudioVolume da sessão (nativo até 100%)
//   Dispositivo -> IAudioEndpointVolume (mestre, nativo até 100%)
//   Canal       -> IAudioEndpointVolume::SetChannelVolumeLevelScalar (até 100%)
//
// Acima de 100% é preciso processar o áudio: com uma rota de boost ativa
// (attachProcessor), o áudio que toca num cabo virtual é capturado em
// loopback, passa pelo GainProcessor e é reinjetado no dispositivo físico
// (LoopbackCapture). Com a rota ativa:
//   - dispositivo: o slider controla o ganho do DSP (0–200%);
//   - canal: até 100% nativo, acima disso ganho por canal no DSP;
//   - app: o slider da sessão vira pré-atenuador. Se algum app pede mais que
//     100%, o DSP sobe pelo maior pedido (B) e cada sessão é atenuada para
//     ganho/B — exatamente a "solução prática v1" do plano.
//
// Boost do sistema (APO do LoudMixer, instalado pelo loudmixer-apo-setup):
// quando o APO está na cadeia de efeitos de um dispositivo, ele passa a ir
// até 200% sem cabo virtual. Ganho efetivo = volume nativo × fator do APO
// (ver ApoRouteMath.h); apps acima de 100% usam o mesmo esquema do B acima,
// com o APO no lugar do DSP. O estado efetivo vai para a memória
// compartilhada do APO na hora e para o registro (em lote), para o APO
// reaplicá-lo quando o Windows o recriar.
//
// Todas as chamadas COM rodam numa thread dedicada (MTA). A UI só vê
// snapshots, atualizados por notificações do IMMDeviceEnumerator e por uma
// varredura periódica (volumes e sessões).

#include "core/IAudioBackend.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace loudmixer {

namespace win {
class LoopbackCapture;
}

class WasapiBackend : public IAudioBackend {
public:
    WasapiBackend();
    ~WasapiBackend() override;

    std::string name() const override { return "WASAPI"; }
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
    std::optional<ProcessorStats> takeDeviceStats(const std::string& deviceId) override;

    // "boostSource": nome (ou parte do nome) do endpoint usado como origem da
    // rota de boost. Vazio = detecta VB-Audio "CABLE Input", VoiceMeeter ou
    // outro dispositivo "Virtual".
    void setOption(const std::string& key, const std::string& value) override;

    struct Impl; // estado COM, definido no .cpp

private:
    // Executa `fn` na thread COM e espera o resultado.
    bool runSync(std::function<bool()> fn);
    void post(std::function<void()> fn);
    void workerLoop(std::string* startError, int* startState);

    void refresh();          // thread COM
    void applyRouteGains();  // thread COM
    void applyApoGains(const std::string& deviceId); // thread COM
    void detachApo(const std::string& deviceId);     // thread COM
    void flushApoState();                            // thread COM: salva no registro
    void notify(ChangeKind kind, const std::string& id);

    std::unique_ptr<Impl> d_;

    std::thread worker_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::function<void()>> queue_;
    bool quit_ = false;

    mutable std::mutex dataMutex_;
    std::vector<AudioDevice> devices_;
    std::vector<AudioSession> sessions_;
    std::string routeTarget_; // dispositivo físico com rota ativa
    std::string routeSource_; // cabo virtual de origem

    std::mutex callbackMutex_;
    ChangeCallback callback_;
};

} // namespace loudmixer
