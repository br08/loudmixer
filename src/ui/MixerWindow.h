#pragma once

// Janela principal: três abas (Apps / Dispositivos / Canais), barra de
// presets e ações globais. Fechar a janela só a esconde (o app segue na
// bandeja).

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QLabel;
class QTabWidget;
class QToolButton;
class QVBoxLayout;

namespace loudmixer {
struct AudioDevice;
class AudioSessionManager;
class AutoStartService;
class PresetStore;
} // namespace loudmixer

namespace loudmixer::ui {

class ChannelsPanel;
class GainSlider;

class MixerWindow : public QWidget {
    Q_OBJECT
public:
    MixerWindow(AudioSessionManager* manager, PresetStore* presets, AutoStartService* autostart, QWidget* parent = nullptr);

    void toggleVisible();
    void showAndRaise();
    void openSettings();
    void refreshPresets();
    void savePreset();

signals:
    void settingsChanged();
    void presetsChanged();
    void hiddenToTray();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QWidget* buildToolbar();
    QWidget* scrollPage(QVBoxLayout** layout, QLabel** empty, const QString& emptyText);
    void syncSessions();
    void syncDevices();
    void updateLeds();
    void deletePreset();
    void toggleBoost(const std::string& deviceId, bool enabled);
    void updateSystemBoostButton(QToolButton* button, const AudioDevice& device);
    void onSystemBoostClicked(const std::string& deviceId);
    void onSystemBoostFinished(const QString& deviceId, bool enabled, int code, const QString& detail);

    AudioSessionManager* manager_;
    PresetStore* presets_;
    AutoStartService* autostart_;

    QTabWidget* tabs_ = nullptr;
    QVBoxLayout* sessionsLayout_ = nullptr;
    QVBoxLayout* devicesLayout_ = nullptr;
    QLabel* emptySessions_ = nullptr;
    QLabel* emptyDevices_ = nullptr;
    ChannelsPanel* channels_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QToolButton* muteAll_ = nullptr;
    QLabel* status_ = nullptr;

    std::map<std::string, GainSlider*> sessionRows_;
    std::map<std::string, GainSlider*> deviceRows_;
    std::map<std::string, QCheckBox*> boostBoxes_;
    std::map<std::string, QToolButton*> systemBoostButtons_;
    std::map<std::string, qint64> clipHoldUntil_;
    QTimer ledTimer_;
    QElapsedTimer clock_;
};

} // namespace loudmixer::ui
