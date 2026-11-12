// Teste de integração contra o servidor de áudio REAL (PulseAudio ou
// pipewire-pulse). Altera volumes temporariamente e os restaura no final,
// por isso só roda com LOUDMIXER_LIVE_TESTS=1.

#include "backend/linux/PulseBackend.h"
#include "core/GainProcessor.h"

#include <gtest/gtest.h>

#include <pulse/simple.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

using namespace loudmixer;
using namespace std::chrono_literals;

namespace {

bool liveEnabled()
{
    const char* v = std::getenv("LOUDMIXER_LIVE_TESTS");
    return v && std::string(v) == "1";
}

template <typename Pred>
bool eventually(Pred pred, std::chrono::milliseconds timeout = 3000ms)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (pred())
            return true;
        std::this_thread::sleep_for(20ms);
    }
    return pred();
}

std::optional<AudioDevice> findDevice(const PulseBackend& b, const std::string& id)
{
    for (auto& d : b.listDevices())
        if (d.id == id)
            return d;
    return std::nullopt;
}

struct LiveFixture : ::testing::Test {
    void SetUp() override
    {
        if (!liveEnabled())
            GTEST_SKIP() << "defina LOUDMIXER_LIVE_TESTS=1 para rodar contra o servidor real";
        backend.subscribeToChanges([this](const ChangeEvent&) { ++events; });
        std::string err;
        ASSERT_TRUE(backend.start(&err)) << err;
        auto devices = backend.listDevices();
        ASSERT_FALSE(devices.empty()) << "nenhum sink disponível";
        device = devices.front();
        for (auto& d : devices)
            if (d.isDefault)
                device = d;
    }
    void TearDown() override
    {
        if (!liveEnabled())
            return;
        // Restaura o estado original do dispositivo.
        backend.attachProcessor(Target::device(device.id), nullptr, nullptr);
        for (const auto& c : device.channels)
            backend.setGain(Target::channelOf(device.id, c.index), c.gain);
        backend.setMuted(Target::device(device.id), device.muted);
        std::this_thread::sleep_for(200ms);
        backend.stop();
    }

    PulseBackend backend;
    AudioDevice device;
    std::atomic<int> events{0};
};

} // namespace

TEST_F(LiveFixture, ConnectsAndListsDevices)
{
    std::cout << "servidor: " << backend.name() << "\n";
    for (const auto& d : backend.listDevices()) {
        std::cout << (d.isDefault ? " * " : "   ") << d.id << " (" << d.description << ") gain=" << d.gain
                  << " ch=" << d.channels.size() << "\n";
        EXPECT_FALSE(d.channels.empty());
    }
    EXPECT_FALSE(backend.name().empty());
}

TEST_F(LiveFixture, DeviceGainAbove100PercentRoundTrips)
{
    ASSERT_TRUE(backend.setGain(Target::device(device.id), 1.5));
    EXPECT_TRUE(eventually([&] { return std::fabs(findDevice(backend, device.id)->gain - 1.5) < 0.01; }));
    EXPECT_GT(events.load(), 0);

    ASSERT_TRUE(backend.setGain(Target::channelOf(device.id, 0), 0.5));
    EXPECT_TRUE(eventually([&] { return std::fabs(findDevice(backend, device.id)->channels[0].gain - 0.5) < 0.01; }));

    ASSERT_TRUE(backend.setMuted(Target::device(device.id), true));
    EXPECT_TRUE(eventually([&] { return findDevice(backend, device.id)->muted; }));
}

TEST_F(LiveFixture, DetectsNewAppSessionAndBoostsIt)
{
    // Abre um stream de reprodução "de outro app" com a API simples.
    pa_sample_spec ss{PA_SAMPLE_FLOAT32NE, 48000, 2};
    int err = 0;
    pa_simple* app = pa_simple_new(nullptr, "LiveTestApp", PA_STREAM_PLAYBACK, nullptr, "test tone", &ss, nullptr, nullptr, &err);
    ASSERT_NE(app, nullptr) << pa_strerror(err);
    std::atomic<bool> playing{true};
    std::thread player([&] {
        std::vector<float> silence(480 * 2, 0.0f);
        while (playing)
            pa_simple_write(app, silence.data(), silence.size() * sizeof(float), nullptr);
    });

    std::string sessionId;
    ASSERT_TRUE(eventually([&] {
        for (auto& s : backend.listSessions())
            if (s.appName == "LiveTestApp")
                sessionId = s.id;
        return !sessionId.empty();
    }));

    ASSERT_TRUE(backend.setGain(Target::app(sessionId), 2.0));
    EXPECT_TRUE(eventually([&] {
        for (auto& s : backend.listSessions())
            if (s.id == sessionId)
                return std::fabs(s.gain - 2.0) < 0.01;
        return false;
    }));

    playing = false;
    player.join();
    pa_simple_free(app);
    EXPECT_TRUE(eventually([&] {
        for (auto& s : backend.listSessions())
            if (s.id == sessionId)
                return false;
        return true;
    }));
}

TEST_F(LiveFixture, BoostRouteProcessesAudioWithSoftClip)
{
    // Silencia o dispositivo físico: o DSP roda antes dele, então a medição
    // funciona sem tocar um bipe alto nas caixas de quem roda o teste.
    ASSERT_TRUE(backend.setMuted(Target::device(device.id), true));

    auto proc = std::make_shared<GainProcessor>(2.0f, ClipCurve::Tanh);
    std::string err;
    ASSERT_TRUE(backend.attachProcessor(Target::device(device.id), proc, &err)) << err;

    // O sink virtual não aparece como dispositivo, e o ganho reportado é o do DSP.
    for (auto& d : backend.listDevices())
        EXPECT_EQ(d.id.rfind("loudmixer_boost_", 0), std::string::npos);
    EXPECT_NEAR(findDevice(backend, device.id)->gain, 2.0, 1e-6);
    ASSERT_TRUE(backend.setGain(Target::device(device.id), 1.8));
    EXPECT_FLOAT_EQ(proc->gain(), 1.8f);

    // Um app tocando no sink padrão (agora o virtual) passa pelo processador.
    pa_sample_spec ss{PA_SAMPLE_FLOAT32NE, 48000, 2};
    int perr = 0;
    pa_simple* app = pa_simple_new(nullptr, "LiveTestLoud", PA_STREAM_PLAYBACK, nullptr, "loud tone", &ss, nullptr, nullptr, &perr);
    ASSERT_NE(app, nullptr) << pa_strerror(perr);
    std::vector<float> tone(480 * 2);
    for (std::size_t i = 0; i < 480; ++i)
        tone[2 * i] = tone[2 * i + 1] = 0.9f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);

    (void)proc->takeStats();
    for (int i = 0; i < 50; ++i) // ~0.5 s
        pa_simple_write(app, tone.data(), tone.size() * sizeof(float), nullptr);
    pa_simple_drain(app, nullptr);

    ProcessorStats total;
    EXPECT_TRUE(eventually([&] {
        auto s = proc->takeStats();
        total.processedSamples += s.processedSamples;
        total.clippedSamples += s.clippedSamples;
        total.peakOut = std::max(total.peakOut, s.peakOut);
        return total.clippedSamples > 0;
    }));
    pa_simple_free(app);

    std::cout << "processadas=" << total.processedSamples << " clip=" << total.clippedSamples
              << " picoSaída=" << total.peakOut << "\n";
    EXPECT_GT(total.processedSamples, 0u);
    EXPECT_LE(total.peakOut, 1.0f);

    ASSERT_TRUE(backend.attachProcessor(Target::device(device.id), nullptr, &err)) << err;
    EXPECT_TRUE(eventually([&] { return findDevice(backend, device.id)->isDefault == device.isDefault; }));
}
