#pragma once

// Aba "Canais": escolhe um dispositivo e mostra um slider por canal físico
// (L/R/C/LFE/...), permitindo balanço e boost individual.

#include <QWidget>

#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QVBoxLayout;

namespace loudmixer {
class AudioSessionManager;
}

namespace loudmixer::ui {

class GainSlider;

class ChannelsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChannelsPanel(AudioSessionManager* manager, QWidget* parent = nullptr);

    void sync(); // chamado quando a lista de dispositivos muda

private:
    void rebuildSliders();
    std::string currentDevice() const;

    AudioSessionManager* manager_;
    QComboBox* deviceCombo_;
    QVBoxLayout* slidersLayout_;
    QLabel* empty_;
    std::vector<GainSlider*> sliders_;
    std::string builtFor_;
};

} // namespace loudmixer::ui
