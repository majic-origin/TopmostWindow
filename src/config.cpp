#include "config.h"
#include <shlobj.h>

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunVal = L"TopMostDesk";

std::wstring GetConfigDir()
{
    wchar_t* p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        dir = p;
        CoTaskMemFree(p);
    } else {
        wchar_t buf[MAX_PATH] = {};
        GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        dir = buf;
    }
    dir += L"\\TopMostDesk";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring GetConfigPath()
{
    return GetConfigDir() + L"\\config.ini";
}

static UINT ReadUInt(const std::wstring& sec, const std::wstring& key, UINT def, const std::wstring& file)
{
    return GetPrivateProfileIntW(sec.c_str(), key.c_str(), (INT)def, file.c_str());
}

static void WriteUInt(const std::wstring& sec, const std::wstring& key, UINT val, const std::wstring& file)
{
    WritePrivateProfileStringW(sec.c_str(), key.c_str(), std::to_wstring(val).c_str(), file.c_str());
}

bool NotifyCfg::Allowed(int cat) const
{
    if (muted) return false;
    switch (cat) {
        case 0:  return pin;
        case 1:  return switchW;
        case 2:  return alpha;
        case 3:  return layout;
        case 4:  return system;
        default: return true;
    }
}

void NotifyCfg::SetAll(bool v)
{
    pin = switchW = alpha = layout = system = v;
}

static void LoadBinding(const std::wstring& file, const wchar_t* prefix, Binding& b)
{
    const std::wstring sec  = L"hotkey";
    const std::wstring p    = prefix;

    UINT dev  = ReadUInt(sec, p + L"Device", b.device, file);
    UINT mods = ReadUInt(sec, p + L"Mods",   b.mods,   file);
    UINT key  = ReadUInt(sec, p + L"Key",    b.key,    file);

    // 兼容 v1 配置：只有 toggleMods / toggleVk
    UINT legacy = ReadUInt(sec, p + L"Vk", 0, file);
    if (legacy) { key = legacy; dev = TMD_DEV_KEYBOARD; }

    b.device = (dev == TMD_DEV_MOUSE) ? TMD_DEV_MOUSE : TMD_DEV_KEYBOARD;
    b.mods   = mods & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN);
    b.key    = key;
}

static void SaveBinding(const std::wstring& file, const wchar_t* prefix, const Binding& b)
{
    const std::wstring sec = L"hotkey";
    const std::wstring p   = prefix;
    WriteUInt(sec, p + L"Device", b.device, file);
    WriteUInt(sec, p + L"Mods",   b.mods,   file);
    WriteUInt(sec, p + L"Key",    b.key,    file);
}

bool LoadConfig(AppConfig& cfg)
{
    const std::wstring f = GetConfigPath();
    if (GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;

    LoadBinding(f, L"toggle", cfg.bindToggle);
    LoadBinding(f, L"next",   cfg.bindNext);
    LoadBinding(f, L"prev",   cfg.bindPrev);
    LoadBinding(f, L"menu",   cfg.bindMenu);
    LoadBinding(f, L"unpinall", cfg.bindUnpinAll);

    const std::wstring G = L"general";
    cfg.autoStart       = ReadUInt(G, L"autoStart",       cfg.autoStart ? 1 : 0, f) != 0;
    cfg.showMarker      = ReadUInt(G, L"showMarker",      cfg.showMarker ? 1 : 0, f) != 0;
    cfg.markerColor     = ReadUInt(G, L"markerColor",     (UINT)cfg.markerColor, f);
    cfg.markerActive    = ReadUInt(G, L"markerActive",    (UINT)cfg.markerActive, f);
    cfg.markerThickness = (int)ReadUInt(G, L"markerThickness", (UINT)cfg.markerThickness, f);
    cfg.defaultAlpha    = (BYTE)ReadUInt(G, L"defaultAlpha", cfg.defaultAlpha, f);

    if (cfg.markerThickness < 1)  cfg.markerThickness = 1;
    if (cfg.markerThickness > 10) cfg.markerThickness = 10;

    const std::wstring N = L"notify";
    cfg.notify.muted   = ReadUInt(N, L"muted",   cfg.notify.muted ? 1 : 0, f) != 0;
    cfg.notify.pin     = ReadUInt(N, L"pin",     cfg.notify.pin ? 1 : 0, f) != 0;
    cfg.notify.switchW = ReadUInt(N, L"switch",  cfg.notify.switchW ? 1 : 0, f) != 0;
    cfg.notify.alpha   = ReadUInt(N, L"alpha",   cfg.notify.alpha ? 1 : 0, f) != 0;
    cfg.notify.layout  = ReadUInt(N, L"layout",  cfg.notify.layout ? 1 : 0, f) != 0;
    cfg.notify.system  = ReadUInt(N, L"system",  cfg.notify.system ? 1 : 0, f) != 0;
    return true;
}

bool SaveConfig(const AppConfig& cfg)
{
    const std::wstring f = GetConfigPath();

    SaveBinding(f, L"toggle", cfg.bindToggle);
    SaveBinding(f, L"next",   cfg.bindNext);
    SaveBinding(f, L"prev",   cfg.bindPrev);
    SaveBinding(f, L"menu",   cfg.bindMenu);
    SaveBinding(f, L"unpinall", cfg.bindUnpinAll);

    WriteUInt(L"general", L"autoStart",       cfg.autoStart ? 1 : 0, f);
    WriteUInt(L"general", L"showMarker",      cfg.showMarker ? 1 : 0, f);
    WriteUInt(L"general", L"markerColor",     (UINT)cfg.markerColor, f);
    WriteUInt(L"general", L"markerActive",    (UINT)cfg.markerActive, f);
    WriteUInt(L"general", L"markerThickness", (UINT)cfg.markerThickness, f);
    WriteUInt(L"general", L"defaultAlpha",    cfg.defaultAlpha, f);

    WriteUInt(L"notify", L"muted",  cfg.notify.muted ? 1 : 0, f);
    WriteUInt(L"notify", L"pin",    cfg.notify.pin ? 1 : 0, f);
    WriteUInt(L"notify", L"switch", cfg.notify.switchW ? 1 : 0, f);
    WriteUInt(L"notify", L"alpha",  cfg.notify.alpha ? 1 : 0, f);
    WriteUInt(L"notify", L"layout", cfg.notify.layout ? 1 : 0, f);
    WriteUInt(L"notify", L"system", cfg.notify.system ? 1 : 0, f);

    // Profile API 内部有缓存，必须显式 flush 才能立刻落盘（否则外部读到的是旧值）
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, f.c_str());
    return true;
}

bool ApplyAutoStart(bool enable)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;

    bool ok = false;
    if (enable) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --tray";
        ok = RegSetValueExW(key, kRunVal, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                            (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        RegDeleteValueW(key, kRunVal);
        ok = true;
    }
    RegCloseKey(key);
    return ok;
}

bool QueryAutoStart()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    LONG r = RegQueryValueExW(key, kRunVal, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && size > 0;
}

std::wstring HotkeyToString(const Binding& b)
{
    return tmd::DescribeBinding(b);
}
