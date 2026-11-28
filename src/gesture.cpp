#include "gesture.h"
#include "winmgr.h"
#include "config.h"
#include <windowsx.h>
#include <share.h>
#include <dwmapi.h>

namespace {

// 诊断日志：只在手势的关键节点写，一次拖动最多几行，落盘后立刻 flush。
// 目的是把"看起来没反应"拆成可判定的几步：钩子有没有接管 → 有没有开始拖 →
// 提交的坐标是多少 → 目标窗口实际去哪了 → 什么时候结束。
// 用 _SH_DENYWR 打开：拒绝别的进程写，但允许读，这样分析时可以一边跑一边看。
void GLog(const wchar_t* fmt, ...)
{
    static FILE* f = nullptr;
    static bool  opened = false;
    if (!opened) {
        opened = true;
        const std::wstring p = GetConfigDir() + L"\\gesture.log";
        f = _wfsopen(p.c_str(), L"w, ccs=UTF-8", _SH_DENYWR);
    }
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fputwc(L'\n', f);
    fflush(f);
}

// 修饰键实况。手势要不要继续、要不要收手，一律以这里为准——钩子算出来的 mods
// 只在"决定是否投递"的那一刻有效，主线程处理时可能已经过去了。
UINT LiveMods()
{
    UINT m = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) m |= MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) m |= MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
        m |= MOD_WIN;
    return m;
}

// 逻辑像素 → 目标窗口所在显示器的物理像素
int ScaleFor(HWND h, int v)
{
    UINT dpi = 96;
    if (h) {
        const UINT d = GetDpiForWindow(h);
        if (d) dpi = d;
    }
    return MulDiv(v, (int)dpi, 96);
}

// 手势能作用的窗口。
// 这里的排除清单必须窄：多排一类就多一批"拖不动"的窗口，而用户只会觉得功能坏了。
// 只排除改了它是灾难的外壳窗口（桌面、任务栏、tooltip）。
// 刻意**不**排除：
//   · ApplicationFrameWindow / Windows.UI.Core.CoreWindow —— 那是 Win11 记事本、计算器
//     等 UWP 应用的真实顶层窗口，排掉等于最常见的窗口拖不动；
//   · 有 owner 的窗口 —— 对话框、带 owner 的无边框窗口一样该能拖；
//   · WS_EX_TOOLWINDOW —— 很多应用的自定义标题栏就是工具窗样式。
bool IsGestureTarget(HWND h, bool allowDwm)
{
    if (!h || !IsWindow(h)) return false;
    if (!IsWindowVisible(h) || IsIconic(h)) return false;
    if (WindowManager::OwnWindow(h)) return false;

    wchar_t cls[64] = {};
    if (GetClassNameW(h, cls, 64) > 0) {
        static const wchar_t* kNever[] = {
            L"Progman", L"WorkerW",
            L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
            L"tooltips_class32",
        };
        for (const wchar_t* n : kNever)
            if (wcscmp(cls, n) == 0) return false;
    }

    RECT rc = {};
    if (!GetWindowRect(h, &rc)) return false;
    if (rc.right - rc.left < 40 || rc.bottom - rc.top < 40) return false;

    // DWM 查询是跨进程调用。钩子回调里一律跳过：那里超过
    // LowLevelHooksTimeout（默认 300ms）会被系统静默摘钩子，得不偿失。
    if (allowDwm) {
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
            return false;
    }
    return true;
}

HWND TargetUnder(POINT pt, bool allowDwm)
{
    HWND h = WindowFromPoint(pt);
    if (!h) return nullptr;
    HWND root = GetAncestor(h, GA_ROOT);
    if (root) h = root;
    return IsGestureTarget(h, allowDwm) ? h : nullptr;
}

UINT MouseKeyOf(UINT msg, const MSLLHOOKSTRUCT& ms)
{
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: return TMD_MOUSE_LEFT;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: return TMD_MOUSE_RIGHT;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: return TMD_MOUSE_MIDDLE;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        return (HIWORD(ms.mouseData) == XBUTTON1) ? TMD_MOUSE_X1 : TMD_MOUSE_X2;
    default: return 0;
    }
}

bool IsDownMsg(UINT msg)
{
    return msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
           msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN;
}

bool IsUpMsg(UINT msg)
{
    return msg == WM_LBUTTONUP || msg == WM_RBUTTONUP ||
           msg == WM_MBUTTONUP || msg == WM_XBUTTONUP;
}

} // namespace

// ---------------------------------------------------------------- 配置

void GestureEngine::Sync(const Config& c)
{
    m_cfg = c;
    // 功能被关掉时立刻收手，不要留下一个"还在改窗口"的悬挂状态
    if (!m_cfg.hoverResize) ResetHover();
    if (!m_cfg.freeDrag)    EndDrag(L"-");
}

void GestureEngine::Shutdown()
{
    ResetHover();
    EndDrag(L"-");
    if (m_notify) KillTimer(m_notify, TMD_IDT_DRAG);   // 防止 EndDrag 早退漏杀
    m_hookDrag = false;
    m_evtMove.store(false);
    m_evtDown.store(false);
    m_evtUp.store(false);
    m_posted.store(false);
}

bool GestureEngine::MatchHover(UINT mods) const
{
    // 修饰键必须是"精确相等"，与 InputHook 的命中规则一致。
    // 而且不允许 mods == 0：那是持续状态，等于"不按任何键移动就改窗口"，绝对不能生效。
    return m_cfg.hoverResize && m_cfg.hoverMods != 0 && mods == m_cfg.hoverMods;
}

bool GestureEngine::MatchDrag(UINT mods) const
{
    if (!m_cfg.freeDrag) return false;
    // 拖动是一次点击触发，允许不带修饰键（裸中键之类）。
    // 但"裸左键"必须排除：它会把全系统的左键按下全部吞掉。
    if (m_cfg.dragButton == TMD_MOUSE_LEFT && m_cfg.dragMods == 0) return false;
    return mods == m_cfg.dragMods;
}

// ---------------------------------------------------------------- 钩子线程

bool GestureEngine::OnHookMouse(UINT msg, const MSLLHOOKSTRUCT& ms, UINT mods)
{
    if (!m_cfg.hoverResize && !m_cfg.freeDrag) return false;

    const POINT pt = { ms.pt.x, ms.pt.y };
    m_ptX.store(pt.x, std::memory_order_relaxed);
    m_ptY.store(pt.y, std::memory_order_relaxed);
    m_diagLastMsg.store((UINT)msg, std::memory_order_relaxed);

    bool eat = false;

    if (msg == WM_MOUSEMOVE) {
        m_evtMove.store(true, std::memory_order_relaxed);
        // 注意：这里刻意**不**吞移动。
        // 要挡住"目标程序以为自己在被拖"只需要吞 down/up——它没收到按下，就不会进入
        // 拖选/定位光标/拖放任何状态，之后的 move 对它只是普通 hover。
        // 而吞 move 会让整个悬停/光标跟踪链路缺帧，风险远大于收益。
    } else if (IsDownMsg(msg)) {
        // 上一次拖动没等到抬起（钩子重装、目标崩溃）——先自愈，否则会一直吞事件
        if (m_hookDrag) m_hookDrag = false;

        const UINT key = MouseKeyOf(msg, ms);
        if (key != 0 && key == m_cfg.dragButton && MatchDrag(mods)) {
            // 只有真的落在可作用窗口上才接管。否则这次点击必须原样放行——
            // 在桌面、任务栏、自绘全屏窗口上误吞一次，用户会觉得"鼠标坏了"。
            if (TargetUnder(pt, false)) {
                m_downX.store(pt.x, std::memory_order_relaxed);
                m_downY.store(pt.y, std::memory_order_relaxed);
                m_evtDown.store(true, std::memory_order_release);
                m_hookDrag = true;   // 从按下这一刻就开始吞
                m_diagDown.store(1, std::memory_order_relaxed);
                eat = true;
            } else {
                m_diagDown.store(2, std::memory_order_relaxed);
            }
        }
    } else if (IsUpMsg(msg)) {
        if (m_hookDrag) {
            m_evtUp.store(true, std::memory_order_release);
            m_hookDrag = false;
            eat = true;          // 抬起必须成对吞掉，否则目标窗口收到一个孤立 up
        }
    } else if (msg == WM_MOUSEWHEEL) {
        // 滚轮不算手势，但要唤醒主线程做一次"修饰键还按着吗"的检查
        m_evtMove.store(true, std::memory_order_relaxed);
    }

    // 只投一条信号，重复的合并掉。主线程处理时读的是"最新"坐标，
    // 中间堆压的帧被天然丢弃——这就是"鼠标一停，窗口下一帧就停"的实现方式。
    bool expect = false;
    if (m_posted.compare_exchange_strong(expect, true, std::memory_order_acq_rel)) {
        if (!m_notify || !PostMessageW(m_notify, WM_TMD_GESTURE, 0, 0))
            m_posted.store(false, std::memory_order_release);
    }

    return eat;
}

// ---------------------------------------------------------------- 主线程

void GestureEngine::OnFrame()
{
    m_posted.store(false, std::memory_order_release);

    const POINT pt      = { m_ptX.load(), m_ptY.load() };
    const UINT  mods    = LiveMods();
    const bool  hasMove = m_evtMove.exchange(false);
    const bool  hasDown = m_evtDown.exchange(false);
    const bool  hasUp   = m_evtUp.exchange(false);

    const int diag = m_diagDown.exchange(0);
    if (diag == 1)      GLog(L"[hook] 按下已接管 mods=%u", mods);
    else if (diag == 2) GLog(L"[hook] 按下被跳过：光标下没有可作用窗口（被 IsGestureTarget 过滤）");

    // ---------------- 任意区域拖动（优先级最高） ----------------
    if (hasDown && !m_dragActive) {
        const POINT dp = { m_downX.load(), m_downY.load() };
        HWND h = TargetUnder(dp, true);
        if (h) {
            BeginDrag(h, dp);
            // 同一次 OnFrame 里按下又抬起（极快的点击，或主线程被别的消息拖住）：
            // 必须立刻收尾，否则 hasUp 会被这次 exchange 吃掉，拖动状态挂死在那儿。
            if (hasUp) EndDrag(L"same-frame up");
            return;   // 这一帧不再处理悬停缩放
        }
        GLog(L"[main] 按下坐标 (%ld,%ld) 找不到目标窗口，放弃拖动", dp.x, dp.y);
    }
    if (m_dragActive) {
        if (hasUp)                                        EndDrag(L"hook up");
        else if (!m_dragTarget || !IsWindow(m_dragTarget)) EndDrag(L"target gone");
        else if (hasMove) {
            // 坐标一律现取：钩子事件里的 ms.pt 在部分鼠标/驱动组合下会停在按下点
            // 附近不往前走（实测过），而 GetCursorPos 读的是系统光标真值。
            POINT cp = {};
            if (GetCursorPos(&cp)) ApplyDrag(cp);
            else                   ApplyDrag(pt);
        }
        return;
    }

    // ---------------- 悬停缩放 ----------------
    if (!m_cfg.hoverResize) { ResetHover(); return; }

    // 修饰键松开或组合变了：立即收手，窗口停在当前尺寸
    if (!MatchHover(mods)) { ResetHover(); return; }

    if (m_rsPhase == Phase::Idle) {
        if (hasMove) {
            HWND h = TargetUnder(pt, true);
            if (h) ArmHover(h, pt);
        }
    } else if (hasMove) {
        ApplyResize(pt);
    }
}

void GestureEngine::OnIdleTick()
{
    // 兜底：修饰键松开这件事没有鼠标事件可依赖（最后可能一个 mousemove 都没来），
    // 200ms 的节拍里补一次检查，最多慢一拍，不会永远挂着。
    if (m_rsPhase != Phase::Idle) {
        if (!MatchHover(LiveMods())) {
            ResetHover();
        } else if (!m_rsTarget || !IsWindow(m_rsTarget) ||
                   !IsWindowVisible(m_rsTarget) || IsIconic(m_rsTarget)) {
            ResetHover();
        }
    }
    // 拖动只在鼠标抬起时结束（和系统拖动窗口一致：中途松开修饰键不算中断），
    // 但窗口没了必须收尾。
    if (m_dragActive && (!m_dragTarget || !IsWindow(m_dragTarget))) EndDrag(L"-");
}

// 拖动期间的位置驱动：不依赖钩子事件，直接读系统光标。
// 好处有两个——钩子事件坐标不可靠时仍然跟手；钩子被系统摘掉（回调超时）时
// 靠物理按键状态也能正常收尾，不会留一个"窗口一直粘在鼠标上"的状态。
void GestureEngine::OnDragTick()
{
    if (!m_dragActive) return;

    if (!m_dragTarget || !IsWindow(m_dragTarget)) { EndDrag(L"target gone"); return; }

    // 只在"这个 VK 的异步状态可信"时才拿它判松手。不可信就完全交给钩子的抬起事件，
    // 否则中键被驱动重映射时会第一帧就判成已松手（实测过）。
    if (m_btnPollable && m_dragVk != 0 && !(GetAsyncKeyState((int)m_dragVk) & 0x8000)) {
        EndDrag(L"button up (polled)");
        return;
    }

    // 绝对超时：万一钩子被系统摘掉、抬起事件永远收不到，也不能让窗口一直粘着。
    if (GetTickCount() - m_dragStartTick > 60000) { EndDrag(L"timeout"); return; }

    POINT cp = {};
    if (!GetCursorPos(&cp)) { EndDrag(L"GetCursorPos failed"); return; }
    ApplyDrag(cp);
}

// ---------------------------------------------------------------- 悬停缩放

void GestureEngine::ArmHover(HWND h, POINT pt)
{
    RECT rc = {};
    if (!GetWindowRect(h, &rc)) return;

    m_rsTarget = h;
    m_rsOrig   = rc;
    m_rsAnchor = pt;
    m_rsAxis   = 0;
    m_rsStreak = 0;
    // 光标在窗口左半就拖左边、右半拖右边；上下同理。这是"不用去抓边框"的落点。
    m_rsEdgeX  = (pt.x < (rc.left + rc.right) / 2) ? -1 : +1;
    m_rsEdgeY  = (pt.y < (rc.top + rc.bottom) / 2) ? -1 : +1;
    m_rsPhase  = Phase::Armed;

    m_minW = std::max((int)GetSystemMetrics(SM_CXMINTRACK), ScaleFor(h, 120));
    m_minH = std::max((int)GetSystemMetrics(SM_CYMINTRACK), ScaleFor(h, 80));

    // 记成"已提交 = 当前矩形"，避免第一帧白跑一次 SetWindowPos
    m_lastX = rc.left;
    m_lastY = rc.top;
    m_lastW = rc.right - rc.left;
    m_lastH = rc.bottom - rc.top;

    if (!m_cfg.edgeSnap) return;

    // 贴边优先（可选）：离哪条边够近就直接锁那条边，跳过"由移动方向决定轴"这一步。
    // 两条边都够近就是角，双轴同时改。
    const int snap = ScaleFor(h, 10);
    const int dL = pt.x - rc.left, dR = rc.right - pt.x;
    const int dT = pt.y - rc.top,  dB = rc.bottom - pt.y;

    const bool nearL = dL >= 0 && dL <= snap;
    const bool nearR = dR >= 0 && dR <= snap;
    const bool nearT = dT >= 0 && dT <= snap;
    const bool nearB = dB >= 0 && dB <= snap;

    if ((nearL || nearR) && (nearT || nearB)) {
        m_rsAxis  = 3;
        m_rsEdgeX = nearL ? -1 : +1;
        m_rsEdgeY = nearT ? -1 : +1;
    } else if (nearL || nearR) {
        m_rsAxis  = 1;
        m_rsEdgeX = nearL ? -1 : +1;
    } else if (nearT || nearB) {
        m_rsAxis  = 2;
        m_rsEdgeY = nearT ? -1 : +1;
    }
}

void GestureEngine::ResetHover()
{
    m_rsPhase  = Phase::Idle;
    m_rsTarget = nullptr;
    m_rsOrig   = RECT{};
    m_rsAnchor = POINT{ 0, 0 };
    m_rsAxis   = 0;
    m_rsStreak = 0;
    m_lastX = m_lastY = m_lastW = m_lastH = INT_MIN;
}

void GestureEngine::ApplyResize(POINT pt)
{
    if (!m_rsTarget || !IsWindow(m_rsTarget)) { ResetHover(); return; }

    // 全部基于"起始矩形 + 绝对位移"，不做增量累加。
    // 增量累加会在目标窗口出于自己的约束（最小尺寸、WM_GETMINMAXINFO）拒绝某一帧时
    // 把偏差永久记在账上，越调越偏，看起来就像"窗口甩出去了"。
    const int dx   = pt.x - m_rsAnchor.x;
    const int dy   = pt.y - m_rsAnchor.y;
    const int dead = ScaleFor(m_rsTarget, m_cfg.deadzone);

    if (m_rsAxis == 0) {
        // 轴未定：谁先超过死区就锁谁，锁定后不再跟着光标抖
        const int adx = std::abs(dx), ady = std::abs(dy);
        if (std::max(adx, ady) < dead) return;
        m_rsAxis  = (adx >= ady) ? 1 : 2;
        m_rsPhase = Phase::Resizing;
    } else if (m_rsAxis != 3) {
        // 已经锁了单轴，但仍允许换到另一轴——这样一次按住里可以先拉宽再拉高。
        // 换轴带迟滞：只有另一轴位移连续 3 帧超过当前轴 1.5 倍才切，否则对角线
        // 轻微抖动会让窗口的两个方向来回抢。
        const int adx = std::abs(dx), ady = std::abs(dy);
        const int cur   = (m_rsAxis == 1) ? adx : ady;
        const int other = (m_rsAxis == 1) ? ady : adx;

        if (other > cur * 3 / 2) {
            if (++m_rsStreak >= 3) {
                // 换轴必须重锚：anchor 挪到当前光标、起始矩形重取一次。
                // 整个手势里只有这一处读 GetWindowRect，绝对位移公式本来不需要每帧读。
                RECT now = {};
                if (GetWindowRect(m_rsTarget, &now)) {
                    m_rsOrig   = now;
                    m_rsAnchor = pt;
                    m_rsAxis   = (m_rsAxis == 1) ? 2 : 1;
                }
                m_rsStreak = 0;
                return;   // 这一帧只重锚，不动窗口
            }
        } else {
            m_rsStreak = 0;
        }
        m_rsPhase = Phase::Resizing;
    }

    const int dw = m_rsOrig.right - m_rsOrig.left;
    const int dh = m_rsOrig.bottom - m_rsOrig.top;

    int x = m_rsOrig.left, y = m_rsOrig.top, w = dw, h = dh;

    if (m_rsAxis == 1 || m_rsAxis == 3) {
        if (m_rsEdgeX < 0) {
            // 拖左边：右边界固定，左边跟着光标走
            int nl = m_rsOrig.left + dx;
            int nw = m_rsOrig.right - nl;
            if (nw < m_minW) { nw = m_minW; nl = m_rsOrig.right - nw; }
            x = nl; w = nw;
        } else {
            w = std::max(dw + dx, m_minW);
        }
    }
    if (m_rsAxis == 2 || m_rsAxis == 3) {
        if (m_rsEdgeY < 0) {
            int nt = m_rsOrig.top + dy;
            int nh = m_rsOrig.bottom - nt;
            if (nh < m_minH) { nh = m_minH; nt = m_rsOrig.bottom - nh; }
            y = nt; h = nh;
        } else {
            h = std::max(dh + dy, m_minH);
        }
    }

    // 与上一帧提交的完全相同就跳过。鼠标静止时反复 SetWindowPos 是"手停了窗口还在动"
    // 的另一个来源：跨进程调用本身有延迟，重复提交会让边界持续做微小修正。
    if (x == m_lastX && y == m_lastY && w == m_lastW && h == m_lastH) return;
    m_lastX = x; m_lastY = y; m_lastW = w; m_lastH = h;

    SetWindowPos(m_rsTarget, nullptr, x, y, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

// ---------------------------------------------------------------- 任意区域拖动

void GestureEngine::BeginDrag(HWND h, POINT pt)
{
    // 拖动和缩放互斥：先把缩放状态清干净（它会重置 m_last*）
    ResetHover();

    RECT rc = {};
    if (!GetWindowRect(h, &rc)) return;

    // 最大化窗口必须先还原，否则 SetWindowPos 改位置根本不生效，
    // 而且还原之后矩形整个变了，起始锚点也得按新的来。
    if (IsZoomed(h)) {
        ShowWindow(h, SW_RESTORE);
        if (!GetWindowRect(h, &rc)) return;
    }

    m_dragActive = true;
    m_dragTarget = h;
    m_dragOrig   = rc;
    m_dragAnchor = pt;

    switch (m_cfg.dragButton) {
    case TMD_MOUSE_LEFT:   m_dragVk = VK_LBUTTON;  break;
    case TMD_MOUSE_RIGHT:  m_dragVk = VK_RBUTTON;  break;
    case TMD_MOUSE_MIDDLE: m_dragVk = VK_MBUTTON;  break;
    case TMD_MOUSE_X1:     m_dragVk = VK_XBUTTON1; break;
    case TMD_MOUSE_X2:     m_dragVk = VK_XBUTTON2; break;
    default:               m_dragVk = 0;           break;
    }
    // 起手这一刻按钮必然是按下的。异步键状态若否认，说明这个键被驱动重映射过，
    // 之后就不能拿它当"松手"判据。
    m_btnPollable   = (m_dragVk != 0) && ((GetAsyncKeyState((int)m_dragVk) & 0x8000) != 0);
    m_dragStartTick = GetTickCount();

    m_lastX = rc.left;
    m_lastY = rc.top;
    m_lastW = m_lastH = INT_MIN;
    m_logLeft = 8;

    // 位置更新走定时器，不再依赖钩子事件送坐标
    if (m_notify) SetTimer(m_notify, TMD_IDT_DRAG, 8, nullptr);

    wchar_t cls[64] = {}; GetClassNameW(h, cls, 64);
    GLog(L"[main] BEGIN hwnd=%p cls=%s rect=(%ld,%ld)-(%ld,%ld) anchor=(%ld,%ld) vk=0x%02X pollable=%d",
         (void*)h, cls, rc.left, rc.top, rc.right, rc.bottom, pt.x, pt.y,
         m_dragVk, (int)m_btnPollable);
}

void GestureEngine::ApplyDrag(POINT pt)
{
    if (!m_dragTarget || !IsWindow(m_dragTarget)) { EndDrag(L"-"); return; }

    // 同样是绝对位移：起始矩形 + (当前光标 - 按下位置)
    const int x = m_dragOrig.left + (pt.x - m_dragAnchor.x);
    const int y = m_dragOrig.top  + (pt.y - m_dragAnchor.y);

    if (x == m_lastX && y == m_lastY) return;
    m_lastX = x;
    m_lastY = y;

    const BOOL ok = SetWindowPos(m_dragTarget, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

    if (m_logLeft > 0) {
        // 回读实际矩形 + 打出本次用的光标坐标和钩子收到的最后一个消息号。
        // 这一行能一次分清三种情况：
        //   pt 不动          → 钩子拿到的坐标没往前走（输入层问题）
        //   pt 正常但 want 不变 → 我们的位移公式出问题
        //   want 正常但 actual 不同 → SetWindowPos 被目标窗口/系统回弹
        RECT now = {};
        const BOOL got = GetWindowRect(m_dragTarget, &now);
        GLog(L"[main] MOVE want=(%d,%d) pt=(%ld,%ld) anchor=(%ld,%ld) lastMsg=0x%04X ok=%d actual=(%ld,%ld) got=%d",
             x, y, pt.x, pt.y, m_dragAnchor.x, m_dragAnchor.y,
             m_diagLastMsg.load(), (int)ok, now.left, now.top, (int)got);
        if (--m_logLeft == 0) GLog(L"[main] MOVE 之后不再逐帧记录");
    }
}

void GestureEngine::EndDrag(const wchar_t* why)
{
    if (!m_dragActive && !m_dragTarget) return;
    if (m_dragActive) GLog(L"[main] END (%s)", why);
    if (m_notify) KillTimer(m_notify, TMD_IDT_DRAG);
    m_dragActive = false;
    m_dragTarget = nullptr;
    m_dragOrig   = RECT{};
    m_dragAnchor = POINT{ 0, 0 };
    m_lastX = m_lastY = m_lastW = m_lastH = INT_MIN;
}
