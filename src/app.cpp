#include "app.h"
#include "dialogs.h"
#include "settings.h"
#include "ui_theme.h"
#include "resource.h"
#include <dwmapi.h>
#include <vector>

static const wchar_t* kAppClass = L"TopMostDeskMainWnd";

App* App::s_self = nullptr;

// 前台窗口切换消息（由 WinEventProc 投递到消息循环，避免在系统回调里直接重绘）
static const UINT WM_TMD_FOREGROUND = WM_APP + 50;

App::App(HINSTANCE inst, int testMode) : m_inst(inst), m_testMode(testMode)
{
    m_testDialog = testMode != 0;
}

App::~App()
{
    m_input.Uninstall();
    if (m_fgHook) { UnhookWinEvent(m_fgHook); m_fgHook = nullptr; }
    if (s_self == this) s_self = nullptr;
    if (m_hwnd) DestroyWindow(m_hwnd);
    m_overlay.Destroy();
    m_tray.Destroy();
    DestroyTrayIcon(m_icon);
    ui::StopGdiplus();
}

bool App::Init()
{
    ui::StartGdiplus();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &App::WndProc;
    wc.hInstance     = m_inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = (HICON)LoadImageW(m_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                         32, 32, LR_DEFAULTCOLOR);
    wc.hIconSm       = (HICON)LoadImageW(m_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.lpszClassName = kAppClass;
    RegisterClassExW(&wc);

    // 隐藏主窗口：仅用于接收热键/托盘/定时器/鼠标钩子消息
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kAppClass, L"TopMostDesk",
                             WS_OVERLAPPED, 0, 0, 0, 0,
                             nullptr, nullptr, m_inst, this);
    if (!m_hwnd) return false;

    LoadConfig(m_cfg);
    m_cfg.autoStart = QueryAutoStart();
    m_groups.Load();

    // 托盘图标：优先用资源里的多尺寸 ICO，退回运行时绘制
    m_icon = (HICON)LoadImageW(m_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                               GetSystemMetrics(SM_CXSMICON),
                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!m_icon) m_icon = CreateTrayIconFromScratch(32);
    m_tray.Create(m_hwnd, m_icon, L"TopMostDesk — 窗口置顶助手");

    m_overlay.Create(m_inst);
    ApplyMarkerConfig();

    // 鼠标点一下置顶窗口时，系统只是换了前台窗口——没有任何消息会主动告诉我们。
    // 不跟着走的话 m_target 永远是上一次用快捷键/菜单操作的那个窗口，
    // 描边就"选中了也不变色"。这里挂 EVENT_SYSTEM_FOREGROUND 补上这一环。
    s_self = this;
    m_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                               nullptr, &App::WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    // 鼠标键（右键/侧键/滚轮/批量选择）靠全局低级鼠标钩子；键盘走 RegisterHotKey
    m_input.SetMatcher([this](UINT mods, UINT mouseKey, POINT pt) -> UINT {
        if (m_modal) return 0;      // 设置窗口打开时不要抢鼠标
        m_lastMouse = pt;
        auto hit = [&](const Binding& b, UINT id) -> UINT {
            if (b.device != TMD_DEV_MOUSE || b.key == 0) return 0;
            return (b.key == mouseKey && b.mods == mods) ? id : 0;
        };
        // 批量选择优先：Ctrl+左键在任何窗口上都先当作"选择"
        if (UINT r = hit(m_cfg.bindSelect,   HK_SELECT))    return r;
        if (UINT r = hit(m_cfg.bindBatchPin, HK_BATCH_PIN)) return r;
        if (UINT r = hit(m_cfg.bindToggle,   HK_TOGGLE))    return r;
        if (UINT r = hit(m_cfg.bindNext,     HK_NEXT))      return r;
        if (UINT r = hit(m_cfg.bindPrev,     HK_PREV))      return r;
        if (UINT r = hit(m_cfg.bindMenu,     HK_MENU))      return r;
        if (UINT r = hit(m_cfg.bindUnpinAll, HK_UNPIN_ALL)) return r;
        if (UINT r = hit(m_cfg.bindHide,     HK_HIDE))      return r;
        if (UINT r = hit(m_cfg.bindShowAll,  HK_SHOW_ALL))  return r;
        if (UINT r = hit(m_cfg.bindSelectAll, HK_SELECT_ALL)) return r;
        if (UINT r = hit(m_cfg.bindAlphaUp,   HK_ALPHA_UP))   return r;
        if (UINT r = hit(m_cfg.bindAlphaDown, HK_ALPHA_DOWN)) return r;
        return 0;
    });
    m_input.SetListener([this](UINT msg, POINT pt) { OnHookMouse(msg, pt); });
    m_input.Install(m_hwnd);

    m_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    ReRegisterHotkeys();
    SetTimer(m_hwnd, IDT_MARKER, 200, nullptr);
    SetTimer(m_hwnd, IDT_PURGE,  2000, nullptr);
    RefreshTrayTip();

    if (m_testMode == 1) PostMessageW(m_hwnd, WM_APP + 40, 0, 0);
    if (m_testMode == 2) PostMessageW(m_hwnd, WM_APP + 41, 0, 0);
    if (m_testMode == 3) PostMessageW(m_hwnd, WM_APP + 42, 0, 0);
    if (m_testMode == 4) PostMessageW(m_hwnd, WM_APP + 43, 0, 0);
    if (m_testMode == 5) PostMessageW(m_hwnd, WM_APP + 46, 0, 0);
    return true;
}

int App::Run()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

void CALLBACK App::WinEventProc(HWINEVENTHOOK, DWORD evt, HWND hwnd,
                                LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (evt != EVENT_SYSTEM_FOREGROUND) return;
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

    App* self = s_self;
    if (!self || !self->m_hwnd || !hwnd) return;
    // 回调里只投递消息：WinEvent 回调期间不能重入 UpdateLayeredWindow / SetWindowPos
    PostMessageW(self->m_hwnd, WM_TMD_FOREGROUND, (WPARAM)hwnd, 0);
}

LRESULT CALLBACK App::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
        self = (App*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->m_hwnd = h;
    } else {
        self = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (self) return self->Handle(h, msg, w, l);
    return DefWindowProcW(h, msg, w, l);
}

LRESULT App::Handle(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    if (hwnd && !m_hwnd) m_hwnd = hwnd;

    if (msg == m_msgTaskbarCreated && m_msgTaskbarCreated != 0) {
        m_tray.Recreate(m_hwnd, m_icon, L"TopMostDesk — 窗口置顶助手");
        m_input.Uninstall();
        m_input.SetListener([this](UINT msg, POINT pt) { OnHookMouse(msg, pt); });
    m_input.Install(m_hwnd);
        return 0;
    }

    switch (msg) {
    case WM_HOTKEY:      // 键盘
    case WM_TMD_INPUT:   // 鼠标（来自 InputHook）
        OnAction((UINT)w);
        return 0;

    case WM_TMD_TRAY:
        OnTray(w, l);
        return 0;

    case WM_TIMER:
        if (w == IDT_MARKER) {
            // 兜底：万一 WinEvent 钩子被系统回收（比如 Explorer 重启、钩子安装失败），
            // 200ms 的节拍里自己对比一次前台窗口，边框颜色最多慢一拍，不会永久卡住
            SyncForegroundTarget();
            RefreshOverlayNow();
        }
        else if (w == IDT_PURGE) m_wm.Purge();
        return 0;

    case WM_TMD_FOREGROUND:
        if ((HWND)w == GetForegroundWindow() && SyncForegroundTarget())
            RefreshOverlayNow();
        return 0;

    case WM_DESTROY:
        KillTimer(m_hwnd, IDT_MARKER);
        KillTimer(m_hwnd, IDT_PURGE);
        PostQuitMessage(0);
        return 0;

    case WM_APP + 40: {          // 调试：置顶快捷键
        ReRegisterToggleForCapture();
        Binding b = m_cfg.bindToggle;
        if (CaptureBinding(m_hwnd, &b, L"设置置顶快捷键",
                           L"按下后切换当前窗口的置顶状态")) {
            m_cfg.bindToggle = b;
            SaveCfg();
        }
        ReRegisterHotkeys();
        RefreshTrayTip();
        return 0;
    }

    case WM_APP + 41: {          // 调试：输入框
        std::wstring name = L"工作布局";
        InputBox(m_hwnd, L"保存分组布局", L"请输入分组名称：", name);
        return 0;
    }

    case WM_APP + 42: {          // 调试：滚轮切换快捷键
        Binding b = m_cfg.bindNext;
        if (CaptureBinding(m_hwnd, &b, L"设置「下一个置顶窗口」快捷键",
                           L"建议使用 Win + 滚轮下滚")) {
            m_cfg.bindNext = b;
            SaveCfg();
        }
        ReRegisterHotkeys();
        return 0;
    }

    case WM_APP + 43: {          // 调试：直接弹系统取色器
        COLORREF c = m_cfg.markerColor;
        if (PickColor(c)) { m_cfg.markerColor = c; SaveCfg(); ApplyMarkerConfig(); RefreshOverlayNow(); }
        return 0;
    }

    case WM_APP + 44:            // 调试：把边框粗细设为 8px
        HandleCommand(IDM_THICK_BASE + 7);
        return 0;

    case WM_APP + 45: {          // 调试：通知开关（w=0..4 分类；97 全关；98 全开；99 静音）
        const UINT n = (UINT)w;
        if (n == 99)      HandleCommand(IDM_NOTIFY_MUTE);
        else if (n == 98) HandleCommand(IDM_NOTIFY_ALL_ON);
        else if (n == 97) HandleCommand(IDM_NOTIFY_ALL_OFF);
        else              HandleCommand(IDM_NOTIFY_BASE + n);
        return 0;
    }

    case WM_APP + 46:            // 调试：打开设置大窗口
        OpenSettings();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd ? hwnd : m_hwnd, msg, w, l);
}

void App::Notify(int cat, const std::wstring& title, const std::wstring& text, DWORD flags)
{
    if (!m_cfg.notify.Allowed(cat)) return;
    m_tray.Balloon(title, text, flags);
}

void App::OnAction(UINT id)
{
    switch (id) {
    case HK_TOGGLE: {
        HWND fg = GetForegroundWindow();
        if (!fg || WindowManager::OwnWindow(fg)) fg = m_wm.Next(nullptr);
        if (fg) ToggleTarget(fg);
        break;
    }
    case HK_NEXT: StepPinned(+1); break;
    case HK_PREV: StepPinned(-1); break;
    case HK_UNPIN_ALL: UnpinAll(); break;
    case HK_HIDE:     HideCurrentOrSelected(); break;
    case HK_SHOW_ALL: {
        int n = m_wm.HiddenCount();
        m_wm.RestoreAllHidden();
        Notify(NCT_PIN, L"已恢复隐藏窗口", tmd::Format(L"共 %d 个窗口", n));
        RefreshTrayTip();
        RefreshOverlayNow();
        break;
    }
    case HK_SELECT:   SelectUnderCursor(m_lastMouse); break;
    case HK_SELECT_ALL: SelectAllWindows(); break;
    case HK_BATCH_PIN: BatchPinSelected(); break;
    case HK_ALPHA_UP:   AdjustTargetAlpha(+1); break;
    case HK_ALPHA_DOWN: AdjustTargetAlpha(-1); break;
    case HK_MENU:
        m_target = GetForegroundWindow();
        ShowContextMenu();
        break;
    default: break;
    }
}

void App::AdjustTargetAlpha(int dir)
{
    // 只对当前选中的窗口生效；没有目标 / 目标是本程序窗口时安全忽略
    HWND h = m_target;
    if (!h || !IsWindow(h) || WindowManager::OwnWindow(h)) return;

    // 与托盘菜单「窗口透明度」同一量纲：100 = 不透明(alpha 255)，0 = 全透明(alpha 0)
    const BYTE cur = m_wm.GetAlpha(h);
    int pct = (cur * 100 + 127) / 255;   // 四舍五入回百分比
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    int next = pct + dir * 10;
    if (next < 0)   next = 0;
    if (next > 100) next = 100;
    if (next == pct) return;             // 已到边界：安全忽略，不打扰用户

    m_wm.SetAlpha(h, (BYTE)(next * 255 / 100));
    Notify(NCT_ALPHA, L"透明度已调整",
           tmd::Format(L"%s → %d%%", WindowManager::Title(h).c_str(), next));
}

// ---------------- 批量选择 ----------------

void App::PruneSelection()
{
    for (size_t i = 0; i < m_selected.size();) {
        if (!IsWindow(m_selected[i])) m_selected.erase(m_selected.begin() + (long)i);
        else ++i;
    }
}

void App::SelectUnderCursor(POINT pt)
{
    HWND h = WindowFromPoint(pt);
    if (!h) return;
    HWND root = GetAncestor(h, GA_ROOT);
    if (root) h = root;
    if (WindowManager::OwnWindow(h)) return;
    if (!IsWindowVisible(h)) return;

    PruneSelection();
    auto it = std::find(m_selected.begin(), m_selected.end(), h);
    if (it != m_selected.end()) {
        m_selected.erase(it);
        Notify(NCT_PIN, L"已移出选择", WindowManager::Title(h));
    } else {
        m_selected.push_back(h);
        Notify(NCT_PIN, tmd::Format(L"已选中 %u 个窗口", (unsigned)m_selected.size()),
               WindowManager::Title(h) + L"  [" + WindowManager::ExeName(h) + L"]");
    }
    m_target = h;
    RefreshTrayTip();
    RefreshOverlayNow();
}

void App::ClearSelection()
{
    if (m_selected.empty()) return;
    m_selected.clear();
    RefreshOverlayNow();
}

void App::HideCurrentOrSelected()
{
    PruneSelection();
    if (!m_selected.empty()) {
        const size_t n = m_selected.size();
        for (HWND h : m_selected) m_wm.HideWindow(h);
        m_selected.clear();
        Notify(NCT_PIN, L"已隐藏选中的窗口", tmd::Format(L"共 %u 个", (unsigned)n));
    } else {
        HWND fg = GetForegroundWindow();
        if (!fg || WindowManager::OwnWindow(fg)) fg = m_target;
        if (!fg || !IsWindow(fg)) return;
        m_wm.HideWindow(fg);
        Notify(NCT_PIN, L"窗口已隐藏", L"用「显示全部隐藏窗口」快捷键可以找回来");
    }
    RefreshTrayTip();
    RefreshOverlayNow();
}

void App::BatchPinSelected()
{
    PruneSelection();
    std::vector<HWND> targets = m_selected;
    if (targets.empty()) {
        HWND fg = GetForegroundWindow();
        if (!fg || WindowManager::OwnWindow(fg)) fg = m_target;
        if (!fg || !IsWindow(fg)) return;
        targets.push_back(fg);
    }

    // 只要有一个还没置顶 → 全部置顶；否则全部取消
    bool anyUnpinned = false;
    for (HWND h : targets)
        if (!WindowManager::IsTopMost(h)) { anyUnpinned = true; break; }

    for (HWND h : targets) {
        m_wm.Pin(h, anyUnpinned);
        if (!anyUnpinned && m_wm.GetAlpha(h) < 255) m_wm.SetAlpha(h, 255);
    }
    m_target = targets.front();

    Notify(NCT_PIN,
           anyUnpinned ? L"已置顶选中的窗口" : L"已取消选中窗口的置顶",
           tmd::Format(L"共 %u 个", (unsigned)targets.size()));

    if (!m_selected.empty()) m_selected.clear();
    RefreshTrayTip();
    RefreshOverlayNow();
}

// 选择集相关的小工具
static bool ContainsHwnd(const std::vector<HWND>& v, HWND h)
{
    return std::find(v.begin(), v.end(), h) != v.end();
}

// 可被「全选」的窗口：可见、未最小化、有标题、非工具窗、非本进程
static BOOL CALLBACK EnumSelectable(HWND h, LPARAM lp)
{
    auto* out = reinterpret_cast<std::vector<HWND>*>(lp);
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    if (GetWindow(h, GW_OWNER) != nullptr) return TRUE;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (WindowManager::OwnWindow(h)) return TRUE;

    RECT rc = {};
    if (!GetWindowRect(h, &rc)) return TRUE;
    if (rc.right - rc.left < 40 || rc.bottom - rc.top < 40) return TRUE;

    wchar_t title[256] = {};
    if (GetWindowTextW(h, title, 256) <= 0) return TRUE;

    // UWP 关掉后仍挂着但被 DWM 隐藏的"幽灵窗口"
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;

    out->push_back(h);
    return TRUE;
}

void App::SelectAllWindows()
{
    std::vector<HWND> all;
    EnumWindows(&EnumSelectable, (LPARAM)&all);
    if (all.empty()) {
        Notify(NCT_PIN, L"没有可选窗口", L"当前桌面没有可选择的普通窗口");
        return;
    }

    // 已经全部选中 → 这次按下就是取消全选
    bool allSelected = true;
    for (HWND h : all)
        if (!ContainsHwnd(m_selected, h)) { allSelected = false; break; }

    if (allSelected && !m_selected.empty()) {
        m_selected.clear();
        Notify(NCT_PIN, L"已取消全选", tmd::Format(L"共 %u 个窗口", (unsigned)all.size()));
    } else {
        m_selected = all;
        Notify(NCT_PIN, tmd::Format(L"已全选 %u 个窗口", (unsigned)all.size()),
               L"按 Ctrl+H 批量隐藏，或 Ctrl+右键 批量置顶");
    }
    m_target = m_selected.empty() ? m_target : m_selected.front();
    RefreshTrayTip();
    RefreshOverlayNow();
}

// ---------------- 同步拖动 ----------------


void App::OnHookMouse(UINT msg, POINT pt)
{
    if (!m_cfg.syncDrag) {
        if (m_dragActive) SyncDragEnd();
        return;
    }
    switch (msg) {
    case WM_LBUTTONDOWN: SyncDragBegin(pt); break;
    case WM_MOUSEMOVE:   SyncDragUpdate();  break;
    case WM_LBUTTONUP:   SyncDragEnd();     break;
    default: break;
    }
}

void App::SyncDragBegin(POINT pt)
{
    if (m_dragActive) SyncDragEnd();
    if (m_cfg.syncDrag == false) return;
    // Ctrl+左键是「批量选择」手势，不当作拖动起点
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) return;

    PruneSelection();
    if (m_selected.size() < 2) return;

    HWND h = WindowFromPoint(pt);
    if (!h) return;
    HWND root = GetAncestor(h, GA_ROOT);
    if (root) h = root;
    if (WindowManager::OwnWindow(h)) return;
    if (IsIconic(h) || !ContainsHwnd(m_selected, h)) return;

    m_dragOrig.clear();
    for (HWND w : m_selected) {
        if (!IsWindow(w) || IsIconic(w) || !IsWindowVisible(w)) continue;  // 最小化/隐藏的不参与，但留在选择集里
        RECT rc = {};
        if (!GetWindowRect(w, &rc)) continue;   // 必须是窗口矩形，SetWindowPos 也按这个摆
        m_dragOrig.push_back(DragOrig{ w, rc });
    }
    if (m_dragOrig.size() < 2) return;

    m_dragAnchor = h;
    m_dragDelta  = POINT{ 0, 0 };
    m_dragActive = true;
}

void App::AlignDraggedWindows()
{
    const DragOrig* base = nullptr;
    for (const auto& o : m_dragOrig)
        if (o.hwnd == m_dragAnchor) { base = &o; break; }
    if (!base) return;

    RECT cur = {};
    if (!GetWindowRect(m_dragAnchor, &cur)) return;

    // 用"相对起始位置的绝对位移"，而不是增量累加：
    // 别的窗口的 SetWindowPos 是跨进程调用，读完矩形再算增量会被 DWM 的延迟带偏
    const int dx = cur.left - base->rc.left;
    const int dy = cur.top  - base->rc.top;
    if (dx == m_dragDelta.x && dy == m_dragDelta.y) return;
    m_dragDelta.x = dx;
    m_dragDelta.y = dy;

    for (const auto& o : m_dragOrig) {
        if (o.hwnd == m_dragAnchor) continue;
        if (!IsWindow(o.hwnd)) continue;                       // 被关闭：跳过
        if (!ContainsHwnd(m_selected, o.hwnd)) continue;       // 拖动途中被取消选中
        if (IsIconic(o.hwnd) || !IsWindowVisible(o.hwnd)) continue;  // 被最小化 / 隐藏
        SetWindowPos(o.hwnd, nullptr, o.rc.left + dx, o.rc.top + dy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    RefreshOverlayNow();
}

void App::SyncDragUpdate()
{
    if (!m_dragActive) return;
    if (!IsWindow(m_dragAnchor) || IsIconic(m_dragAnchor) ||
        !IsWindowVisible(m_dragAnchor) || !ContainsHwnd(m_selected, m_dragAnchor)) {
        SyncDragEnd();
        return;
    }
    AlignDraggedWindows();
}

void App::SyncDragEnd()
{
    if (!m_dragActive) return;

    // 收尾对齐：抬起鼠标时锚点窗口的 DWM 边界可能还差最后一帧，按最终位置再摆一次
    if (m_dragAnchor && IsWindow(m_dragAnchor) && IsWindowVisible(m_dragAnchor) &&
        !IsIconic(m_dragAnchor)) {
        AlignDraggedWindows();
    }

    m_dragActive = false;
    m_dragAnchor = nullptr;
    m_dragDelta  = POINT{ 0, 0 };
    m_dragOrig.clear();
    PruneSelection();
    RefreshOverlayNow();
}

void App::OpenSettings()
{
    // 先释放自己占用的热键，否则"改回原组合"会被自己挡住，占用校验也不准
    const UINT ids[] = { HK_TOGGLE, HK_NEXT, HK_PREV, HK_MENU,
                         HK_UNPIN_ALL, HK_HIDE, HK_SHOW_ALL, HK_SELECT_ALL,
                         HK_ALPHA_UP, HK_ALPHA_DOWN };
    for (UINT id : ids) UnregisterHotKey(m_hwnd, id);
    m_toggleHotkeyOk = false;

    AppConfig next = m_cfg;
    m_modal = true;
    const bool saved = ShowSettingsWindow(m_hwnd, &next);
    m_modal = false;

    if (saved) {
        m_cfg = next;
        SaveCfg();
        ApplyMarkerConfig();
        RefreshOverlayNow();
    }
    ReRegisterHotkeys();
    RefreshTrayTip();
}

void App::StepPinned(int dir)
{
    std::vector<HWND> list = m_wm.List();
    if (list.empty()) {
        Notify(NCT_SWITCH, L"没有置顶窗口", L"先把需要的窗口置顶，再用滚轮切换");
        return;
    }
    if (list.size() == 1) {
        m_target = list[0];
        ActivateWindow(list[0]);
        return;
    }

    int idx = 0;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i] == m_target) { idx = (int)i; break; }

    const int n = (int)list.size();
    idx = ((idx + dir) % n + n) % n;
    m_target = list[idx];

    ActivateWindow(m_target);
    Notify(NCT_SWITCH, tmd::Format(L"置顶窗口 %d / %d", idx + 1, n),
           WindowManager::Title(m_target) + L"  [" +
           WindowManager::ExeName(m_target) + L"]");
    RefreshOverlayNow();
}

void App::UnpinAll()
{
    std::vector<HWND> pins = m_wm.List();
    if (pins.empty()) {
        Notify(NCT_PIN, L"没有置顶窗口", L"当前没有需要取消的置顶");
        return;
    }
    for (HWND h : pins) m_wm.Pin(h, false);
    Notify(NCT_PIN, L"已取消全部置顶",
           tmd::Format(L"共 %u 个窗口", (unsigned)pins.size()));
    RefreshTrayTip();
    RefreshOverlayNow();
}

void App::ToggleTarget(HWND h)
{
    if (!h || WindowManager::OwnWindow(h)) return;

    const bool nowPinned = m_wm.Toggle(h);
    m_target = h;

    Notify(NCT_PIN, nowPinned ? L"已置顶" : L"已取消置顶", WindowManager::Title(h));

    // 取消置顶时恢复不透明，避免"看不见的窗口"
    if (!nowPinned && m_wm.GetAlpha(h) < 255)
        m_wm.SetAlpha(h, 255);

    RefreshTrayTip();
    RefreshOverlayNow();
}

void App::SetTarget(HWND h)
{
    m_target = h;
}

// 前台窗口落到某个「已置顶」窗口上时，把它认成当前目标。
// 判定条件刻意收紧：
//   · 本进程自己的窗口（主窗口 / 覆盖层 / 托盘气泡 / 设置窗口）不算
//   · 已经在批量选择集里的不算——那是绿框，不该被蓝框顶掉
//   · 没置顶的普通窗口不算——点一下别人的窗口不该让描边跟过去
bool App::SyncForegroundTarget()
{
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindow(fg)) return false;
    if (WindowManager::OwnWindow(fg)) return false;
    if (fg == m_target) return false;

    for (size_t i = 0; i < m_selected.size();) {
        if (!IsWindow(m_selected[i])) m_selected.erase(m_selected.begin() + (long)i);
        else ++i;
    }
    if (std::find(m_selected.begin(), m_selected.end(), fg) != m_selected.end())
        return false;

    if (!m_wm.IsPinned(fg)) return false;

    m_target = fg;
    return true;
}

void App::ActivateWindow(HWND h)
{
    if (!IsWindow(h)) return;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);

    DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD myThread = GetCurrentThreadId();
    bool attached = false;
    if (fgThread && fgThread != myThread)
        attached = AttachThreadInput(myThread, fgThread, TRUE) != FALSE;

    SetForegroundWindow(h);
    BringWindowToTop(h);
    if (attached) AttachThreadInput(myThread, fgThread, FALSE);
}

void App::OnTray(WPARAM w, LPARAM l)
{
    const UINT evt = LOWORD(l);
    (void)w;

    switch (evt) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: {
        HWND fg = GetForegroundWindow();
        if (fg && !WindowManager::OwnWindow(fg)) ToggleTarget(fg);
        else ShowContextMenu();
        return;
    }
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        return;
    default:
        return;
    }
}

// 点击后应保持菜单打开、便于连续调整的命令（纯设置项 + 不改变窗口焦点的操作）
static bool IsStickyCommand(UINT id)
{
    if (id >= IDM_COLOR_BASE  && id < IDM_COLOR_BASE  + 10) return true;   // 预设色
    if (id >= IDM_ACOLOR_BASE && id < IDM_ACOLOR_BASE + 10) return true;
    if (id >= IDM_SCOLOR_BASE && id < IDM_SCOLOR_BASE + 10) return true;   // 批量选中色
    if (id >= IDM_THICK_BASE  && id < IDM_THICK_BASE  + 10) return true;
    if (id >= IDM_NOTIFY_BASE && id < IDM_NOTIFY_BASE + 5)  return true;

    switch (id) {
    case IDM_COLOR_CUSTOM:
    case IDM_ACOLOR_CUSTOM:
    case IDM_SCOLOR_CUSTOM:
    case IDM_MARKER:
    case IDM_AUTOSTART:
    case IDM_NOTIFY_MUTE:
    case IDM_NOTIFY_ALL_ON:
    case IDM_NOTIFY_ALL_OFF:
    case IDM_SETHOTKEY:
    case IDM_SETNEXTHOTKEY:
    case IDM_SETPREVHOTKEY:
    case IDM_SETUNPINHOTKEY:
    case IDM_SETMENUHOTKEY:
    case IDM_UNPIN_ALL:
    case IDM_SHOW_ALL:
    case IDM_SYNCDRAG:
    case IDM_REFRESH:
        return true;
    default:
        return false;
    }
}

void App::ShowContextMenu()
{
    if (!m_target || !IsWindow(m_target))
        m_target = GetForegroundWindow();

    // 初始锚点固定：重开时菜单不跳动，视觉上就是"没有关闭"
    POINT anchor = { 0, 0 };
    GetCursorPos(&anchor);

    // 循环显示：命中「设置项」时重建菜单继续显示，直到用户点空白 / Esc / 退出
    for (int guard = 0; guard < 128; ++guard) {
        HMENU menu = BuildMenu();
        SetForegroundWindow(m_hwnd);
        UINT cmd = (UINT)TrackPopupMenuEx(menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            anchor.x, anchor.y, m_hwnd, nullptr);
        DestroyMenu(menu);
        PostMessageW(m_hwnd, WM_NULL, 0, 0);


        if (cmd == 0) break;              // 点外部 / Esc
        if (cmd == IDM_EXIT) { HandleCommand(cmd); break; }

        HandleCommand(cmd);
        if (!IsStickyCommand(cmd)) break;
    }
}

void App::RefreshOverlayNow()
{
    if (!m_cfg.showMarker) {
        m_overlay.Refresh({}, nullptr, {});
        return;
    }

    // 目标窗口只有还处于置顶状态才配拥有"选中色"描边。
    // 否则取消置顶之后那条蓝框会一直挂在窗口上，看着像"取消了还在选中"。
    HWND active = m_target;
    if (active && !m_wm.IsPinned(active))
        active = nullptr;

    m_overlay.Refresh(m_wm.List(), active, m_selected);
}

void App::ApplyMarkerConfig()
{
    m_overlay.SetStyle(m_cfg.markerColor, m_cfg.markerActive,
                       m_cfg.markerSelect, m_cfg.markerThickness);
}

void App::RefreshTrayTip()
{
    int n = (int)m_wm.List().size();
    m_tray.SetTip(tmd::Format(L"TopMostDesk — 置顶 %d 个\n置顶：%s\n切换：%s / %s",
        n,
        tmd::DescribeBinding(m_cfg.bindToggle).c_str(),
        tmd::DescribeBinding(m_cfg.bindPrev).c_str(),
        tmd::DescribeBinding(m_cfg.bindNext).c_str()));
}

void App::ReRegisterHotkeys()
{
    const UINT ids[] = { HK_TOGGLE, HK_NEXT, HK_PREV, HK_MENU,
                         HK_UNPIN_ALL, HK_HIDE, HK_SHOW_ALL, HK_SELECT_ALL,
                         HK_ALPHA_UP, HK_ALPHA_DOWN };
    for (UINT id : ids) UnregisterHotKey(m_hwnd, id);

    auto reg = [&](UINT id, const Binding& b) -> BOOL {
        if (b.device != TMD_DEV_KEYBOARD || b.key == 0) return FALSE;
        return RegisterHotKey(m_hwnd, id, b.mods | MOD_NOREPEAT, b.key);
    };

    BOOL ok = reg(HK_TOGGLE, m_cfg.bindToggle);
    m_toggleHotkeyOk = ok != FALSE;

    if (!ok && m_cfg.bindToggle.device == TMD_DEV_KEYBOARD) {
        // 首选组合被占用时退让到备用键盘组合
        struct Alt { UINT mods; UINT vk; };
        const Alt alts[] = {
            { MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'T' },
            { MOD_CONTROL | MOD_ALT,             'T' },
            { MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'P' },
        };
        const std::wstring oldCombo = tmd::DescribeBinding(m_cfg.bindToggle);

        for (const Alt& a : alts) {
            if (a.mods == m_cfg.bindToggle.mods && a.vk == m_cfg.bindToggle.key) continue;
            if (RegisterHotKey(m_hwnd, HK_TOGGLE, a.mods | MOD_NOREPEAT, a.vk)) {
                m_cfg.bindToggle = Binding{ TMD_DEV_KEYBOARD, a.mods, a.vk };
                SaveCfg();
                m_toggleHotkeyOk = true;
                Notify(NCT_SYSTEM, L"已自动更换快捷键",
                    oldCombo + L" 被占用，已改用 " + tmd::DescribeBinding(m_cfg.bindToggle) +
                    L"。可在托盘菜单中重新设置。");
                break;
            }
        }
        if (!m_toggleHotkeyOk) {
            Notify(NCT_SYSTEM, L"快捷键不可用",
                L"常用键盘组合均被占用。可在托盘菜单里改绑鼠标键（如 Win + 鼠标中键）。",
                NIIF_WARNING);
        }
    }

    reg(HK_NEXT, m_cfg.bindNext);
    reg(HK_PREV, m_cfg.bindPrev);
    reg(HK_MENU, m_cfg.bindMenu);
    reg(HK_UNPIN_ALL, m_cfg.bindUnpinAll);
    reg(HK_HIDE,     m_cfg.bindHide);
    reg(HK_SHOW_ALL, m_cfg.bindShowAll);
    reg(HK_SELECT_ALL, m_cfg.bindSelectAll);
    reg(HK_ALPHA_UP,   m_cfg.bindAlphaUp);
    reg(HK_ALPHA_DOWN, m_cfg.bindAlphaDown);
}

void App::ReRegisterToggleForCapture()
{
    UnregisterHotKey(m_hwnd, HK_TOGGLE);
    m_toggleHotkeyOk = false;
}

void App::SaveCfg()
{
    SaveConfig(m_cfg);
}

void App::OnTick()
{
    ++m_ticks;
}
