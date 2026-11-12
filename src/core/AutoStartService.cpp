#include "AutoStartService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace loudmixer {

namespace {
constexpr auto kEntryName = "LoudMixer";
#ifdef Q_OS_WIN
constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif
} // namespace

AutoStartService::AutoStartService(QString executablePath, QString autostartDir)
    : executable_(std::move(executablePath))
    , autostartDir_(std::move(autostartDir))
{
    if (executable_.isEmpty()) {
        // Dentro de um AppImage o executável real fica num mount temporário;
        // o caminho estável é o do próprio arquivo .AppImage.
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        executable_ = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    }
    if (autostartDir_.isEmpty())
        autostartDir_ = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart");
}

bool AutoStartService::isSupported() const
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

QString AutoStartService::desktopEntryPath() const
{
    return autostartDir_ + QStringLiteral("/loudmixer.desktop");
}

bool AutoStartService::isEnabled() const
{
#ifdef Q_OS_WIN
    QSettings run(kRunKey, QSettings::NativeFormat);
    return run.contains(kEntryName);
#else
    return QFile::exists(desktopEntryPath());
#endif
}

bool AutoStartService::setEnabled(bool enabled, QString* error)
{
#ifdef Q_OS_WIN
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue(kEntryName, QStringLiteral("\"%1\" --minimized").arg(QDir::toNativeSeparators(executable_)));
    else
        run.remove(kEntryName);
    run.sync();
    if (run.status() != QSettings::NoError) {
        if (error)
            *error = QCoreApplication::translate("loudmixer::AutoStartService", "Could not write to the Windows registry.");
        return false;
    }
    return true;
#else
    const QString path = desktopEntryPath();
    if (!enabled) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error)
                *error = QCoreApplication::translate("loudmixer::AutoStartService", "Could not remove %1").arg(path);
            return false;
        }
        return true;
    }
    if (!QDir().mkpath(autostartDir_)) {
        if (error)
            *error = QCoreApplication::translate("loudmixer::AutoStartService", "Could not create %1").arg(autostartDir_);
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    // Aspas no Exec seguem a especificação Desktop Entry (escapar " ` $ \).
    QString quoted = executable_;
    quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
        .replace(QLatin1Char('"'), QStringLiteral("\\\""))
        .replace(QLatin1Char('`'), QStringLiteral("\\`"))
        .replace(QLatin1Char('$'), QStringLiteral("\\$"));
    QTextStream out(&f);
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=LoudMixer\n"
        << "Comment=Volume booster per app, device and channel\n"
        << "Exec=\"" << quoted << "\" --minimized\n"
        << "Icon=loudmixer\n"
        << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n";
    return true;
#endif
}

} // namespace loudmixer
