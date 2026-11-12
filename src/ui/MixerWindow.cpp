#include "MixerWindow.h"

#include "AppIcons.h"
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
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace loudmixer::ui {

namespace {
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
    setWindowTitle(QStringLiteral("LoudMixer"));
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
    bool anyRoute = false;

    for (const auto& d : devices) {
        alive.insert(d.id);
        GainSlider*& row = deviceRows_[d.id];
        if (!row) {
            row = new GainSlider(this);
            const std::string id = d.id;
            connect(row, &GainSlider::gainChanged, this, [this, id](double g) { manager_->setGain(Target::device(id), g); });
            connect(row, &GainSlider::muteToggled, this, [this, id](bool m) { manager_->setMuted(Target::device(id), m); });
            if (canBoost) {
                auto* box = new QCheckBox(tr("DSP boost"), row);
                box->setToolTip(tr("Route this device through LoudMixer's DSP: real boost up to 200% with soft-clipping "
                                   "and a clipping indicator (adds a few milliseconds of latency)"));
                connect(box, &QCheckBox::toggled, this, [this, id](bool on) { toggleBoost(id, on); });
                row->setExtraWidget(box);
                boostBoxes_[id] = box;
            }
        }
        const bool routed = manager_->boostRouteActive(d.id);
        anyRoute |= routed;
        const QString label = QString::fromStdString(d.description);
        row->setIcon(deviceIcon(d.isDefault));
        row->setLabel(label, d.isDefault ? tr("%1\nDefault output device").arg(label) : label);
        row->setLabelBold(d.isDefault);
        row->setMaxGain(manager_->maxGain(Target::device(d.id)));
        row->setGain(d.gain);
        row->setMuted(d.muted);
        row->setLedVisible(routed);
        if (auto box = boostBoxes_.find(d.id); box != boostBoxes_.end()) {
            const QSignalBlocker block(box->second);
            box->second->setChecked(routed);
        }
    }
    for (auto it = boostBoxes_.begin(); it != boostBoxes_.end();)
        it = alive.count(it->first) ? std::next(it) : boostBoxes_.erase(it);
    dropMissing(deviceRows_, alive);

    for (const auto& d : devices)
        devicesLayout_->addWidget(deviceRows_[d.id]);
    emptyDevices_->setVisible(devices.empty());

    if (anyRoute && isVisible())
        ledTimer_.start();
    else if (!anyRoute)
        ledTimer_.stop();

    QString status = tr("Audio backend: %1").arg(manager_->backendName());
    if (!devices.empty() && manager_->maxGain(Target::device(manager_->defaultDeviceId())) <= 1.0)
        status += tr(" — enable “DSP boost” on a device to go above 100%");
    status_->setText(status);
    muteAll_->blockSignals(true);
    muteAll_->setChecked(manager_->allMuted());
    muteAll_->blockSignals(false);
}

void MixerWindow::updateLeds()
{
    const qint64 now = clock_.elapsed();
    for (const auto& id : manager_->activeBoostRoutes()) {
        auto row = deviceRows_.find(id);
        if (row == deviceRows_.end())
            continue;
        if (auto stats = manager_->takeStats(id); stats && stats->clippedSamples > 0)
            clipHoldUntil_[id] = now + kClipHoldMs;
        row->second->setLedState(now < clipHoldUntil_[id] ? ClipLed::State::Clipping : ClipLed::State::Active);
    }
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
    if (dlg.exec() == QDialog::Accepted)
        emit settingsChanged();
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
    if (!manager_->activeBoostRoutes().empty())
        ledTimer_.start();
}

void MixerWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    ledTimer_.stop(); // sem janela, sem polling do medidor
}

} // namespace loudmixer::ui
