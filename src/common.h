#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <cstdarg>
#include <cstdio>
#include <algorithm>

// ---- 自定义消息 ----
#define WM_TMD_TRAY   (WM_APP + 10)   // 托盘回调
#define WM_TMD_TICK   (WM_APP + 11)   // 定时器心跳

#define TMD_TRAY_ID   1
#define IDT_MARKER    1
#define IDT_PURGE     2

// 动作 ID（键盘热键与鼠标钩子共用）
#define HK_TOGGLE     1
#define HK_NEXT       2
#define HK_PREV       3
#define HK_MENU       4
#define HK_UNPIN_ALL  5
#define HK_HIDE       6
#define HK_SHOW_ALL   7
#define HK_SELECT     8   // Ctrl+左键：把光标下的窗口加入/移出批量选择
#define HK_BATCH_PIN  9   // Ctrl+右键：批量置顶 / 取消置顶
#define HK_SELECT_ALL 10  // Ctrl+Shift+A：全选未最小化窗口 / 取消全选
#define HK_ALPHA_UP   11  // Ctrl+↑：当前选中窗口透明度数值 +10（上限 100）
#define HK_ALPHA_DOWN 12  // Ctrl+↓：当前选中窗口透明度数值 -10（下限 0）

// 鼠标键的伪键值（与键盘 VK 共用同一字段，靠 device 区分）
#define TMD_MOUSE_LEFT       1
#define TMD_MOUSE_RIGHT      2
#define TMD_MOUSE_MIDDLE     3
#define TMD_MOUSE_X1         4
#define TMD_MOUSE_X2         5
#define TMD_MOUSE_WHEEL_UP   6
#define TMD_MOUSE_WHEEL_DOWN 7

// 悬停缩放专用伪键值：表示"鼠标移动本身"。
// 它不是一次点击，所以不会出现在 InputHook 的命中判定里（那条通道只在按键按下时匹配），
// 只在 GestureEngine 里当作"这个 Binding 绑定的是'移动'而不是某个鼠标键"的标记。
#define TMD_MOUSE_HOVER      8

// 设备类型
#define TMD_DEV_KEYBOARD 0
#define TMD_DEV_MOUSE    1

struct Binding
{
    UINT device = TMD_DEV_KEYBOARD;
    UINT mods   = 0;
    UINT key    = 0;

    bool Empty() const { return key == 0; }
};

namespace tmd {

// 格式化宽字符串
inline std::wstring Format(const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    return std::wstring(buf);
}

inline std::wstring Trim(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 取可执行文件名（不含路径）
inline std::wstring FileNameOf(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}

// UTF-8 <-> UTF-16
std::string  ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);

// 打开调试输出（OutputDebugString）
void Log(const std::wstring& s);

// 鼠标伪键值 → 中文名
std::wstring MouseButtonName(UINT code);

// 组合键的完整可读描述（含鼠标键）
std::wstring DescribeBinding(const Binding& b);

// 系统取色器（CC_FULLOPEN 的 RGB 色板）
// 弹出的对话框本身不是 topmost，被置顶窗口/置顶面板压住时会"看起来没反应"，
// 所以这里起一个看门线程，对话框一出现就把它提到最前。
bool ChooseColorTopmost(HWND owner, COLORREF& inout);

} // namespace tmd
