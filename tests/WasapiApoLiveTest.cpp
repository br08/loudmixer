// Teste ao vivo da rota APO no Windows: WasapiBackend real contra o APO do
// LoudMixer instalado num dispositivo (loudmixer-apo-setup install ...).
//
// Pula sozinho sem LOUDMIXER_LIVE_TESTS=1 ou sem dispositivo com o APO.
// Mexe nos volumes por alguns segundos e restaura tudo no final.

#include "apo/ApoRegistry.h"
#include "apo/ApoShared.h"
#include "backend/windows/ComUtil.h"
#include "backend/windows/WasapiBackend.h"

#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace loudmixer;

namespace {

template <typename Pred>
bool waitFor(Pred pred, int timeoutMs = 4000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

// Toca silêncio no dispositivo numa thread própria: faz o Windows carregar o
// APO e cria uma sessão de áudio deste processo.
class SilentStream {
public:
    explicit SilentStream(const std::string& deviceId)
        : thread_([this, deviceId] { run(deviceId); })
    {
    }
    ~SilentStream()
    {
        stop_ = true;
        thread_.join();
    }
    bool started() const { return started_; }

private:
    void run(const std::string& deviceId)
    {
        win::ComApartment com;
        win::ComPtr<IMMDeviceEnumerator> en;
        win::ComPtr<IMMDevice> dev;
        win::ComPtr<IAudioClient> client;
        win::ComPtr<IAudioRenderClient> render;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    en.putVoid()))
            || FAILED(en->GetDevice(win::toWide(deviceId).c_str(), dev.put()))
            || FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.putVoid())))
            return;
        WAVEFORMATEX* fmt = nullptr;
        if (FAILED(client->GetMixFormat(&fmt)))
            return;
        const WORD align = fmt->nBlockAlign;
        const HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, fmt, nullptr);
        CoTaskMemFree(fmt);
        UINT32 frames = 0;
        if (FAILED(hr) || FAILED(client->GetBufferSize(&frames))
            || FAILED(client->GetService(__uuidof(IAudioRenderClient), render.putVoid())))
            return;
        client->Start();
        started_ = true;
        while (!stop_) {
            UINT32 padding = 0;
            client->GetCurrentPadding(&padding);
            BYTE* data = nullptr;
            if (frames > padding && SUCCEEDED(render->GetBuffer(frames - padding, &data))) {
                std::memset(data, 0, static_cast<size_t>(frames - padding) * align);
                render->ReleaseBuffer(frames - padding, 0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        client->Stop();
    }

    std::atomic<bool> stop_{false};
    std::atomic<bool> started_{false};
    std::thread thread_;
};

float nativeMaster(const std::string& deviceId)
{
    win::ComPtr<IMMDeviceEnumerator> en;
    win::ComPtr<IMMDevice> dev;
    win::ComPtr<IAudioEndpointVolume> vol;
    float v = -1.0f;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   en.putVoid()))
        && SUCCEEDED(en->GetDevice(win::toWide(deviceId).c_str(), dev.put()))
        && SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, vol.putVoid())))
        vol->GetMasterVolumeLevelScalar(&v);
    return v;
}

} // namespace

TEST(WasapiApoLive, DeviceChannelAndAppGainsReachTheApo)
{
    if (!std::getenv("LOUDMIXER_LIVE_TESTS"))
        GTEST_SKIP() << "defina LOUDMIXER_LIVE_TESTS=1 para rodar contra o áudio real";
    win::ComApartment com;

    WasapiBackend backend;
    std::string err;
    ASSERT_TRUE(backend.start(&err)) << err;

    AudioDevice dev;
    for (const auto& d : backend.listDevices())
        if (d.systemBoost && (dev.id.empty() || d.isDefault))
            dev = d;
    if (dev.id.empty()) {
        backend.stop();
        GTEST_SKIP() << "nenhum dispositivo com o APO do LoudMixer (rode loudmixer-apo-setup install default)";
    }
    const std::wstring guid = apo::normalizeEndpointGuid(win::toWide(dev.id));
    const double originalDevice = dev.gain;
    const double originalCh0 = dev.channels.empty() ? 1.0 : dev.channels[0].gain;

    SilentStream stream(dev.id);
    ASSERT_TRUE(waitFor([&] { return stream.started(); })) << "não abriu stream no dispositivo";

    // A sessão deste processo aparece no backend (varredura de 750 ms).
    std::string sessionId;
    ASSERT_TRUE(waitFor([&] {
        for (const auto& s : backend.listSessions())
            if (s.processId == GetCurrentProcessId() && s.deviceId == dev.id)
                sessionId = s.id;
        return !sessionId.empty();
    })) << "sessão do teste não apareceu";

    apo::SharedView view;
    ASSERT_TRUE(waitFor([&] { return view.open(guid); })) << "APO não carregou no dispositivo";
    auto* sh = view.get();

    EXPECT_DOUBLE_EQ(backend.maxGain(Target::device(dev.id)), kMaxGain);
    EXPECT_DOUBLE_EQ(backend.maxGain(Target::app(sessionId)), kMaxGain);

    // Dispositivo 150%: 100% no volume do Windows, 1.5 no APO.
    ASSERT_TRUE(backend.setGain(Target::device(dev.id), 1.5));
    EXPECT_NEAR(sh->gain.load(), 1.5f, 1e-4f);
    EXPECT_NEAR(nativeMaster(dev.id), 1.0f, 1e-3f);

    // Canal 0 em 120%.
    if (!dev.channels.empty()) {
        ASSERT_TRUE(backend.setGain(Target::channelOf(dev.id, 0), 1.2));
        EXPECT_NEAR(sh->channelGains[0].load(), 1.2f, 1e-4f);
    }

    // App em 180%: o APO sobe por B = 1.8 (limitado a 200% no total).
    ASSERT_TRUE(backend.setGain(Target::app(sessionId), 1.8));
    EXPECT_NEAR(sh->gain.load(), 2.0f, 1e-4f); // 1.5 × 1.8 = 2.7 -> 2.0

    // O snapshot mostra os valores efetivos depois da próxima varredura.
    EXPECT_TRUE(waitFor([&] {
        for (const auto& s : backend.listSessions())
            if (s.id == sessionId)
                return std::abs(s.gain - 1.8) < 0.02;
        return false;
    }));
    EXPECT_TRUE(waitFor([&] {
        for (const auto& d : backend.listDevices())
            if (d.id == dev.id)
                return std::abs(d.gain - 1.5) < 0.02;
        return false;
    }));

    // Persistido no registro para o APO reaplicar quando for recriado.
    EXPECT_TRUE(waitFor([&] {
        const auto p = apo::readPersisted(guid);
        return p.found && std::abs(p.master - 2.0f) < 0.011f && std::abs(p.appBoost - 1.8f) < 0.011f;
    })) << "estado não foi salvo no registro (a chave aceita escrita de usuários?)";

    // Restaura.
    backend.setGain(Target::app(sessionId), 1.0);
    if (!dev.channels.empty())
        backend.setGain(Target::channelOf(dev.id, 0), originalCh0);
    backend.setGain(Target::device(dev.id), originalDevice);
    EXPECT_NEAR(sh->gain.load(), static_cast<float>(std::max(1.0, originalDevice)), 1e-3f);
    std::this_thread::sleep_for(std::chrono::milliseconds(900)); // deixa a varredura salvar o estado restaurado
    backend.stop();
}
