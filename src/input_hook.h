#pragma once
#include "common.h"
#include <functional>

// 全局鼠标钩子：RegisterHotKey 只认键盘，鼠标侧键/滚轮/批量选择要靠 WH_MOUSE_LL。
// 命中配置的组合时向 notify 窗口 PostMessage(WM_TMD_INPUT, actionId, 0)，并吞掉这次事件。
class InputHook
{
public:
    // 返回命中的动作 ID（0 = 不匹配）。pt 是这次鼠标事件的屏幕坐标。
    using Matcher = std::function<UINT(UINT mods, UINT mouseKey, POINT pt)>;

    bool Install(HWND notify);
    void Uninstall();
    void SetMatcher(Matcher m) { m_matcher = std::move(m); }

private:
    static LRESULT CALLBACK Proc(int code, WPARAM w, LPARAM l);

    HWND    m_notify  = nullptr;
    Matcher m_matcher;
    HHOOK   m_hook    = nullptr;
};

#define WM_TMD_INPUT (WM_APP + 60)
