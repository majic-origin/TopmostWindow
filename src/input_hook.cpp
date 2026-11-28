#include "input_hook.h"
#include <windowsx.h>

static InputHook* g_self = nullptr;

static UINT LiveMods()
{
    UINT m = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) m |= MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) m |= MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
        m |= MOD_WIN;
    return m;
}

bool InputHook::Install(HWND notify)
{
    m_notify = notify;
    if (m_hook) return true;
    g_self = this;
    m_hook = SetWindowsHookExW(WH_MOUSE_LL, &InputHook::Proc, GetModuleHandleW(nullptr), 0);
    return m_hook != nullptr;
}

void InputHook::Uninstall()
{
    if (m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
    }
    if (g_self == this) g_self = nullptr;
}

LRESULT CALLBACK InputHook::Proc(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION && g_self) {
        // 注意：WH_MOUSE_LL 的 lParam 是 MSLLHOOKSTRUCT，消息号在 wParam 里
        const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)l;
        const POINT pt = { ms->pt.x, ms->pt.y };

        // 手势拦截优先于一切：命中即整条吞掉，Matcher/Listener 都不参与，
        // 所以"在窗口任意区域拖动"不会同时触发批量选择或同步拖动。
        if (g_self->m_interceptor && g_self->m_interceptor((UINT)w, *ms, LiveMods()))
            return 1;

        // 旁路监听（同步拖动）：不参与命中判定，也不会吞掉事件
        if (g_self->m_listener &&
            (w == WM_MOUSEMOVE || w == WM_LBUTTONDOWN || w == WM_LBUTTONUP)) {
            g_self->m_listener((UINT)w, pt);
        }

        if (!g_self->m_matcher)
            return CallNextHookEx(g_self->m_hook, code, w, l);

        UINT mouseKey = 0;
        bool down = false;

        switch (w) {
        case WM_LBUTTONDOWN: mouseKey = TMD_MOUSE_LEFT;   down = true; break;
        case WM_RBUTTONDOWN: mouseKey = TMD_MOUSE_RIGHT;  down = true; break;
        case WM_MBUTTONDOWN: mouseKey = TMD_MOUSE_MIDDLE; down = true; break;
        case WM_XBUTTONDOWN:
            mouseKey = (HIWORD(ms->mouseData) == XBUTTON1) ? TMD_MOUSE_X1 : TMD_MOUSE_X2;
            down = true;
            break;
        case WM_MOUSEWHEEL: {
            short delta = (short)HIWORD(ms->mouseData);
            if (delta == 0) break;
            mouseKey = (delta > 0) ? TMD_MOUSE_WHEEL_UP : TMD_MOUSE_WHEEL_DOWN;
            down = true;
            break;
        }
        default: break;
        }

        if (mouseKey && down) {
            const UINT act = g_self->m_matcher(LiveMods(), mouseKey, pt);
            if (act) {
                if (g_self->m_notify)
                    PostMessageW(g_self->m_notify, WM_TMD_INPUT, (WPARAM)act, 0);
                return 1;   // 命中即吞掉，避免同时触发目标窗口自身的右键菜单/滚动/选中
            }
        }
    }
    return CallNextHookEx(g_self ? g_self->m_hook : nullptr, code, w, l);
}
