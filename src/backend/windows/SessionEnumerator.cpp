#include "SessionEnumerator.h"

#include <audioclient.h>

namespace loudmixer::win {

bool processImage(std::uint32_t pid, std::string* baseName, std::string* fullPath)
{
    if (pid == 0)
        return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return false;
    wchar_t buf[MAX_PATH * 2];
    DWORD size = static_cast<DWORD>(std::size(buf));
    const BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &size);
    CloseHandle(h);
    if (!ok)
        return false;

    const std::wstring path(buf, size);
    if (fullPath)
        *fullPath = toUtf8(path.c_str());
    if (baseName) {
        std::wstring base = path.substr(path.find_last_of(L"\\/") + 1);
        const auto dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos)
            base.resize(dot);
        // "chrome" -> "Chrome"
        if (!base.empty())
            base[0] = static_cast<wchar_t>(towupper(base[0]));
        *baseName = toUtf8(base.c_str());
    }
    return true;
}

std::vector<SessionEntry> enumerateSessions(IMMDevice* device)
{
    std::vector<SessionEntry> out;
    if (!device)
        return out;

    ComPtr<IAudioSessionManager2> manager;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, manager.putVoid())))
        return out;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(manager->GetSessionEnumerator(sessions.put())))
        return out;
    int count = 0;
    sessions->GetCount(&count);

    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> control;
        if (FAILED(sessions->GetSession(i, control.put())))
            continue;
        AudioSessionState state = AudioSessionStateInactive;
        control->GetState(&state);
        if (state == AudioSessionStateExpired)
            continue;
        auto control2 = control.as<IAudioSessionControl2>();
        auto volume = control.as<ISimpleAudioVolume>();
        if (!control2 || !volume)
            continue;

        SessionEntry e;
        CoTaskString id;
        if (SUCCEEDED(control2->GetSessionInstanceIdentifier(id.put())))
            e.id = toUtf8(id.p);
        if (e.id.empty())
            continue;

        DWORD pid = 0;
        control2->GetProcessId(&pid);
        e.pid = pid;
        e.isSystemSounds = control2->IsSystemSoundsSession() == S_OK;

        CoTaskString display;
        if (SUCCEEDED(control2->GetDisplayName(display.put())) && display.p && display.p[0] != L'@')
            e.appName = toUtf8(display.p);
        std::string exeName;
        processImage(pid, &exeName, &e.iconPath);
        if (e.appName.empty())
            e.appName = e.isSystemSounds ? std::string("System Sounds") : (exeName.empty() ? "PID " + std::to_string(pid) : exeName);

        float level = 1.0f;
        BOOL mute = FALSE;
        volume->GetMasterVolume(&level);
        volume->GetMute(&mute);
        e.volume = level;
        e.muted = mute != FALSE;
        e.control = volume;
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace loudmixer::win
