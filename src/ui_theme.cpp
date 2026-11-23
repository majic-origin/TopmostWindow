#include "ui_theme.h"

namespace ui {

static ULONG_PTR g_token = 0;
static bool      g_started = false;

bool StartGdiplus()
{
    if (g_started) return true;
    Gdiplus::GdiplusStartupInput in;
    g_started = Gdiplus::GdiplusStartup(&g_token, &in, nullptr) == Gdiplus::Ok;
    return g_started;
}

void StopGdiplus()
{
    if (g_started) {
        Gdiplus::GdiplusShutdown(g_token);
        g_started = false;
    }
}

static void MakePath(Gdiplus::GraphicsPath& p, const Gdiplus::RectF& r, float rad)
{
    if (rad <= 0.0f) { p.AddRectangle(r); return; }
    const float d = rad * 2.0f;
    p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float rad, COLORREF color, BYTE a)
{
    Gdiplus::GraphicsPath path;
    MakePath(path, r, rad);
    Gdiplus::SolidBrush br(C(color, a));
    g.FillPath(&br, &path);
}

void StrokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float rad, COLORREF color, float w, BYTE a)
{
    Gdiplus::GraphicsPath path;
    MakePath(path, r, rad);
    Gdiplus::Pen pen(C(color, a), w);
    g.DrawPath(&pen, &path);
}

void Text(Gdiplus::Graphics& g, const std::wstring& s, const Gdiplus::RectF& r, float px,
          COLORREF color, bool bold, Gdiplus::StringAlignment h,
          Gdiplus::StringAlignment v, bool ellipsis)
{
    const wchar_t* family = L"Microsoft YaHei UI";
    {
        Gdiplus::FontFamily probe(family);
        if (!probe.IsAvailable()) family = L"Segoe UI";
    }
    Gdiplus::FontFamily ff(family);

    Gdiplus::Font font(&ff, px, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    fmt.SetTrimming(ellipsis ? Gdiplus::StringTrimmingEllipsisCharacter
                             : Gdiplus::StringTrimmingNone);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    Gdiplus::SolidBrush br(C(color));
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.DrawString(s.c_str(), (INT)s.size(), &font, r, &fmt, &br);
}

void ApplyRoundedWindow(HWND hwnd, int w, int h, int radius)
{
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius * 2, radius * 2);
    SetWindowRgn(hwnd, rgn, TRUE);

    // Win11：让 DWM 也画圆角与投影，避免直角阴影
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        typedef HRESULT (WINAPI* Fn)(HWND, DWORD, LPCVOID, DWORD);
        auto setAttr = (Fn)GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (setAttr) {
            int pref = 2;   // DWMWCP_ROUND
            setAttr(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof(pref));
            int shadow = 1;
            setAttr(hwnd, 2 /*DWMWA_NCRENDERING_POLICY*/, &shadow, sizeof(shadow));
        }
        FreeLibrary(dwm);
    }
}

void ForceForeground(HWND hwnd)
{
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();
    bool attached = false;
    if (fgThread && fgThread != myThread)
        attached = AttachThreadInput(myThread, fgThread, TRUE) != FALSE;

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (attached) AttachThreadInput(myThread, fgThread, FALSE);
}

void DrawButton(Gdiplus::Graphics& g, const Button& b)
{
    Gdiplus::RectF r((Gdiplus::REAL)b.rc.left, (Gdiplus::REAL)b.rc.top,
                     (Gdiplus::REAL)(b.rc.right - b.rc.left),
                     (Gdiplus::REAL)(b.rc.bottom - b.rc.top));

    COLORREF fill, line, txt;
    if (b.primary) {
        fill = b.hover ? kAccentHov : kAccent;
        line = fill;
        txt  = RGB(255, 255, 255);
    } else {
        fill = b.hover ? kBtnGhost : kCard;
        line = kBtnLine;
        txt  = kTitle;
    }
    if (b.pressed) fill = b.primary ? RGB(16, 78, 168) : RGB(232, 236, 242);

    FillRound(g, r, 7.0f, fill);
    if (!b.primary) StrokeRound(g, r, 7.0f, line, 1.0f);
    Text(g, b.text, r, 13.5f, txt, b.primary, Gdiplus::StringAlignmentCenter);
}

void DrawCardChrome(Gdiplus::Graphics& g, int w, int h, const std::wstring& title,
                    const RECT& rcClose, bool closeHover)
{
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(C(kCard));

    // 标题
    Text(g, title, Gdiplus::RectF(22.0f, 14.0f, (Gdiplus::REAL)(w - 90), 26.0f),
         15.0f, kTitle, true);

    // 关闭按钮
    Gdiplus::RectF cr((Gdiplus::REAL)rcClose.left, (Gdiplus::REAL)rcClose.top,
                      (Gdiplus::REAL)(rcClose.right - rcClose.left),
                      (Gdiplus::REAL)(rcClose.bottom - rcClose.top));
    if (closeHover) FillRound(g, cr, 6.0f, RGB(240, 242, 246));

    Gdiplus::Pen pen(C(closeHover ? kTitle : kSubText), 1.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    float pad = 7.0f;
    g.DrawLine(&pen, cr.X + pad, cr.Y + pad, cr.X + cr.Width - pad, cr.Y + cr.Height - pad);
    g.DrawLine(&pen, cr.X + cr.Width - pad, cr.Y + pad, cr.X + pad, cr.Y + cr.Height - pad);

    // 底部分隔线
    Gdiplus::Pen sep(C(RGB(238, 241, 245)), 1.0f);
    g.DrawLine(&sep, 0.0f, (Gdiplus::REAL)h - 72.0f, (Gdiplus::REAL)w, (Gdiplus::REAL)h - 72.0f);
}

} // namespace ui
