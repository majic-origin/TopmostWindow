#include "overlay.h"
#include "winmgr.h"
#include <cstring>

static const wchar_t* kOverlayClass = L"TopMostDeskOverlay";

bool Overlay::Create(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &Overlay::Proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kOverlayClass, L"", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, hInst, nullptr);
    return m_hwnd != nullptr;
}

void Overlay::Destroy()
{
    if (m_bmp) { DeleteObject(m_bmp); m_bmp = nullptr; m_bits = nullptr; }
    if (m_memDC) { DeleteDC(m_memDC); m_memDC = nullptr; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

void Overlay::SetStyle(COLORREF color, COLORREF activeColor, COLORREF selectColor, int thickness)
{
    m_color  = color;
    m_active = activeColor;
    m_select = selectColor;
    m_thick  = (thickness < 1) ? 1 : thickness;
}

LRESULT CALLBACK Overlay::Proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST:     return HTTRANSPARENT;
        default: break;
    }
    return DefWindowProcW(h, msg, w, l);
}

void Overlay::EnsureBuffer(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (w == m_w && h == m_h && m_bits) return;

    if (m_memDC && m_bmp) {
        // 旧位图还选在 DC 里，直接 DeleteObject 会失败（GDI 不允许删被选中的对象），
        // 屏幕分辨率一改就漏一个 DC 位图；先换一块 1×1 占位图把它换出来。
        HBITMAP tmp = CreateBitmap(1, 1, 1, 1, nullptr);
        if (tmp) {
            SelectObject(m_memDC, tmp);
            DeleteObject(m_bmp);
            DeleteObject(tmp);
        }
        m_bmp  = nullptr;
        m_bits = nullptr;
    }
    if (!m_memDC) m_memDC = CreateCompatibleDC(nullptr);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;          // top-down，便于按行寻址
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { m_w = 0; m_h = 0; return; }   // 建失败就把尺寸清 0，下一帧重试

    m_bmp  = bmp;
    m_bits = (DWORD*)bits;
    SelectObject(m_memDC, m_bmp);
    m_w = w;
    m_h = h;
}

void Overlay::FillRect(int x0, int y0, int x1, int y1, COLORREF color)
{
    if (!m_bits) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > m_w) x1 = m_w;
    if (y1 > m_h) y1 = m_h;
    if (x0 >= x1 || y0 >= y1) return;

    // DIB 32bpp 内存序为 BGRA，DWORD 视角即 0xAARRGGBB
    const DWORD px = 0xFF000000u
                   | ((DWORD)GetRValue(color) << 16)
                   | ((DWORD)GetGValue(color) << 8)
                   |  (DWORD)GetBValue(color);
    for (int y = y0; y < y1; ++y) {
        DWORD* row = m_bits + (size_t)y * m_w;
        for (int x = x0; x < x1; ++x) row[x] = px;
    }
}

void Overlay::Present()
{
    if (!m_hwnd || !m_memDC || !m_bmp) return;
    HDC screen = GetDC(nullptr);
    POINT dst = { m_ox, m_oy };
    SIZE  sz  = { m_w, m_h };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, screen, &dst, &sz, m_memDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

void Overlay::Refresh(const std::vector<HWND>& pins, HWND active,
                      const std::vector<HWND>& selected)
{
    if (!m_hwnd) return;

    if (pins.empty() && selected.empty()) {
        if (m_shown) { ShowWindow(m_hwnd, SW_HIDE); m_shown = false; }
        return;
    }

    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    const bool geomChanged = (vw != m_w || vh != m_h || vx != m_ox || vy != m_oy);
    if (geomChanged) {
        EnsureBuffer(vw, vh);
        m_ox = vx;
        m_oy = vy;
        SetWindowPos(m_hwnd, HWND_TOPMOST, vx, vy, vw, vh,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (!m_bits) return;

    memset(m_bits, 0, (size_t)m_w * m_h * sizeof(DWORD));

    const int base = (m_thick < 1) ? 1 : (m_thick > 10 ? 10 : m_thick);

    auto drawOne = [&](HWND h, COLORREF c, int t, bool inner) -> bool {
        if (!IsWindow(h) || !IsWindowVisible(h)) return false;
        if (IsIconic(h)) return false;
        RECT rc;
        if (!WindowManager::GetFrameRect(h, rc)) return false;

        int x0 = rc.left - m_ox;
        int y0 = rc.top - m_oy;
        int x1 = rc.right - m_ox;
        int y1 = rc.bottom - m_oy;
        // 允许超出屏幕（最大化窗口的 DWM 扩展边界会比屏幕大 8px），FillRect 自动裁剪
        if (x1 <= 0 || y1 <= 0 || x0 >= m_w || y0 >= m_h) return false;

        FillRect(x0, y0, x1, y0 + t, c);           // 上
        FillRect(x0, y1 - t, x1, y1, c);           // 下
        FillRect(x0, y0, x0 + t, y1, c);           // 左
        FillRect(x1 - t, y0, x1, y1, c);           // 右

        if (inner) {
            // 当前目标 / 批量选中的窗口再补一圈内描边，视觉上更"实"
            FillRect(x0 + t + 2, y0 + t + 2, x1 - t - 2, y0 + t + 3, c);
        }
        return true;
    };

    bool any = false;
    // 1) 普通置顶
    for (HWND h : pins) {
        bool isSel = std::find(selected.begin(), selected.end(), h) != selected.end();
        if (isSel || h == active) continue;
        if (drawOne(h, m_color, base, false)) any = true;
    }
    // 2) 当前操作目标
    if (active && std::find(selected.begin(), selected.end(), active) == selected.end())
        if (drawOne(active, m_active, base + 2, true)) any = true;
    // 3) 批量选中（最高优先级，即使未置顶也描边）
    for (HWND h : selected)
        if (drawOne(h, m_select, base + 2, true)) any = true;

    // 没有任何可画的窗口（全部最小化/不可见）时先隐藏，等下次有内容再显示
    if (!any) {
        if (m_shown) { ShowWindow(m_hwnd, SW_HIDE); m_shown = false; }
        return;
    }

    Present();

    // 关键：隐藏过一次之后必须重新 ShowWindow，否则位置/尺寸没变化就永远不显示
    if (!m_shown) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        m_shown = true;
    }

    // 其它 topmost 窗口被激活后会排到本窗口之上，每帧重新提到最前
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}
