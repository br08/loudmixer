#include "LoopbackCapture.h"

#include "ComUtil.h"
#include "core/AudioTypes.h"
#include "core/GainProcessor.h"

#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace loudmixer::win {

namespace {
constexpr REFERENCE_TIME kCaptureBuffer = 200 * 10000; // 200 ms (unidades de 100 ns)
constexpr REFERENCE_TIME kRenderBuffer = 40 * 10000;   // 40 ms: latência extra percebida
constexpr DWORD kPollMs = 5;

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, definido localmente para não depender de
// INITGUID/ksuser.lib (que variam entre o Windows SDK e o MinGW).
const GUID kSubtypeIeeeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

bool isFloat32(const WAVEFORMATEX* fmt)
{
    if (fmt->wBitsPerSample != 32)
        return false;
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        return IsEqualGUID(ext->SubFormat, kSubtypeIeeeFloat) != FALSE;
    }
    return false;
}

std::string hrMessage(const char* what, HRESULT hr)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return std::string(what) + " [HRESULT " + buf + "]";
}
} // namespace

LoopbackCapture::~LoopbackCapture()
{
    stop();
}

bool LoopbackCapture::start(const std::string& sourceEndpointId,
                            const std::string& targetEndpointId,
                            std::shared_ptr<GainProcessor> processor,
                            std::string* error)
{
    stop();
    source_ = sourceEndpointId;
    target_ = targetEndpointId;
    processor_ = std::move(processor);
    stopRequested_ = false;

    // 0 = iniciando, 1 = ok, -1 = falhou
    std::atomic<int> startState{0};
    std::string threadError;
    thread_ = std::thread([this, &threadError, &startState] { run(&threadError, &startState); });
    while (startState.load() == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (startState.load() < 0) {
        thread_.join();
        if (error)
            *error = threadError;
        return false;
    }
    return true;
}

void LoopbackCapture::stop()
{
    stopRequested_ = true;
    if (thread_.joinable())
        thread_.join();
    running_ = false;
}

void LoopbackCapture::run(std::string* error, std::atomic<int>* startState)
{
    auto fail = [&](std::string msg) {
        *error = std::move(msg);
        startState->store(-1);
    };

    ComApartment com;
    if (!com.ok())
        return fail("CoInitializeEx failed");

    // Prioridade de áudio profissional (MMCSS) para evitar estalos.
    struct MmcssGuard {
        HANDLE h;
        ~MmcssGuard() { if (h) AvRevertMmThreadCharacteristics(h); }
    };
    DWORD taskIndex = 0;
    MmcssGuard mmcss{AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex)};

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  enumerator.putVoid());
    if (FAILED(hr))
        return fail(hrMessage("CoCreateInstance(MMDeviceEnumerator) failed", hr));

    ComPtr<IMMDevice> src, dst;
    if (FAILED(hr = enumerator->GetDevice(toWide(source_).c_str(), src.put())))
        return fail(hrMessage(LM_TRANSLATE_NOOP("backend", "Could not open the boost source device"), hr));
    if (FAILED(hr = enumerator->GetDevice(toWide(target_).c_str(), dst.put())))
        return fail(hrMessage(LM_TRANSLATE_NOOP("backend", "Could not open the output device"), hr));

    ComPtr<IAudioClient> capture, render;
    if (FAILED(hr = src->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, capture.putVoid())))
        return fail(hrMessage("IAudioClient activation failed (source)", hr));
    if (FAILED(hr = dst->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, render.putVoid())))
        return fail(hrMessage("IAudioClient activation failed (target)", hr));

    WAVEFORMATEX* fmt = nullptr;
    if (FAILED(hr = capture->GetMixFormat(&fmt)) || !fmt)
        return fail(hrMessage("GetMixFormat failed", hr));
    struct FmtGuard {
        WAVEFORMATEX* f;
        ~FmtGuard() { CoTaskMemFree(f); }
    } fmtGuard{fmt};
    if (!isFloat32(fmt))
        return fail(LM_TRANSLATE_NOOP("backend", "The boost source device does not use a float32 mix format (not supported in v1)."));
    const int channels = fmt->nChannels;

    if (FAILED(hr = capture->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kCaptureBuffer, 0, fmt, nullptr)))
        return fail(hrMessage("IAudioClient::Initialize (loopback) failed", hr));
    // O destino recebe o mesmo formato da origem; o Windows converte taxa e
    // canais se o dispositivo físico for diferente (AUTOCONVERTPCM).
    if (FAILED(hr = render->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                       AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                       kRenderBuffer, 0, fmt, nullptr)))
        return fail(hrMessage("IAudioClient::Initialize (render) failed", hr));

    ComPtr<IAudioCaptureClient> captureClient;
    ComPtr<IAudioRenderClient> renderClient;
    if (FAILED(hr = capture->GetService(__uuidof(IAudioCaptureClient), captureClient.putVoid())))
        return fail(hrMessage("GetService(IAudioCaptureClient) failed", hr));
    if (FAILED(hr = render->GetService(__uuidof(IAudioRenderClient), renderClient.putVoid())))
        return fail(hrMessage("GetService(IAudioRenderClient) failed", hr));

    UINT32 renderFrames = 0;
    render->GetBufferSize(&renderFrames);

    // Pré-enche o destino com silêncio para não começar com underrun.
    BYTE* pre = nullptr;
    if (SUCCEEDED(renderClient->GetBuffer(renderFrames / 2, &pre)))
        renderClient->ReleaseBuffer(renderFrames / 2, AUDCLNT_BUFFERFLAGS_SILENT);

    if (FAILED(hr = capture->Start()) || FAILED(hr = render->Start()))
        return fail(hrMessage("IAudioClient::Start failed", hr));

    processor_->resetRamp();
    running_ = true;
    startState->store(1);

    while (!stopRequested_.load()) {
        Sleep(kPollMs);
        UINT32 packet = 0;
        while (SUCCEEDED(captureClient->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;

            UINT32 padding = 0;
            render->GetCurrentPadding(&padding);
            // Se o destino estiver cheio (relógios diferentes), descarta o
            // excesso em vez de acumular latência.
            const UINT32 n = std::min(frames, renderFrames - padding);
            BYTE* out = nullptr;
            if (n > 0 && SUCCEEDED(renderClient->GetBuffer(n, &out))) {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::memset(out, 0, static_cast<std::size_t>(n) * fmt->nBlockAlign);
                } else {
                    processor_->process(reinterpret_cast<const float*>(data), reinterpret_cast<float*>(out), n, channels);
                }
                renderClient->ReleaseBuffer(n, 0);
            }
            captureClient->ReleaseBuffer(frames);
        }
    }

    capture->Stop();
    render->Stop();
    running_ = false;
}

} // namespace loudmixer::win
