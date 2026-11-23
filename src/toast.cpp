#include "toast.h"
#include "ui_theme.h"
#include <windowsx.h>

namespace toast {
namespace {

using Gdiplus::REAL;

const wchar_t* kCls   = L"TopMostDeskToast";
const int   kW        = 380;     // 逻辑尺寸（96 DPI），实际按 DPI 放大
const int   kH        = 88;
const int   kMargin   = 18;
const DWORD kShowMs   = 2800;    // 最后一次更新后存活时间
const DWORD kBurstMs  = 1000;    // 同一事件的连续触发窗口
const UINT_PTR kTimer = 1;

struct St {
    HWND         hwnd   = nullptr;
    std::wstring key;
    std::wstring title;
    std::wstring text;
    DWORD        flags  = NIIF_INFO;
    ULONGLONG    deadline    = 0;   // 自动关闭时刻
    ULONGLONG    lastSameKey = 0;   // 上次同 key 的时间
    bool         hover  = false;
    bool         shown  = false;
    float        scale  = 1.0f;
};

St s;

int PX(int v) { return (int)(v * s.scale + 0.5f); }

float DpiScale()
{
    HDC dc = GetDC(nullptr);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    float k = (dpi > 0) ? (dpi / 96.0f) : 1.0f;
    if (k < 0.75f) k = 0.75f;
    if (k > 3.0f)  k = 3.0f;
    return k;
}

// NIIF_INFO/WARNING/ERROR 的值是 1/2/3，位与判断会把 INFO 误判成 ERROR
static UINT KindOf(DWORD flags) { return (UINT)(flags & 0x0F); }

COLORREF AccentOf(DWORD flags)
{
    switch (KindOf(flags)) {
    case NIIF_ERROR:   return RGB(216, 58, 58);
    case NIIF_WARNING: return RGB(214, 138, 20);
    case NIIF_NONE:    return RGB(120, 128, 140);
    default:           return RGB(27, 111, 224);
    }
}

// 支持换行的文字（最多两行，超出省略）
void TextWrap(Gdiplus::Graphics& g, const std::wstring& str, const Gdiplus::RectF& r,
              float px, COLORREF col)
{
    Gdiplus::FontFamily yahei(L"Microsoft YaHei UI");
    Gdiplus::FontFamily segoe(L"Segoe UI");
    Gdiplus::FontFamily* fam = yahei.IsAvailable() ? &yahei : &segoe;
    Gdiplus::Font font(fam, px, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
    Gdiplus::SolidBrush br(ui::C(col));
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.DrawString(str.c_str(), (INT)str.size(), &font, r, &fmt, &br);
}

void DrawBadge(Gdiplus::Graphics& g, const Gdiplus::RectF& r, COLORREF col, DWORD flags)
{
    Gdiplus::SolidBrush bg(ui::C(col));
    g.FillEllipse(&bg, r);

    Gdiplus::Pen pen(ui::C(RGB(255, 255, 255)), 2.0f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const REAL cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;

    if (KindOf(flags) == NIIF_ERROR || KindOf(flags) == NIIF_WARNING) {
        // 感叹号
        g.DrawLine(&pen, cx, r.Y + 7.0f, cx, cy + 2.0f);
        Gdiplus::SolidBrush w(ui::C(RGB(255, 255, 255)));
        g.FillEllipse(&w, cx - 1.4f, r.Y + r.Height - 9.0f, 2.8f, 2.8f);
    } else {
        // 对勾
        g.DrawLine(&pen, r.X + 8.0f, cy + 1.0f, cx - 1.0f, r.Y + r.Height - 9.0f);
        g.DrawLine(&pen, cx - 1.0f, r.Y + r.Height - 9.0f, r.X + r.Width - 8.0f, r.Y + 9.0f);
    }
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    const int pw = PX(kW), ph = PX(kH);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, pw, ph);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    {
        Gdiplus::Graphics gfx(mem);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gfx.ScaleTransform(s.scale, s.scale);

        const COLORREF accent = AccentOf(s.flags);
        Gdiplus::RectF card(0.5f, 0.5f, (REAL)kW - 1.0f, (REAL)kH - 1.0f);
        ui::FillRound(gfx, card, 12.0f, ui::kCard);
        ui::StrokeRound(gfx, card, 12.0f, RGB(226, 230, 236), 1.0f);

        // 左侧强调条
        ui::FillRound(gfx, Gdiplus::RectF(1.0f, 16.0f, 4.0f, (REAL)kH - 32.0f), 2.0f, accent);

        DrawBadge(gfx, Gdiplus::RectF(20.0f, 16.0f, 28.0f, 28.0f), accent, s.flags);

        ui::Text(gfx, s.title, Gdiplus::RectF(60.0f, 12.0f, (REAL)kW - 78.0f, 22.0f),
                 13.5f, ui::kTitle, true);
        TextWrap(gfx, s.text, Gdiplus::RectF(60.0f, 35.0f, (REAL)kW - 78.0f, 42.0f),
                 11.5f, ui::kSubText);
    }
    BitBlt(dc, 0, 0, pw, ph, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void Reposition(HWND hwnd)
{
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int pw = PX(kW), ph = PX(kH);
    const int m = PX(kMargin);
    const int x = work.right - pw - m;
    const int y = work.bottom - ph - m;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, pw, ph,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void HideInternal()
{
    if (s.hwnd && s.shown) ShowWindow(s.hwnd, SW_HIDE);
    s.shown = false;
    if (s.hwnd) KillTimer(s.hwnd, kTimer);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_PAINT:      Paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_TIMER:
        if (w == kTimer) {
            // 悬停时暂停倒计时；同时把窗口顶回最前（其它 topmost 窗口会盖住它）
            if (!s.hover && GetTickCount64() > s.deadline) {
                HideInternal();
                return 0;
            }
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        return 0;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        if (!s.hover) {
            s.hover = true;
            if (s.hwnd) SetTimer(s.hwnd, kTimer, 200, nullptr);   // 保活 watchdog 继续跑，但不倒计时
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        s.hover = false;
        s.deadline = GetTickCount64() + 800;    // 移开后给个短暂宽限
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        HideInternal();                          // 点一下就收，不用点关闭按钮
        return 0;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;

    case WM_DESTROY:
        return 0;

    default: break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

bool EnsureWindow()
{
    if (s.hwnd && IsWindow(s.hwnd)) return true;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = Proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCls;
    RegisterClassExW(&wc);

    s.hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kCls, L"", WS_POPUP, 0, 0, PX(kW), PX(kH),
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!s.hwnd) return false;

    // 圆角外形（Win11 上 DWM 还会补投影）
    HRGN rgn = CreateRoundRectRgn(0, 0, PX(kW) + 1, PX(kH) + 1,
                                  (int)(24 * s.scale), (int)(24 * s.scale));
    SetWindowRgn(s.hwnd, rgn, TRUE);
    SetLayeredWindowAttributes(s.hwnd, 0, 255, LWA_ALPHA);
    return true;
}

} // namespace

bool Visible() { return s.shown; }

void Show(const std::wstring& key, const std::wstring& title,
          const std::wstring& text, DWORD flags)
{
    ui::StartGdiplus();
    s.scale = DpiScale();
    if (!EnsureWindow()) return;

    const ULONGLONG now = GetTickCount64();
    const bool burst = (key == s.key) && (now - s.lastSameKey <= kBurstMs);

    // 关键：无论 burst 还是新事件，都只改同一块窗口的内容
    s.key         = key;
    s.title       = title;
    s.text        = text;
    s.flags       = flags;
    s.lastSameKey = now;
    s.deadline    = now + kShowMs;
    s.hover       = false;

    // 位置每次都重算：任务栏/DPI/分辨率变了也跟得上
    Reposition(s.hwnd);
    ShowWindow(s.hwnd, SW_SHOWNOACTIVATE);
    InvalidateRect(s.hwnd, nullptr, FALSE);
    UpdateWindow(s.hwnd);

    s.shown = true;
    SetWindowPos(s.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    SetTimer(s.hwnd, kTimer, burst ? 60 : 200, nullptr);   // 合并中的高频事件用更密的检视节拍
}

void Hide() { HideInternal(); }

void Destroy()
{
    if (s.hwnd && IsWindow(s.hwnd)) DestroyWindow(s.hwnd);
    s.hwnd = nullptr;
    s.shown = false;
    s.key.clear();
}

} // namespace toast
