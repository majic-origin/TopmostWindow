#include "common.h"
#include <commdlg.h>

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
        case TMD_MOUSE_HOVER:      return L"鼠标移动";
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

// ---------------- 系统取色器（带"顶到最前"看门线程） ----------------

namespace {

struct PromoteCtx
{
    DWORD         tid  = 0;
    volatile LONG stop = 0;
    HWND          found = nullptr;
};

// 只认本线程创建的 #32770（通用对话框类名），别的窗口一概不碰
BOOL CALLBACK PromoteEnum(HWND h, LPARAM lp)
{
    auto* c = reinterpret_cast<PromoteCtx*>(lp);
    if (!IsWindowVisible(h)) return TRUE;
    wchar_t cls[64] = {};
    if (!GetClassNameW(h, cls, 64)) return TRUE;
    if (wcscmp(cls, L"#32770") != 0) return TRUE;
    c->found = h;
    return FALSE;
}

DWORD WINAPI PromoteProc(LPVOID p)
{
    auto* c = reinterpret_cast<PromoteCtx*>(p);
    for (int i = 0; i < 40 && !c->stop; ++i) {     // 最多盯 4 秒
        Sleep(100);
        if (c->stop) break;
        c->found = nullptr;
        EnumThreadWindows(c->tid, &PromoteEnum, (LPARAM)c);
        if (c->found) {
            // 对话框自己不是 topmost，被置顶的窗口压住时用户会以为"点了没反应"
            SetWindowPos(c->found, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            SetForegroundWindow(c->found);
            break;
        }
    }
    return 0;
}

} // namespace

bool ChooseColorTopmost(HWND owner, COLORREF& inout)
{
    static COLORREF customs[16] = {};

    PromoteCtx ctx;
    ctx.tid = GetCurrentThreadId();
    HANDLE th = CreateThread(nullptr, 0, &PromoteProc, &ctx, 0, nullptr);

    CHOOSECOLORW cc = {};
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner    = owner;
    cc.rgbResult    = inout;
    cc.lpCustColors = customs;
    cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
    const BOOL ok = ChooseColorW(&cc);

    InterlockedExchange(&ctx.stop, 1);
    if (th) {
        WaitForSingleObject(th, 1500);   // 线程最多再睡 100ms，等它自己退
        CloseHandle(th);
    }

    if (ok) {
        inout = cc.rgbResult;
        return true;
    }
    return false;
}

} // namespace tmd
