#include "SettingsDialog.h"

#include "GlobalHotkeys.h"
#include "core/AutoStartService.h"
#include "core/GainProcessor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace loudmixer::ui {

SettingsDialog::SettingsDialog(AutoStartService* autostart, QWidget* parent)
    : QDialog(parent)
    , autostart_(autostart)
    , curve_(new QComboBox(this))
    , autostartBox_(new QCheckBox(tr("Start LoudMixer when I log in"), this))
    , startMinimized_(new QCheckBox(tr("Start minimized to the system tray"), this))
    , step_(new QSpinBox(this))
    , hotkeys_(new QCheckBox(tr("Enable global hotkeys (Ctrl+Alt+↑ / ↓ / M)"), this))
    , language_(new QComboBox(this))
{
    setWindowTitle(tr("LoudMixer Settings"));
    QSettings s;

    auto* layout = new QVBoxLayout(this);

    // --- Áudio ---------------------------------------------------------------
    auto* audio = new QGroupBox(tr("Audio"), this);
    auto* audioForm = new QFormLayout(audio);
    curve_->addItem(tr("Soft (tanh) — recommended"), static_cast<int>(ClipCurve::Tanh));
    curve_->addItem(tr("Soft (cubic) — lighter on CPU"), static_cast<int>(ClipCurve::Cubic));
    curve_->addItem(tr("Hard clip"), static_cast<int>(ClipCurve::Hard));
    curve_->setCurrentIndex(curve_->findData(s.value(keys::clipCurve, static_cast<int>(ClipCurve::Tanh)).toInt()));
    curve_->setToolTip(tr("How peaks above 0 dBFS are limited when DSP boost is active"));
    audioForm->addRow(tr("Clipping curve:"), curve_);
    layout->addWidget(audio);

#ifdef Q_OS_WIN
    // --- Avançado: rota por cabo virtual ------------------------------------
    auto* advanced = new QGroupBox(tr("Advanced"), this);
    auto* advancedForm = new QFormLayout(advanced);
    showCableRoute_ = new QCheckBox(tr("Show the virtual cable route (“DSP boost”) on devices"), this);
    showCableRoute_->setToolTip(tr("Alternative way to go above 100% for devices where the boost button does not work. "
                                   "Needs a virtual audio cable (e.g. VB-Audio Cable) set as the Windows default output."));
    showCableRoute_->setChecked(s.value(keys::showCableRoute, false).toBool());
    advancedForm->addRow(showCableRoute_);
    boostSource_ = new QLineEdit(this);
    boostSource_->setPlaceholderText(tr("auto (CABLE Input, VoiceMeeter, ...)"));
    boostSource_->setText(s.value(keys::boostSource).toString());
    boostSource_->setToolTip(tr("Virtual output device whose audio is captured and boosted"));
    boostSource_->setEnabled(showCableRoute_->isChecked());
    connect(showCableRoute_, &QCheckBox::toggled, boostSource_, &QLineEdit::setEnabled);
    advancedForm->addRow(tr("Boost source device:"), boostSource_);
    layout->addWidget(advanced);
#endif

    // --- Inicialização -------------------------------------------------------
    auto* startup = new QGroupBox(tr("Startup"), this);
    auto* startupLayout = new QVBoxLayout(startup);
    autostartBox_->setChecked(autostart_ && autostart_->isEnabled());
    autostartBox_->setEnabled(autostart_ && autostart_->isSupported());
    startMinimized_->setChecked(s.value(keys::startMinimized, false).toBool());
    startupLayout->addWidget(autostartBox_);
    startupLayout->addWidget(startMinimized_);
    layout->addWidget(startup);

    // --- Atalhos ---------------------------------------------------------------
    auto* keysBox = new QGroupBox(tr("Hotkeys"), this);
    auto* keysForm = new QFormLayout(keysBox);
    step_->setRange(1, 25);
    step_->setSuffix(QStringLiteral("%"));
    step_->setValue(s.value(keys::hotkeyStep, 5).toInt());
    keysForm->addRow(tr("Volume step:"), step_);
    if (GlobalHotkeys::isSupported()) {
        hotkeys_->setChecked(s.value(keys::globalHotkeys, true).toBool());
        keysForm->addRow(hotkeys_);
    } else {
        hotkeys_->hide();
        auto* help = new QLabel(tr("Bind these commands to keys in your desktop's keyboard settings:\n"
                                   "  loudmixer --volume-up\n  loudmixer --volume-down\n  loudmixer --mute\n"
                                   "  loudmixer --toggle"),
                                this);
        help->setTextInteractionFlags(Qt::TextSelectableByMouse);
        keysForm->addRow(help);
    }
    layout->addWidget(keysBox);

    // --- Idioma ----------------------------------------------------------------
    auto* langBox = new QGroupBox(tr("Language"), this);
    auto* langForm = new QFormLayout(langBox);
    language_->addItem(tr("System default"), QString());
    language_->addItem(QStringLiteral("Português (Brasil)"), QStringLiteral("pt_BR"));
    language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language_->setCurrentIndex(std::max(0, language_->findData(s.value(keys::language).toString())));
    langForm->addRow(tr("Interface language:"), language_);
    layout->addWidget(langBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::accept()
{
    QSettings s;
    s.setValue(keys::clipCurve, curve_->currentData().toInt());
    s.setValue(keys::startMinimized, startMinimized_->isChecked());
    s.setValue(keys::hotkeyStep, step_->value());
    s.setValue(keys::globalHotkeys, hotkeys_->isChecked());
    if (boostSource_)
        s.setValue(keys::boostSource, boostSource_->text().trimmed());
    if (showCableRoute_)
        s.setValue(keys::showCableRoute, showCableRoute_->isChecked());

    const QString oldLang = s.value(keys::language).toString();
    const QString newLang = language_->currentData().toString();
    s.setValue(keys::language, newLang);
    if (oldLang != newLang)
        QMessageBox::information(this, windowTitle(), tr("The new language will be used after restarting LoudMixer."));

    if (autostart_ && autostart_->isSupported() && autostartBox_->isChecked() != autostart_->isEnabled()) {
        QString err;
        if (!autostart_->setEnabled(autostartBox_->isChecked(), &err))
            QMessageBox::warning(this, windowTitle(), tr("Could not change the startup setting:\n%1").arg(err));
    }
    QDialog::accept();
}

} // namespace loudmixer::ui
