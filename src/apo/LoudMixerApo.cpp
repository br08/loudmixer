// APO de sistema (EFX: efeito de endpoint, depois da mistura) do LoudMixer.
//
// Roda dentro do audiodg.exe. Aplica o GainProcessor (ganho até +6 dB com
// soft-clip) ao áudio final do dispositivo. O ganho vem da memória
// compartilhada (ApoShared.h), escrita pelo app; o valor inicial vem de
// HKLM\SOFTWARE\LoudMixer\Apo\{guid-do-endpoint} (DWORD, em %).
//
// Regras de tempo real em APOProcess: sem locks, sem alocação, sem chamadas
// ao sistema. Tudo que pode falhar acontece em Initialize/LockForProcess.

#include "apo/ApoInterfaces.h"
#include "apo/ApoRegistry.h"
#include "apo/ApoShared.h"
#include "core/GainProcessor.h"

#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

using namespace loudmixer::apo;

namespace {

std::atomic<long> g_objects{0};
std::atomic<long> g_serverLocks{0};

constexpr float kDefaultGain = 1.0f;

// Log de diagnóstico em %ProgramData%\LoudMixer\apo.log, ligado só quando
// HKLM\SOFTWARE\LoudMixer\Apo\DebugLog != 0 (lido uma vez por processo).
// Nunca é chamado do thread de tempo real. Limitado a ~1 MB.
bool logEnabled()
{
    static const bool enabled = [] {
        DWORD v = 0;
        DWORD size = sizeof(v);
        return RegGetValueW(HKEY_LOCAL_MACHINE, kApoSettingsKey, kDebugLogValue, RRF_RT_REG_DWORD, nullptr, &v, &size)
                == ERROR_SUCCESS
            && v != 0;
    }();
    return enabled;
}

void apoLog(const wchar_t* fmt, ...)
{
    if (!logEnabled())
        return;
    wchar_t path[MAX_PATH];
    if (!ExpandEnvironmentStringsW(L"%ProgramData%\\LoudMixer\\apo.log", path, MAX_PATH))
        return;
    wchar_t msg[768];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(msg, 768, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[1024];
    swprintf(line, 1024, L"%02u:%02u:%02u.%03u pid=%lu %ls\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
             GetCurrentProcessId(), msg);
    char utf8[2048];
    const int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (n <= 1)
        return;
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart < 1024 * 1024) {
        DWORD written = 0;
        WriteFile(f, utf8, static_cast<DWORD>(n - 1), &written, nullptr);
    }
    CloseHandle(f);
}

// A memória compartilhada pode ser escrita por qualquer processo do usuário:
// nada lido dela é confiável (ganhos já são limitados pelo GainProcessor).
loudmixer::ClipCurve sanitizeCurve(std::int32_t v)
{
    switch (v) {
    case static_cast<std::int32_t>(loudmixer::ClipCurve::Hard):
    case static_cast<std::int32_t>(loudmixer::ClipCurve::Tanh):
    case static_cast<std::int32_t>(loudmixer::ClipCurve::Cubic):
        return static_cast<loudmixer::ClipCurve>(v);
    default:
        return loudmixer::ClipCurve::Tanh;
    }
}

bool isFloat32(const UNCOMPRESSEDAUDIOFORMAT& f)
{
    return f.guidFormatType == kSubtypeIeeeFloat && f.dwBytesPerSampleContainer == 4 && f.dwValidBitsPerSample == 32
        && f.dwSamplesPerFrame >= 1 && f.dwSamplesPerFrame <= static_cast<DWORD>(loudmixer::GainProcessor::kMaxChannels);
}

float initialGainFor(const std::wstring& guid)
{
    const Persisted p = readPersisted(guid);
    return p.found ? p.master : kDefaultGain;
}

std::wstring guidText(const GUID& g)
{
    wchar_t buf[64] = L"?";
    StringFromGUID2(g, buf, 64);
    return buf;
}

// Máximo atômico com número limitado de tentativas: o alvo está numa memória
// que outros processos escrevem, e o thread de tempo real não pode ficar
// preso num laço de CAS. Perder uma atualização de pico é irrelevante.
void atomicMax(std::atomic<float>& target, float value)
{
    float cur = target.load(std::memory_order_relaxed);
    for (int i = 0; i < 4 && value > cur; ++i) {
        if (target.compare_exchange_weak(cur, value, std::memory_order_relaxed))
            return;
    }
}

// Lado "dono" da memória compartilhada (o APO cria; o app só abre).
class SharedSection {
public:
    ~SharedSection() { close(); }

    void createOrOpen(const std::wstring& endpointGuid)
    {
        close();
        // SY/LS/BA: controle total; AU (usuários autenticados): ler e escrever.
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;BA)(A;;GRGW;;;AU)", SDDL_REVISION_1, &sd, nullptr))
            return;
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};

        // No audiodg (sessão 0) "Global\" funciona. Fora dele (testes), criar
        // em Global exige privilégio; aí cai para o namespace da sessão.
        std::wstring name = sharedSectionName(endpointGuid);
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(SharedState), name.c_str());
        if (!mapping_) {
            apoLog(L"CreateFileMapping(%ls) falhou: %lu; tentando Local", name.c_str(), GetLastError());
            name.replace(0, 6, L"Local");
            mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(SharedState), name.c_str());
        }
        const DWORD err = GetLastError();
        const bool created = mapping_ && err != ERROR_ALREADY_EXISTS;
        LocalFree(sd);
        apoLog(L"seção %ls: %ls (erro %lu)", name.c_str(),
               !mapping_ ? L"FALHOU" : (created ? L"criada" : L"já existia"), err);
        if (!mapping_)
            return;

        view_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState));
        if (!view_) {
            close();
            return;
        }
        auto* s = get();
        if (created) {
            // A memória vem zerada do sistema; preenche e publica o magic por último.
            s->version = kSharedVersion;
            // Último estado salvo pelo app (ou pelo install): o boost continua
            // valendo quando o Windows recria o APO, mesmo com o app fechado.
            const Persisted saved = readPersisted(endpointGuid);
            s->gain.store(saved.found ? saved.master : kDefaultGain, std::memory_order_relaxed);
            s->curve.store(static_cast<std::int32_t>(loudmixer::ClipCurve::Tanh), std::memory_order_relaxed);
            for (int c = 0; c < kSharedMaxChannels; ++c)
                s->channelGains[c].store(c < static_cast<int>(saved.channels.size()) ? saved.channels[c] : 1.0f,
                                         std::memory_order_relaxed);
            s->magic.store(kSharedMagic, std::memory_order_release);
        } else {
            // Outra instância está criando: espera ela publicar (fora do tempo real).
            for (int i = 0; i < 200 && s->magic.load(std::memory_order_acquire) != kSharedMagic; ++i)
                Sleep(1);
            if (s->magic.load(std::memory_order_acquire) != kSharedMagic || s->version != kSharedVersion)
                close();
        }
    }

    void close()
    {
        if (view_)
            UnmapViewOfFile(view_);
        if (mapping_)
            CloseHandle(mapping_);
        view_ = nullptr;
        mapping_ = nullptr;
    }

    SharedState* get() const { return static_cast<SharedState*>(view_); }

private:
    HANDLE mapping_ = nullptr;
    void* view_ = nullptr;
};

// Interfaces COM não têm destrutor virtual; o objeto é final e se destrói
// pelo próprio Release().
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
class LoudMixerApo final : public IAudioProcessingObject,
                           public IAudioProcessingObjectRT,
                           public IAudioProcessingObjectConfiguration,
                           public IAudioSystemEffects2 {
public:
    // O audiodg cria APOs por agregação COM: passa um IUnknown externo e
    // pede o nosso IUnknown interno (não delegante). As demais interfaces
    // delegam QueryInterface/AddRef/Release ao externo. Sem agregação, o
    // "externo" é o próprio interno.
    explicit LoudMixerApo(IUnknown* outer) : outer_(outer ? outer : &inner_)
    {
        inner_.owner = this;
        ++g_objects;
        apoLog(L"instância criada (%ld ativas, agregada=%d)", g_objects.load(), outer != nullptr);
    }
    ~LoudMixerApo()
    {
        --g_objects;
        apoLog(L"instância destruída");
    }

    IUnknown* innerUnknown() { return &inner_; }

    // --- IUnknown (delegante) -----------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { return outer_->QueryInterface(riid, ppv); }
    ULONG STDMETHODCALLTYPE AddRef() override { return outer_->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return outer_->Release(); }

    // --- IAudioProcessingObject --------------------------------------------------
    HRESULT STDMETHODCALLTYPE Reset() override
    {
        apoLog(L"Reset");
        processor_.resetRamp();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* pTime) override
    {
        if (!pTime)
            return E_POINTER;
        apoLog(L"GetLatency");
        *pTime = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) override
    {
        if (!ppRegProps)
            return E_POINTER;
        apoLog(L"GetRegistrationProperties");
        auto* p = static_cast<APO_REG_PROPERTIES*>(CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES)));
        if (!p)
            return E_OUTOFMEMORY;
        fillRegistrationProperties(*p);
        *ppRegProps = p;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Initialize(UINT32 cbDataSize, BYTE* pbyData) override
    {
        if (initialized_)
            return APOERR_ALREADY_INITIALIZED;
        apoLog(L"Initialize: cbDataSize=%u", cbDataSize);
        if (pbyData && cbDataSize >= sizeof(APOInitSystemEffectsPrefix)) {
            const auto* init = reinterpret_cast<const APOInitSystemEffectsPrefix*>(pbyData);
            if (init->APOInit.cbSize != cbDataSize)
                return E_INVALIDARG;
            try {
                const std::wstring guid = endpointGuid(init->pAPOEndpointProperties);
                apoLog(L"Initialize: endpoint=%ls props=%p", guid.empty() ? L"(desconhecido)" : guid.c_str(),
                       static_cast<void*>(init->pAPOEndpointProperties));
                if (cbDataSize >= sizeof(APOInitSystemEffects2Layout)) {
                    const auto* v2 = reinterpret_cast<const APOInitSystemEffects2Layout*>(pbyData);
                    apoLog(L"Initialize: modo=%ls discoveryOnly=%d", guidText(v2->AudioProcessingMode).c_str(),
                           v2->InitializeForDiscoveryOnly);
                }
                if (!guid.empty()) {
                    fallbackGain_ = initialGainFor(guid);
                    shared_.createOrOpen(guid);
                }
            } catch (...) {
                return E_OUTOFMEMORY;
            }
        }
        initialized_ = true;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* pOppositeFormat,
                                                     IAudioMediaType* pRequestedInputFormat,
                                                     IAudioMediaType** ppSupportedInputFormat) override
    {
        return checkFormat(pOppositeFormat, pRequestedInputFormat, ppSupportedInputFormat);
    }

    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* pOppositeFormat,
                                                      IAudioMediaType* pRequestedOutputFormat,
                                                      IAudioMediaType** ppSupportedOutputFormat) override
    {
        return checkFormat(pOppositeFormat, pRequestedOutputFormat, ppSupportedOutputFormat);
    }

    HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* pu32ChannelCount) override
    {
        if (!pu32ChannelCount)
            return E_POINTER;
        apoLog(L"GetInputChannelCount = %u", channels_);
        *pu32ChannelCount = channels_;
        return S_OK;
    }

    // --- IAudioProcessingObjectConfiguration ---------------------------------
    HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 u32NumInputConnections,
                                             APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                             UINT32 u32NumOutputConnections,
                                             APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override
    {
        if (!initialized_)
            return APOERR_NOT_INITIALIZED;
        if (locked_.load())
            return APOERR_APO_LOCKED;
        if (u32NumInputConnections != 1 || u32NumOutputConnections != 1 || !ppInputConnections || !ppOutputConnections
            || !ppInputConnections[0] || !ppOutputConnections[0] || !ppInputConnections[0]->pFormat)
            return APOERR_NUM_CONNECTIONS_INVALID;

        UNCOMPRESSEDAUDIOFORMAT f{};
        const HRESULT hr = ppInputConnections[0]->pFormat->GetUncompressedAudioFormat(&f);
        if (FAILED(hr))
            return hr;
        apoLog(L"LockForProcess: %u canais, %.0f Hz, %u bits, float=%d, maxFrames=%u", f.dwSamplesPerFrame,
               static_cast<double>(f.fFramesPerSecond), f.dwValidBitsPerSample, f.guidFormatType == kSubtypeIeeeFloat,
               ppInputConnections[0]->u32MaxFrameCount);
        if (!isFloat32(f))
            return APOERR_FORMAT_NOT_SUPPORTED;

        channels_ = f.dwSamplesPerFrame;
        maxFrames_ = std::min(ppInputConnections[0]->u32MaxFrameCount, ppOutputConnections[0]->u32MaxFrameCount);
        blocks_.store(0, std::memory_order_relaxed);
        processor_.resetRamp();
        if (auto* s = shared_.get()) {
            s->sampleRate.store(static_cast<std::uint32_t>(f.fFramesPerSecond), std::memory_order_relaxed);
            s->channels.store(channels_, std::memory_order_relaxed);
            s->lockedInstances.fetch_add(1, std::memory_order_relaxed);
        }
        locked_.store(true, std::memory_order_release);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UnlockForProcess() override
    {
        if (!locked_.load())
            return APOERR_ALREADY_UNLOCKED;
        locked_.store(false, std::memory_order_release);
        if (auto* s = shared_.get())
            s->lockedInstances.fetch_sub(1, std::memory_order_relaxed);
        apoLog(L"UnlockForProcess: %llu blocos processados", static_cast<unsigned long long>(blocks_.load()));
        return S_OK;
    }

    // --- IAudioProcessingObjectRT (tempo real) --------------------------------
    void STDMETHODCALLTYPE APOProcess(UINT32 u32NumInputConnections,
                                      APO_CONNECTION_PROPERTY** ppInputConnections,
                                      UINT32 u32NumOutputConnections,
                                      APO_CONNECTION_PROPERTY** ppOutputConnections) override
    {
        if (!locked_.load(std::memory_order_acquire) || u32NumInputConnections != 1 || u32NumOutputConnections != 1)
            return;
        APO_CONNECTION_PROPERTY* in = ppInputConnections[0];
        APO_CONNECTION_PROPERTY* out = ppOutputConnections[0];
        if (!in || !out || !in->pBuffer || !out->pBuffer)
            return;
        // Defesa: nunca processa além do máximo combinado no LockForProcess.
        const UINT32 frames = std::min(in->u32ValidFrameCount, maxFrames_);
        SharedState* s = shared_.get();
        blocks_.fetch_add(1, std::memory_order_relaxed);

        if (in->u32BufferFlags == BUFFER_SILENT) {
            // Silêncio continua silêncio: só repassa a marcação.
            out->u32ValidFrameCount = frames;
            out->u32BufferFlags = BUFFER_SILENT;
            if (s)
                s->heartbeat.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto* src = reinterpret_cast<const float*>(in->pBuffer);
        auto* dst = reinterpret_cast<float*>(out->pBuffer);
        const bool bypass = s && s->bypass.load(std::memory_order_relaxed) != 0;
        if (bypass) {
            if (src != dst)
                std::memcpy(dst, src, static_cast<size_t>(frames) * channels_ * sizeof(float));
        } else {
            if (s) {
                processor_.setGain(s->gain.load(std::memory_order_relaxed));
                processor_.setCurve(sanitizeCurve(s->curve.load(std::memory_order_relaxed)));
                const int n = std::min(static_cast<int>(channels_), kSharedMaxChannels);
                for (int c = 0; c < n; ++c)
                    processor_.setChannelGain(c, s->channelGains[c].load(std::memory_order_relaxed));
            } else {
                processor_.setGain(fallbackGain_);
            }
            processor_.process(src, dst, frames, static_cast<int>(channels_));
        }
        out->u32ValidFrameCount = frames;
        out->u32BufferFlags = BUFFER_VALID;

        if (s) {
            const auto st = processor_.takeStats();
            s->processedSamples.fetch_add(st.processedSamples, std::memory_order_relaxed);
            s->clippedSamples.fetch_add(st.clippedSamples, std::memory_order_relaxed);
            atomicMax(s->peakIn, st.peakIn);
            atomicMax(s->peakOut, st.peakOut);
            s->heartbeat.fetch_add(1, std::memory_order_relaxed);
        }
    }

    UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 u32OutputFrameCount) override { return u32OutputFrameCount; }
    UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 u32InputFrameCount) override { return u32InputFrameCount; }

    // --- IAudioSystemEffects2 ---------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE) override
    {
        if (!ppEffectsIds || !pcEffects)
            return E_POINTER;
        apoLog(L"GetEffectsList");
        *ppEffectsIds = nullptr;
        *pcEffects = 0;
        return S_OK;
    }

    static void fillRegistrationProperties(APO_REG_PROPERTIES& p)
    {
        std::memset(&p, 0, sizeof(p));
        p.clsid = CLSID_LoudMixerApo;
        p.Flags = static_cast<APO_FLAG>(APO_FLAG_INPLACE | APO_FLAG_DEFAULT);
        wcscpy_s(p.szFriendlyName, L"LoudMixer Boost");
        wcscpy_s(p.szCopyrightInfo, L"LoudMixer");
        p.u32MajorVersion = 1;
        p.u32MinorVersion = 0;
        p.u32MinInputConnections = 1;
        p.u32MaxInputConnections = 1;
        p.u32MinOutputConnections = 1;
        p.u32MaxOutputConnections = 1;
        p.u32MaxInstances = 0xFFFFFFFF;
        p.u32NumAPOInterfaces = 1;
        p.iidAPOInterfaceList[0] = IID_IAudioProcessingObject;
    }

private:
    // IUnknown interno (não delegante): dono do ciclo de vida do objeto.
    struct Inner final : IUnknown {
        LoudMixerApo* owner = nullptr;
        LoudMixerApo* self() const { return owner; }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (!ppv)
                return E_POINTER;
            if (riid == IID_IUnknown) {
                *ppv = static_cast<IUnknown*>(this);
                AddRef();
                return S_OK;
            }
            if (!self()->interfaceFor(riid, ppv)) {
                apoLog(L"QueryInterface(%ls) = E_NOINTERFACE", guidText(riid).c_str());
                return E_NOINTERFACE;
            }
            static_cast<IUnknown*>(*ppv)->AddRef(); // delega ao externo (regra da agregação)
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++self()->refs_; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            LoudMixerApo* s = self();
            const ULONG r = --s->refs_;
            if (r == 0)
                delete s;
            return r;
        }
    };

    bool interfaceFor(REFIID riid, void** ppv)
    {
        if (riid == IID_IAudioProcessingObject)
            *ppv = static_cast<IAudioProcessingObject*>(this);
        else if (riid == IID_IAudioProcessingObjectRT)
            *ppv = static_cast<IAudioProcessingObjectRT*>(this);
        else if (riid == IID_IAudioProcessingObjectConfiguration)
            *ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
        else if (riid == IID_IAudioSystemEffects)
            *ppv = static_cast<IAudioSystemEffects*>(static_cast<IAudioSystemEffects2*>(this));
        else if (riid == IID_IAudioSystemEffects2)
            *ppv = static_cast<IAudioSystemEffects2*>(this);
        else {
            *ppv = nullptr;
            return false;
        }
        return true;
    }

    static std::wstring endpointGuid(IPropertyStore* props)
    {
        if (!props)
            return {};
        PROPVARIANT v;
        PropVariantInit(&v);
        std::wstring guid;
        if (SUCCEEDED(props->GetValue(kPkeyAudioEndpointGuid, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
            guid = v.pwszVal;
        PropVariantClear(&v);
        return guid;
    }

    HRESULT checkFormat(IAudioMediaType* opposite, IAudioMediaType* requested, IAudioMediaType** supported)
    {
        if (!requested || !supported)
            return E_POINTER;
        *supported = nullptr;
        if (locked_.load())
            return APOERR_APO_LOCKED;
        UNCOMPRESSEDAUDIOFORMAT f{};
        if (FAILED(requested->GetUncompressedAudioFormat(&f)) || !isFloat32(f)) {
            apoLog(L"formato recusado: %u canais, %u bits, float=%d", f.dwSamplesPerFrame, f.dwValidBitsPerSample,
                   f.guidFormatType == kSubtypeIeeeFloat);
            return APOERR_FORMAT_NOT_SUPPORTED;
        }
        if (opposite) {
            UNCOMPRESSEDAUDIOFORMAT o{};
            if (SUCCEEDED(opposite->GetUncompressedAudioFormat(&o))
                && (o.dwSamplesPerFrame != f.dwSamplesPerFrame || o.fFramesPerSecond != f.fFramesPerSecond))
                return APOERR_FORMAT_NOT_SUPPORTED;
        }
        apoLog(L"formato aceito: %u canais, %.0f Hz (oposto=%p)", f.dwSamplesPerFrame,
               static_cast<double>(f.fFramesPerSecond), static_cast<void*>(opposite));
        requested->AddRef();
        *supported = requested;
        return S_OK;
    }

    Inner inner_;
    IUnknown* outer_;
    std::atomic<ULONG> refs_{1};
    std::atomic<std::uint64_t> blocks_{0};
    bool initialized_ = false;
    std::atomic<bool> locked_{false};
    UINT32 channels_ = 0;
    UINT32 maxFrames_ = 0;
    float fallbackGain_ = kDefaultGain;
    loudmixer::GainProcessor processor_{kDefaultGain, loudmixer::ClipCurve::Tanh};
    SharedSection shared_;
};

class ClassFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    // Fábrica estática: vive enquanto a DLL estiver carregada.
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        apoLog(L"CreateInstance(outer=%p, riid=%ls)", static_cast<void*>(outer), guidText(riid).c_str());
        // Regra da agregação: com externo, só se pode pedir o IUnknown interno.
        if (outer && riid != IID_IUnknown)
            return CLASS_E_NOAGGREGATION;
        auto* apo = new (std::nothrow) LoudMixerApo(outer);
        if (!apo)
            return E_OUTOFMEMORY;
        IUnknown* inner = apo->innerUnknown();
        const HRESULT hr = inner->QueryInterface(riid, ppv);
        if (FAILED(hr))
            apoLog(L"CreateInstance: interface não suportada (0x%08lX)", static_cast<unsigned long>(hr));
        inner->Release(); // solta a referência inicial
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    {
        if (lock)
            ++g_serverLocks;
        else
            --g_serverLocks;
        return S_OK;
    }
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

ClassFactory g_factory;

} // namespace

// Exportadas via loudmixer_apo.def.
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_LoudMixerApo)
        return CLASS_E_CLASSNOTAVAILABLE;
    const HRESULT hr = g_factory.QueryInterface(riid, ppv);
    apoLog(L"DllGetClassObject(riid=%ls) = 0x%08lX", guidText(riid).c_str(), static_cast<unsigned long>(hr));
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return (g_objects.load() == 0 && g_serverLocks.load() == 0) ? S_OK : S_FALSE;
}
