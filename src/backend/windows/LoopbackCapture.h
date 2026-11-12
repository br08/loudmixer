#pragma once

// Rota de boost no Windows: captura em loopback o que toca num endpoint de
// origem (um cabo virtual, ex.: VB-Audio "CABLE Input"), processa com o
// GainProcessor (ganho até 200% + soft-clip) e reinjeta no endpoint físico.
//
//   apps -> [CABLE Input] --loopback--> GainProcessor --render--> [Alto-falantes]
//
// Esta é a solução v1 descrita no plano ("rotear o áudio por um endpoint
// virtual controlado pelo app"). Um APO assinado fica para a v2.

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace loudmixer {
class GainProcessor;
}

namespace loudmixer::win {

class LoopbackCapture {
public:
    LoopbackCapture() = default;
    ~LoopbackCapture();

    LoopbackCapture(const LoopbackCapture&) = delete;
    LoopbackCapture& operator=(const LoopbackCapture&) = delete;

    // Ids de endpoint (IMMDevice::GetId). Bloqueia até a thread de áudio
    // abrir os dois lados (ou falhar).
    bool start(const std::string& sourceEndpointId,
               const std::string& targetEndpointId,
               std::shared_ptr<GainProcessor> processor,
               std::string* error);
    void stop();
    bool running() const { return running_.load(); }

    const std::string& source() const { return source_; }
    const std::string& target() const { return target_; }

private:
    void run(std::string* error, std::atomic<int>* startState);

    std::string source_;
    std::string target_;
    std::shared_ptr<GainProcessor> processor_;
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
};

} // namespace loudmixer::win
