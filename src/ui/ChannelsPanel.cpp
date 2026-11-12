#include "ChannelsPanel.h"

#include "GainSlider.h"
#include "core/AudioSessionManager.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace loudmixer::ui {

ChannelsPanel::ChannelsPanel(AudioSessionManager* manager, QWidget* parent)
    : QWidget(parent)
    , manager_(manager)
    , deviceCombo_(new QComboBox(this))
    , slidersLayout_(new QVBoxLayout)
    , empty_(new QLabel(tr("No output devices."), this))
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->addRow(tr("Device:"), deviceCombo_);
    layout->addLayout(form);
    layout->addLayout(slidersLayout_);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setEnabled(false);
    layout->addWidget(empty_);
    layout->addStretch(1);

    connect(deviceCombo_, &QComboBox::currentIndexChanged, this, [this] { rebuildSliders(); });
    sync();
}

std::string ChannelsPanel::currentDevice() const
{
    return deviceCombo_->currentData().toString().toStdString();
}

void ChannelsPanel::sync()
{
    // Atualiza a lista de dispositivos preservando a seleção.
    const QString selected = deviceCombo_->currentData().toString();
    {
        const QSignalBlocker block(deviceCombo_);
        deviceCombo_->clear();
        for (const auto& d : manager_->devices())
            deviceCombo_->addItem(QString::fromStdString(d.description), QString::fromStdString(d.id));
        int idx = deviceCombo_->findData(selected);
        if (idx < 0) // padrão: dispositivo padrão do sistema
            idx = deviceCombo_->findData(QString::fromStdString(manager_->defaultDeviceId()));
        deviceCombo_->setCurrentIndex(std::max(0, idx));
    }
    empty_->setVisible(deviceCombo_->count() == 0);

    const auto dev = manager_->device(currentDevice());
    if (!dev || dev->id != builtFor_ || dev->channels.size() != sliders_.size()) {
        rebuildSliders();
        return;
    }
    for (std::size_t i = 0; i < sliders_.size(); ++i) {
        sliders_[i]->setMaxGain(manager_->maxGain(Target::channelOf(dev->id, static_cast<int>(i))));
        sliders_[i]->setGain(dev->channels[i].gain);
    }
}

void ChannelsPanel::rebuildSliders()
{
    for (GainSlider* s : sliders_)
        s->deleteLater();
    sliders_.clear();
    builtFor_.clear();

    const auto dev = manager_->device(currentDevice());
    if (!dev)
        return;
    builtFor_ = dev->id;
    for (const auto& ch : dev->channels) {
        auto* slider = new GainSlider(this);
        slider->setLabel(QString::fromStdString(ch.name));
        slider->setMuteVisible(false);
        slider->setMaxGain(manager_->maxGain(Target::channelOf(dev->id, ch.index)));
        slider->setGain(ch.gain);
        const std::string id = dev->id;
        const int index = ch.index;
        connect(slider, &GainSlider::gainChanged, this,
                [this, id, index](double g) { manager_->setGain(Target::channelOf(id, index), g); });
        slidersLayout_->addWidget(slider);
        sliders_.push_back(slider);
    }
}

} // namespace loudmixer::ui
