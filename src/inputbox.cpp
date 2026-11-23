#include "dialogs.h"
#include "ui_theme.h"
#include <windowsx.h>

namespace {

const wchar_t* kInputClass = L"TopMostDeskInputBox";
const int kInW = 460;
const int kInH = 236;

struct InState {
    HWND         hwnd = nullptr;
    HWND         edit = nullptr;
    HFONT        font = nullptr;
    HBRUSH       fieldBrush = nullptr;
    bool         done = false;
    bool         ok   = false;
    std::wstring prompt;
    std::wstring title;
    ui::Button   okBtn;
    ui::Button   cancelBtn;
    RECT         rcClose = {};
    bool         closeHover = false;
};

InState g;

void Repaint(HWND h)
{
    InvalidateRect(h, nullptr, FALSE);
}

void Finish(bool ok)
{
    if (g.done) return;
    g.ok = ok;
    g.done = true;
    if (g.hwnd) PostMessageW(g.hwnd, WM_NULL, 0, 0);
}

void UpdateHover(POINT p)
{
    bool dirty = false;
    bool inClose = PtInRect(&g.rcClose, p) != FALSE;
    if (inClose != g.closeHover) { g.closeHover = inClose; dirty = true; }
    if (g.okBtn.Hit(p) != g.okBtn.hover) { g.okBtn.hover = !g.okBtn.hover; dirty = true; }
    if (g.cancelBtn.Hit(p) != g.cancelBtn.hover) { g.cancelBtn.hover = !g.cancelBtn.hover; dirty = true; }
    if (dirty) Repaint(g.hwnd);
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, kInW, kInH);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    {
        Gdiplus::Graphics gfx(mem);
        ui::DrawCardChrome(gfx, kInW, kInH, g.title, g.rcClose, g.closeHover);

        ui::Text(gfx, g.prompt, Gdiplus::RectF(24.0f, 50.0f, (Gdiplus::REAL)(kInW - 48), 22.0f),
                 12.5f, ui::kSubText, false);

        // 输入框底板（真正的 EDIT 叠在上面）
        Gdiplus::RectF field(22.0f, 78.0f, (Gdiplus::REAL)(kInW - 44), 46.0f);
        ui::FillRound(gfx, field, 9.0f, ui::kField);
        ui::StrokeRound(gfx, field, 9.0f, ui::kFieldLine, 1.0f);

        ui::DrawButton(gfx, g.cancelBtn);
        ui::DrawButton(gfx, g.okBtn);
    }
    BitBlt(dc, 0, 0, kInW, kInH, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK InProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetTextColor(dc, RGB(23, 26, 33));
        SetBkColor(dc, ui::kField);
        return (LRESULT)(g.fieldBrush ? g.fieldBrush : GetSysColorBrush(COLOR_WINDOW));
    }

    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (p.y < 52) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        UpdateHover(p);
        return 0;
    }

    case WM_MOUSELEAVE:
        g.closeHover = false;
        g.okBtn.hover = g.cancelBtn.hover = false;
        Repaint(hwnd);
        return 0;

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

    case WM_LBUTTONUP: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (GetCapture() == hwnd) ReleaseCapture();
        if (g.okBtn.pressed) {
            g.okBtn.pressed = false;
            if (g.okBtn.Hit(p)) { Finish(true); return 0; }
        }
        if (g.cancelBtn.pressed) {
            g.cancelBtn.pressed = false;
            if (g.cancelBtn.Hit(p)) { Finish(false); return 0; }
        }
        Repaint(hwnd);
        return 0;
    }

    case WM_CLOSE:
        Finish(false);
        return 0;

    case WM_DESTROY:
        if (g.font) { DeleteObject(g.font); g.font = nullptr; }
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

} // namespace

bool InputBox(HWND owner, const wchar_t* title, const wchar_t* prompt, std::wstring& value)
{
    ui::StartGdiplus();

    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    if (!inst) inst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = InProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kInputClass;
    RegisterClassExW(&wc);

    g = InState();
    g.prompt = prompt ? prompt : L"";
    g.title  = title ? title : L"输入";
    g.fieldBrush = CreateSolidBrush(ui::kField);

    const int btnW = 92, btnH = 38, gap = 10, margin = 22;
    g.okBtn.rc     = { kInW - margin - btnW, kInH - 60, kInW - margin, kInH - 60 + btnH };
    g.cancelBtn.rc = { kInW - margin - btnW - gap - btnW, kInH - 60, kInW - margin - btnW - gap, kInH - 60 + btnH };
    g.okBtn.text     = L"确定";
    g.cancelBtn.text = L"取消";
    g.okBtn.primary  = true;
    g.rcClose        = { kInW - 48, 8, kInW - 8, 48 };

    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - kInW) / 2;
    int y = work.top + ((work.bottom - work.top) - kInH) / 2;

    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kInputClass, title,
                             WS_POPUP, x, y, kInW, kInH,
                             owner, nullptr, inst, nullptr);
    if (!h) return false;
    g.hwnd = h;
    ui::ApplyRoundedWindow(h, kInW, kInH, 12);

    g.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

    g.edit = CreateWindowExW(0, L"EDIT", value.c_str(),
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             34, 90, kInW - 68, 24, h, nullptr, inst, nullptr);
    SendMessageW(g.edit, WM_SETFONT, (WPARAM)g.font, TRUE);
    SendMessageW(g.edit, EM_SETSEL, 0, (LPARAM)-1);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    ui::ForceForeground(h);
    SetFocus(g.edit);
    SendMessageW(g.edit, EM_SETSEL, 0, (LPARAM)-1);

    MSG msg;
    while (!g.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // 编辑框里的回车/ESC 直接生效
        if (msg.hwnd == g.edit && msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) { Finish(true); continue; }
            if (msg.wParam == VK_ESCAPE) { Finish(false); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    bool ok = g.ok;
    if (ok && IsWindow(g.edit)) {
        int len = GetWindowTextLengthW(g.edit);
        std::wstring buf((size_t)len + 1, L'\0');
        GetWindowTextW(g.edit, &buf[0], len + 1);
        buf.resize((size_t)len);
        value = tmd::Trim(buf);
        ok = !value.empty();
    }

    if (owner) EnableWindow(owner, TRUE);
    if (IsWindow(h)) DestroyWindow(h);
    if (g.fieldBrush) { DeleteObject(g.fieldBrush); g.fieldBrush = nullptr; }
    ui::ForceForeground(owner);
    return ok;
}
