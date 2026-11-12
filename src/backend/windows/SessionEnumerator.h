#pragma once

// Enumeração de sessões de áudio (um app = uma ou mais sessões) de um
// endpoint de saída, via IAudioSessionManager2.

#include "ComUtil.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace loudmixer::win {

struct SessionEntry {
    std::string id;        // GetSessionInstanceIdentifier (único por instância)
    std::string appName;   // nome de exibição ou nome do executável
    std::string iconPath;  // caminho do executável (a UI extrai o ícone dele)
    std::uint32_t pid = 0;
    bool isSystemSounds = false;
    float volume = 1.0f;   // ISimpleAudioVolume (0.0 – 1.0)
    bool muted = false;
    ComPtr<ISimpleAudioVolume> control;
};

// Lista as sessões ativas/inativas (ignora as expiradas) do dispositivo.
std::vector<SessionEntry> enumerateSessions(IMMDevice* device);

// Nome base do executável de um processo ("chrome") e caminho completo.
bool processImage(std::uint32_t pid, std::string* baseName, std::string* fullPath);

} // namespace loudmixer::win
