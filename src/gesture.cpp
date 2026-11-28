#include "gesture.h"
#include "winmgr.h"
#include <windowsx.h>
#include <dwmapi.h>

namespace {

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

// 手势能作用的窗口。刻意与 App::EnumSelectable 保持同一套排除规则，
// 免得出现"全选选得到、手势改不了"的不一致。
bool IsGestureTarget(HWND h)
{
    if (!h || !IsWindow(h)) return false;
    if (!IsWindowVisible(h) || IsIconic(h)) return false;
    if (WindowManager::OwnWindow(h)) return false;
    if (GetWindow(h, GW_OWNER) != nullptr) return false;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;

    wchar_t cls[64] = {};
    if (GetClassNameW(h, cls, 64) > 0) {
        // 桌面与外壳窗口永远不参与：改它们的大小是灾难
        static const wchar_t* kNever[] = {
            L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
            L"ApplicationFrameWindow", L"Windows.UI.Core.CoreWindow",
        };
        for (const wchar_t* n : kNever)
            if (wcscmp(cls, n) == 0) return false;
    }

    RECT rc = {};
    if (!GetWindowRect(h, &rc)) return false;
    if (rc.right - rc.left < 40 || rc.bottom - rc.top < 40) return false;

    // UWP 关掉后仍挂着但被 DWM 藏起来的"幽灵窗口"
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;

    return true;
}

HWND TargetUnder(POINT pt)
{
    HWND h = WindowFromPoint(pt);
    if (!h) return nullptr;
    HWND root = GetAncestor(h, GA_ROOT);
    if (root) h = root;
    return IsGestureTarget(h) ? h : nullptr;
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
    if (!m_cfg.freeDrag)    EndDrag();
}

void GestureEngine::Shutdown()
{
    ResetHover();
    EndDrag();
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

    bool eat = false;

    if (msg == WM_MOUSEMOVE) {
        m_evtMove.store(true, std::memory_order_relaxed);
        // 拖动期间吞掉移动：目标窗口不该知道"有人正按住它拖"
        if (m_hookDrag) eat = true;
    } else if (IsDownMsg(msg)) {
        // 上一次拖动没等到抬起（钩子重装、目标崩溃）——先自愈，否则会一直吞事件
        if (m_hookDrag) m_hookDrag = false;

        const UINT key = MouseKeyOf(msg, ms);
        if (key != 0 && key == m_cfg.dragButton && MatchDrag(mods)) {
            // 只有真的落在可作用窗口上才接管。否则这次点击必须原样放行——
            // 在桌面、任务栏、自绘全屏窗口上误吞一次，用户会觉得"鼠标坏了"。
            if (TargetUnder(pt)) {
                m_downX.store(pt.x, std::memory_order_relaxed);
                m_downY.store(pt.y, std::memory_order_relaxed);
                m_evtDown.store(true, std::memory_order_release);
                m_hookDrag = true;   // 从按下这一刻就开始吞
                eat = true;
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

    // ---------------- 任意区域拖动（优先级最高） ----------------
    if (hasDown && !m_dragActive) {
        const POINT dp = { m_downX.load(), m_downY.load() };
        HWND h = TargetUnder(dp);
        if (h) {
            BeginDrag(h, dp);
            return;   // 这一帧不再处理悬停缩放
        }
    }
    if (m_dragActive) {
        if (hasUp || !m_dragTarget || !IsWindow(m_dragTarget)) EndDrag();
        else if (hasMove)                                     ApplyDrag(pt);
        return;
    }

    // ---------------- 悬停缩放 ----------------
    if (!m_cfg.hoverResize) { ResetHover(); return; }

    // 修饰键松开或组合变了：立即收手，窗口停在当前尺寸
    if (!MatchHover(mods)) { ResetHover(); return; }

    if (m_rsPhase == Phase::Idle) {
        if (hasMove) {
            HWND h = TargetUnder(pt);
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
    if (m_dragActive && (!m_dragTarget || !IsWindow(m_dragTarget))) EndDrag();
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

    m_lastX = rc.left;
    m_lastY = rc.top;
    m_lastW = m_lastH = INT_MIN;
}

void GestureEngine::ApplyDrag(POINT pt)
{
    if (!m_dragTarget || !IsWindow(m_dragTarget)) { EndDrag(); return; }

    // 同样是绝对位移：起始矩形 + (当前光标 - 按下位置)
    const int x = m_dragOrig.left + (pt.x - m_dragAnchor.x);
    const int y = m_dragOrig.top  + (pt.y - m_dragAnchor.y);

    if (x == m_lastX && y == m_lastY) return;
    m_lastX = x;
    m_lastY = y;

    SetWindowPos(m_dragTarget, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void GestureEngine::EndDrag()
{
    if (!m_dragActive && !m_dragTarget) return;
    m_dragActive = false;
    m_dragTarget = nullptr;
    m_dragOrig   = RECT{};
    m_dragAnchor = POINT{ 0, 0 };
    m_lastX = m_lastY = m_lastW = m_lastH = INT_MIN;
}
