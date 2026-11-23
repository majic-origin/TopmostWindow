#include "common.h"

namespace tmd {

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

void Log(const std::wstring& s)
{
#ifdef _DEBUG
    OutputDebugStringW((L"[TopMostDesk] " + s + L"\n").c_str());
#else
    (void)s;
#endif
}

std::wstring MouseButtonName(UINT code)
{
    switch (code) {
        case TMD_MOUSE_LEFT:       return L"鼠标左键";
        case TMD_MOUSE_RIGHT:      return L"鼠标右键";
        case TMD_MOUSE_MIDDLE:     return L"鼠标中键";
        case TMD_MOUSE_X1:         return L"鼠标侧键1";
        case TMD_MOUSE_X2:         return L"鼠标侧键2";
        case TMD_MOUSE_WHEEL_UP:   return L"滚轮上滚";
        case TMD_MOUSE_WHEEL_DOWN: return L"滚轮下滚";
        default:                   return L"?";
    }
}

// 键码 → 可读名称（键盘）
static std::wstring KeyDisplayName(UINT vk)
{
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t name[64] = {};
    LONG lp = (LONG)(sc << 16);
    switch (vk) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            lp |= (1 << 24);
            break;
        default: break;
    }
    if (!GetKeyNameTextW(lp, name, 64))
        swprintf_s(name, L"0x%02X", vk);
    return name;
}

std::wstring DescribeBinding(const Binding& b)
{
    if (b.key == 0) return L"未设置";

    std::wstring s;
    auto add = [&](const std::wstring& t) {
        if (!s.empty()) s += L" + ";
        s += t;
    };
    if (b.mods & MOD_CONTROL) add(L"Ctrl");
    if (b.mods & MOD_ALT)     add(L"Alt");
    if (b.mods & MOD_SHIFT)   add(L"Shift");
    if (b.mods & MOD_WIN)     add(L"Win");

    add(b.device == TMD_DEV_MOUSE ? MouseButtonName(b.key) : KeyDisplayName(b.key));
    return s;
}

} // namespace tmd
