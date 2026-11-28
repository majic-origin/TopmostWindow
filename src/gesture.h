#pragma once
#include "common.h"
#include <atomic>
#include <climits>

// 手势帧信号：钩子线程已经写好最新坐标，等主线程来处理。
// 与 WM_TMD_INPUT(WM_APP+60) 分开，因为语义不同——那条是"命中了一个动作"，
// 这条是"坐标变了，来算一次"。
#define WM_TMD_GESTURE (WM_APP + 61)

// 鼠标手势引擎。两个功能都默认关闭，开关打开才接管：
//
//  1. 悬停缩放——按住 bindHoverResize 的修饰键，光标停在目标窗口上，
//     左右移动改宽度、上下移动改高度；不用抓边框，也不用按住鼠标键。
//  2. 任意区域拖动——按住 bindFreeDrag 描述的组合，在窗口任意区域按下并拖动
//     即可移动窗口，不需要标题栏。
//
// 线程模型（这是"跟手、不滞后、手停了窗口立刻停"的全部关键）：
//   低级鼠标钩子回调里只做 O(1) 的事——判修饰键、把坐标写进原子量、投递一条信号。
//   绝不在回调里调用 SetWindowPos：
//     · 跨进程 SetWindowPos 是 SendMessage 语义，要等目标线程处理完 WM_WINDOWPOSCHANGING；
//     · 1000Hz 鼠标每秒一千次回调，任何一次阻塞都会把系统输入队列拖住（表现为黏滞）；
//     · 回调超过 LowLevelHooksTimeout（默认 300ms）会被系统静默摘掉钩子，功能整体
//       失效且不报错——这是最隐蔽的一种"用着用着就不灵了"。
//   真正的 SetWindowPos 全部在主窗口线程执行，而且每帧只读"最新"坐标：中间被堆压的
//   帧天然丢弃，所以鼠标一停，窗口下一帧就停，不会出现"手停了窗口还在动"。
class GestureEngine
{
public:
    // 与 AppConfig 同步的运行时快照
    struct Config
    {
        bool hoverResize = false;
        bool freeDrag    = false;
        bool edgeSnap    = false;                 // 贴边优先，默认关
        UINT hoverMods   = 0;                     // 悬停缩放的触发修饰键
        UINT dragMods    = 0;                     // 拖动窗口的触发修饰键
        UINT dragButton  = TMD_MOUSE_MIDDLE;      // 拖动窗口的触发鼠标键
        int  deadzone    = 8;                     // 缩放手势死区（逻辑像素）
    };

    void SetNotify(HWND h) { m_notify = h; }
    void Sync(const Config& c);
    void Shutdown();

    // 钩子线程调用。返回 true = 吞掉这次鼠标事件（目标窗口与本程序的 Matcher 都收不到）
    bool OnHookMouse(UINT msg, const MSLLHOOKSTRUCT& ms, UINT mods);

    // 主线程调用：处理一条合并后的手势帧
    void OnFrame();
    // 主线程调用：200ms 兜底节拍（修饰键松开、目标窗口没了）
    void OnIdleTick();

    bool Dragging() const { return m_dragActive; }
    bool Resizing() const { return m_rsPhase != Phase::Idle; }

private:
    enum class Phase { Idle, Armed, Resizing };

    bool MatchHover(UINT mods) const;
    bool MatchDrag(UINT mods) const;

    void ArmHover(HWND h, POINT pt);
    void ResetHover();
    void ApplyResize(POINT pt);

    void BeginDrag(HWND h, POINT pt);
    void EndDrag();
    void ApplyDrag(POINT pt);

    HWND   m_notify = nullptr;
    Config m_cfg;

    // ---- 钩子线程 → 主线程（全部原子）----
    std::atomic<LONG> m_ptX     { 0 };      // 最新光标位置，可被覆盖
    std::atomic<LONG> m_ptY     { 0 };
    std::atomic<LONG> m_downX   { 0 };      // 按下那一刻的坐标，不被后续 move 覆盖
    std::atomic<LONG> m_downY   { 0 };
    std::atomic<bool> m_evtMove { false };  // 可覆盖
    std::atomic<bool> m_evtDown { false };  // 不可丢
    std::atomic<bool> m_evtUp   { false };  // 不可丢
    std::atomic<bool> m_posted  { false };  // 已有一条待处理信号，合并重复投递
    bool              m_hookDrag = false;   // 钩子侧独占：这次按下已被接管，抬起必须成对吞掉

    // ---- 主线程独占状态：悬停缩放 ----
    Phase m_rsPhase  = Phase::Idle;
    HWND  m_rsTarget = nullptr;
    RECT  m_rsOrig   = {};
    POINT m_rsAnchor = { 0, 0 };
    int   m_rsAxis   = 0;    // 0 未定 / 1 水平 / 2 垂直 / 3 双轴（贴边模式的"角"）
    int   m_rsEdgeX  = 1;    // -1 拖左边 / +1 拖右边
    int   m_rsEdgeY  = 1;
    int   m_rsStreak = 0;    // 换轴迟滞计数

    // ---- 主线程独占状态：任意区域拖动 ----
    bool  m_dragActive = false;
    HWND  m_dragTarget = nullptr;
    RECT  m_dragOrig   = {};
    POINT m_dragAnchor = { 0, 0 };

    // 上一帧真正提交过的位置：完全相同就跳过 SetWindowPos
    int m_lastX = INT_MIN, m_lastY = INT_MIN;
    int m_lastW = INT_MIN, m_lastH = INT_MIN;
    int m_minW  = 160, m_minH = 100;
};
