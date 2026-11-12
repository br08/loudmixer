#pragma once

#include "core/AudioTypes.h"

#include <QColor>
#include <QIcon>

namespace loudmixer::ui {

enum class TrayState { Normal, Boosted, Muted };

// Ícone do app desenhado em código (não depende do plugin de SVG do Qt).
QIcon appIcon(TrayState state = TrayState::Normal);

// Ícone de um aplicativo: tema freedesktop no Linux, ícone do .exe no Windows.
QIcon sessionIcon(const AudioSession& session);

// Ícone pequeno de alto-falante (botão de mute), desenhado em código para
// funcionar mesmo sem tema de ícones.
QIcon volumeIcon(bool muted);

// Ícone genérico para dispositivos de saída.
QIcon deviceIcon(bool isDefault);

// Paleta compartilhada pelos widgets.
namespace palette {
inline const QColor normal{0x3d, 0x8b, 0xfd};  // 0–100%
inline const QColor boost{0xff, 0xb0, 0x20};   // 100–150%
inline const QColor danger{0xe5, 0x48, 0x4d};  // 150–200% / clipping
} // namespace palette

} // namespace loudmixer::ui
