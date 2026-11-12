// LoudMixer — amplificador de volume por app, dispositivo e canal.

#include "backend/BackendFactory.h"
#include "core/AudioSessionManager.h"
#include "core/AutoStartService.h"
#include "core/PresetStore.h"
#include "ui/AppIcons.h"
#include "ui/GlobalHotkeys.h"
#include "ui/MixerWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/SingleInstance.h"
#include "ui/TrayController.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QTranslator>

#include <iostream>

#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace loudmixer;

namespace {

// Comandos aceitos tanto pela linha de comando quanto pelo socket local.
const QStringList kCommands = {"show", "toggle", "volume-up", "volume-down", "mute", "mute-all", "quit"};

void loadTranslations(QApplication& app)
{
    QString lang = QSettings().value(ui::keys::language).toString();
    const QLocale locale = lang.isEmpty() ? QLocale::system() : QLocale(lang);
    if (locale.language() == QLocale::English)
        return; // strings do código-fonte já estão em inglês

    auto* qtTr = new QTranslator(&app);
    if (qtTr->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(qtTr);
    auto* appTr = new QTranslator(&app);
    if (appTr->load(locale, QStringLiteral("loudmixer"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        app.installTranslator(appTr);
}

#ifdef Q_OS_UNIX
// SIGINT/SIGTERM -> encerramento limpo (desfaz rotas de boost). Usa o truque
// do "self-pipe", pois quase nada é seguro dentro de um signal handler.
int g_signalFd[2] = {-1, -1};

void installSignalHandlers(QObject* parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFd) != 0)
        return;
    auto* notifier = new QSocketNotifier(g_signalFd[1], QSocketNotifier::Read, parent);
    QObject::connect(notifier, &QSocketNotifier::activated, parent, [] {
        char c;
        if (::read(g_signalFd[1], &c, 1) > 0)
            QCoreApplication::quit();
    });
    struct sigaction sa {};
    sa.sa_handler = [](int) {
        const char c = 1;
        (void)!::write(g_signalFd[0], &c, 1);
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}
#endif

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LoudMixer"));
    QApplication::setApplicationName(QStringLiteral("LoudMixer"));
    QApplication::setApplicationVersion(QStringLiteral(LOUDMIXER_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("loudmixer"));
    QApplication::setWindowIcon(ui::appIcon());
    QApplication::setQuitOnLastWindowClosed(false);

    loadTranslations(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Volume booster per app, device and channel"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimized("minimized", QCoreApplication::translate("main", "Start hidden in the system tray."));
    QCommandLineOption backendOpt("backend",
                                  QCoreApplication::translate("main", "Audio backend: auto, %1.")
                                      .arg(QString::fromStdString([] {
                                          std::string all;
                                          for (const auto& b : availableBackends())
                                              all += (all.empty() ? "" : ", ") + b;
                                          return all;
                                      }())),
                                  QStringLiteral("name"), QStringLiteral("auto"));
    QCommandLineOption volUp("volume-up", QCoreApplication::translate("main", "Raise the default device volume (running instance)."));
    QCommandLineOption volDown("volume-down", QCoreApplication::translate("main", "Lower the default device volume (running instance)."));
    QCommandLineOption mute("mute", QCoreApplication::translate("main", "Toggle mute on the default device (running instance)."));
    QCommandLineOption muteAll("mute-all", QCoreApplication::translate("main", "Toggle “mute all” (running instance)."));
    QCommandLineOption toggle("toggle", QCoreApplication::translate("main", "Show or hide the mixer window (running instance)."));
    QCommandLineOption quit("quit", QCoreApplication::translate("main", "Quit the running instance."));
    parser.addOptions({minimized, backendOpt, volUp, volDown, mute, muteAll, toggle, quit});
    parser.process(app);

    QString command = QStringLiteral("show");
    for (const auto* opt : {&volUp, &volDown, &mute, &muteAll, &toggle, &quit})
        if (parser.isSet(*opt))
            command = opt->names().constFirst();

    // Instância única: se já houver uma rodando, entrega o comando e sai.
    ui::SingleInstance instance;
    if (!instance.tryBecomePrimary(command))
        return 0;
    if (command != QLatin1String("show") && command != QLatin1String("toggle")) {
        std::cerr << "LoudMixer is not running.\n";
        return 1;
    }

    // Escolha do backend: o pedido explícito, ou o primeiro que conectar.
    const QString requested = parser.value(backendOpt);
    std::vector<std::string> candidates;
    if (requested == QLatin1String("auto"))
        candidates = autoBackendOrder();
    else
        candidates = {requested.toStdString()};

    std::unique_ptr<AudioSessionManager> manager;
    QStringList failures;
    for (const auto& name : candidates) {
        auto backend = createBackend(name);
        if (!backend) {
            failures << QCoreApplication::translate("main", "%1: not available in this build").arg(QString::fromStdString(name));
            continue;
        }
        auto candidate = std::make_unique<AudioSessionManager>(std::move(backend));
        QString err;
        if (candidate->start(&err)) {
            manager = std::move(candidate);
            break;
        }
        failures << QStringLiteral("%1: %2").arg(QString::fromStdString(name), err);
    }
    if (!manager) {
        QMessageBox::critical(nullptr, QStringLiteral("LoudMixer"),
                              QCoreApplication::translate("main", "Could not connect to any audio system.\n\n%1")
                                  .arg(failures.join(QLatin1Char('\n'))));
        return 1;
    }

    QSettings settings;
    auto applySettings = [&] {
        QSettings s;
        manager->setClipCurve(static_cast<ClipCurve>(s.value(ui::keys::clipCurve, static_cast<int>(ClipCurve::Tanh)).toInt()));
        manager->backend()->setOption("boostSource", s.value(ui::keys::boostSource).toString().toStdString());
    };
    applySettings();

    PresetStore presets;
    AutoStartService autostart;
    ui::MixerWindow window(manager.get(), &presets, &autostart);

    const bool trayAvailable = ui::TrayController::isAvailable();
    std::unique_ptr<ui::TrayController> tray;
    if (trayAvailable) {
        tray = std::make_unique<ui::TrayController>(manager.get(), &window, &presets, &autostart);
        tray->show();
        QObject::connect(&window, &ui::MixerWindow::hiddenToTray, &window, [&] {
            QSettings s;
            if (!s.value(ui::keys::trayHintShown, false).toBool()) {
                tray->showMessage(QStringLiteral("LoudMixer"),
                                  QCoreApplication::translate("main", "LoudMixer is still running in the system tray."));
                s.setValue(ui::keys::trayHintShown, true);
            }
        });
    } else {
        // Sem bandeja (alguns ambientes Wayland/GNOME sem extensão): fechar a janela encerra.
        QApplication::setQuitOnLastWindowClosed(true);
    }

    // Atalhos: Windows via RegisterHotKey; Linux via comandos da linha de comando.
    ui::GlobalHotkeys hotkeys;
    auto applyHotkeys = [&] {
        if (ui::GlobalHotkeys::isSupported())
            hotkeys.setEnabled(QSettings().value(ui::keys::globalHotkeys, true).toBool());
    };
    applyHotkeys();

    auto runCommand = [&](const QString& cmd) {
        const double step = QSettings().value(ui::keys::hotkeyStep, 5).toInt() / 100.0;
        if (cmd == QLatin1String("show"))
            window.showAndRaise();
        else if (cmd == QLatin1String("toggle"))
            window.toggleVisible();
        else if (cmd == QLatin1String("volume-up"))
            manager->nudgeDefaultDevice(+step);
        else if (cmd == QLatin1String("volume-down"))
            manager->nudgeDefaultDevice(-step);
        else if (cmd == QLatin1String("mute"))
            manager->toggleMuteDefaultDevice();
        else if (cmd == QLatin1String("mute-all"))
            manager->setAllMuted(!manager->allMuted());
        else if (cmd == QLatin1String("quit"))
            QCoreApplication::quit();
    };
    QObject::connect(&instance, &ui::SingleInstance::messageReceived, &app, runCommand);
    QObject::connect(&hotkeys, &ui::GlobalHotkeys::triggered, &app, [&](ui::GlobalHotkeys::Action a) {
        switch (a) {
        case ui::GlobalHotkeys::Action::VolumeUp: runCommand(QStringLiteral("volume-up")); break;
        case ui::GlobalHotkeys::Action::VolumeDown: runCommand(QStringLiteral("volume-down")); break;
        case ui::GlobalHotkeys::Action::ToggleMute: runCommand(QStringLiteral("mute")); break;
        }
    });
    QObject::connect(&window, &ui::MixerWindow::settingsChanged, &app, [&] {
        applySettings();
        applyHotkeys();
    });
    QObject::connect(manager.get(), &AudioSessionManager::errorOccurred, &window,
                     [&](const QString& msg) { QMessageBox::warning(&window, QStringLiteral("LoudMixer"), msg); });

#ifdef Q_OS_UNIX
    installSignalHandlers(&app);
#endif
    // Ao sair, desfaz rotas de boost para o áudio voltar ao dispositivo real.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] { manager->stop(); });

    const bool startHidden = trayAvailable && (parser.isSet(minimized) || settings.value(ui::keys::startMinimized, false).toBool());
    if (!startHidden)
        window.showAndRaise();

    return app.exec();
}
