#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace loudmixer {
class AutoStartService;
}

namespace loudmixer::ui {

// Chaves do QSettings usadas pela UI.
namespace keys {
inline constexpr auto clipCurve = "ui/clipCurve";
inline constexpr auto startMinimized = "ui/startMinimized";
inline constexpr auto hotkeyStep = "ui/hotkeyStep";
inline constexpr auto globalHotkeys = "ui/globalHotkeys";
inline constexpr auto language = "ui/language";
inline constexpr auto geometry = "ui/geometry";
inline constexpr auto lastTab = "ui/lastTab";
inline constexpr auto trayHintShown = "ui/trayHintShown";
inline constexpr auto boostSource = "windows/boostSource";
// Windows: mostra a rota por cabo virtual ("Boost DSP") nos dispositivos.
inline constexpr auto showCableRoute = "windows/showCableRoute";
} // namespace keys

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(AutoStartService* autostart, QWidget* parent = nullptr);

    // Aplicado ao clicar em OK; quem abriu o diálogo relê o QSettings.
    void accept() override;

private:
    AutoStartService* autostart_;
    QComboBox* curve_;
    QCheckBox* autostartBox_;
    QCheckBox* startMinimized_;
    QSpinBox* step_;
    QCheckBox* hotkeys_;
    QComboBox* language_;
    QLineEdit* boostSource_ = nullptr;
    QCheckBox* showCableRoute_ = nullptr;
};

} // namespace loudmixer::ui
