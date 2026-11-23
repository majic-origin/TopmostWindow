#include "app.h"
#include "dialogs.h"
#include <commdlg.h>

static const BYTE kAlphaSteps[] = { 100, 90, 80, 70, 60, 50, 40, 30, 20 };
static const int  kAlphaCount   = (int)(sizeof(kAlphaSteps) / sizeof(kAlphaSteps[0]));

struct ColorPreset { const wchar_t* name; COLORREF color; };
static const ColorPreset kPresets[] = {
    { L"橙色", RGB(255, 110, 20) },
    { L"蓝色", RGB(27, 111, 224) },
    { L"绿色", RGB(22, 163, 74) },
    { L"红色", RGB(220, 38, 38) },
    { L"紫色", RGB(147, 51, 234) },
    { L"青色", RGB(6, 182, 212) },
    { L"灰色", RGB(120, 128, 140) },
};
static const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

static std::wstring Clip(const std::wstring& s, size_t n = 42)
{
    if (s.size() <= n) return s;
    return s.substr(0, n) + L"…";
}

static void AppendColorMenu(HMENU menu, UINT baseId, UINT customId, COLORREF cur)
{
    for (int i = 0; i < kPresetCount; ++i) {
        UINT flags = MF_STRING | ((kPresets[i].color == cur) ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, baseId + (UINT)i, kPresets[i].name);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, customId, L"自定义颜色…");
}

HMENU App::BuildMenu()
{
    HMENU root = CreatePopupMenu();

    const bool hasTarget = m_target && IsWindow(m_target) && !WindowManager::OwnWindow(m_target);
    std::wstring tn = hasTarget ? WindowManager::Title(m_target) : L"（未选中窗口）";
    std::wstring ex = hasTarget ? WindowManager::ExeName(m_target) : L"";

    AppendMenuW(root, MF_STRING | MF_GRAYED, IDM_NONE, (L"目标：" + Clip(tn, 34)).c_str());
    if (!ex.empty())
        AppendMenuW(root, MF_STRING | MF_GRAYED, IDM_NONE, (L"　　　" + ex).c_str());
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);

    const bool pinned = hasTarget && WindowManager::IsTopMost(m_target);
    AppendMenuW(root, MF_STRING, IDM_TOGGLE,
                (std::wstring(pinned ? L"取消置顶" : L"置顶该窗口") + L"\t" +
                 tmd::DescribeBinding(m_cfg.bindToggle)).c_str());

    // ---- 透明度 ----
    HMENU alphaMenu = CreatePopupMenu();
    const BYTE curA = hasTarget ? m_wm.GetAlpha(m_target) : 255;
    for (int i = 0; i < kAlphaCount; ++i) {
        BYTE v = (BYTE)(kAlphaSteps[i] * 255 / 100);
        UINT flags = MF_STRING | ((curA == v) ? MF_CHECKED : 0);
        AppendMenuW(alphaMenu, flags, IDM_ALPHA_BASE + i,
                    tmd::Format(L"%d%%", kAlphaSteps[i]).c_str());
    }
    AppendMenuW(root, MF_POPUP | (hasTarget ? 0 : MF_GRAYED),
                (UINT_PTR)alphaMenu, L"窗口透明度");

    // ---- 隐藏 / 显示 ----
    AppendMenuW(root, MF_STRING | (hasTarget ? 0 : MF_GRAYED), IDM_HIDE_TARGET,
                (L"隐藏该窗口\t" + tmd::DescribeBinding(m_cfg.bindHide)).c_str());
    int hidden = m_wm.HiddenCount();
    AppendMenuW(root, MF_STRING | (hidden ? 0 : MF_GRAYED), IDM_SHOW_ALL,
                (tmd::Format(L"显示全部隐藏窗口 (%d)", hidden) + L"\t" +
                 tmd::DescribeBinding(m_cfg.bindShowAll)).c_str());
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);

    // ---- 置顶窗口列表 ----
    std::vector<HWND> pins = m_wm.List();
    HMENU listMenu = CreatePopupMenu();
    if (pins.empty()) {
        AppendMenuW(listMenu, MF_STRING | MF_GRAYED, IDM_NONE, L"（暂无置顶窗口）");
    } else {
        for (size_t i = 0; i < pins.size() && i < 100; ++i) {
            HWND h = pins[i];
            std::wstring text = tmd::Format(L"%u. ", (unsigned)(i + 1)) +
                                Clip(WindowManager::Title(h), 30) + L"  [" +
                                WindowManager::ExeName(h) + L"]";
            UINT flags = MF_STRING | ((h == m_target) ? MF_CHECKED : 0);
            AppendMenuW(listMenu, flags, IDM_PINNED_BASE + (UINT)i, text.c_str());
        }
        AppendMenuW(listMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(listMenu, MF_STRING | (pins.size() > 1 ? 0 : MF_GRAYED), IDM_PREV,
                    (L"切换到上一个\t" + tmd::DescribeBinding(m_cfg.bindPrev)).c_str());
        AppendMenuW(listMenu, MF_STRING | (pins.size() > 1 ? 0 : MF_GRAYED), IDM_NEXT,
                    (L"切换到下一个\t" + tmd::DescribeBinding(m_cfg.bindNext)).c_str());
        AppendMenuW(listMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(listMenu, MF_STRING, IDM_UNPIN_ALL, L"全部取消置顶");
    }
    AppendMenuW(root, MF_POPUP, (UINT_PTR)listMenu,
                tmd::Format(L"置顶窗口列表 (%u)", (unsigned)pins.size()).c_str());

    // ---- 边框外观 ----
    HMENU look = CreatePopupMenu();
    HMENU c1 = CreatePopupMenu();
    AppendColorMenu(c1, IDM_COLOR_BASE, IDM_COLOR_CUSTOM, m_cfg.markerColor);
    AppendMenuW(look, MF_POPUP, (UINT_PTR)c1, L"置顶边框颜色");

    HMENU c2 = CreatePopupMenu();
    AppendColorMenu(c2, IDM_ACOLOR_BASE, IDM_ACOLOR_CUSTOM, m_cfg.markerActive);
    AppendMenuW(look, MF_POPUP, (UINT_PTR)c2, L"选中窗口边框颜色");

    HMENU c3 = CreatePopupMenu();
    AppendColorMenu(c3, IDM_SCOLOR_BASE, IDM_SCOLOR_CUSTOM, m_cfg.markerSelect);
    AppendMenuW(look, MF_POPUP, (UINT_PTR)c3, L"批量选中边框颜色");

    HMENU thick = CreatePopupMenu();
    for (int i = 1; i <= 10; ++i) {
        UINT flags = MF_STRING | ((m_cfg.markerThickness == i) ? MF_CHECKED : 0);
        AppendMenuW(thick, flags, IDM_THICK_BASE + (UINT)(i - 1),
                    tmd::Format(L"%d 像素", i).c_str());
    }
    AppendMenuW(look, MF_POPUP, (UINT_PTR)thick, L"边框粗细");
    AppendMenuW(look, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(look, MF_STRING | (m_cfg.showMarker ? MF_CHECKED : 0), IDM_MARKER, L"显示置顶标记");
    AppendMenuW(root, MF_POPUP, (UINT_PTR)look, L"置顶标记外观");

    // ---- 通知设置（分类开关）----
    HMENU notifyMenu = CreatePopupMenu();
    AppendMenuW(notifyMenu, MF_STRING | (m_cfg.notify.muted ? MF_CHECKED : 0),
                IDM_NOTIFY_MUTE, L"全部静音");
    AppendMenuW(notifyMenu, MF_SEPARATOR, 0, nullptr);
    auto catItem = [&](UINT idx, const wchar_t* label, bool on) {
        AppendMenuW(notifyMenu,
                    MF_STRING | (on ? MF_CHECKED : 0) | (m_cfg.notify.muted ? MF_GRAYED : 0),
                    IDM_NOTIFY_BASE + idx, label);
    };
    catItem(0, L"置顶 / 取消置顶提示",      m_cfg.notify.pin);
    catItem(1, L"切换置顶窗口提示",         m_cfg.notify.switchW);
    catItem(2, L"透明度调整提示",           m_cfg.notify.alpha);
    catItem(3, L"分组布局提示",             m_cfg.notify.layout);
    catItem(4, L"系统与错误提示",           m_cfg.notify.system);
    AppendMenuW(notifyMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(notifyMenu, MF_STRING | (m_cfg.notify.muted ? MF_GRAYED : 0),
                IDM_NOTIFY_ALL_ON,  L"全部开启");
    AppendMenuW(notifyMenu, MF_STRING | (m_cfg.notify.muted ? 0 : MF_GRAYED),
                IDM_NOTIFY_ALL_OFF, L"全部关闭");
    AppendMenuW(root, MF_POPUP, (UINT_PTR)notifyMenu, L"弹窗提醒");

    // ---- 分组布局 ----
    HMENU groupMenu = CreatePopupMenu();
    AppendMenuW(groupMenu, MF_STRING, IDM_GROUP_SAVE, L"保存当前置顶布局…");
    auto names = m_groups.Names();
    if (!names.empty()) {
        AppendMenuW(groupMenu, MF_SEPARATOR, 0, nullptr);
        HMENU restoreMenu = CreatePopupMenu();
        HMENU deleteMenu  = CreatePopupMenu();
        for (size_t i = 0; i < names.size() && i < 40; ++i) {
            const SavedGroup* g = m_groups.Find(names[i]);
            std::wstring label = tmd::Format(L"%s (%u 窗口)",
                names[i].c_str(), g ? (unsigned)g->windows.size() : 0u);
            AppendMenuW(restoreMenu, MF_STRING, IDM_GROUP_RESTORE + (UINT)i, label.c_str());
            AppendMenuW(deleteMenu,  MF_STRING, IDM_GROUP_DELETE  + (UINT)i, names[i].c_str());
        }
        AppendMenuW(groupMenu, MF_POPUP, (UINT_PTR)restoreMenu, L"恢复布局");
        AppendMenuW(groupMenu, MF_POPUP, (UINT_PTR)deleteMenu,  L"删除布局");
    }
    AppendMenuW(root, MF_POPUP, (UINT_PTR)groupMenu, L"分组布局");
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);

    // ---- 批量选择（按住设定键点窗口可多选，这里做批量动作）----
    for (size_t i = 0; i < m_selected.size();) {
        if (!IsWindow(m_selected[i])) m_selected.erase(m_selected.begin() + (long)i);
        else ++i;
    }
    HMENU selMenu = CreatePopupMenu();
    const UINT selN = (UINT)m_selected.size();
    AppendMenuW(selMenu, MF_STRING | MF_GRAYED, IDM_NONE,
                tmd::Format(L"已选中 %u 个窗口", selN).c_str());
    AppendMenuW(selMenu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < m_selected.size() && i < 10; ++i) {
        AppendMenuW(selMenu, MF_STRING | MF_GRAYED, IDM_NONE,
                    (L"· " + Clip(WindowManager::Title(m_selected[i]), 28)).c_str());
    }
    if (selN > 10)
        AppendMenuW(selMenu, MF_STRING | MF_GRAYED, IDM_NONE, L"· …");
    AppendMenuW(selMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(selMenu, MF_STRING | (selN ? 0 : MF_GRAYED), IDM_SEL_HIDE,
                (L"隐藏选中的窗口\t" + tmd::DescribeBinding(m_cfg.bindHide)).c_str());
    AppendMenuW(selMenu, MF_STRING | (selN ? 0 : MF_GRAYED), IDM_SEL_PIN,
                (L"置顶 / 取消置顶选中窗口\t" + tmd::DescribeBinding(m_cfg.bindBatchPin)).c_str());
    AppendMenuW(selMenu, MF_STRING | (selN ? 0 : MF_GRAYED), IDM_SEL_CLEAR, L"清空选择");
    AppendMenuW(selMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(selMenu, MF_STRING, IDM_SELECT_ALL,
                (L"全选 / 取消全选未最小化窗口\t" + tmd::DescribeBinding(m_cfg.bindSelectAll)).c_str());
    AppendMenuW(selMenu, MF_STRING | (m_cfg.syncDrag ? MF_CHECKED : 0), IDM_SYNCDRAG,
                L"同步拖动（多选窗口一起移动）");
    AppendMenuW(root, MF_POPUP, (UINT_PTR)selMenu,
                (L"批量选择" + (selN ? tmd::Format(L" (%u)", selN) : std::wstring())).c_str());

    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);

    // ---- 设置（大窗口，所有快捷键集中在一页里改）----
    AppendMenuW(root, MF_STRING, IDM_SETTINGS, L"快捷键与设置…");

    AppendMenuW(root, MF_STRING | (m_cfg.autoStart ? MF_CHECKED : 0), IDM_AUTOSTART, L"开机自动启动");
    AppendMenuW(root, MF_STRING, IDM_REFRESH, L"刷新窗口状态");
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root, MF_STRING, IDM_ABOUT, L"关于 / 使用说明");
    AppendMenuW(root, MF_STRING, IDM_EXIT, L"退出");

    return root;
}

void App::EditBinding(Binding AppConfig::* field, const wchar_t* title, const wchar_t* hint)
{
    // 先释放自己占用的键盘组合，否则"改回原组合"会被自己挡住
    const UINT ids[] = { HK_TOGGLE, HK_NEXT, HK_PREV, HK_MENU,
                         HK_UNPIN_ALL, HK_HIDE, HK_SHOW_ALL };
    for (UINT id : ids) UnregisterHotKey(m_hwnd, id);

    Binding b = m_cfg.*field;
    if (CaptureBinding(m_hwnd, &b, title, hint)) {
        m_cfg.*field = b;
        SaveCfg();
    }
    ReRegisterHotkeys();
    RefreshTrayTip();
}

bool App::PickColor(COLORREF& inout)
{
    // 主窗口是隐藏的，而置顶窗口本身是 topmost——直接 ChooseColorW 弹出来的
    // 对话框没有 topmost 属性，会被置顶窗口压住，用户看到的就是"点了自定义颜色没反应"。
    // 走带看门线程的版本：对话框一出现就把它提到最前。
    return tmd::ChooseColorTopmost(m_hwnd, inout);
}

void App::HandleCommand(UINT id)
{
    if (id >= IDM_PINNED_BASE && id < IDM_PINNED_BASE + 100) {
        std::vector<HWND> pins = m_wm.List();
        size_t idx = id - IDM_PINNED_BASE;
        if (idx < pins.size()) {
            m_target = pins[idx];
            ActivateWindow(m_target);
            RefreshOverlayNow();
        }
        return;
    }

    if (id >= IDM_ALPHA_BASE && id < IDM_ALPHA_BASE + kAlphaCount) {
        if (!m_target || !IsWindow(m_target)) return;
        int step = (int)(id - IDM_ALPHA_BASE);
        BYTE v = (BYTE)(kAlphaSteps[step] * 255 / 100);
        m_wm.SetAlpha(m_target, v);
        if (v < 255) m_wm.Pin(m_target, true);
        Notify(NCT_ALPHA, L"透明度已调整",
               tmd::Format(L"%s → %d%%", WindowManager::Title(m_target).c_str(), kAlphaSteps[step]));
        RefreshTrayTip();
        RefreshOverlayNow();
        return;
    }

    if (id >= IDM_COLOR_BASE && id < IDM_COLOR_BASE + (UINT)kPresetCount) {
        int i = (int)(id - IDM_COLOR_BASE);
        m_cfg.markerColor = kPresets[i].color;
        SaveCfg(); ApplyMarkerConfig(); RefreshOverlayNow();
        return;
    }

    if (id >= IDM_ACOLOR_BASE && id < IDM_ACOLOR_BASE + (UINT)kPresetCount) {
        int i = (int)(id - IDM_ACOLOR_BASE);
        m_cfg.markerActive = kPresets[i].color;
        SaveCfg(); ApplyMarkerConfig(); RefreshOverlayNow();
        return;
    }

    if (id >= IDM_SCOLOR_BASE && id < IDM_SCOLOR_BASE + (UINT)kPresetCount) {
        int i = (int)(id - IDM_SCOLOR_BASE);
        m_cfg.markerSelect = kPresets[i].color;
        SaveCfg(); ApplyMarkerConfig(); RefreshOverlayNow();
        return;
    }

    if (id >= IDM_THICK_BASE && id < IDM_THICK_BASE + 10) {
        m_cfg.markerThickness = (int)(id - IDM_THICK_BASE) + 1;
        SaveCfg(); ApplyMarkerConfig(); RefreshOverlayNow();
        return;
    }

    if (id >= IDM_NOTIFY_BASE && id < IDM_NOTIFY_BASE + 5) {
        switch (id - IDM_NOTIFY_BASE) {
        case 0: m_cfg.notify.pin     = !m_cfg.notify.pin;     break;
        case 1: m_cfg.notify.switchW = !m_cfg.notify.switchW; break;
        case 2: m_cfg.notify.alpha   = !m_cfg.notify.alpha;   break;
        case 3: m_cfg.notify.layout  = !m_cfg.notify.layout;  break;
        case 4: m_cfg.notify.system  = !m_cfg.notify.system;  break;
        default: break;
        }
        SaveCfg();
        return;
    }

    if (id >= IDM_GROUP_RESTORE && id < IDM_GROUP_RESTORE + 40) {
        auto names = m_groups.Names();
        size_t idx = id - IDM_GROUP_RESTORE;
        if (idx < names.size()) {
            bool ok = m_groups.Restore(names[idx], m_wm);
            Notify(NCT_LAYOUT, ok ? L"布局已恢复" : L"布局恢复失败",
                   ok ? names[idx] : L"未找到与快照匹配的窗口",
                   ok ? NIIF_INFO : NIIF_WARNING);
            RefreshTrayTip();
            RefreshOverlayNow();
        }
        return;
    }

    if (id >= IDM_GROUP_DELETE && id < IDM_GROUP_DELETE + 40) {
        auto names = m_groups.Names();
        size_t idx = id - IDM_GROUP_DELETE;
        if (idx < names.size()) m_groups.Remove(names[idx]);
        return;
    }

    switch (id) {
    case IDM_TOGGLE:
        if (m_target && IsWindow(m_target)) ToggleTarget(m_target);
        break;

    case IDM_SETTINGS:
        OpenSettings();
        break;

    case IDM_SEL_CLEAR:
        ClearSelection();
        RefreshTrayTip();
        break;

    case IDM_SEL_HIDE:
        HideCurrentOrSelected();
        break;

    case IDM_SEL_PIN:
        BatchPinSelected();
        break;

    case IDM_SELECT_ALL:
        SelectAllWindows();
        break;

    case IDM_SYNCDRAG:
        m_cfg.syncDrag = !m_cfg.syncDrag;
        SaveCfg();
        if (!m_cfg.syncDrag) SyncDragEnd();
        Notify(NCT_SYSTEM, L"同步拖动",
               m_cfg.syncDrag ? L"已开启：选中多个窗口后拖动其中一个，其余一起移动"
                              : L"已关闭：拖动只移动当前窗口");
        break;

    case IDM_NEXT: StepPinned(+1); break;
    case IDM_PREV: StepPinned(-1); break;

    case IDM_HIDE_TARGET:
        if (m_target && IsWindow(m_target)) {
            m_wm.HideWindow(m_target);
            Notify(NCT_PIN, L"窗口已隐藏", L"按「显示全部隐藏窗口」的快捷键可以找回来");
            RefreshTrayTip();
            RefreshOverlayNow();
        }
        break;

    case IDM_SHOW_ALL:
        m_wm.RestoreAllHidden();
        RefreshTrayTip();
        RefreshOverlayNow();
        break;

    case IDM_UNPIN_ALL:
        UnpinAll();
        break;

    case IDM_GROUP_SAVE: {
        std::wstring name = tmd::Format(L"布局 %u", (unsigned)(m_groups.Names().size() + 1));
        if (InputBox(m_hwnd, L"保存分组布局", L"请输入分组名称：", name)) {
            bool ok = m_groups.Capture(name, m_wm);
            Notify(NCT_LAYOUT, ok ? L"布局已保存" : L"保存失败",
                   ok ? name : L"当前没有可保存的置顶窗口",
                   ok ? NIIF_INFO : NIIF_WARNING);
        }
        break;
    }

    case IDM_MARKER:
        m_cfg.showMarker = !m_cfg.showMarker;
        SaveCfg();
        if (m_cfg.showMarker) RefreshOverlayNow();
        else m_overlay.Refresh({}, nullptr);
        break;

    case IDM_NOTIFY_MUTE:
        m_cfg.notify.muted = !m_cfg.notify.muted;
        SaveCfg();
        break;

    case IDM_NOTIFY_ALL_ON:
        m_cfg.notify.muted = false;
        m_cfg.notify.SetAll(true);
        SaveCfg();
        break;

    case IDM_NOTIFY_ALL_OFF:
        m_cfg.notify.muted = false;
        m_cfg.notify.SetAll(false);
        SaveCfg();
        break;

    case IDM_COLOR_CUSTOM:
        if (PickColor(m_cfg.markerColor)) { SaveCfg(); ApplyMarkerConfig(); RefreshOverlayNow(); }
        break;

    case IDM_ACOLOR_CUSTOM:
        if (PickColor(m_cfg.markerActive)) { SaveCfg(); ApplyMarkerConfig(); RefreshOverlayNow(); }
        break;

    case IDM_SCOLOR_CUSTOM:
        if (PickColor(m_cfg.markerSelect)) { SaveCfg(); ApplyMarkerConfig(); RefreshOverlayNow(); }
        break;

    case IDM_AUTOSTART: {
        m_cfg.autoStart = !m_cfg.autoStart;
        if (ApplyAutoStart(m_cfg.autoStart)) {
            SaveCfg();
            Notify(NCT_SYSTEM, L"开机自启",
                   m_cfg.autoStart ? L"已加入当前用户启动项" : L"已移除启动项");
        } else {
            m_cfg.autoStart = !m_cfg.autoStart;
            Notify(NCT_SYSTEM, L"设置失败", L"无法写入注册表启动项", NIIF_ERROR);
        }
        break;
    }

    case IDM_SETHOTKEY:
        EditBinding(&AppConfig::bindToggle, L"设置置顶快捷键", L"按下后切换当前窗口的置顶状态");
        break;

    case IDM_SETNEXTHOTKEY:
        EditBinding(&AppConfig::bindNext, L"设置「下一个置顶窗口」快捷键",
                    L"建议绑定 Win + 滚轮下滚，或用键盘组合");
        break;

    case IDM_SETPREVHOTKEY:
        EditBinding(&AppConfig::bindPrev, L"设置「上一个置顶窗口」快捷键",
                    L"建议绑定 Win + 滚轮上滚，或用键盘组合");
        break;

    case IDM_SETUNPINHOTKEY:
        EditBinding(&AppConfig::bindUnpinAll, L"设置「取消全部置顶」快捷键",
                    L"默认 Ctrl + Shift + 鼠标右键，可改成任意键鼠组合");
        break;

    case IDM_SETMENUHOTKEY:
        EditBinding(&AppConfig::bindMenu, L"设置「打开菜单」快捷键", L"按下后弹出管理菜单");
        break;

    case IDM_REFRESH:
        m_wm.Purge();
        RefreshTrayTip();
        RefreshOverlayNow();
        break;

    case IDM_ABOUT:
        MessageBoxW(m_hwnd,
            L"TopMostDesk 1.5 — Windows 窗口置顶助手\n\n"
            L"· 置顶 / 取消置顶：焦点放到目标窗口后按快捷键（默认 Win+Shift+T，被占用时自动退让）\n"
            L"· 隐藏窗口：Ctrl+H　　显示全部隐藏窗口：Ctrl+Shift+H\n"
            L"· 批量选择：按住 Ctrl 点窗口可连续多选；再按 Ctrl+H 或 Ctrl+鼠标右键批量隐藏 / 置顶\n"
            L"· 切换置顶窗口：默认 Win+滚轮下滚 / Win+滚轮上滚\n"
            L"· 快捷键与外观：托盘菜单 →「快捷键与设置…」，所有项都能自定义\n"
            L"· 快捷键支持键盘组合与鼠标键（左/右/中/侧键/滚轮方向）\n"
            L"· 配置与布局：%APPDATA%\\TopMostDesk\\config.ini\n",
            L"关于 TopMostDesk", MB_OK | MB_ICONINFORMATION);
        break;

    case IDM_EXIT:
        if (m_hwnd) DestroyWindow(m_hwnd);
        break;

    default:
        break;
    }
}
