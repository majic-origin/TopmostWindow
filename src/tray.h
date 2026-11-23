#pragma once
#include "common.h"

class TrayIcon
{
public:
    bool Create(HWND owner, HICON icon, const std::wstring& tip);
    void Destroy();
    void SetTip(const std::wstring& tip);
    void Balloon(const std::wstring& title, const std::wstring& text, DWORD flags = NIIF_INFO);
    bool Recreate(HWND owner, HICON icon, const std::wstring& tip);

private:
    NOTIFYICONDATAW m_nid = {};
    bool            m_ok  = false;
    HWND            m_owner = nullptr;
    HICON           m_icon  = nullptr;
    std::wstring    m_tip;
};

// 运行时生成托盘图标（橙色圆角块 + 白色置顶箭头），避免依赖 .ico 资源
HICON CreateTrayIconFromScratch(int size = 32);
void  DestroyTrayIcon(HICON icon);
