#include "tray.h"
#include "toast.h"

bool TrayIcon::Create(HWND owner, HICON icon, const std::wstring& tip)
{
    m_owner = owner;
    m_icon  = icon;
    m_tip   = tip;

    m_nid = {};
    m_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd             = owner;
    m_nid.uID              = TMD_TRAY_ID;
    m_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TMD_TRAY;
    m_nid.hIcon            = icon;
    wcsncpy_s(m_nid.szTip, tip.c_str(), _TRUNCATE);

    m_ok = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    if (m_ok) {
        m_nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &m_nid);
    }
    return m_ok;
}

bool TrayIcon::Recreate(HWND owner, HICON icon, const std::wstring& tip)
{
    Destroy();
    return Create(owner, icon, tip);
}

void TrayIcon::Destroy()
{
    toast::Destroy();
    if (m_ok) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_ok = false;
    }
}

void TrayIcon::SetTip(const std::wstring& tip)
{
    m_tip = tip;
    if (!m_ok) return;
    wcsncpy_s(m_nid.szTip, tip.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

void TrayIcon::Balloon(const std::wstring& title, const std::wstring& text, DWORD flags)
{
    // 不再用 Shell_NotifyIcon 的 NIF_INFO 气泡：它按队列逐个弹，连点几次就要关几次，
    // 而且关不掉的会堆进通知中心。改成自绘单实例弹窗，新内容原地覆盖。
    toast::Show(title, title, text, flags);
}

HICON CreateTrayIconFromScratch(int size)
{
    const int S = size;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = S;
    bmi.bmiHeader.biHeight      = -S;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) { DeleteDC(dc); return LoadIconW(nullptr, IDI_APPLICATION); }

    DWORD* px = (DWORD*)bits;
    const DWORD clear = 0x00000000;
    const DWORD bg    = 0xFF1B6FE0;   // 蓝色底
    const DWORD fg    = 0xFFFFFFFF;

    for (int i = 0; i < S * S; ++i) px[i] = clear;

    // 圆角底板
    const int r = S / 6;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            bool inside = true;
            int cx = -1, cy = -1;
            if (x < r && y < r)            { cx = r; cy = r; }
            else if (x >= S - r && y < r)  { cx = S - r - 1; cy = r; }
            else if (x < r && y >= S - r)  { cx = r; cy = S - r - 1; }
            else if (x >= S - r && y >= S - r) { cx = S - r - 1; cy = S - r - 1; }
            if (cx >= 0) {
                int dx = x - cx, dy = y - cy;
                inside = (dx * dx + dy * dy) <= r * r;
            }
            if (inside) px[y * S + x] = bg;
        }
    }

    // 向上箭头（置顶语义）
    int top = S / 5, mid = S / 2, bottom = S * 3 / 4;
    int cxCenter = S / 2;
    for (int y = top; y <= mid; ++y) {
        int half = (y - top) * (S / 4) / (mid - top);
        for (int x = cxCenter - half; x <= cxCenter + half; ++x)
            if (x >= 0 && x < S) px[y * S + x] = fg;
    }
    int stemW = std::max(2, S / 10);
    for (int y = mid; y <= bottom; ++y)
        for (int x = cxCenter - stemW / 2; x <= cxCenter + stemW / 2; ++x)
            if (x >= 0 && x < S) px[y * S + x] = fg;

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);   // 全 0：使用彩色位

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmMask  = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(dc);
    if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
    return icon;
}

void DestroyTrayIcon(HICON icon)
{
    if (icon) DestroyIcon(icon);
}
