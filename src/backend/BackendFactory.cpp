#include "BackendFactory.h"

#include "mock/MockBackend.h"

#ifdef LOUDMIXER_HAVE_PULSE
#include "linux/PulseBackend.h"
#endif
#ifdef LOUDMIXER_HAVE_PIPEWIRE
#include "linux/PipeWireBackend.h"
#endif
#ifdef LOUDMIXER_HAVE_WASAPI
#include "windows/WasapiBackend.h"
#endif

namespace loudmixer {

std::vector<std::string> autoBackendOrder()
{
    std::vector<std::string> order;
#ifdef LOUDMIXER_HAVE_WASAPI
    order.emplace_back("wasapi");
#endif
#ifdef LOUDMIXER_HAVE_PULSE
    // Primeiro a libpulse: é a implementação completa e testada, e funciona
    // tanto no PulseAudio quanto no PipeWire (via pipewire-pulse).
    order.emplace_back("pulse");
#endif
#ifdef LOUDMIXER_HAVE_PIPEWIRE
    // Fallback para sistemas PipeWire sem o pipewire-pulse.
    order.emplace_back("pipewire");
#endif
    return order;
}

std::vector<std::string> availableBackends()
{
    auto all = autoBackendOrder();
    all.emplace_back("mock");
    return all;
}

std::unique_ptr<IAudioBackend> createBackend(const std::string& name)
{
    if (name == "mock")
        return std::make_unique<MockBackend>(true);
#ifdef LOUDMIXER_HAVE_PULSE
    if (name == "pulse")
        return std::make_unique<PulseBackend>();
#endif
#ifdef LOUDMIXER_HAVE_PIPEWIRE
    if (name == "pipewire")
        return std::make_unique<PipeWireBackend>();
#endif
#ifdef LOUDMIXER_HAVE_WASAPI
    if (name == "wasapi")
        return std::make_unique<WasapiBackend>();
#endif
    return nullptr;
}

} // namespace loudmixer
