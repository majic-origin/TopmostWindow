#include "dialogs.h"
#include "config.h"
#include "ui_theme.h"
#include <windowsx.h>

namespace {

const wchar_t* kCapClass = L"TopMostDeskHotkeyCapture";
const int kCapW = 500;
const int kCapH = 312;

struct CapState {
    HWND         hwnd    = nullptr;
    bool         done    = false;
    bool         ok      = false;
    UINT         device  = TMD_DEV_KEYBOARD;
    UINT         mods    = 0;
    UINT         key     = 0;
    UINT         live    = 0;      // 钩子自维护的修饰键状态
    std::wstring error;
    std::wstring title;
    std::wstring hint;
    ui::Button   okBtn, cancelBtn;
    RECT         rcClose = {};
    bool         closeHover = false;
};

CapState g;
HHOOK    g_kbHook = nullptr;
HHOOK    g_msHook = nullptr;

// ---------- 修饰键 ----------
bool IsModifier(UINT vk)
{
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:
        case VK_MENU:    case VK_LMENU:    case VK_RMENU:
        case VK_LWIN:    case VK_RWIN:
            return true;
        default: return false;
    }
}

void ModDown(UINT vk)
{
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: g.live |= MOD_CONTROL; break;
        case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:   g.live |= MOD_SHIFT;   break;
        case VK_MENU:    case VK_LMENU:    case VK_RMENU:    g.live |= MOD_ALT;     break;
        case VK_LWIN:    case VK_RWIN:                       g.live |= MOD_WIN;     break;
        default: break;
    }
}

void ModUp(UINT vk)
{
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: g.live &= ~MOD_CONTROL; break;
        case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:   g.live &= ~MOD_SHIFT;   break;
        case VK_MENU:    case VK_LMENU:    case VK_RMENU:    g.live &= ~MOD_ALT;     break;
        case VK_LWIN:    case VK_RWIN:                       g.live &= ~MOD_WIN;     break;
        default: break;
    }
}

// 键盘组合的可用性探测（调用方需先释放自己已占用的热键）
bool ComboFree(UINT mods, UINT vk)
{
    if (!g.hwnd) return false;
    if (RegisterHotKey(g.hwnd, 0x7FFE, mods | MOD_NOREPEAT, vk)) {
        UnregisterHotKey(g.hwnd, 0x7FFE);
        return true;
    }
    return false;
}

void Repaint(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

void Finish(bool ok)
{
    if (g.done) return;
    g.ok = ok && g.key != 0;
    g.done = true;
    if (g.hwnd) PostMessageW(g.hwnd, WM_NULL, 0, 0);
}

void SetError(const std::wstring& e)
{
    g.error = e;
    Repaint(g.hwnd);
}

void TryAccept()
{
    if (g.key == 0) { SetError(L"还没有按下有效的按键"); return; }
    if (g.device == TMD_DEV_KEYBOARD) {
        if (!ComboFree(g.mods, g.key)) {
            SetError(L"该组合已被系统或其他程序占用，请换一个");
            return;
        }
        if ((g.mods & MOD_WIN) && g.key == 'L') {
            SetError(L"Win+L 属于系统保留键，无法使用");
            return;
        }
    }
    Finish(true);
}

void OnKey(UINT vk)
{
    if (vk == VK_ESCAPE) { Finish(false); return; }
    if (vk == VK_RETURN) { TryAccept(); return; }
    if (vk == VK_BACK)   { g.key = 0; g.mods = 0; g.error.clear(); Repaint(g.hwnd); return; }

    if (IsModifier(vk)) { Repaint(g.hwnd); return; }

    if (g.live == 0) {
        g.key = 0;
        SetError(L"至少需要一个修饰键（Ctrl / Alt / Shift / Win）");
        return;
    }
    g.device = TMD_DEV_KEYBOARD;
    g.mods   = g.live;
    g.key    = vk;

    if (!ComboFree(g.mods, g.key))
        g.error = L"该组合已被系统或其他程序占用，请换一个";
    else if ((g.mods & MOD_WIN) && g.key == 'L')
        g.error = L"Win+L 属于系统保留键，无法使用";
    else
        g.error.clear();

    Repaint(g.hwnd);
}

void OnMouse(UINT mouseKey)
{
    if (g.live == 0) {
        g.key = 0;
        SetError(L"鼠标键也需要搭配修饰键（如 Win + 鼠标右键）");
        return;
    }
    g.device = TMD_DEV_MOUSE;
    g.mods   = g.live;
    g.key    = mouseKey;
    g.error.clear();
    Repaint(g.hwnd);
}

LRESULT CALLBACK LLKey(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) {
        const bool down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
        const UINT vk = ((KBDLLHOOKSTRUCT*)l)->vkCode;
        if (IsModifier(vk)) {
            if (down) { ModDown(vk); OnKey(vk); return 1; }
            ModUp(vk);
            return CallNextHookEx(g_kbHook, code, w, l);
        }
        if (down) { OnKey(vk); return 1; }
        return CallNextHookEx(g_kbHook, code, w, l);
    }
    return CallNextHookEx(g_kbHook, code, w, l);
}

LRESULT CALLBACK LLMouse(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) {
        // WH_MOUSE_LL：消息号在 wParam，lParam 是 MSLLHOOKSTRUCT
        const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)l;
        switch (w) {
        case WM_LBUTTONDOWN: OnMouse(TMD_MOUSE_LEFT);   return 1;
        case WM_RBUTTONDOWN: OnMouse(TMD_MOUSE_RIGHT);  return 1;
        case WM_MBUTTONDOWN: OnMouse(TMD_MOUSE_MIDDLE); return 1;
        case WM_XBUTTONDOWN:
            OnMouse(HIWORD(ms->mouseData) == XBUTTON1 ? TMD_MOUSE_X1 : TMD_MOUSE_X2);
            return 1;
        case WM_MOUSEWHEEL: {
            short d = (short)HIWORD(ms->mouseData);
            if (d != 0) { OnMouse(d > 0 ? TMD_MOUSE_WHEEL_UP : TMD_MOUSE_WHEEL_DOWN); return 1; }
            break;
        }
        default: break;
        }
    }
    return CallNextHookEx(g_msHook, code, w, l);
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, kCapW, kCapH);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    {
        Gdiplus::Graphics gfx(mem);
        ui::DrawCardChrome(gfx, kCapW, kCapH, g.title, g.rcClose, g.closeHover);

        const bool hasKey = g.key != 0;
        Gdiplus::RectF field(22.0f, 54.0f, (Gdiplus::REAL)(kCapW - 44), 76.0f);
        ui::FillRound(gfx, field, 10.0f, ui::kField);
        ui::StrokeRound(gfx, field, 10.0f, hasKey ? ui::kAccent : ui::kFieldLine,
                        hasKey ? 1.6f : 1.0f);

        Binding show{ g.device, g.mods, g.key };
        std::wstring text;
        if (hasKey) {
            text = tmd::DescribeBinding(show);
        } else if (g.live) {
            text = tmd::DescribeBinding(Binding{ g.device, g.live, 0 }) + L" + …";
        }
        if (text.empty())
            ui::Text(gfx, L"请按下组合键或鼠标键…", field, 20.0f, ui::kSubText, false,
                     Gdiplus::StringAlignmentCenter);
        else
            ui::Text(gfx, text, field, 21.0f, ui::kTitle, true,
                     Gdiplus::StringAlignmentCenter);

        if (!g.error.empty())
            ui::Text(gfx, g.error, Gdiplus::RectF(24.0f, 138.0f, (Gdiplus::REAL)(kCapW - 48), 22.0f),
                     12.5f, ui::kDanger, false);
        else
            ui::Text(gfx, L"回车确认 · Esc 取消 · 退格清除",
                     Gdiplus::RectF(24.0f, 138.0f, (Gdiplus::REAL)(kCapW - 48), 22.0f),
                     12.5f, ui::kSubText, false);

        ui::Text(gfx, g.hint, Gdiplus::RectF(24.0f, 164.0f, (Gdiplus::REAL)(kCapW - 48), 20.0f),
                 11.5f, ui::kSubText, false);
        ui::Text(gfx, L"支持键盘键与鼠标左/右/中/侧键、滚轮方向，均需搭配至少一个修饰键。",
                 Gdiplus::RectF(24.0f, 186.0f, (Gdiplus::REAL)(kCapW - 48), 20.0f),
                 11.5f, ui::kSubText, false);

        ui::DrawButton(gfx, g.cancelBtn);
        ui::DrawButton(gfx, g.okBtn);
    }
    BitBlt(dc, 0, 0, kCapW, kCapH, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void UpdateHover(POINT p)
{
    bool dirty = false;
    bool inClose = PtInRect(&g.rcClose, p) != FALSE;
    if (inClose != g.closeHover) { g.closeHover = inClose; dirty = true; }
    if (g.okBtn.Hit(p) != g.okBtn.hover)         { g.okBtn.hover = !g.okBtn.hover; dirty = true; }
    if (g.cancelBtn.Hit(p) != g.cancelBtn.hover) { g.cancelBtn.hover = !g.cancelBtn.hover; dirty = true; }
    if (dirty) Repaint(g.hwnd);
}

LRESULT CALLBACK CapProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_PAINT:      Paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        UpdateHover(p);
        return 0;
    }

    case WM_MOUSELEAVE:
        g.closeHover = false;
        g.okBtn.hover = g.cancelBtn.hover = false;
        g.okBtn.pressed = g.cancelBtn.pressed = false;
        Repaint(hwnd);
        return 0;

    case WM_LBUTTONUP: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (GetCapture() == hwnd) ReleaseCapture();
        if (g.okBtn.pressed) {
            g.okBtn.pressed = false;
            if (g.okBtn.Hit(p)) { TryAccept(); return 0; }
        }
        if (g.cancelBtn.pressed) {
            g.cancelBtn.pressed = false;
            if (g.cancelBtn.Hit(p)) { Finish(false); return 0; }
        }
        Repaint(hwnd);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (PtInRect(&g.rcClose, p)) { Finish(false); return 0; }
        if (g.okBtn.Hit(p))     { g.okBtn.pressed = true; SetCapture(hwnd); Repaint(hwnd); return 0; }
        if (g.cancelBtn.Hit(p)) { g.cancelBtn.pressed = true; SetCapture(hwnd); Repaint(hwnd); return 0; }
        if (p.y < 52) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;

    case WM_CLOSE:
        Finish(false);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        OnKey((UINT)w);
        return 0;

    default: break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

} // namespace

bool CaptureBinding(HWND owner, Binding* b, const wchar_t* title, const wchar_t* hint)
{
    ui::StartGdiplus();

    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    if (!inst) inst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = CapProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCapClass;
    RegisterClassExW(&wc);

    g = CapState();
    if (b) {
        g.device = b->device;
        g.mods   = b->mods;
        g.key    = b->key;
    }
    g.title = title ? title : L"设置快捷键";
    g.hint  = hint ? hint : L"";

    const int btnW = 92, btnH = 38, gap = 10, margin = 22;
    g.okBtn.rc     = { kCapW - margin - btnW, kCapH - 60, kCapW - margin, kCapH - 60 + btnH };
    g.cancelBtn.rc = { kCapW - margin - btnW - gap - btnW, kCapH - 60,
                       kCapW - margin - btnW - gap, kCapH - 60 + btnH };
    g.okBtn.text     = L"确定";
    g.cancelBtn.text = L"取消";
    g.okBtn.primary  = true;
    g.rcClose        = { kCapW - 48, 8, kCapW - 8, 48 };

    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - kCapW) / 2;
    int y = work.top + ((work.bottom - work.top) - kCapH) / 2;

    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kCapClass, L"",
                             WS_POPUP, x, y, kCapW, kCapH,
                             owner, nullptr, inst, nullptr);
    if (!h) return false;
    g.hwnd = h;
    ui::ApplyRoundedWindow(h, kCapW, kCapH, 12);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    ui::ForceForeground(h);

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LLKey,   GetModuleHandleW(nullptr), 0);
    g_msHook = SetWindowsHookExW(WH_MOUSE_LL,    LLMouse, GetModuleHandleW(nullptr), 0);

    MSG msg;
    while (!g.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
    if (g_msHook) { UnhookWindowsHookEx(g_msHook); g_msHook = nullptr; }
    if (owner) EnableWindow(owner, TRUE);
    if (IsWindow(h)) DestroyWindow(h);

    if (g.ok && b) {
        b->device = g.device;
        b->mods   = g.mods;
        b->key    = g.key;
        return true;
    }
    return false;
}
