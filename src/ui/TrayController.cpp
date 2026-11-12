#include "TrayController.h"

#include "AppIcons.h"
#include "MixerWindow.h"
#include "core/AudioSessionManager.h"
#include "core/AutoStartService.h"
#include "core/PresetStore.h"

#include <QApplication>
#include <QMenu>
#include <QMessageBox>
#include <QSystemTrayIcon>

#include <cmath>

namespace loudmixer::ui {

TrayController::TrayController(AudioSessionManager* manager, MixerWindow* window, PresetStore* presets,
                               AutoStartService* autostart, QObject* parent)
    : QObject(parent)
    , manager_(manager)
    , window_(window)
    , presets_(presets)
    , autostart_(autostart)
    , tray_(new QSystemTrayIcon(appIcon(), this))
    , menu_(std::make_unique<QMenu>())
{
    menu_->addAction(tr("Open mixer"), window_, &MixerWindow::showAndRaise);
    menu_->addSeparator();

    presetsMenu_ = menu_->addMenu(tr("Presets"));
    connect(presetsMenu_, &QMenu::aboutToShow, this, &TrayController::rebuildPresetsMenu);

    muteAll_ = menu_->addAction(tr("Mute all"));
    muteAll_->setCheckable(true);
    connect(muteAll_, &QAction::toggled, manager_, &AudioSessionManager::setAllMuted);

    menu_->addAction(tr("Reset all gains"), manager_, &AudioSessionManager::resetAllGains);
    menu_->addSeparator();

    autostartAction_ = menu_->addAction(tr("Start with the system"));
    autostartAction_->setCheckable(true);
    autostartAction_->setEnabled(autostart_->isSupported());
    connect(autostartAction_, &QAction::toggled, this, [this](bool on) {
        QString err;
        if (!autostart_->setEnabled(on, &err)) {
            QMessageBox::warning(nullptr, QStringLiteral("LoudMixer"), err);
            const QSignalBlocker block(autostartAction_);
            autostartAction_->setChecked(autostart_->isEnabled());
        }
    });
    connect(menu_.get(), &QMenu::aboutToShow, this, [this] {
        const QSignalBlocker b1(autostartAction_);
        autostartAction_->setChecked(autostart_->isEnabled());
        const QSignalBlocker b2(muteAll_);
        muteAll_->setChecked(manager_->allMuted());
    });

    menu_->addAction(tr("Settings…"), window_, &MixerWindow::openSettings);
    menu_->addSeparator();
    menu_->addAction(tr("Quit"), qApp, &QApplication::quit);

    tray_->setContextMenu(menu_.get());
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            window_->toggleVisible();
    });

    connect(manager_, &AudioSessionManager::devicesChanged, this, &TrayController::updateState);
    updateState();
}

TrayController::~TrayController() = default;

bool TrayController::isAvailable() { return QSystemTrayIcon::isSystemTrayAvailable(); }

void TrayController::show() { tray_->show(); }

void TrayController::showMessage(const QString& title, const QString& text)
{
    tray_->showMessage(title, text, appIcon(), 4000);
}

void TrayController::updateState()
{
    const auto dev = manager_->device(manager_->defaultDeviceId());
    TrayState state = TrayState::Normal;
    QString tip = QStringLiteral("LoudMixer");
    if (dev) {
        if (dev->muted)
            state = TrayState::Muted;
        else if (dev->gain > 1.0 + 1e-6)
            state = TrayState::Boosted;
        tip += QStringLiteral("\n%1: %2%%3")
                   .arg(QString::fromStdString(dev->description))
                   .arg(std::lround(dev->gain * 100.0))
                   .arg(dev->muted ? tr(" (muted)") : QString());
    }
    tray_->setIcon(appIcon(state));
    tray_->setToolTip(tip);
}

void TrayController::rebuildPresetsMenu()
{
    presetsMenu_->clear();
    const QStringList names = presets_->names();
    if (names.isEmpty()) {
        presetsMenu_->addAction(tr("(no presets saved)"))->setEnabled(false);
    }
    for (const QString& name : names) {
        presetsMenu_->addAction(name, this, [this, name] {
            if (auto p = presets_->load(name))
                manager_->applyPreset(*p);
        });
    }
    presetsMenu_->addSeparator();
    presetsMenu_->addAction(tr("Save current as preset…"), this, [this] {
        window_->showAndRaise();
        window_->savePreset();
    });
}

} // namespace loudmixer::ui
