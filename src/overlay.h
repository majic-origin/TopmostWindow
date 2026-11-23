#pragma once
#include "common.h"

// 置顶窗口描边覆盖层：
// 单个 topmost + 点击穿透 + 逐像素 alpha 的弹出窗口，覆盖整个虚拟屏幕。
// 通过 UpdateLayeredWindow 每帧重绘所有置顶/选中窗口的边框，无需注入目标进程。
// 三种配色：普通置顶 / 当前操作目标 / 批量选中的窗口。
class Overlay
{
public:
    bool Create(HINSTANCE hInst);
    void Destroy();
    void SetStyle(COLORREF color, COLORREF activeColor, COLORREF selectColor, int thickness);
    void Refresh(const std::vector<HWND>& pins, HWND active,
                 const std::vector<HWND>& selected = {});

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void EnsureBuffer(int w, int h);
    void Present();
    void FillRect(int x0, int y0, int x1, int y1, COLORREF color);

    HWND      m_hwnd      = nullptr;
    HDC       m_memDC     = nullptr;
    HBITMAP   m_bmp       = nullptr;
    DWORD*    m_bits      = nullptr;
    int       m_w         = 0;
    int       m_h         = 0;
    int       m_ox        = 0;
    int       m_oy        = 0;
    COLORREF  m_color     = RGB(255, 110, 20);
    COLORREF  m_active    = RGB(27, 111, 224);
    COLORREF  m_select    = RGB(22, 163, 74);
    int       m_thick     = 2;
    bool      m_shown     = false;
};
