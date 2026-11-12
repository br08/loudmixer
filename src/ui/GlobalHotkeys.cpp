#include "GlobalHotkeys.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace loudmixer::ui {

GlobalHotkeys::GlobalHotkeys(QObject* parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeys::~GlobalHotkeys()
{
    setEnabled(false);
    if (auto* app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
}

bool GlobalHotkeys::isSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool GlobalHotkeys::setEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    if (enabled == enabled_)
        return true;
    const struct {
        Action action;
        UINT key;
    } keys[] = {{Action::VolumeUp, VK_UP}, {Action::VolumeDown, VK_DOWN}, {Action::ToggleMute, 'M'}};
    if (!enabled) {
        for (const auto& k : keys)
            UnregisterHotKey(nullptr, static_cast<int>(k.action));
        enabled_ = false;
        return true;
    }
    bool all = true;
    for (const auto& k : keys)
        all &= RegisterHotKey(nullptr, static_cast<int>(k.action), MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, k.key) != FALSE;
    enabled_ = true;
    return all;
#else
    Q_UNUSED(enabled);
    return false;
#endif
}

bool GlobalHotkeys::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_dispatcher_MSG" || eventType == "windows_generic_MSG") {
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_HOTKEY && enabled_) {
            const int id = static_cast<int>(msg->wParam);
            if (id >= 1 && id <= 3) {
                emit triggered(static_cast<Action>(id));
                if (result)
                    *result = 0;
                return true;
            }
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}

} // namespace loudmixer::ui
