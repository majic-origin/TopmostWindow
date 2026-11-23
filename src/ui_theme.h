#pragma once
#include "common.h"

// gdiplus.h 需要 min/max 宏，而工程里定义了 NOMINMAX，这里临时补上
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define TMD_RESTORE_MINMAX 1
#endif
#include <objidl.h>
#include <gdiplus.h>
#ifdef TMD_RESTORE_MINMAX
#undef min
#undef max
#undef TMD_RESTORE_MINMAX
#endif

#pragma comment(lib, "gdiplus.lib")

namespace ui {

// 统一配色（浅色卡片式）
inline Gdiplus::Color C(COLORREF c, BYTE a = 255)
{
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

constexpr COLORREF kCard      = RGB(255, 255, 255);
constexpr COLORREF kBorder    = RGB(226, 230, 236);
constexpr COLORREF kTitle     = RGB(23, 26, 33);
constexpr COLORREF kSubText   = RGB(120, 128, 140);
constexpr COLORREF kField     = RGB(246, 247, 249);
constexpr COLORREF kFieldLine = RGB(216, 221, 229);
constexpr COLORREF kAccent    = RGB(27, 111, 224);
constexpr COLORREF kAccentHov = RGB(20, 95, 200);
constexpr COLORREF kDanger    = RGB(216, 58, 58);
constexpr COLORREF kBtnGhost  = RGB(244, 246, 249);
constexpr COLORREF kBtnLine   = RGB(216, 221, 229);

bool StartGdiplus();
void StopGdiplus();

void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float rad, COLORREF color, BYTE a = 255);
void StrokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float rad, COLORREF color, float w = 1.0f, BYTE a = 255);
void Text(Gdiplus::Graphics& g, const std::wstring& s, const Gdiplus::RectF& r, float px,
          COLORREF color, bool bold = false,
          Gdiplus::StringAlignment h = Gdiplus::StringAlignmentNear,
          Gdiplus::StringAlignment v = Gdiplus::StringAlignmentCenter,
          bool ellipsis = true);

// 圆角窗口外形（Win10 需要 SetWindowRgn；Win11 额外用 DWM 圆角补齐阴影）
void ApplyRoundedWindow(HWND hwnd, int w, int h, int radius);

// 让本进程窗口可靠地拿到前台焦点
void ForceForeground(HWND hwnd);

// 简易自绘按钮
struct Button {
    RECT  rc = {};
    std::wstring text;
    bool  primary = false;
    bool  hover   = false;
    bool  pressed = false;

    bool Hit(POINT p) const { return PtInRect(&rc, p) != FALSE; }
};

void DrawButton(Gdiplus::Graphics& g, const Button& b);

// 弹出卡片的外框：白底 + 标题 + 右上角关闭按钮
void DrawCardChrome(Gdiplus::Graphics& g, int w, int h, const std::wstring& title,
                    const RECT& rcClose, bool closeHover);

} // namespace ui
