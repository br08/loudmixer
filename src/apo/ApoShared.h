#pragma once
// Estado compartilhado entre o APO (que roda dentro do audiodg.exe, como
// LOCAL SERVICE) e o LoudMixer / ferramenta de setup (sessão do usuário).
//
// O APO cria uma seção de memória "Global\LoudMixer.Apo.{guid-do-endpoint}"
// com uma DACL que deixa usuários autenticados lerem e escreverem. O app só
// escreve o ganho e lê as estatísticas; tudo é atômico e sem locks, porque o
// APO lê no thread de tempo real do mecanismo de áudio.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cwctype>
#include <string>

namespace loudmixer::apo {

constexpr std::uint32_t kSharedMagic = 0x4C4D4150; // "LMAP"
constexpr std::uint32_t kSharedVersion = 1;

struct SharedState {
    std::atomic<std::uint32_t> magic;   // escrito por último: indica que o resto está pronto
    std::uint32_t version;
    std::atomic<float> gain;            // linear, 0..2 (escrito pelo app)
    std::atomic<std::int32_t> curve;    // loudmixer::ClipCurve
    std::atomic<std::uint32_t> bypass;  // != 0: passa o áudio sem processar
    std::atomic<std::uint32_t> sampleRate;
    std::atomic<std::uint32_t> channels;
    std::atomic<std::uint32_t> lockedInstances;
    std::atomic<std::uint64_t> heartbeat; // blocos processados (prova de vida)
    std::atomic<std::uint64_t> processedSamples;
    std::atomic<std::uint64_t> clippedSamples;
    std::atomic<float> peakIn;  // máximos desde a última leitura (o leitor zera)
    std::atomic<float> peakOut;
};

static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

// Chave em HKLM com o ganho inicial por endpoint (DWORD, em %), lida pelo APO
// ao criar a seção. Assim o boost vale mesmo antes de o app abrir.
constexpr const wchar_t* kApoSettingsKey = L"SOFTWARE\\LoudMixer\\Apo";

inline std::wstring normalizeEndpointGuid(std::wstring guid)
{
    // Aceita "{0.0.0.00000000}.{guid}" (id do IMMDevice) ou só "{guid}".
    const auto dot = guid.rfind(L"}.{");
    if (dot != std::wstring::npos)
        guid = guid.substr(dot + 2);
    for (auto& c : guid)
        c = static_cast<wchar_t>(std::towlower(c));
    return guid;
}

inline std::wstring sharedSectionName(const std::wstring& endpointGuid)
{
    return L"Global\\LoudMixer.Apo." + normalizeEndpointGuid(endpointGuid);
}

// Abre a seção já criada pelo APO (lado do app / ferramenta).
class SharedView {
public:
    SharedView() = default;
    explicit SharedView(const std::wstring& endpointGuid) { open(endpointGuid); }
    ~SharedView() { close(); }
    SharedView(const SharedView&) = delete;
    SharedView& operator=(const SharedView&) = delete;

    bool open(const std::wstring& endpointGuid)
    {
        close();
        std::wstring name = sharedSectionName(endpointGuid);
        mapping_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str());
        if (!mapping_) {
            // O APO cai para "Local\" quando não pode criar em "Global\" (testes).
            name.replace(0, 6, L"Local");
            mapping_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str());
        }
        if (!mapping_)
            return false;
        view_ = MapViewOfFile(mapping_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedState));
        auto* s = static_cast<SharedState*>(view_);
        if (!s || s->magic.load(std::memory_order_acquire) != kSharedMagic || s->version != kSharedVersion) {
            close();
            return false;
        }
        return true;
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
    explicit operator bool() const { return view_ != nullptr; }

private:
    HANDLE mapping_ = nullptr;
    void* view_ = nullptr;
};

} // namespace loudmixer::apo
