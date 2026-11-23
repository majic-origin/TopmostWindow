#include "app.h"
#include "dialogs.h"
#include "ui_theme.h"
#include "resource.h"
#include <vector>

static const wchar_t* kAppClass = L"TopMostDeskMainWnd";

App::App(HINSTANCE inst, int testMode) : m_inst(inst), m_testMode(testMode)
{
    m_testDialog = testMode != 0;
}

App::~App()
{
    m_input.Uninstall();
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

    // 鼠标键（右键/侧键/滚轮）靠全局低级鼠标钩子；键盘走 RegisterHotKey
    m_input.SetMatcher([this](UINT mods, UINT mouseKey) -> UINT {
        auto hit = [&](const Binding& b, UINT id) -> UINT {
            if (b.device != TMD_DEV_MOUSE || b.key == 0) return 0;
            return (b.key == mouseKey && b.mods == mods) ? id : 0;
        };
        if (UINT r = hit(m_cfg.bindToggle, HK_TOGGLE)) return r;
        if (UINT r = hit(m_cfg.bindNext,   HK_NEXT))   return r;
        if (UINT r = hit(m_cfg.bindPrev,   HK_PREV))   return r;
        if (UINT r = hit(m_cfg.bindMenu,   HK_MENU))   return r;
        if (UINT r = hit(m_cfg.bindUnpinAll, HK_UNPIN_ALL)) return r;
        return 0;
    });
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
        if (w == IDT_MARKER) RefreshOverlayNow();
        else if (w == IDT_PURGE) m_wm.Purge();
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
    case HK_MENU:
        m_target = GetForegroundWindow();
        ShowContextMenu();
        break;
    default: break;
    }
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
    if (id >= IDM_THICK_BASE  && id < IDM_THICK_BASE  + 10) return true;
    if (id >= IDM_NOTIFY_BASE && id < IDM_NOTIFY_BASE + 5)  return true;

    switch (id) {
    case IDM_COLOR_CUSTOM:
    case IDM_ACOLOR_CUSTOM:
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
    if (!m_cfg.showMarker) return;
    m_overlay.Refresh(m_wm.List(), m_target);
}

void App::ApplyMarkerConfig()
{
    m_overlay.SetStyle(m_cfg.markerColor, m_cfg.markerActive, m_cfg.markerThickness);
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
    const UINT ids[] = { HK_TOGGLE, HK_NEXT, HK_PREV, HK_MENU, HK_UNPIN_ALL };
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
