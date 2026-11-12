#pragma once

#include "core/IAudioBackend.h"

#include <memory>
#include <string>
#include <vector>

namespace loudmixer {

// Backends compilados nesta build, em ordem de preferência para "auto".
//  Linux:   pulse (PulseAudio ou pipewire-pulse), pipewire (nativo, experimental)
//  Windows: wasapi
//  Sempre:  mock (só por pedido explícito)
std::vector<std::string> autoBackendOrder();
std::vector<std::string> availableBackends();

// nullptr se o nome for desconhecido ou não tiver sido compilado.
std::unique_ptr<IAudioBackend> createBackend(const std::string& name);

} // namespace loudmixer
