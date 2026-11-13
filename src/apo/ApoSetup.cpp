// loudmixer-apo-setup: registra/remove o APO do LoudMixer num dispositivo de
// saída e mostra o estado dele.
//
//   loudmixer-apo-setup list
//   loudmixer-apo-setup install   <endpoint|default> [--gain 100] [--stage auto|efx|mfx|sfx]
//                                 [--no-verify] [--debug-log]          (admin)
//   loudmixer-apo-setup uninstall <endpoint|default|all>               (admin)
//   loudmixer-apo-setup status    <endpoint|default>
//   loudmixer-apo-setup set-gain  <endpoint|default> <percent>
//   ... [--log arquivo]   (útil quando roda elevado, sem console visível)
//
// Saída: 0 = ok, 1 = erro, 2 = uso inválido, 3 = nenhum estágio da cadeia
// funcionou no dispositivo (install; o dispositivo é restaurado).
//
// O install guarda os valores originais de FxProperties em
// HKLM\SOFTWARE\LoudMixer\Apo\Backup\{guid}; o uninstall os restaura
// exatamente (inclusive apagando o que não existia antes).

#include "apo/ApoInterfaces.h"
#include "apo/ApoShared.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace loudmixer::apo;

namespace {

// --- Saída -------------------------------------------------------------------------

FILE* g_log = nullptr;

std::string utf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

void say(const std::wstring& line)
{
    const std::string s = utf8(line) + "\n";
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
    if (g_log) {
        std::fwrite(s.data(), 1, s.size(), g_log);
        std::fflush(g_log);
    }
}

std::wstring errorText(DWORD code)
{
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring s = buf ? buf : L"";
    if (buf)
        LocalFree(buf);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' '))
        s.pop_back();
    return std::to_wstring(code) + L" (" + s + L")";
}

struct Failure {
    std::wstring message;
};

void check(LONG rc, const std::wstring& what)
{
    if (rc != ERROR_SUCCESS)
        throw Failure{what + L": " + errorText(static_cast<DWORD>(rc))};
}

// --- Constantes do registro ---------------------------------------------------------

const std::wstring kRenderRoot = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render\\";
const std::wstring kSfxComposite = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13"; // PKEY_CompositeFX_StreamEffectClsid
const std::wstring kMfxComposite = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14"; // PKEY_CompositeFX_ModeEffectClsid
const std::wstring kEfxComposite = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15"; // PKEY_CompositeFX_EndpointEffectClsid
const std::wstring kEfxLegacy = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7";     // PKEY_FX_EndpointEffectClsid
const std::wstring kSfxModes = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},5";      // PKEY_SFX_ProcessingModes_Supported_For_Streaming
const std::wstring kMfxModes = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},6";      // PKEY_MFX_ProcessingModes_Supported_For_Streaming
const std::wstring kEfxModes = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},7";      // PKEY_EFX_ProcessingModes_Supported_For_Streaming
const std::wstring kDisableSysFx = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";  // PKEY_AudioEndpoint_Disable_SysFx
const std::wstring kModeDefault = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";     // AUDIO_SIGNALPROCESSINGMODE_DEFAULT
const std::wstring kTouchedValues[] = {kSfxComposite, kMfxComposite, kEfxComposite, kSfxModes,
                                       kMfxModes,     kEfxModes,     kDisableSysFx};

// Estágios da cadeia de efeitos onde o APO pode entrar.
struct Stage {
    const wchar_t* name;
    const std::wstring* composite;
    const std::wstring* modes;
};
const Stage kStages[] = {{L"sfx", &kSfxComposite, &kSfxModes},
                         {L"mfx", &kMfxComposite, &kMfxModes},
                         {L"efx", &kEfxComposite, &kEfxModes}};

const std::wstring kBackupRoot = std::wstring(kApoSettingsKey) + L"\\Backup";
const std::wstring kAbsentMarker = L"__absent";
const std::wstring kClsidKey = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kApoClsidString;
const std::wstring kApoRegKey = std::wstring(L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\") + kApoClsidString;

// PKEY_Device_FriendlyName, definido aqui (os headers variam entre SDK e MinGW).
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

bool sameText(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

// --- Registro ------------------------------------------------------------------------

class Key {
public:
    Key() = default;
    ~Key() { reset(); }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;

    static Key open(HKEY root, const std::wstring& path, REGSAM access)
    {
        Key k;
        check(RegOpenKeyExW(root, path.c_str(), 0, access, &k.h_), L"abrir " + path);
        return k;
    }
    static std::optional<Key> tryOpen(HKEY root, const std::wstring& path, REGSAM access)
    {
        Key k;
        if (RegOpenKeyExW(root, path.c_str(), 0, access, &k.h_) != ERROR_SUCCESS)
            return std::nullopt;
        return std::optional<Key>(std::move(k));
    }
    static Key create(HKEY root, const std::wstring& path)
    {
        Key k;
        check(RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &k.h_, nullptr),
              L"criar " + path);
        return k;
    }

    Key(Key&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    HKEY get() const { return h_; }

    std::optional<std::vector<std::wstring>> multiSz(const std::wstring& name) const
    {
        DWORD type = 0, size = 0;
        if (RegQueryValueExW(h_, name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_MULTI_SZ)
            return std::nullopt;
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, L'\0');
        if (RegQueryValueExW(h_, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &size) != ERROR_SUCCESS)
            return std::nullopt;
        std::vector<std::wstring> out;
        for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1)
            out.emplace_back(p);
        return out;
    }
    std::optional<std::wstring> string(const std::wstring& name) const
    {
        DWORD type = 0, size = 0;
        if (RegQueryValueExW(h_, name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ)
            return std::nullopt;
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(h_, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &size) != ERROR_SUCCESS)
            return std::nullopt;
        return std::wstring(buf.data());
    }
    std::optional<DWORD> dword(const std::wstring& name) const
    {
        DWORD v = 0, size = sizeof(v), type = 0;
        if (RegQueryValueExW(h_, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(&v), &size) != ERROR_SUCCESS
            || type != REG_DWORD)
            return std::nullopt;
        return v;
    }
    // Valor bruto (tipo + bytes), para backup fiel.
    bool raw(const std::wstring& name, DWORD& type, std::vector<BYTE>& data) const
    {
        DWORD size = 0;
        if (RegQueryValueExW(h_, name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
            return false;
        data.resize(size);
        return RegQueryValueExW(h_, name.c_str(), nullptr, &type, data.data(), &size) == ERROR_SUCCESS;
    }

    void setMultiSz(const std::wstring& name, const std::vector<std::wstring>& values) const
    {
        std::wstring blob;
        for (const auto& v : values) {
            blob += v;
            blob.push_back(L'\0');
        }
        blob.push_back(L'\0');
        check(RegSetValueExW(h_, name.c_str(), 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(blob.data()),
                             static_cast<DWORD>(blob.size() * sizeof(wchar_t))),
              L"gravar " + name);
    }
    void setString(const std::wstring& name, const std::wstring& value) const
    {
        check(RegSetValueExW(h_, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(value.c_str()),
                             static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))),
              L"gravar " + name);
    }
    void setDword(const std::wstring& name, DWORD value) const
    {
        check(RegSetValueExW(h_, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)),
              L"gravar " + name);
    }
    void setRaw(const std::wstring& name, DWORD type, const std::vector<BYTE>& data) const
    {
        check(RegSetValueExW(h_, name.c_str(), 0, type, data.data(), static_cast<DWORD>(data.size())), L"gravar " + name);
    }
    void remove(const std::wstring& name) const
    {
        const LONG rc = RegDeleteValueW(h_, name.c_str());
        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND)
            check(rc, L"apagar " + name);
    }

private:
    void reset()
    {
        if (h_)
            RegCloseKey(h_);
        h_ = nullptr;
    }
    HKEY h_ = nullptr;
};

std::wstring fxPath(const std::wstring& guid) { return kRenderRoot + guid + L"\\FxProperties"; }

bool containsClsid(const std::vector<std::wstring>& list)
{
    return std::any_of(list.begin(), list.end(), [](const std::wstring& s) { return sameText(s, kApoClsidString); });
}

// --- Dispositivos ---------------------------------------------------------------------

struct Endpoint {
    std::wstring id; // id completo do IMMDevice
    std::wstring guid;
    std::wstring name;
    bool isDefault = false;
};

struct ComInit {
    ComInit() { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComInit() { CoUninitialize(); }
};

template <typename T>
struct Com {
    T* p = nullptr;
    ~Com()
    {
        if (p)
            p->Release();
    }
    T* operator->() const { return p; }
    T** put() { return &p; }
};

std::vector<Endpoint> listEndpoints()
{
    std::vector<Endpoint> out;
    Com<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(en.put()))))
        throw Failure{L"não foi possível acessar os dispositivos de áudio"};

    std::wstring defaultGuid;
    {
        Com<IMMDevice> def;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, def.put()))) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(def->GetId(&id))) {
                defaultGuid = normalizeEndpointGuid(id);
                CoTaskMemFree(id);
            }
        }
    }

    Com<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, col.put())))
        return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        Com<IMMDevice> dev;
        if (FAILED(col->Item(i, dev.put())))
            continue;
        Endpoint e;
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id))) {
            e.id = id;
            e.guid = normalizeEndpointGuid(id);
            CoTaskMemFree(id);
        }
        Com<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, props.put()))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR)
                e.name = v.pwszVal;
            PropVariantClear(&v);
        }
        e.isDefault = e.guid == defaultGuid;
        out.push_back(std::move(e));
    }
    return out;
}

Endpoint resolveEndpoint(const std::wstring& arg)
{
    const auto all = listEndpoints();
    for (const auto& e : all) {
        if ((sameText(arg, L"default") && e.isDefault) || e.guid == normalizeEndpointGuid(arg))
            return e;
    }
    throw Failure{L"dispositivo não encontrado: " + arg + L" (veja 'list')"};
}

// --- Serviços de áudio ------------------------------------------------------------------

struct ServiceHandle {
    SC_HANDLE h = nullptr;
    ~ServiceHandle()
    {
        if (h)
            CloseServiceHandle(h);
    }
};

bool waitState(SC_HANDLE svc, DWORD wanted)
{
    for (int i = 0; i < 200; ++i) {
        SERVICE_STATUS st{};
        if (!QueryServiceStatus(svc, &st))
            return false;
        if (st.dwCurrentState == wanted)
            return true;
        Sleep(100);
    }
    return false;
}

// Para o serviço e, antes, os que dependem dele. Guarda a ordem para religar.
void stopServiceTree(SC_HANDLE scm, const std::wstring& name, std::vector<std::wstring>& stopped)
{
    ServiceHandle svc{OpenServiceW(scm, name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS)};
    if (!svc.h)
        throw Failure{L"abrir serviço " + name + L": " + errorText(GetLastError())};
    SERVICE_STATUS st{};
    QueryServiceStatus(svc.h, &st);
    if (st.dwCurrentState == SERVICE_STOPPED)
        return;

    DWORD bytes = 0, count = 0;
    EnumDependentServicesW(svc.h, SERVICE_ACTIVE, nullptr, 0, &bytes, &count);
    if (bytes) {
        std::vector<BYTE> buf(bytes);
        auto* deps = reinterpret_cast<ENUM_SERVICE_STATUSW*>(buf.data());
        if (EnumDependentServicesW(svc.h, SERVICE_ACTIVE, deps, bytes, &bytes, &count)) {
            for (DWORD i = 0; i < count; ++i)
                stopServiceTree(scm, deps[i].lpServiceName, stopped);
        }
    }
    if (!ControlService(svc.h, SERVICE_CONTROL_STOP, &st) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
        throw Failure{L"parar " + name + L": " + errorText(GetLastError())};
    if (!waitState(svc.h, SERVICE_STOPPED))
        throw Failure{L"o serviço " + name + L" não parou a tempo"};
    say(L"  serviço parado: " + name);
    stopped.push_back(name);
}

// Para o áudio no construtor e religa no destrutor (mesmo se algo falhar).
class AudioServicesStopped {
public:
    AudioServicesStopped()
    {
        scm_.h = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm_.h)
            throw Failure{L"abrir o gerenciador de serviços: " + errorText(GetLastError())};
        try {
            stopServiceTree(scm_.h, L"Audiosrv", stopped_);
            stopServiceTree(scm_.h, L"AudioEndpointBuilder", stopped_);
        } catch (...) {
            restart();
            throw;
        }
    }
    ~AudioServicesStopped() { restart(); }

private:
    void restart()
    {
        for (auto it = stopped_.rbegin(); it != stopped_.rend(); ++it) {
            ServiceHandle svc{OpenServiceW(scm_.h, it->c_str(), SERVICE_START | SERVICE_QUERY_STATUS)};
            if (svc.h && (StartServiceW(svc.h, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)) {
                waitState(svc.h, SERVICE_RUNNING);
                say(L"  serviço iniciado: " + *it);
            } else {
                say(L"  AVISO: não consegui iniciar " + *it + L": " + errorText(GetLastError()));
            }
        }
        stopped_.clear();
    }
    ServiceHandle scm_;
    std::vector<std::wstring> stopped_;
};

// --- Instalação ------------------------------------------------------------------------

bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION el{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &el, sizeof(el), &size) && el.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

std::wstring moduleDir()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L"\\/"));
}

std::wstring programFilesDir()
{
    PWSTR pf = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &pf)))
        throw Failure{L"pasta Program Files não encontrada"};
    std::wstring dir = pf;
    CoTaskMemFree(pf);
    return dir;
}

// Pasta para onde a DLL é copiada quando a ferramenta roda de fora de
// Program Files (build de desenvolvimento).
std::wstring installDir() { return programFilesDir() + L"\\LoudMixer\\apo"; }

bool isUnder(const std::wstring& path, const std::wstring& dir)
{
    return path.size() > dir.size() && _wcsnicmp(path.c_str(), dir.c_str(), dir.size()) == 0
        && (path[dir.size()] == L'\\' || path[dir.size()] == L'/');
}

// O audiodg roda como LOCAL SERVICE e não lê a pasta do usuário: a DLL
// precisa ficar num lugar legível pelo sistema. Instalado (Program Files),
// registra no lugar; em desenvolvimento, copia para Program Files.
std::wstring deployDll()
{
    const std::wstring src = moduleDir() + L"\\loudmixer_apo.dll";
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES)
        throw Failure{L"DLL do APO não encontrada ao lado da ferramenta: " + src};
    if (isUnder(src, programFilesDir())) {
        say(L"  DLL: " + src);
        return src;
    }
    const std::wstring dir = installDir();
    const std::wstring dst = dir + L"\\loudmixer_apo.dll";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE))
        throw Failure{L"copiar a DLL para " + dst + L": " + errorText(GetLastError())};
    say(L"  DLL copiada para: " + dst);
    return dst;
}

std::wstring logDir()
{
    wchar_t dir[MAX_PATH];
    if (!ExpandEnvironmentStringsW(L"%ProgramData%\\LoudMixer", dir, MAX_PATH))
        return {};
    return dir;
}

void registerCom(const std::wstring& dll)
{
    {
        Key k = Key::create(HKEY_LOCAL_MACHINE, kClsidKey);
        k.setString(L"", L"LoudMixer Boost APO");
        Key srv = Key::create(HKEY_LOCAL_MACHINE, kClsidKey + L"\\InprocServer32");
        srv.setString(L"", dll);
        srv.setString(L"ThreadingModel", L"Both");
    }
    Key a = Key::create(HKEY_LOCAL_MACHINE, kApoRegKey);
    a.setString(L"FriendlyName", L"LoudMixer Boost");
    a.setString(L"Copyright", L"LoudMixer");
    a.setDword(L"MajorVersion", 1);
    a.setDword(L"MinorVersion", 0);
    a.setDword(L"Flags", APO_FLAG_INPLACE | APO_FLAG_DEFAULT);
    a.setDword(L"MinInputConnections", 1);
    a.setDword(L"MaxInputConnections", 1);
    a.setDword(L"MinOutputConnections", 1);
    a.setDword(L"MaxOutputConnections", 1);
    a.setDword(L"MaxInstances", 0xFFFFFFFF);
    a.setDword(L"NumAPOInterfaces", 1);
    a.setString(L"APOInterface0", L"{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}");
    say(L"  COM e APO registrados: " + std::wstring(kApoClsidString));
}

void unregisterCom()
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKey.c_str());
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, kClsidKey.c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kApoRegKey.c_str());
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, kApoRegKey.c_str());
    say(L"  registro COM/APO removido");
}

// Guarda o valor original de cada valor na primeira vez em que ele é tocado
// (reinstalar não pode sobrescrever o original com o nosso próprio valor).
void backupValues(const Key& fx, const std::wstring& guid)
{
    const std::wstring path = kBackupRoot + L"\\" + guid;
    Key b = Key::create(HKEY_LOCAL_MACHINE, path);
    std::vector<std::wstring> absent = b.multiSz(kAbsentMarker).value_or(std::vector<std::wstring>{});
    bool changed = false;
    for (const auto& name : kTouchedValues) {
        DWORD t = 0;
        std::vector<BYTE> d;
        const bool known = b.raw(name, t, d)
            || std::any_of(absent.begin(), absent.end(), [&](const std::wstring& a) { return sameText(a, name); });
        if (known)
            continue;
        if (fx.raw(name, t, d))
            b.setRaw(name, t, d);
        else
            absent.push_back(name);
        changed = true;
    }
    b.setMultiSz(kAbsentMarker, absent);
    if (changed)
        say(L"  backup dos valores originais em HKLM\\" + path);
}

void restoreValues(const std::wstring& guid)
{
    const std::wstring path = kBackupRoot + L"\\" + guid;
    auto b = Key::tryOpen(HKEY_LOCAL_MACHINE, path, KEY_READ);
    if (!b) {
        say(L"  sem backup para " + guid + L" (nada a restaurar)");
        return;
    }
    Key fx = Key::open(HKEY_LOCAL_MACHINE, fxPath(guid), KEY_QUERY_VALUE | KEY_SET_VALUE);
    for (const auto& name : kTouchedValues) {
        DWORD type = 0;
        std::vector<BYTE> data;
        if (b->raw(name, type, data))
            fx.setRaw(name, type, data);
    }
    if (auto absent = b->multiSz(kAbsentMarker)) {
        for (const auto& name : *absent)
            fx.remove(name);
    }
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, path.c_str());
    say(L"  FxProperties restaurado: " + guid);
}

// Pasta do log de diagnóstico do APO: o audiodg (LOCAL SERVICE) escreve,
// usuários leem.
void ensureLogDir()
{
    const std::wstring d = logDir();
    if (d.empty())
        return;
    const wchar_t* dir = d.c_str();
    CreateDirectoryW(dir, nullptr);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;LS)(A;OICI;0x1200a9;;;BU)", SDDL_REVISION_1, &sd,
            nullptr)) {
        SetFileSecurityW(dir, DACL_SECURITY_INFORMATION, sd);
        LocalFree(sd);
    }
    say(L"  log do APO: " + std::wstring(dir) + L"\\apo.log");
}

void attachToEndpoint(const std::wstring& guid, const Stage& stage)
{
    Key fx = Key::open(HKEY_LOCAL_MACHINE, fxPath(guid), KEY_QUERY_VALUE | KEY_SET_VALUE);
    backupValues(fx, guid);

    // Tira o APO dos outros estágios (de instalações anteriores).
    for (const auto& st : kStages) {
        if (&st == &stage)
            continue;
        if (auto list = fx.multiSz(*st.composite); list && containsClsid(*list)) {
            list->erase(std::remove_if(list->begin(), list->end(),
                                       [](const std::wstring& c) { return sameText(c, kApoClsidString); }),
                        list->end());
            if (list->empty())
                fx.remove(*st.composite);
            else
                fx.setMultiSz(*st.composite, *list);
        }
    }

    // Mantém a cadeia do driver e põe o nosso por último (ganho e soft-clip
    // depois dos efeitos dele).
    std::vector<std::wstring> chain = fx.multiSz(*stage.composite).value_or(std::vector<std::wstring>{});
    if (chain.empty() && stage.composite == &kEfxComposite) {
        if (auto legacy = fx.string(kEfxLegacy); legacy && !legacy->empty()
            && !sameText(*legacy, L"{00000000-0000-0000-0000-000000000000}"))
            chain.push_back(*legacy);
    }
    if (!containsClsid(chain))
        chain.push_back(kApoClsidString);
    fx.setMultiSz(*stage.composite, chain);

    if (!fx.multiSz(*stage.modes))
        fx.setMultiSz(*stage.modes, {kModeDefault});

    // "Aprimoramentos de áudio" precisam estar ligados para o Windows rodar APOs.
    fx.setDword(kDisableSysFx, 0);
    say(L"  APO ligado ao dispositivo " + guid + L" no estágio " + stage.name + L" (cadeia: "
        + std::to_wstring(chain.size()) + L" item(ns))");
}

std::vector<std::wstring> installedEndpoints()
{
    std::vector<std::wstring> out;
    auto root = Key::tryOpen(HKEY_LOCAL_MACHINE, kBackupRoot, KEY_READ);
    if (!root)
        return out;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumKeyExW(root->get(), i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        out.emplace_back(name);
    }
    return out;
}

// O app (sem administrador) salva o ganho efetivo nesta chave para o APO
// reaplicá-lo depois. Usuários autenticados podem ler e gravar valores só
// nela; as subchaves (Backup) continuam só para administradores. Ver #11.
void allowUsersToSaveGains(const Key& settings)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;CI;KA;;;SY)(A;CI;KA;;;BA)(A;CI;KR;;;BU)(A;;0x20003;;;AU)", SDDL_REVISION_1, &sd, nullptr))
        throw Failure{L"montar a ACL da chave de ajustes: " + errorText(GetLastError())};
    const LONG rc = RegSetKeySecurity(settings.get(), DACL_SECURITY_INFORMATION, sd);
    LocalFree(sd);
    check(rc, L"ajustar a ACL de HKLM\\" + std::wstring(kApoSettingsKey));
}

// Ganho inicial do install. Zera o estado salvo pelo app (B dos apps e
// fatores por canal) de uma ativação anterior: ele não vale mais.
void setInitialGain(const std::wstring& guid, DWORD percent)
{
    Key k = Key::create(HKEY_LOCAL_MACHINE, kApoSettingsKey);
    k.setDword(guid, percent);
    k.remove(guid + L".appboost");
    k.remove(guid + L".channels");
}

// --- Comandos -----------------------------------------------------------------------------

void requireAdmin()
{
    if (!isElevated())
        throw Failure{L"este comando precisa ser executado como administrador"};
}

const Stage& stageByName(const std::wstring& name)
{
    for (const auto& st : kStages)
        if (sameText(name, st.name))
            return st;
    throw Failure{L"estágio inválido: " + name + L" (use sfx, mfx ou efx)"};
}

// Stream de silêncio num dispositivo, para fazer o Windows montar a cadeia
// de efeitos. Abrir pode falhar por alguns instantes logo depois de os
// serviços de áudio reiniciarem (o dispositivo aparece antes de aceitar
// streams), então quem usa tenta de novo.
class SilentStream {
public:
    bool open(const std::wstring& deviceId)
    {
        close();
        Com<IMMDeviceEnumerator> en;
        Com<IMMDevice> dev;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(en.put())))
            || FAILED(en->GetDevice(deviceId.c_str(), dev.put()))
            || FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client_))))
            return fail();
        WAVEFORMATEX* fmt = nullptr;
        if (FAILED(client_->GetMixFormat(&fmt)))
            return fail();
        align_ = fmt->nBlockAlign;
        const HRESULT hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000 /* 200 ms */, 0, fmt, nullptr);
        CoTaskMemFree(fmt);
        if (FAILED(hr) || FAILED(client_->GetBufferSize(&frames_))
            || FAILED(client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render_))))
            return fail();
        feed();
        if (FAILED(client_->Start()))
            return fail();
        return true;
    }

    // Completa o buffer com silêncio "de verdade" (zeros, sem a marca de
    // silêncio, que poderia fazer o Windows pular o processamento).
    bool feed()
    {
        UINT32 padding = 0;
        if (!client_ || FAILED(client_->GetCurrentPadding(&padding)))
            return false;
        BYTE* data = nullptr;
        const UINT32 free = frames_ - padding;
        if (free && SUCCEEDED(render_->GetBuffer(free, &data))) {
            std::memset(data, 0, static_cast<size_t>(free) * align_);
            render_->ReleaseBuffer(free, 0);
        }
        return true;
    }

    void close()
    {
        if (client_)
            client_->Stop();
        if (render_)
            render_->Release();
        if (client_)
            client_->Release();
        render_ = nullptr;
        client_ = nullptr;
    }
    ~SilentStream() { close(); }

private:
    bool fail()
    {
        close();
        return false;
    }
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    UINT32 frames_ = 0;
    WORD align_ = 0;
};

enum class Verify { Processing, NotProcessing, NoStream };

// Toca silêncio no dispositivo e espera o APO processar algum bloco. É a
// única forma de saber se o Windows montou o estágio escolhido (o EFX, por
// exemplo, não é montado em alguns dispositivos Bluetooth). Só conclui que o
// estágio não funciona se o stream de fato rodou por `runMs` sem o APO
// processar; se nem o stream abre, devolve NoStream.
Verify verifyProcessing(const Endpoint& ep, DWORD totalMs = 10000, DWORD runMs = 4000)
{
    SilentStream stream;
    SharedView view;
    const ULONGLONG deadline = GetTickCount64() + totalMs;
    ULONGLONG runningSince = 0;
    while (GetTickCount64() < deadline) {
        if (!runningSince) {
            if (!stream.open(ep.id)) {
                Sleep(200);
                continue;
            }
            runningSince = GetTickCount64();
        }
        Sleep(20);
        if (!stream.feed()) { // dispositivo sumiu/reiniciou no meio: abre de novo
            stream.close();
            runningSince = 0;
            continue;
        }
        if ((view || view.open(ep.guid)) && view.get()->lockedInstances.load() > 0 && view.get()->heartbeat.load() > 0)
            return Verify::Processing;
        if (GetTickCount64() - runningSince >= runMs)
            return Verify::NotProcessing;
    }
    return runningSince ? Verify::NotProcessing : Verify::NoStream;
}

std::wstring registeredDll()
{
    if (auto k = Key::tryOpen(HKEY_LOCAL_MACHINE, kClsidKey + L"\\InprocServer32", KEY_READ))
        return k->string(L"").value_or(L"");
    return {};
}

// Sem nenhum dispositivo com o APO: remove registro, DLL copiada, ajustes e log.
// Chamar com os serviços de áudio parados (a DLL pode estar carregada).
void cleanupIfUnused()
{
    if (!installedEndpoints().empty())
        return;
    const std::wstring dll = registeredDll();
    unregisterCom();
    if (!dll.empty() && isUnder(dll, installDir())) {
        if (DeleteFileW(dll.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) {
            RemoveDirectoryW(installDir().c_str());
            RemoveDirectoryW((programFilesDir() + L"\\LoudMixer").c_str()); // só se estiver vazia
            say(L"  DLL removida");
        } else {
            MoveFileExW(dll.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            say(L"  DLL em uso: será removida no próximo boot");
        }
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kApoSettingsKey);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, kApoSettingsKey);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\LoudMixer"); // só se estiver vazia
    const std::wstring logs = logDir();
    if (!logs.empty()) {
        DeleteFileW((logs + L"\\apo.log").c_str());
        RemoveDirectoryW(logs.c_str());
    }
}

void detachEndpoint(const std::wstring& guid)
{
    restoreValues(guid);
    if (auto k = Key::tryOpen(HKEY_LOCAL_MACHINE, kApoSettingsKey, KEY_SET_VALUE)) {
        k->remove(guid);
        k->remove(guid + L".appboost");
        k->remove(guid + L".channels");
    }
}

constexpr int kExitNoWorkingStage = 3;

struct InstallOptions {
    DWORD gainPercent = 100;
    std::wstring stage = L"auto";
    bool verify = true;
    bool debugLog = false;
};

int cmdInstall(const std::wstring& target, const InstallOptions& opt)
{
    requireAdmin();
    std::vector<const Stage*> stages;
    if (sameText(opt.stage, L"auto")) {
        // EFX pega todos os modos de processamento; MFX e SFX são alternativas.
        stages = {&stageByName(L"efx"), &stageByName(L"mfx"), &stageByName(L"sfx")};
    } else {
        stages = {&stageByName(opt.stage)};
    }
    ComInit com;
    const Endpoint ep = resolveEndpoint(target);
    say(L"Instalando o APO em: " + ep.name + L" " + ep.guid);
    {
        AudioServicesStopped audio;
        const std::wstring dll = deployDll();
        registerCom(dll);
        Key settings = Key::create(HKEY_LOCAL_MACHINE, kApoSettingsKey);
        allowUsersToSaveGains(settings);
        settings.setDword(kDebugLogValue, opt.debugLog ? 1 : 0);
        if (opt.debugLog)
            ensureLogDir();
        setInitialGain(ep.guid, opt.gainPercent);
        say(L"  ganho inicial: " + std::to_wstring(opt.gainPercent) + L"%");
        attachToEndpoint(ep.guid, *stages[0]);
    }
    if (!opt.verify) {
        say(L"OK (sem verificação). Toque algum som e rode 'status " + ep.guid + L"'.");
        return 0;
    }
    for (size_t i = 0; i < stages.size(); ++i) {
        if (i > 0) {
            AudioServicesStopped audio;
            attachToEndpoint(ep.guid, *stages[i]);
        }
        say(L"  verificando o estágio " + std::wstring(stages[i]->name) + L"...");
        const Verify v = verifyProcessing(ep);
        if (v == Verify::Processing) {
            say(L"OK. O APO está processando o áudio no estágio " + std::wstring(stages[i]->name) + L".");
            return 0;
        }
        say(v == Verify::NoStream ? L"  não foi possível tocar no dispositivo para verificar o estágio "
                                        + std::wstring(stages[i]->name)
                                  : L"  o Windows não montou o APO no estágio " + std::wstring(stages[i]->name));
    }
    {
        AudioServicesStopped audio;
        detachEndpoint(ep.guid);
        cleanupIfUnused();
    }
    say(L"ERRO: nenhum estágio funcionou neste dispositivo; ele foi restaurado como estava.");
    return kExitNoWorkingStage;
}

int cmdUninstall(const std::wstring& target)
{
    requireAdmin();
    ComInit com;
    std::vector<std::wstring> guids;
    if (sameText(target, L"all"))
        guids = installedEndpoints();
    else
        guids.push_back(resolveEndpoint(target).guid);
    if (guids.empty()) {
        // Nenhum dispositivo com o APO (ex.: desinstalar o LoudMixer sem nunca
        // ter ligado o boost): nada carregado no audiodg, não precisa parar o áudio.
        cleanupIfUnused();
        say(L"OK. Nenhum dispositivo com o APO.");
        return 0;
    }
    {
        AudioServicesStopped audio;
        for (const auto& g : guids)
            detachEndpoint(g);
        cleanupIfUnused();
    }
    say(L"OK. APO removido.");
    return 0;
}

void printFxState(const std::wstring& guid)
{
    auto fx = Key::tryOpen(HKEY_LOCAL_MACHINE, fxPath(guid), KEY_READ);
    if (!fx) {
        say(L"    (sem FxProperties)");
        return;
    }
    const auto sysfxOff = fx->dword(kDisableSysFx).value_or(0);
    std::wstring line = L"    aprimoramentos: " + std::wstring(sysfxOff ? L"DESLIGADOS" : L"ligados");
    for (const auto& st : kStages) {
        const auto chain = fx->multiSz(*st.composite).value_or(std::vector<std::wstring>{});
        std::wstring c;
        for (const auto& s : chain)
            c += (c.empty() ? L"" : L", ") + (sameText(s, kApoClsidString) ? std::wstring(L"LoudMixer") : s.substr(0, 9) + L"…}");
        line += L" | " + std::wstring(st.name) + L": " + (c.empty() ? L"-" : c);
    }
    say(line);
}

int cmdList()
{
    ComInit com;
    for (const auto& e : listEndpoints()) {
        say((e.isDefault ? L"* " : L"  ") + e.guid + L"  " + e.name);
        printFxState(e.guid);
        SharedView v(e.guid);
        if (v)
            say(L"    APO ativo: blocos processados = " + std::to_wstring(v.get()->heartbeat.load()));
    }
    return 0;
}

int cmdStatus(const std::wstring& target)
{
    ComInit com;
    const Endpoint ep = resolveEndpoint(target);
    say(ep.name + L" " + ep.guid);
    printFxState(ep.guid);
    SharedView v(ep.guid);
    if (!v) {
        say(L"  APO não está carregado neste dispositivo (memória compartilhada ausente).");
        return 1;
    }
    auto* s = v.get();
    const auto hb0 = s->heartbeat.load();
    const auto n0 = s->processedSamples.load();
    s->peakIn.store(0.0f);
    s->peakOut.store(0.0f);
    Sleep(1000);
    const auto hb1 = s->heartbeat.load();
    const auto n1 = s->processedSamples.load();
    wchar_t line[512];
    swprintf(line, 512,
             L"  APO carregado: %u Hz, %u canais, %u instância(s) ativa(s)\n"
             L"  ganho: %.0f%%  bypass: %s\n"
             L"  último segundo: %llu blocos, %llu amostras com áudio, pico entrada %.3f, pico saída %.3f\n"
             L"  total: %llu amostras, %llu com clipping evitado",
             s->sampleRate.load(), s->channels.load(), s->lockedInstances.load(), s->gain.load() * 100.0f,
             s->bypass.load() ? L"sim" : L"não", static_cast<unsigned long long>(hb1 - hb0),
             static_cast<unsigned long long>(n1 - n0), s->peakIn.load(), s->peakOut.load(),
             static_cast<unsigned long long>(n1), static_cast<unsigned long long>(s->clippedSamples.load()));
    say(line);
    return 0;
}

int cmdSetGain(const std::wstring& target, DWORD percent)
{
    ComInit com;
    const Endpoint ep = resolveEndpoint(target);
    SharedView v(ep.guid);
    if (!v)
        throw Failure{L"APO não está carregado em " + ep.name};
    v.get()->gain.store(std::clamp(static_cast<float>(percent), 0.0f, 200.0f) / 100.0f);
    say(L"Ganho de " + ep.name + L" = " + std::to_wstring(percent) + L"%");
    // Também persiste (a chave aceita escrita de usuários após o install).
    const DWORD saved = std::min<DWORD>(percent, 200);
    if (RegSetKeyValueW(HKEY_LOCAL_MACHINE, kApoSettingsKey, ep.guid.c_str(), REG_DWORD, &saved, sizeof(saved))
        != ERROR_SUCCESS)
        say(L"  (aviso: não foi possível salvar no registro; vale até o áudio reiniciar)");
    return 0;
}

int usage()
{
    say(L"uso: loudmixer-apo-setup list\n"
        L"     loudmixer-apo-setup install   <dispositivo|default> [--gain 100] [--stage auto|efx|mfx|sfx]\n"
        L"                                    [--no-verify] [--debug-log]\n"
        L"     loudmixer-apo-setup uninstall <dispositivo|default|all>\n"
        L"     loudmixer-apo-setup status    <dispositivo|default>\n"
        L"     loudmixer-apo-setup set-gain  <dispositivo|default> <porcentagem>\n"
        L"     opções: --log <arquivo>");
    return 2;
}

} // namespace

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    InstallOptions opt;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argvW[i];
        if (a == L"--log" && i + 1 < argc) {
            g_log = _wfopen(argvW[++i], L"ab");
        } else if (a == L"--stage" && i + 1 < argc) {
            opt.stage = argvW[++i];
        } else if (a == L"--no-verify") {
            opt.verify = false;
        } else if (a == L"--debug-log") {
            opt.debugLog = true;
        } else if (a == L"--gain" && i + 1 < argc) {
            opt.gainPercent = std::min<DWORD>(static_cast<DWORD>(std::wcstoul(argvW[++i], nullptr, 10)), 200);
        } else {
            args.push_back(a);
        }
    }
    LocalFree(argvW);

    int rc = 2;
    try {
        if (args.empty())
            rc = usage();
        else if (args[0] == L"list")
            rc = cmdList();
        else if (args[0] == L"install" && args.size() == 2)
            rc = cmdInstall(args[1], opt);
        else if (args[0] == L"uninstall" && args.size() == 2)
            rc = cmdUninstall(args[1]);
        else if (args[0] == L"status" && args.size() == 2)
            rc = cmdStatus(args[1]);
        else if (args[0] == L"set-gain" && args.size() == 3)
            rc = cmdSetGain(args[1], static_cast<DWORD>(std::wcstoul(args[2].c_str(), nullptr, 10)));
        else
            rc = usage();
    } catch (const Failure& f) {
        say(L"ERRO: " + f.message);
        rc = 1;
    }
    if (g_log)
        std::fclose(g_log);
    return rc;
}
