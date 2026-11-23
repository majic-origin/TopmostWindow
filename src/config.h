#pragma once
#include "common.h"

// 弹窗分类开关：用户自己决定哪些提醒要看
struct NotifyCfg
{
    bool muted    = false;   // 总开关：全部静音
    bool pin      = true;    // 置顶 / 取消置顶
    bool switchW  = true;    // 切换置顶窗口
    bool alpha    = true;    // 透明度调整
    bool layout   = true;    // 分组布局
    bool system   = true;    // 快捷键占用 / 设置失败等

    bool Allowed(int cat) const;
    void SetAll(bool v);
};

// 全局配置（存放于 %APPDATA%\TopMostDesk\config.ini）
struct AppConfig
{
    // 置顶/取消置顶：默认 Win+Shift+T（键盘），也可绑定鼠标键
    Binding bindToggle { TMD_DEV_KEYBOARD, MOD_WIN | MOD_SHIFT, 'T' };
    // 切换到下一个 / 上一个置顶窗口：默认 Win+滚轮下 / Win+滚轮上
    Binding bindNext   { TMD_DEV_MOUSE, MOD_WIN, TMD_MOUSE_WHEEL_DOWN };
    Binding bindPrev   { TMD_DEV_MOUSE, MOD_WIN, TMD_MOUSE_WHEEL_UP };
    // 打开管理菜单
    Binding bindMenu   { TMD_DEV_KEYBOARD, MOD_WIN | MOD_SHIFT, 'M' };
    // 取消全部置顶：默认 Ctrl+Shift+鼠标右键
    Binding bindUnpinAll { TMD_DEV_MOUSE, MOD_CONTROL | MOD_SHIFT, TMD_MOUSE_RIGHT };
    // 隐藏当前窗口 / 显示全部隐藏窗口
    Binding bindHide    { TMD_DEV_KEYBOARD, MOD_CONTROL, 'H' };
    Binding bindShowAll { TMD_DEV_KEYBOARD, MOD_CONTROL | MOD_SHIFT, 'H' };
    // 批量选择（把光标下的窗口加入选择集）与批量置顶
    Binding bindSelect  { TMD_DEV_MOUSE, MOD_CONTROL, TMD_MOUSE_LEFT };
    Binding bindBatchPin{ TMD_DEV_MOUSE, MOD_CONTROL, TMD_MOUSE_RIGHT };
    // 全选 / 取消全选未最小化窗口
    Binding bindSelectAll { TMD_DEV_KEYBOARD, MOD_CONTROL | MOD_SHIFT, 'A' };

    bool     autoStart       = false;                 // 开机自启
    bool     showMarker      = true;                  // 置顶窗口视觉标记
    bool     syncDrag        = false;                 // 多选后拖动一个，其余同步移动
    COLORREF markerColor     = RGB(255, 110, 20);      // 普通置顶窗口的边框颜色
    COLORREF markerActive    = RGB(27, 111, 224);      // 当前选中窗口的边框颜色
    COLORREF markerSelect    = RGB(22, 163, 74);       // 批量选中窗口的边框颜色
    int      markerThickness = 2;                     // 边框粗细（像素）
    BYTE     defaultAlpha    = 210;

    NotifyCfg notify;                                 // 分类弹窗开关
};

std::wstring GetConfigPath();
std::wstring GetConfigDir();

bool LoadConfig(AppConfig& cfg);
bool SaveConfig(const AppConfig& cfg);

bool ApplyAutoStart(bool enable);
bool QueryAutoStart();

// 设备+修饰键+键值 → 可读文本
std::wstring HotkeyToString(const Binding& b);
