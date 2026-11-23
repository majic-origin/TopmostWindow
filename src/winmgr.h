#pragma once
#include "common.h"

// 单个被管理窗口的状态
struct Managed
{
    HWND   hwnd          = nullptr;
    bool   pinned        = false;   // 当前处于置顶
    BYTE   alpha         = 255;     // 255 = 不透明
    bool   layeredAdded  = false;   // WS_EX_LAYERED 是我们加的
    bool   hiddenByUs    = false;
    RECT   savedRect     = {};
};

class WindowManager
{
public:
    // ---- 静态工具 ----
    static bool         Alive(HWND h);
    static bool         IsTopMost(HWND h);
    static std::wstring Title(HWND h);
    static std::wstring ClassName(HWND h);
    static std::wstring ExeName(HWND h);
    static bool         OwnWindow(HWND h);          // 是否属于本进程
    static void         ApplyTopMost(HWND h, bool on);
    static bool         GetFrameRect(HWND h, RECT& rc);  // DWM 精确边界

    // ---- 状态操作 ----
    bool  Toggle(HWND h);                            // 返回操作后是否置顶
    bool  Pin(HWND h, bool on);
    bool  IsPinned(HWND h);
    BYTE  GetAlpha(HWND h);
    void  SetAlpha(HWND h, BYTE a);
    bool  ToggleHide(HWND h);
    void  HideWindow(HWND h);
    void  ShowWindowAgain(HWND h);
    void  RestoreAllHidden();
    int   HiddenCount();
    void  Purge();                                   // 清理失效句柄/同步外部改动
    void  BeginCapture(HWND h);                      // 布局恢复前登记
    void  ApplySnapshot(HWND h, const RECT& rc, bool topmost, BYTE alpha);

    std::vector<HWND> List() const;                  // 置顶窗口（按登记顺序）
    std::vector<HWND> AllManaged() const;
    HWND  Next(HWND after) const;                    // 循环切换置顶窗口
    Managed* Find(HWND h);

private:
    Managed* Ensure(HWND h);
    void     Erase(HWND h);

    std::map<HWND, Managed> m_map;
    std::vector<HWND>       m_order;                 // 登记顺序
};
