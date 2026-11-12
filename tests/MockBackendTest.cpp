// Testes do núcleo Qt (AudioSessionManager, PresetStore, AutoStartService)
// usando o MockBackend no lugar de um servidor de áudio real.

#include "backend/mock/MockBackend.h"
#include "core/AudioSessionManager.h"
#include "core/AutoStartService.h"
#include "core/PresetStore.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace loudmixer;

namespace {

// Processa eventos até `pred` ser verdadeiro (ou estourar o tempo).
template <typename Pred>
bool waitFor(Pred pred, int timeoutMs = 1000)
{
    QElapsedTimer t;
    t.start();
    while (!pred() && t.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return pred();
}

struct Fixture : ::testing::Test {
    void SetUp() override
    {
        auto backend = std::make_unique<MockBackend>(true);
        mock = backend.get();
        manager = std::make_unique<AudioSessionManager>(std::move(backend));
        ASSERT_TRUE(manager->start());
    }
    MockBackend* mock = nullptr;
    std::unique_ptr<AudioSessionManager> manager;
};

} // namespace

TEST_F(Fixture, StartLoadsInitialSnapshot)
{
    EXPECT_EQ(manager->devices().size(), 2u);
    EXPECT_EQ(manager->sessions().size(), 3u);
    EXPECT_EQ(manager->defaultDeviceId(), "mock.speakers");
    EXPECT_EQ(manager->backendName(), "Mock");
}

TEST_F(Fixture, SetGainUpdatesBackendAndSnapshot)
{
    ASSERT_TRUE(manager->setGain(Target::app("2"), 1.6));
    EXPECT_DOUBLE_EQ(manager->session("2")->gain, 1.6);
    EXPECT_DOUBLE_EQ(mock->listSessions()[1].gain, 1.6);

    // Valores fora da faixa são limitados.
    ASSERT_TRUE(manager->setGain(Target::app("2"), 9.0));
    EXPECT_DOUBLE_EQ(manager->session("2")->gain, kMaxGain);
}

TEST_F(Fixture, DeviceGainPreservesChannelBalance)
{
    ASSERT_TRUE(manager->setGain(Target::channelOf("mock.speakers", 0), 0.5));
    ASSERT_TRUE(waitFor([&] { return manager->device("mock.speakers")->channels[0].gain == 0.5; }));
    // Mestre = maior canal (1.0). Dobrar o mestre dobra os dois canais.
    ASSERT_TRUE(manager->setGain(Target::device("mock.speakers"), 2.0));
    ASSERT_TRUE(waitFor([&] { return manager->device("mock.speakers")->channels[1].gain == 2.0; }));
    EXPECT_DOUBLE_EQ(manager->device("mock.speakers")->channels[0].gain, 1.0);
}

TEST_F(Fixture, BackendEventsAreCoalescedIntoSignals)
{
    int sessionsSignals = 0;
    int devicesSignals = 0;
    QObject::connect(manager.get(), &AudioSessionManager::sessionsChanged, [&] { ++sessionsSignals; });
    QObject::connect(manager.get(), &AudioSessionManager::devicesChanged, [&] { ++devicesSignals; });

    mock->addSession({"10", "VLC", "vlc", "mock.speakers", 42, false, 1.0});
    mock->addSession({"11", "mpv", "mpv", "mock.speakers", 43, false, 1.0});
    mock->removeSession("1");

    ASSERT_TRUE(waitFor([&] { return manager->sessions().size() == 4; }));
    EXPECT_EQ(sessionsSignals, 1); // três eventos, um único sinal
    EXPECT_EQ(devicesSignals, 0);
    EXPECT_FALSE(manager->session("1").has_value());
    EXPECT_TRUE(manager->session("11").has_value());
}

TEST_F(Fixture, HotplugDeviceRemovalDropsBoostRoute)
{
    QString err;
    ASSERT_TRUE(manager->setBoostRoute("mock.headphones", true, &err)) << err.toStdString();
    EXPECT_TRUE(manager->boostRouteActive("mock.headphones"));

    bool routeDropped = false;
    QObject::connect(manager.get(), &AudioSessionManager::boostRouteChanged,
                     [&](const QString& id, bool active) { routeDropped = (id == "mock.headphones" && !active); });
    mock->removeDevice("mock.headphones");
    ASSERT_TRUE(waitFor([&] { return manager->devices().size() == 1; }));
    EXPECT_TRUE(routeDropped);
    EXPECT_FALSE(manager->boostRouteActive("mock.headphones"));
}

TEST_F(Fixture, BoostRouteDrivesProcessorGain)
{
    ASSERT_TRUE(manager->setGain(Target::device("mock.speakers"), 0.8));
    ASSERT_TRUE(manager->setBoostRoute("mock.speakers", true));
    auto proc = mock->processorFor("mock.speakers");
    ASSERT_NE(proc, nullptr);
    // Ativar a rota não muda o volume percebido.
    EXPECT_FLOAT_EQ(proc->gain(), 0.8f);

    ASSERT_TRUE(manager->setGain(Target::device("mock.speakers"), 1.9));
    EXPECT_FLOAT_EQ(proc->gain(), 1.9f);

    manager->setClipCurve(ClipCurve::Cubic);
    EXPECT_EQ(proc->curve(), ClipCurve::Cubic);

    // Medição chega à UI pelo manager.
    std::vector<float> loud(64, 0.9f);
    proc->process(loud.data(), 64, 1);
    auto stats = manager->takeStats("mock.speakers");
    ASSERT_TRUE(stats.has_value());
    EXPECT_GT(stats->clippedSamples, 0u);

    ASSERT_TRUE(manager->setBoostRoute("mock.speakers", false));
    EXPECT_EQ(mock->processorFor("mock.speakers"), nullptr);
    EXPECT_FALSE(manager->takeStats("mock.speakers").has_value());
}

TEST_F(Fixture, MuteAllRestoresPreviousState)
{
    ASSERT_TRUE(manager->setMuted(Target::device("mock.headphones"), true));
    manager->setAllMuted(true);
    EXPECT_TRUE(manager->device("mock.speakers")->muted);
    EXPECT_TRUE(manager->device("mock.headphones")->muted);

    manager->setAllMuted(false);
    EXPECT_FALSE(manager->device("mock.speakers")->muted);
    // Estava mudo antes do "silenciar tudo": continua mudo.
    EXPECT_TRUE(manager->device("mock.headphones")->muted);
}

TEST_F(Fixture, ResetAllGainsReturnsEverythingToUnity)
{
    manager->setGain(Target::app("1"), 1.7);
    manager->setGain(Target::channelOf("mock.speakers", 1), 0.3);
    manager->setGain(Target::device("mock.headphones"), 1.5);
    manager->resetAllGains();
    waitFor([] { return false; }, 100);
    for (const auto& s : manager->sessions())
        EXPECT_DOUBLE_EQ(s.gain, 1.0) << s.appName;
    for (const auto& d : manager->devices()) {
        EXPECT_DOUBLE_EQ(d.gain, 1.0) << d.id;
        for (const auto& c : d.channels)
            EXPECT_DOUBLE_EQ(c.gain, 1.0) << d.id << "/" << c.name;
    }
}

TEST_F(Fixture, NudgeAdjustsDefaultDevice)
{
    manager->nudgeDefaultDevice(+0.05);
    EXPECT_NEAR(manager->device("mock.speakers")->gain, 1.05, 1e-9);
    manager->nudgeDefaultDevice(-0.5);
    EXPECT_NEAR(manager->device("mock.speakers")->gain, 0.55, 1e-9);
    manager->toggleMuteDefaultDevice();
    EXPECT_TRUE(manager->device("mock.speakers")->muted);
}

TEST_F(Fixture, PresetRoundTripThroughManager)
{
    manager->setGain(Target::app("2"), 1.4);          // Spotify
    manager->setGain(Target::channelOf("mock.speakers", 0), 0.6);
    waitFor([] { return false; }, 60);
    const Preset night = manager->capturePreset("Filme à noite");

    manager->resetAllGains();
    waitFor([] { return false; }, 60);
    // Um novo Spotify (id diferente) também recebe o ganho: o preset usa o nome do app.
    mock->addSession({"99", "Spotify", "spotify", "mock.speakers", 7, false, 1.0});
    ASSERT_TRUE(waitFor([&] { return manager->session("99").has_value(); }));

    manager->applyPreset(night);
    waitFor([] { return false; }, 60);
    EXPECT_DOUBLE_EQ(manager->session("2")->gain, 1.4);
    EXPECT_DOUBLE_EQ(manager->session("99")->gain, 1.4);
    EXPECT_DOUBLE_EQ(manager->device("mock.speakers")->channels[0].gain, 0.6);
    EXPECT_DOUBLE_EQ(manager->device("mock.speakers")->channels[1].gain, 1.0);
}

TEST_F(Fixture, PresetAppliesMasterGainWhenBoostRouteIsActive)
{
    ASSERT_TRUE(manager->setBoostRoute("mock.speakers", true));
    Preset p = manager->capturePreset("x");
    p.deviceGains["mock.speakers"] = 1.8;
    manager->applyPreset(p);
    EXPECT_FLOAT_EQ(mock->processorFor("mock.speakers")->gain(), 1.8f);
}

// --- PresetStore -------------------------------------------------------------

TEST(PresetStore, SaveLoadRemove)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath("presets.ini");

    Preset p;
    p.name = "Reunião";
    p.appGains = {{"Zoom", 1.5}, {"Spotify", 0.2}};
    p.deviceGains = {{"alsa_output.usb", 1.25}};
    p.channelGains = {{"alsa_output.usb", {1.0, 0.75}}};
    {
        PresetStore store(std::make_unique<QSettings>(file, QSettings::IniFormat));
        store.save(p);
        Preset other;
        other.name = "Filme/noite"; // barras não podem quebrar a chave
        other.deviceGains = {{"x", 2.0}};
        store.save(other);
    }
    PresetStore store(std::make_unique<QSettings>(file, QSettings::IniFormat));
    EXPECT_EQ(store.names(), QStringList({"Reunião", "Filme/noite"}));
    auto loaded = store.load("Reunião");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, p);
    EXPECT_DOUBLE_EQ(store.load("Filme/noite")->deviceGains.value("x"), 2.0);

    // Sobrescrever mantém a ordem e a quantidade.
    p.appGains["Zoom"] = 1.9;
    store.save(p);
    EXPECT_EQ(store.names().size(), 2);
    EXPECT_DOUBLE_EQ(store.load("Reunião")->appGains.value("Zoom"), 1.9);

    EXPECT_TRUE(store.remove("Reunião"));
    EXPECT_FALSE(store.remove("Reunião"));
    EXPECT_EQ(store.names(), QStringList({"Filme/noite"}));
    EXPECT_FALSE(store.load("Reunião").has_value());
}

TEST(PresetStore, EmptyNameIsIgnored)
{
    QTemporaryDir dir;
    PresetStore store(std::make_unique<QSettings>(dir.filePath("p.ini"), QSettings::IniFormat));
    store.save(Preset{});
    EXPECT_TRUE(store.names().isEmpty());
}

// --- AutoStartService --------------------------------------------------------

#ifndef Q_OS_WIN
TEST(AutoStartService, WritesAndRemovesDesktopEntry)
{
    QTemporaryDir dir;
    AutoStartService svc("/opt/Loud Mixer/loud\"mixer", dir.filePath("autostart"));
    EXPECT_TRUE(svc.isSupported());
    EXPECT_FALSE(svc.isEnabled());

    QString err;
    ASSERT_TRUE(svc.setEnabled(true, &err)) << err.toStdString();
    EXPECT_TRUE(svc.isEnabled());
    QFile f(svc.desktopEntryPath());
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    EXPECT_TRUE(content.contains("Exec=\"/opt/Loud Mixer/loud\\\"mixer\" --minimized")) << content.toStdString();
    EXPECT_TRUE(content.startsWith("[Desktop Entry]"));
    f.close();

    ASSERT_TRUE(svc.setEnabled(false, &err));
    EXPECT_FALSE(svc.isEnabled());
    EXPECT_TRUE(svc.setEnabled(false, &err)); // idempotente
}
#endif

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
