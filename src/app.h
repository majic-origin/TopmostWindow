#pragma once
#include "common.h"
#include "config.h"
#include "winmgr.h"
#include "overlay.h"
#include "tray.h"
#include "groups.h"
#include "input_hook.h"

// 菜单命令 ID —— 每一类占独立号段，禁止相互重叠（v3 的粗细/自定义色失效就是号段撞车）
enum : UINT {
    IDM_NONE             = 0,
    IDM_TOGGLE           = 100,
    IDM_ALPHA_BASE       = 200,     // 200..208 -> 100%..20%
    IDM_HIDE_TARGET      = 300,
    IDM_SHOW_ALL         = 301,
    IDM_UNPIN_ALL        = 302,
    IDM_PINNED_BASE      = 400,     // 400..499
    IDM_GROUP_SAVE       = 500,
    IDM_GROUP_RESTORE    = 520,     // 520..559
    IDM_GROUP_DELETE     = 560,     // 560..599
    IDM_MARKER           = 600,
    IDM_AUTOSTART        = 601,
    IDM_SETHOTKEY        = 602,
    IDM_SETMENUHOTKEY    = 603,
    IDM_REFRESH          = 604,
    IDM_ABOUT            = 605,
    IDM_EXIT             = 606,
    IDM_NEXT             = 607,
    IDM_PREV             = 608,
    IDM_SETNEXTHOTKEY    = 610,
    IDM_SETPREVHOTKEY    = 611,
    IDM_SETUNPINHOTKEY   = 612,
    IDM_COLOR_BASE       = 620,     // 620..626 预设色（普通边框）
    IDM_COLOR_CUSTOM     = 630,
    IDM_ACOLOR_BASE      = 640,     // 640..646 预设色（选中边框）
    IDM_ACOLOR_CUSTOM    = 650,
    IDM_THICK_BASE       = 660,     // 660..669 -> 1..10 px
    IDM_NOTIFY_MUTE      = 700,
    IDM_NOTIFY_BASE      = 701,     // 701..705 分类开关
    IDM_NOTIFY_ALL_ON    = 710,
    IDM_NOTIFY_ALL_OFF   = 711
};

// 通知分类
enum NotifyCat {
    NCT_PIN    = 0,   // 置顶 / 取消置顶
    NCT_SWITCH = 1,   // 滚轮或快捷键切换置顶窗口
    NCT_ALPHA  = 2,   // 透明度调整
    NCT_LAYOUT = 3,   // 分组布局保存 / 恢复
    NCT_SYSTEM = 4    // 快捷键占用、设置失败等系统提示
};

class App
{
public:
    explicit App(HINSTANCE inst, int testMode = 0);
    ~App();

    bool Init();
    int  Run();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l);

    void  OnAction(UINT id);        // 键盘热键与鼠标钩子共用
    void  OnTray(WPARAM w, LPARAM l);
    void  OnTick();

    HMENU BuildMenu();
    void  HandleCommand(UINT id);
    void  ShowContextMenu();

    void  ToggleTarget(HWND h);
    void  StepPinned(int dir);      // 在置顶窗口之间循环切换
    void  UnpinAll();               // 取消全部置顶
    void  SetTarget(HWND h);
    void  ActivateWindow(HWND h);
    void  ReRegisterHotkeys();
    void  ReRegisterToggleForCapture();
    void  RefreshOverlayNow();
    void  RefreshTrayTip();
    void  ApplyMarkerConfig();
    void  SaveCfg();
    void  EditBinding(Binding AppConfig::* field, const wchar_t* title, const wchar_t* hint);
    bool  PickColor(COLORREF& inout);
    void  Notify(int cat, const std::wstring& title, const std::wstring& text,
                 DWORD flags = NIIF_INFO);

    HINSTANCE     m_inst = nullptr;
    HWND          m_hwnd = nullptr;
    HICON         m_icon = nullptr;
    AppConfig     m_cfg;
    WindowManager m_wm;
    Overlay       m_overlay;
    TrayIcon      m_tray;
    GroupStore    m_groups;
    InputHook     m_input;

    HWND  m_target = nullptr;    // 当前操作目标（也是"选中"的置顶窗口）
    UINT  m_ticks  = 0;
    UINT  m_msgTaskbarCreated = 0;
    bool  m_testDialog = false;
    int   m_testMode = 0;
    bool  m_toggleHotkeyOk = false;
};
