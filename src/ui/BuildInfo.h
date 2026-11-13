#pragma once

#include <QString>

namespace loudmixer::ui {

// Nome exibido na janela e na bandeja. Builds de Debug mostram "(dev)" para
// não serem confundidos com uma versão instalada aberta ao mesmo tempo.
// (QApplication::applicationName continua "LoudMixer": é ele que define onde
// ficam as configurações.)
inline QString appDisplayName()
{
#ifdef LOUDMIXER_DEV_BUILD
    return QStringLiteral("LoudMixer (dev)");
#else
    return QStringLiteral("LoudMixer");
#endif
}

} // namespace loudmixer::ui
