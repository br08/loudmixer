#include "MixerWindow.h"

#include "AppIcons.h"
#include "BuildInfo.h"
#include "ChannelsPanel.h"
#include "GainSlider.h"
#include "SettingsDialog.h"
#include "core/AudioSessionManager.h"
#include "core/PresetStore.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace loudmixer::ui {

namespace {

// Windows: a rota por cabo virtual é opção avançada (o boost do sistema é o
// caminho normal). No Linux, a rota DSP é o caminho do soft-clip: sempre visível.
bool showCableRoute()
{
#ifdef Q_OS_WIN
    return QSettings().value(keys::showCableRoute, false).toBool();
#else
    return true;
#endif
}
constexpr int kLedIntervalMs = 33;  // ~30 Hz
constexpr qint64 kClipHoldMs = 250; // LED fica aceso um pouco após o pico

template <typename Map>
void dropMissing(Map& rows, const std::set<std::string>& alive)
{
    for (auto it = rows.begin(); it != rows.end();) {
        if (!alive.count(it->first)) {
            it->second->deleteLater();
            it = rows.erase(it);
        } else {
            ++it;
        }
    }
}
} // namespace

MixerWindow::MixerWindow(AudioSessionManager* manager, PresetStore* presets, AutoStartService* autostart, QWidget* parent)
    : QWidget(parent)
    , manager_(manager)
    , presets_(presets)
    , autostart_(autostart)
{
    setWindowTitle(appDisplayName());
    setWindowIcon(appIcon());
    clock_.start();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(buildToolbar());

    tabs_ = new QTabWidget(this);
    tabs_->addTab(scrollPage(&sessionsLayout_, &emptySessions_, tr("No application is playing audio right now.")),
                  tr("Applications"));
    tabs_->addTab(scrollPage(&devicesLayout_, &emptyDevices_, tr("No output devices.")), tr("Devices"));
    channels_ = new ChannelsPanel(manager_, this);
    tabs_->addTab(channels_, tr("Channels"));
    layout->addWidget(tabs_, 1);

    status_ = new QLabel(this);
    status_->setEnabled(false);
    layout->addWidget(status_);

    connect(manager_, &AudioSessionManager::sessionsChanged, this, &MixerWindow::syncSessions);
    connect(manager_, &AudioSessionManager::devicesChanged, this, [this] {
        syncDevices();
        syncSessions(); // o rótulo dos apps mostra o dispositivo
        channels_->sync();
    });
    connect(manager_, &AudioSessionManager::boostRouteChanged, this, [this] { syncDevices(); });
    connect(manager_, &AudioSessionManager::systemBoostPendingChanged, this, [this] { syncDevices(); });
    connect(manager_, &AudioSessionManager::systemBoostFinished, this, &MixerWindow::onSystemBoostFinished);

    ledTimer_.setInterval(kLedIntervalMs);
    connect(&ledTimer_, &QTimer::timeout, this, &MixerWindow::updateLeds);

    QSettings s;
    resize(680, 440);
    restoreGeometry(s.value(keys::geometry).toByteArray());
    tabs_->setCurrentIndex(s.value(keys::lastTab, 0).toInt());
    connect(tabs_, &QTabWidget::currentChanged, this, [](int i) { QSettings().setValue(keys::lastTab, i); });

    refreshPresets();
    syncDevices();
    syncSessions();
}

// ---------------------------------------------------------------------------
// Construção
// ---------------------------------------------------------------------------

QWidget* MixerWindow::buildToolbar()
{
    auto* bar = new QWidget(this);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);

    presetCombo_ = new QComboBox(bar);
    presetCombo_->setMinimumWidth(180);
    presetCombo_->setToolTip(tr("Apply a saved preset"));
    connect(presetCombo_, &QComboBox::activated, this, [this](int index) {
        const QString name = presetCombo_->itemData(index).toString();
        if (name.isEmpty())
            return;
        if (auto p = presets_->load(name))
            manager_->applyPreset(*p);
    });

    auto* save = new QToolButton(bar);
    save->setText(tr("Save…"));
    save->setToolTip(tr("Save the current gains as a preset"));
    connect(save, &QToolButton::clicked, this, &MixerWindow::savePreset);

    auto* del = new QToolButton(bar);
    del->setText(tr("Delete"));
    del->setToolTip(tr("Delete the selected preset"));
    connect(del, &QToolButton::clicked, this, &MixerWindow::deletePreset);

    muteAll_ = new QToolButton(bar);
    muteAll_->setText(tr("Mute all"));
    muteAll_->setCheckable(true);
    connect(muteAll_, &QToolButton::toggled, manager_, &AudioSessionManager::setAllMuted);

    auto* reset = new QToolButton(bar);
    reset->setText(tr("Reset all"));
    reset->setToolTip(tr("Set every app, device and channel back to 100%"));
    connect(reset, &QToolButton::clicked, manager_, &AudioSessionManager::resetAllGains);

    auto* settings = new QToolButton(bar);
    settings->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system")));
    if (settings->icon().isNull())
        settings->setText(QStringLiteral("⚙"));
    settings->setToolTip(tr("Settings"));
    connect(settings, &QToolButton::clicked, this, &MixerWindow::openSettings);

    row->addWidget(presetCombo_);
    row->addWidget(save);
    row->addWidget(del);
    row->addStretch(1);
    row->addWidget(muteAll_);
    row->addWidget(reset);
    row->addWidget(settings);
    return bar;
}

QWidget* MixerWindow::scrollPage(QVBoxLayout** layout, QLabel** empty, const QString& emptyText)
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* outer = new QVBoxLayout(page);
    *layout = new QVBoxLayout;
    (*layout)->setSpacing(2);
    outer->addLayout(*layout);
    *empty = new QLabel(emptyText, page);
    (*empty)->setAlignment(Qt::AlignCenter);
    (*empty)->setEnabled(false);
    (*empty)->setWordWrap(true);
    outer->addWidget(*empty);
    outer->addStretch(1);
    scroll->setWidget(page);
    return scroll;
}

// ---------------------------------------------------------------------------
// Sincronização com o AudioSessionManager
// ---------------------------------------------------------------------------

void MixerWindow::syncSessions()
{
    auto sessions = manager_->sessions();
    std::sort(sessions.begin(), sessions.end(), [](const auto& a, const auto& b) {
        return QString::fromStdString(a.appName).compare(QString::fromStdString(b.appName), Qt::CaseInsensitive) < 0;
    });
    std::set<std::string> alive;
    const bool multipleDevices = manager_->devices().size() > 1;

    for (const auto& s : sessions) {
        alive.insert(s.id);
        GainSlider*& row = sessionRows_[s.id];
        if (!row) {
            row = new GainSlider(this);
            row->setIcon(sessionIcon(s));
            const std::string id = s.id;
            connect(row, &GainSlider::gainChanged, this, [this, id](double g) { manager_->setGain(Target::app(id), g); });
            connect(row, &GainSlider::muteToggled, this, [this, id](bool m) { manager_->setMuted(Target::app(id), m); });
        }
        QString deviceName;
        if (auto d = manager_->device(s.deviceId))
            deviceName = QString::fromStdString(d->description);
        const QString name = QString::fromStdString(s.appName);
        QString tip = name;
        if (s.processId)
            tip += tr("\nPID %1").arg(s.processId);
        if (!deviceName.isEmpty())
            tip += tr("\nOutput: %1").arg(deviceName);
        row->setLabel(multipleDevices && !deviceName.isEmpty() ? QStringLiteral("%1 · %2").arg(name, deviceName) : name, tip);
        row->setMaxGain(manager_->maxGain(Target::app(s.id)));
        row->setGain(s.gain);
        row->setMuted(s.muted);
    }
    dropMissing(sessionRows_, alive);

    // Reordena (widgets existentes são reaproveitados).
    for (const auto& s : sessions)
        sessionsLayout_->addWidget(sessionRows_[s.id]);
    emptySessions_->setVisible(sessions.empty());
}

void MixerWindow::syncDevices()
{
    const auto& devices = manager_->devices();
    std::set<std::string> alive;
    const bool canBoost = manager_->supportsBoostRoute();
    bool anyLed = false;

    for (const auto& d : devices) {
        alive.insert(d.id);
        GainSlider*& row = deviceRows_[d.id];
        if (!row) {
            row = new GainSlider(this);
            const std::string id = d.id;
            connect(row, &GainSlider::gainChanged, this, [this, id](double g) { manager_->setGain(Target::device(id), g); });
            connect(row, &GainSlider::muteToggled, this, [this, id](bool m) { manager_->setMuted(Target::device(id), m); });
            auto* extra = new QWidget(row);
            auto* extraLayout = new QHBoxLayout(extra);
            extraLayout->setContentsMargins(0, 0, 0, 0);
            extraLayout->setSpacing(6);
            // Boost do sistema (Windows: APO).
            auto* sysBoost = new QToolButton(extra);
            sysBoost->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            sysBoost->hide();
            connect(sysBoost, &QToolButton::clicked, this, [this, id] { onSystemBoostClicked(id); });
            extraLayout->addWidget(sysBoost);
            systemBoostButtons_[id] = sysBoost;
            if (canBoost) {
                auto* box = new QCheckBox(tr("DSP boost"), extra);
                box->setToolTip(tr("Route this device through LoudMixer's DSP: real boost up to 200% with soft-clipping "
                                   "and a clipping indicator (adds a few milliseconds of latency)"));
                connect(box, &QCheckBox::toggled, this, [this, id](bool on) { toggleBoost(id, on); });
                extraLayout->addWidget(box);
                boostBoxes_[id] = box;
            }
            row->setExtraWidget(extra);
        }
        const bool routed = manager_->boostRouteActive(d.id);
        // Com boost do sistema (APO), o LED mostra o soft-clip dele.
        const bool led = routed || d.systemBoost;
        anyLed |= led;
        const QString label = QString::fromStdString(d.description);
        row->setIcon(deviceIcon(d.isDefault));
        row->setLabel(label, d.isDefault ? tr("%1\nDefault output device").arg(label) : label);
        row->setLabelBold(d.isDefault);
        row->setMaxGain(manager_->maxGain(Target::device(d.id)));
        row->setGain(d.gain);
        row->setMuted(d.muted);
        row->setLedVisible(led);
        if (auto box = boostBoxes_.find(d.id); box != boostBoxes_.end()) {
            const QSignalBlocker block(box->second);
            box->second->setChecked(routed);
            // Com boost do sistema a rota DSP é redundante; no Windows ela só
            // aparece se ativada nas configurações avançadas (ou se já estiver ativa).
            box->second->setVisible((showCableRoute() && !d.systemBoost) || routed);
        }
        if (auto btn = systemBoostButtons_.find(d.id); btn != systemBoostButtons_.end())
            updateSystemBoostButton(btn->second, d);
    }
    for (auto it = boostBoxes_.begin(); it != boostBoxes_.end();)
        it = alive.count(it->first) ? std::next(it) : boostBoxes_.erase(it);
    for (auto it = systemBoostButtons_.begin(); it != systemBoostButtons_.end();)
        it = alive.count(it->first) ? std::next(it) : systemBoostButtons_.erase(it);
    dropMissing(deviceRows_, alive);

    for (const auto& d : devices)
        devicesLayout_->addWidget(deviceRows_[d.id]);
    emptyDevices_->setVisible(devices.empty());

    if (anyLed && isVisible())
        ledTimer_.start();
    else if (!anyLed)
        ledTimer_.stop();

    QString status = tr("Audio backend: %1").arg(manager_->backendName());
    if (const auto def = manager_->device(manager_->defaultDeviceId());
        def && manager_->maxGain(Target::device(def->id)) <= 1.0) {
        if (def->systemBoostState != SystemBoostState::Unsupported)
            status += tr(" — click “Boost 200%” on a device to go above 100%");
        else if (showCableRoute())
            status += tr(" — enable “DSP boost” on a device to go above 100%");
    }
    status_->setText(status);
    muteAll_->blockSignals(true);
    muteAll_->setChecked(manager_->allMuted());
    muteAll_->blockSignals(false);
}

void MixerWindow::updateLeds()
{
    const qint64 now = clock_.elapsed();
    for (const auto& d : manager_->devices()) {
        if (!d.systemBoost && !manager_->boostRouteActive(d.id))
            continue;
        auto row = deviceRows_.find(d.id);
        if (row == deviceRows_.end())
            continue;
        const auto stats = manager_->takeStats(d.id);
        if (!stats) { // boost do sistema ainda não carregado (nenhum áudio tocou)
            row->second->setLedState(ClipLed::State::Off);
            continue;
        }
        if (stats->clippedSamples > 0)
            clipHoldUntil_[d.id] = now + kClipHoldMs;
        row->second->setLedState(now < clipHoldUntil_[d.id] ? ClipLed::State::Clipping : ClipLed::State::Active);
    }
}

void MixerWindow::updateSystemBoostButton(QToolButton* button, const AudioDevice& d)
{
    const std::string pending = manager_->systemBoostPending();
    const QIcon shield = style()->standardIcon(QStyle::SP_VistaShield); // pede administrador
    const QIcon warning = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    switch (d.systemBoostState) {
    case SystemBoostState::Unsupported:
        button->hide();
        return;
    case SystemBoostState::Off:
        button->setIcon(shield);
        button->setText(tr("Boost 200%"));
        button->setToolTip(tr("Let this device, its channels and its apps go up to 200% "
                              "(asks for administrator permission)"));
        break;
    case SystemBoostState::Active:
        button->setIcon(shield);
        button->setText(tr("Boost on"));
        button->setToolTip(tr("Boost up to 200% is on for this device. Click to turn it off."));
        break;
    case SystemBoostState::EnhancementsOff:
        button->setIcon(warning);
        button->setText(tr("Boost paused"));
        button->setToolTip(tr("Audio enhancements were turned off for this device in the Windows settings, so the "
                              "boost stopped. Click to turn it back on."));
        break;
    case SystemBoostState::RemovedExternally:
        button->setIcon(warning);
        button->setText(tr("Boost removed"));
        button->setToolTip(tr("A driver or Windows update removed the boost from this device. "
                              "Click to turn it back on."));
        break;
    }
    if (pending == d.id)
        button->setText(d.systemBoost ? tr("Turning off…") : tr("Turning on…"));
    button->setEnabled(pending.empty());
    button->show();
}

void MixerWindow::onSystemBoostClicked(const std::string& deviceId)
{
    const auto d = manager_->device(deviceId);
    if (!d)
        return;
    const QString name = QString::fromStdString(d->description);
    const bool enable = d->systemBoostState != SystemBoostState::Active;
    QString text;
    if (enable) {
        text = tr("LoudMixer will add its audio effect to “%1” so it can go up to 200%.").arg(name);
        text += QStringLiteral("\n\n• ") + tr("Windows will ask for administrator permission.");
        text += QStringLiteral("\n• ") + tr("The audio pauses for a few seconds.");
        text += QStringLiteral("\n• ") + tr("Audio enhancements are turned on for this device.");
        if (!d->deviceEffects.empty()) {
            QStringList effects;
            for (const auto& e : d->deviceEffects)
                effects << QString::fromStdString(e);
            text += QStringLiteral("\n• ") + tr("This also turns on the effects already installed for it: %1.")
                                                   .arg(effects.join(QStringLiteral(", ")));
        }
    } else {
        text = tr("Turn off the boost on “%1”? The device goes back to how it was before (up to 100%).").arg(name);
        text += QStringLiteral("\n\n• ") + tr("Windows will ask for administrator permission.");
        text += QStringLiteral("\n• ") + tr("The audio pauses for a few seconds.");
    }
    const auto answer = QMessageBox::question(this, enable ? tr("Boost up to 200%") : tr("Turn off boost"), text,
                                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
    if (answer == QMessageBox::Ok)
        manager_->requestSystemBoost(deviceId, enable);
}

void MixerWindow::onSystemBoostFinished(const QString& deviceId, bool enabled, int code, const QString& detail)
{
    const auto d = manager_->device(deviceId.toStdString());
    const QString name = d ? QString::fromStdString(d->description) : deviceId;
    switch (static_cast<SystemBoostResult::Code>(code)) {
    case SystemBoostResult::Code::Ok:
    case SystemBoostResult::Code::Cancelled:
        break;
    case SystemBoostResult::Code::NoWorkingStage:
        QMessageBox::information(
            this, tr("Boost up to 200%"),
            tr("Windows does not run audio effects on “%1”, so the boost could not be turned on. "
               "The device was left as it was.\n\nAs an alternative, the virtual cable route can be turned on "
               "in Settings > Advanced.")
                .arg(name));
        break;
    case SystemBoostResult::Code::Unsupported:
        QMessageBox::warning(this, tr("Boost up to 200%"),
                             tr("The LoudMixer audio effect tool (loudmixer-apo-setup.exe) was not found. "
                                "Reinstall LoudMixer."));
        break;
    case SystemBoostResult::Code::Failed:
        QMessageBox::warning(this, tr("Boost up to 200%"),
                             (enabled ? tr("Could not turn on the boost on “%1”:\n%2")
                                      : tr("Could not turn off the boost on “%1”:\n%2"))
                                 .arg(name, detail));
        break;
    }
    syncDevices();
}

void MixerWindow::toggleBoost(const std::string& deviceId, bool enabled)
{
    QString err;
    if (!manager_->setBoostRoute(deviceId, enabled, &err)) {
        QMessageBox::warning(this, tr("DSP boost"), tr("Could not change DSP boost:\n%1").arg(err));
        syncDevices(); // desfaz o checkbox
    }
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

void MixerWindow::refreshPresets()
{
    const QSignalBlocker block(presetCombo_);
    presetCombo_->clear();
    presetCombo_->addItem(tr("Presets…"), QString());
    for (const QString& name : presets_->names())
        presetCombo_->addItem(name, name);
}

void MixerWindow::savePreset()
{
    bool ok = false;
    const QString current = presetCombo_->currentData().toString();
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name (e.g. “Movie night”):"),
                                               QLineEdit::Normal, current, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (presets_->load(name) && name != current
        && QMessageBox::question(this, tr("Save preset"), tr("Replace the existing preset “%1”?").arg(name))
            != QMessageBox::Yes)
        return;
    presets_->save(manager_->capturePreset(name));
    refreshPresets();
    presetCombo_->setCurrentIndex(presetCombo_->findData(name));
    emit presetsChanged();
}

void MixerWindow::deletePreset()
{
    const QString name = presetCombo_->currentData().toString();
    if (name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete preset"), tr("Delete the preset “%1”?").arg(name)) != QMessageBox::Yes)
        return;
    presets_->remove(name);
    refreshPresets();
    emit presetsChanged();
}

// ---------------------------------------------------------------------------
// Janela
// ---------------------------------------------------------------------------

void MixerWindow::openSettings()
{
    showAndRaise();
    SettingsDialog dlg(autostart_, this);
    if (dlg.exec() == QDialog::Accepted) {
        emit settingsChanged();
        syncDevices(); // a opção da rota por cabo virtual pode ter mudado
    }
}

void MixerWindow::toggleVisible()
{
    if (isVisible() && !isMinimized() && isActiveWindow())
        hide();
    else
        showAndRaise();
}

void MixerWindow::showAndRaise()
{
    if (isMinimized())
        showNormal();
    else
        show();
    raise();
    activateWindow();
}

void MixerWindow::closeEvent(QCloseEvent* event)
{
    QSettings().setValue(keys::geometry, saveGeometry());
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        // Fechar = minimizar para a bandeja; o processo continua rodando.
        hide();
        event->ignore();
        emit hiddenToTray();
        return;
    }
    QWidget::closeEvent(event);
}

void MixerWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    const auto& devices = manager_->devices();
    if (!manager_->activeBoostRoutes().empty()
        || std::any_of(devices.begin(), devices.end(), [](const AudioDevice& d) { return d.systemBoost; }))
        ledTimer_.start();
}

void MixerWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    ledTimer_.stop(); // sem janela, sem polling do medidor
}

} // namespace loudmixer::ui
