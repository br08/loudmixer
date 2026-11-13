#pragma once
// Registro do APO compartilhado entre o APO, o backend WASAPI e a ferramenta
// de setup: detectar se o APO está na cadeia de um dispositivo e persistir o
// ganho efetivo para o APO reaplicar quando o Windows o recria (reboot,
// reconexão Bluetooth, reinício do áudio).
//
// HKLM\SOFTWARE\LoudMixer\Apo
//   {guid}            DWORD      ganho mestre do APO, em % (0–200)
//   {guid}.channels   REG_BINARY float[] com o fator por canal (1.0–2.0)
//   {guid}.appboost   DWORD      B (maior ganho de app), em %; uso do app
//
// O install da ferramenta dá a usuários autenticados permissão de escrever
// valores nessa chave (só nela), para o app salvar sem UAC.

#include "apo/ApoInterfaces.h"
#include "apo/ApoShared.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace loudmixer::apo {

constexpr const wchar_t* kRenderRoot = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render\\";

// PKEY_CompositeFX_StreamEffectClsid / ModeEffectClsid / EndpointEffectClsid
inline const wchar_t* const kCompositeFxValues[] = {
    L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13",
    L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14",
    L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15",
};

// O APO do LoudMixer está em alguma cadeia de efeitos do dispositivo?
// (Só leitura: não precisa de administrador.)
inline bool isAttached(const std::wstring& endpointGuid)
{
    const std::wstring path = kRenderRoot + normalizeEndpointGuid(endpointGuid) + L"\\FxProperties";
    for (const wchar_t* name : kCompositeFxValues) {
        DWORD size = 0;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), name, RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size)
            != ERROR_SUCCESS)
            continue;
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, L'\0');
        if (RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), name, RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size)
            != ERROR_SUCCESS)
            continue;
        for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1)
            if (_wcsicmp(p, kApoClsidString) == 0)
                return true;
    }
    return false;
}

struct Persisted {
    bool found = false;     // havia um ganho mestre salvo
    float master = 1.0f;    // linear
    float appBoost = 1.0f;  // linear
    std::vector<float> channels;
};

inline Persisted readPersisted(const std::wstring& endpointGuid)
{
    Persisted p;
    const std::wstring g = normalizeEndpointGuid(endpointGuid);
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kApoSettingsKey, g.c_str(), RRF_RT_REG_DWORD, nullptr, &v, &size)
        == ERROR_SUCCESS) {
        p.found = true;
        p.master = std::clamp(static_cast<float>(v) / 100.0f, 0.0f, 2.0f);
    }
    size = sizeof(v);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kApoSettingsKey, (g + L".appboost").c_str(), RRF_RT_REG_DWORD, nullptr, &v,
                     &size)
        == ERROR_SUCCESS)
        p.appBoost = std::clamp(static_cast<float>(v) / 100.0f, 1.0f, 2.0f);
    float ch[kSharedMaxChannels];
    size = sizeof(ch);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kApoSettingsKey, (g + L".channels").c_str(), RRF_RT_REG_BINARY, nullptr, ch,
                     &size)
        == ERROR_SUCCESS) {
        for (DWORD i = 0; i < size / sizeof(float); ++i)
            p.channels.push_back(std::isfinite(ch[i]) ? std::clamp(ch[i], 0.0f, 2.0f) : 1.0f);
    }
    return p;
}

// Devolve false se não houver permissão (instalação antiga, sem a ACL).
inline bool writePersisted(const std::wstring& endpointGuid, float master, float appBoost,
                           const std::vector<float>& channels)
{
    const std::wstring g = normalizeEndpointGuid(endpointGuid);
    const DWORD m = static_cast<DWORD>(std::lround(std::clamp(master, 0.0f, 2.0f) * 100.0f));
    const DWORD b = static_cast<DWORD>(std::lround(std::clamp(appBoost, 1.0f, 2.0f) * 100.0f));
    const size_t n = std::min<size_t>(channels.size(), kSharedMaxChannels);
    return RegSetKeyValueW(HKEY_LOCAL_MACHINE, kApoSettingsKey, g.c_str(), REG_DWORD, &m, sizeof(m)) == ERROR_SUCCESS
        && RegSetKeyValueW(HKEY_LOCAL_MACHINE, kApoSettingsKey, (g + L".appboost").c_str(), REG_DWORD, &b, sizeof(b))
        == ERROR_SUCCESS
        && RegSetKeyValueW(HKEY_LOCAL_MACHINE, kApoSettingsKey, (g + L".channels").c_str(), REG_BINARY,
                           channels.data(), static_cast<DWORD>(n * sizeof(float)))
        == ERROR_SUCCESS;
}

} // namespace loudmixer::apo
