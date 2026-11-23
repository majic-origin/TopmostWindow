#include "settings.h"
#include "ui_theme.h"
#include <commdlg.h>
#include <windowsx.h>
#include <algorithm>

namespace {

using Gdiplus::REAL;

const wchar_t* kCls = L"TopMostDeskSettings";
const int kWinW = 900;
const int kWinH = 640;
const int kTitleH = 56;
const int kSideW  = 228;
const int kCX0    = 252;    // 右侧内容左边界
const int kCX1    = 866;    // 右侧内容右边界
const int kRowH   = 36;   // 12 行（含透明度 ±）压到 36，避免撞到底部按钮
const int kRowTop = 130;
const int kFootTop = kWinH - 68;

// ---------------- 数据 ----------------

struct RowDef {
    Binding AppConfig::* field;
    const wchar_t* label;
    const wchar_t* desc;
};

const RowDef kRows[] = {
    { &AppConfig::bindToggle,   L"置顶 / 取消置顶",     L"焦点放到目标窗口后按下，切换它的置顶状态" },
    { &AppConfig::bindHide,     L"隐藏当前窗口",        L"隐藏当前窗口；有批量选中时隐藏全部选中窗口" },
    { &AppConfig::bindShowAll,  L"显示全部隐藏窗口",    L"把所有被本软件隐藏过的窗口还原" },
    { &AppConfig::bindNext,     L"下一个置顶窗口",      L"在置顶窗口之间循环切换（可用滚轮）" },
    { &AppConfig::bindPrev,     L"上一个置顶窗口",      L"在置顶窗口之间循环切换（可用滚轮）" },
    { &AppConfig::bindUnpinAll, L"取消全部置顶",        L"一次性取消所有窗口的置顶" },
    { &AppConfig::bindSelect,   L"批量选择窗口",        L"绑定后按住它点窗口，即把该窗口加入 / 移出选择集" },
    { &AppConfig::bindBatchPin, L"批量置顶 / 取消置顶", L"有选中窗口时作用于全部选中；没有选中时作用于当前窗口" },
    { &AppConfig::bindSelectAll, L"全选 / 取消全选窗口", L"一次选中桌面上所有未最小化窗口；已全选时再按一次则清空" },
    { &AppConfig::bindMenu,     L"打开管理菜单",        L"在鼠标位置弹出完整的管理菜单" },
    { &AppConfig::bindAlphaUp,   L"提高选中窗口透明度",  L"当前选中窗口 +10%（最高 100%）；默认 Ctrl+↑" },
    { &AppConfig::bindAlphaDown, L"降低选中窗口透明度",  L"当前选中窗口 -10%（最低 0%）；默认 Ctrl+↓" },
};
const int kRowCount = (int)(sizeof(kRows) / sizeof(kRows[0]));

struct NavDef { const wchar_t* label; };
const NavDef kNav[] = {
    { L"快捷键" }, { L"边框外观" }, { L"弹窗提醒" }, { L"行为" }, { L"关于" },
};
const int kNavCount = (int)(sizeof(kNav) / sizeof(kNav[0]));

struct SwatchDef { const wchar_t* name; COLORREF color; };
const SwatchDef kPresets[] = {
    { L"橙色", RGB(255, 110, 20) }, { L"蓝色", RGB(27, 111, 224) },
    { L"绿色", RGB(22, 163, 74) },  { L"红色", RGB(220, 38, 38) },
    { L"紫色", RGB(147, 51, 234) }, { L"青色", RGB(6, 182, 212) },
    { L"灰色", RGB(120, 128, 140) },
};
const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

const wchar_t* kLookLabels[3] = {
    L"置顶窗口边框", L"当前选中窗口边框", L"批量选中窗口边框"
};

struct NotifyRow { bool NotifyCfg::* field; const wchar_t* label; const wchar_t* desc; };
const NotifyRow kNotifyRows[] = {
    { &NotifyCfg::muted,   L"全部静音",        L"一票否决：忽略下面所有分类的提醒" },
    { &NotifyCfg::pin,     L"置顶 / 取消置顶", L"每次切换置顶、隐藏窗口后的气泡" },
    { &NotifyCfg::switchW, L"切换置顶窗口",    L"用快捷键或滚轮在置顶窗口间跳转时" },
    { &NotifyCfg::alpha,   L"透明度调整",      L"从菜单调整窗口透明度后" },
    { &NotifyCfg::layout,  L"分组布局",        L"保存 / 恢复布局的结果" },
    { &NotifyCfg::system,  L"系统与错误",      L"快捷键被占用、注册表写入失败等" },
};
const int kNotifyCount = (int)(sizeof(kNotifyRows) / sizeof(kNotifyRows[0]));

// ---------------- 状态 ----------------

struct St {
    HWND        hwnd = nullptr;
    HINSTANCE   inst = nullptr;
    AppConfig   work;
    int         page = 0;
    int         recRow = -1;      // 正在录制的行
    UINT        recMods = 0;      // 录制期间钩子自维护的修饰键
    std::wstring error;
    RECT        rcClose = {};
    bool        closeHover = false;
    bool        done = false;
    bool        ok = false;

    int  hotNav = -1;
    int  hotRow = -1;
    int  hotClear = -1;
    int  hotSwatch = -1;   // kind * 10 + i
    int  hotCustom = -1;   // kind
    int  hotThick = -1;    // 1..10
    int  hotSwitch = -1;

    ui::Button saveBtn, cancelBtn;
};

St s;
HHOOK g_kb = nullptr;
HHOOK g_ms = nullptr;
float g_scale = 1.0f;    // 物理像素 / 逻辑像素（按系统 DPI 缩放，125% → 1.25）

int PX(int v) { return (int)(v * g_scale + 0.5f); }

// ---------------- 布局 ----------------

RECT NavRect(int i) { RECT r = { 12, 72 + i * 46, kSideW - 12, 72 + i * 46 + 38 }; return r; }
RECT RowFieldRect(int i) { RECT r = { 470, kRowTop + i * kRowH + 5, kCX1, kRowTop + i * kRowH + 35 }; return r; }
RECT RowClearRect(int i) { RECT f = RowFieldRect(i); RECT r = { f.right - 34, f.top + 3, f.right - 6, f.bottom - 3 }; return r; }

RECT SwatchRect(int kind, int i)
{
    // 间距 32（26 色块 + 6 空隙）：7 个预设收在 470~688，
    // 「自定义…」从 700 起，右边的 #RRGGBB 才不会顶出 kCX1
    RECT r = { 470 + i * 32, kRowTop + kind * 58 + 1, 470 + i * 32 + 26, kRowTop + kind * 58 + 27 };
    return r;
}
RECT CustomRect(int kind)
{
    RECT r = { 700, kRowTop + kind * 58 + 1, 792, kRowTop + kind * 58 + 27 };
    return r;
}
RECT ThickRect(int px)
{
    RECT r = { 470 + (px - 1) * 38, kRowTop + 3 * 58 + 22, 470 + (px - 1) * 38 + 32, kRowTop + 3 * 58 + 52 };
    return r;
}
RECT SwitchRect(int i)
{
    RECT r = { 772, kRowTop + i * 52 + 8, 816, kRowTop + i * 52 + 32 };
    return r;
}

RECT BehaveSwitchRect(int i)
{
    RECT r = { 772, kRowTop + i * 52 + 8, 816, kRowTop + i * 52 + 32 };
    return r;
}

COLORREF& ColOf(AppConfig& c, int kind)
{
    if (kind == 1) return c.markerActive;
    if (kind == 2) return c.markerSelect;
    return c.markerColor;
}

bool InRect(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }

void Repaint(HWND h) { if (h) InvalidateRect(h, nullptr, FALSE); }

std::wstring ModsText(UINT mods)
{
    std::wstring t;
    auto add = [&](const wchar_t* n) { if (!t.empty()) t += L" + "; t += n; };
    if (mods & MOD_CONTROL) add(L"Ctrl");
    if (mods & MOD_ALT)     add(L"Alt");
    if (mods & MOD_SHIFT)   add(L"Shift");
    if (mods & MOD_WIN)     add(L"Win");
    return t;
}

// ---------------- 小部件绘制 ----------------

void DrawNavIcon(Gdiplus::Graphics& gfx, int idx, const Gdiplus::RectF& r, COLORREF col)
{
    Gdiplus::SolidBrush br(ui::C(col));
    switch (idx) {
    case 0:    // 键盘
        ui::StrokeRound(gfx, Gdiplus::RectF(r.X, r.Y + 4, r.Width, r.Height - 8), 4.0f, col, 1.5f);
        for (int i = 0; i < 4; ++i)
            gfx.FillRectangle(&br, r.X + 3.5f + i * 3.6f, r.Y + 8.5f, 1.8f, 1.8f);
        gfx.FillRectangle(&br, r.X + 3.5f, r.Y + 12.0f, r.Width - 7.0f, 1.8f);
        break;
    case 1:    // 边框
        ui::StrokeRound(gfx, Gdiplus::RectF(r.X, r.Y + 1, r.Width - 3, r.Height - 3), 3.0f, col, 1.9f);
        ui::StrokeRound(gfx, Gdiplus::RectF(r.X + 3, r.Y + 4, r.Width - 3, r.Height - 3), 3.0f, col, 1.0f);
        break;
    case 2:    // 提醒
        ui::StrokeRound(gfx, Gdiplus::RectF(r.X, r.Y + 3, r.Width - 2, r.Height - 5), 3.0f, col, 1.5f);
        gfx.FillEllipse(&br, r.X + r.Width - 6.0f, r.Y, 6.5f, 6.5f);
        break;
    case 3: {  // 行为：两个方块 + 斜向箭头
        ui::StrokeRound(gfx, Gdiplus::RectF(r.X, r.Y + 2, 7.0f, 7.0f), 2.0f, col, 1.5f);
        ui::StrokeRound(gfx, Gdiplus::RectF(r.X + 10.0f, r.Y + 10.0f, 7.0f, 7.0f), 2.0f, col, 1.5f);
        Gdiplus::Pen p(ui::C(col), 1.5f);
        p.SetStartCap(Gdiplus::LineCapRound);
        p.SetEndCap(Gdiplus::LineCapRound);
        gfx.DrawLine(&p, r.X + 6.0f, r.Y + 12.0f, r.X + 12.0f, r.Y + 7.0f);
        break;
    }
    default: { // 关于
        Gdiplus::Pen p(ui::C(col), 1.5f);
        gfx.DrawEllipse(&p, r.X, r.Y, r.Width, r.Height);
        gfx.FillEllipse(&br, r.X + r.Width / 2 - 1.2f, r.Y + 4.0f, 2.4f, 2.4f);
        gfx.FillRectangle(&br, r.X + r.Width / 2 - 1.2f, r.Y + 8.5f, 2.4f, 4.6f);
        break;
    }
    }
}

void DrawSwitch(Gdiplus::Graphics& gfx, const RECT& rc, bool on)
{
    Gdiplus::RectF r((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                     (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    ui::FillRound(gfx, r, r.Height / 2.0f, on ? ui::kAccent : RGB(206, 212, 220));
    const float d = r.Height - 6.0f;
    const float x = on ? (r.X + r.Width - d - 3.0f) : (r.X + 3.0f);
    Gdiplus::SolidBrush br(ui::C(RGB(255, 255, 255)));
    gfx.FillEllipse(&br, x, r.Y + 3.0f, d, d);
}

void DrawCloseBtn(Gdiplus::Graphics& gfx, const RECT& rc, bool hover)
{
    Gdiplus::RectF r((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                     (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    if (hover) ui::FillRound(gfx, r, 6.0f, RGB(240, 242, 246));
    Gdiplus::Pen pen(ui::C(hover ? ui::kTitle : ui::kSubText), 1.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float pad = 7.0f;
    gfx.DrawLine(&pen, r.X + pad, r.Y + pad, r.X + r.Width - pad, r.Y + r.Height - pad);
    gfx.DrawLine(&pen, r.X + r.Width - pad, r.Y + pad, r.X + pad, r.Y + r.Height - pad);
}

// ---------------- 命中测试 ----------------

int HitNav(POINT p)
{
    for (int i = 0; i < kNavCount; ++i) { RECT r = NavRect(i); if (InRect(r, p)) return i; }
    return -1;
}
int HitRow(POINT p)
{
    for (int i = 0; i < kRowCount; ++i) { RECT r = RowFieldRect(i); if (InRect(r, p)) return i; }
    return -1;
}
int HitClear(POINT p)
{
    for (int i = 0; i < kRowCount; ++i) { RECT r = RowClearRect(i); if (InRect(r, p)) return i; }
    return -1;
}
int HitSwatch(POINT p)
{
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < kPresetCount; ++i) {
            RECT r = SwatchRect(k, i);
            if (InRect(r, p)) return k * 10 + i;
        }
    return -1;
}
int HitCustom(POINT p)
{
    for (int k = 0; k < 3; ++k) { RECT r = CustomRect(k); if (InRect(r, p)) return k; }
    return -1;
}
int HitThick(POINT p)
{
    for (int i = 1; i <= 10; ++i) { RECT r = ThickRect(i); if (InRect(r, p)) return i; }
    return -1;
}
int HitSwitch(POINT p)
{
    for (int i = 0; i < kNotifyCount; ++i) { RECT r = SwitchRect(i); if (InRect(r, p)) return i; }
    return -1;
}

int HitBehaveSwitch(POINT p)
{
    for (int i = 0; i < 2; ++i) { RECT r = BehaveSwitchRect(i); if (InRect(r, p)) return i; }
    return -1;
}

// ---------------- 绘制 ----------------

void PaintPageHotkeys(Gdiplus::Graphics& gfx)
{
    ui::Text(gfx, L"快捷键", Gdiplus::RectF((REAL)kCX0, 70.0f, 300.0f, 28.0f), 17.0f, ui::kTitle, true);
    ui::Text(gfx, L"点一下右侧输入框就能直接录制；按 ✕ 清除该项。键盘组合与鼠标键都支持。",
             Gdiplus::RectF((REAL)kCX0, 100.0f, (REAL)(kCX1 - kCX0), 20.0f), 11.5f, ui::kSubText);

    for (int i = 0; i < kRowCount; ++i) {
        const int y = kRowTop + i * kRowH;
        const bool rec = (s.recRow == i);
        RECT fr = RowFieldRect(i);
        Gdiplus::RectF f((REAL)fr.left, (REAL)fr.top, (REAL)(fr.right - fr.left),
                         (REAL)(fr.bottom - fr.top));

        ui::Text(gfx, kRows[i].label, Gdiplus::RectF((REAL)kCX0, (REAL)y + 6, 210.0f, (REAL)(kRowH - 6)),
                 13.5f, rec ? ui::kAccent : ui::kTitle, rec);

        ui::FillRound(gfx, f, 8.0f, rec ? RGB(255, 255, 255) : ui::kField);
        ui::StrokeRound(gfx, f, 8.0f,
                        rec ? ui::kAccent : (s.hotRow == i ? RGB(190, 200, 212) : ui::kFieldLine),
                        rec ? 1.7f : 1.0f);

        const Binding& b = s.work.*(kRows[i].field);
        const bool has = b.key != 0;
        const bool bareMouse = has && b.device == TMD_DEV_MOUSE && b.mods == 0;
        std::wstring txt;
        COLORREF col = has ? (bareMouse ? RGB(198, 116, 18) : ui::kTitle) : ui::kSubText;
        if (rec) {
            txt = s.recMods ? (ModsText(s.recMods) + L" + …") : L"请按下组合键…";
            col = ui::kAccent;
        } else if (has) {
            txt = tmd::DescribeBinding(b);
        } else {
            txt = L"未设置";
        }
        ui::Text(gfx, txt, Gdiplus::RectF(f.X, f.Y, f.Width - 30.0f, f.Height), 13.0f, col,
                 has && !rec, Gdiplus::StringAlignmentCenter);

        // 清除按钮
        RECT cr = RowClearRect(i);
        if (has || rec) {
            Gdiplus::RectF c((REAL)cr.left, (REAL)cr.top, (REAL)(cr.right - cr.left),
                             (REAL)(cr.bottom - cr.top));
            if (s.hotClear == i) ui::FillRound(gfx, c, 6.0f, RGB(238, 241, 246));
            Gdiplus::Pen pen(ui::C(s.hotClear == i ? ui::kTitle : RGB(150, 158, 170)), 1.5f);
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            const float pad = 9.0f;
            gfx.DrawLine(&pen, c.X + pad, c.Y + 9.0f, c.X + c.Width - pad, c.Y + c.Height - 9.0f);
            gfx.DrawLine(&pen, c.X + c.Width - pad, c.Y + 9.0f, c.X + pad, c.Y + c.Height - 9.0f);
        }
    }
}

void PaintPageLook(Gdiplus::Graphics& gfx)
{
    ui::Text(gfx, L"边框外观", Gdiplus::RectF((REAL)kCX0, 70.0f, 300.0f, 28.0f), 17.0f, ui::kTitle, true);
    ui::Text(gfx, L"置顶标记用逐像素覆盖层绘制，不注入目标窗口，改完立即生效。",
             Gdiplus::RectF((REAL)kCX0, 100.0f, (REAL)(kCX1 - kCX0), 20.0f), 11.5f, ui::kSubText);

    for (int k = 0; k < 3; ++k) {
        const int y = kRowTop + k * 58;
        ui::Text(gfx, kLookLabels[k], Gdiplus::RectF((REAL)kCX0, (REAL)y + 3, 210.0f, 22.0f),
                 13.0f, ui::kTitle);

        for (int i = 0; i < kPresetCount; ++i) {
            RECT r = SwatchRect(k, i);
            Gdiplus::RectF rr((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left),
                              (REAL)(r.bottom - r.top));
            ui::FillRound(gfx, rr, 7.0f, kPresets[i].color);
            const bool cur = (ColOf(s.work, k) == kPresets[i].color);
            const bool hot = (s.hotSwatch == k * 10 + i);
            ui::StrokeRound(gfx, rr, 7.0f,
                            cur ? ui::kAccent : (hot ? RGB(180, 190, 202) : RGB(255, 255, 255)),
                            cur ? 2.4f : 1.0f);
        }

        RECT cbr = CustomRect(k);
        Gdiplus::RectF cb((REAL)cbr.left, (REAL)cbr.top, (REAL)(cbr.right - cbr.left),
                          (REAL)(cbr.bottom - cbr.top));
        ui::FillRound(gfx, cb, 7.0f, s.hotCustom == k ? ui::kBtnGhost : RGB(255, 255, 255));
        ui::StrokeRound(gfx, cb, 7.0f, s.hotCustom == k ? RGB(180, 190, 202) : ui::kFieldLine, 1.0f);
        ui::Text(gfx, L"自定义…", cb, 11.5f, ui::kSubText, false, Gdiplus::StringAlignmentCenter);

        // 当前色值
        COLORREF cc = ColOf(s.work, k);
        ui::Text(gfx, tmd::Format(L"#%02X%02X%02X", GetRValue(cc), GetGValue(cc), GetBValue(cc)),
                 Gdiplus::RectF((REAL)cbr.right + 10, (REAL)cbr.top, 62.0f, (REAL)(cbr.bottom - cbr.top)),
                 10.5f, ui::kSubText);
    }

    const int ty = kRowTop + 3 * 58;
    ui::Text(gfx, L"边框粗细", Gdiplus::RectF((REAL)kCX0, (REAL)ty + 22, 210.0f, 30.0f),
             13.0f, ui::kTitle);
    for (int i = 1; i <= 10; ++i) {
        RECT r = ThickRect(i);
        Gdiplus::RectF rr((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left),
                          (REAL)(r.bottom - r.top));
        const bool cur = (s.work.markerThickness == i);
        ui::FillRound(gfx, rr, 7.0f, cur ? ui::kAccent : (s.hotThick == i ? ui::kBtnGhost : ui::kField));
        if (!cur) ui::StrokeRound(gfx, rr, 7.0f, ui::kFieldLine, 1.0f);
        ui::Text(gfx, tmd::Format(L"%d", i), rr, 12.5f,
                 cur ? RGB(255, 255, 255) : ui::kTitle, cur, Gdiplus::StringAlignmentCenter);
    }
    ui::Text(gfx, L"当前选中 / 批量选中的窗口线宽会自动 +2px，并多画一条内描边。",
             Gdiplus::RectF((REAL)kCX0, (REAL)ty + 60, (REAL)(kCX1 - kCX0), 20.0f), 11.5f, ui::kSubText);
}

void PaintPageNotify(Gdiplus::Graphics& gfx)
{
    ui::Text(gfx, L"弹窗提醒", Gdiplus::RectF((REAL)kCX0, 70.0f, 300.0f, 28.0f), 17.0f, ui::kTitle, true);
    ui::Text(gfx, L"气泡只影响提示，不影响功能——关掉之后置顶、隐藏、切换照样生效。",
             Gdiplus::RectF((REAL)kCX0, 100.0f, (REAL)(kCX1 - kCX0), 20.0f), 11.5f, ui::kSubText);

    for (int i = 0; i < kNotifyCount; ++i) {
        const int y = kRowTop + i * 52;
        const bool on = s.work.notify.*(kNotifyRows[i].field);
        const bool dim = (i > 0 && s.work.notify.muted);

        ui::Text(gfx, kNotifyRows[i].label,
                 Gdiplus::RectF((REAL)kCX0, (REAL)y + 2, 300.0f, 22.0f), 13.0f,
                 dim ? ui::kSubText : ui::kTitle);
        ui::Text(gfx, kNotifyRows[i].desc,
                 Gdiplus::RectF((REAL)kCX0, (REAL)y + 23, 460.0f, 20.0f), 11.0f, ui::kSubText);

        RECT sr = SwitchRect(i);
        DrawSwitch(gfx, sr, on);
    }
}

void PaintPageBehavior(Gdiplus::Graphics& gfx)
{
    ui::Text(gfx, L"行为", Gdiplus::RectF((REAL)kCX0, 70.0f, 300.0f, 28.0f), 17.0f, ui::kTitle, true);
    ui::Text(gfx, L"这两项会影响日常操作手感，开关状态立即写入配置，下次启动保持。",
             Gdiplus::RectF((REAL)kCX0, 100.0f, (REAL)(kCX1 - kCX0), 20.0f), 11.5f, ui::kSubText);

    struct Line { const wchar_t* label; const wchar_t* desc; bool on; };
    const Line rows[2] = {
        { L"同步拖动（多选一起移动）",
          L"选中 2 个以上窗口后，按住任意一个选中窗口拖动，其余按相同位移一起走", s.work.syncDrag },
        { L"显示置顶标记",
          L"被置顶的窗口描边；当前目标与批量选中各用一种颜色，线宽 +2px", s.work.showMarker },
    };

    for (int i = 0; i < 2; ++i) {
        const int y = kRowTop + i * 52;
        ui::Text(gfx, rows[i].label, Gdiplus::RectF((REAL)kCX0, (REAL)y + 2, 420.0f, 22.0f),
                 13.0f, ui::kTitle);
        ui::Text(gfx, rows[i].desc, Gdiplus::RectF((REAL)kCX0, (REAL)y + 23, 470.0f, 20.0f),
                 11.0f, ui::kSubText);
        DrawSwitch(gfx, BehaveSwitchRect(i), rows[i].on);
    }

    ui::Text(gfx, L"同步拖动的边界：锚点窗口被最小化 / 关闭 / 取消选中时立即停止跟随。",
             Gdiplus::RectF((REAL)kCX0, (REAL)kRowTop + 118, (REAL)(kCX1 - kCX0), 20.0f),
             11.0f, ui::kSubText);
    ui::Text(gfx, L"其余窗口若最小化、隐藏或已关闭，会跳过不移动，但仍留在选择集里。",
             Gdiplus::RectF((REAL)kCX0, (REAL)kRowTop + 140, (REAL)(kCX1 - kCX0), 20.0f),
             11.0f, ui::kSubText);
}

void PaintPageAbout(Gdiplus::Graphics& gfx)
{
    ui::Text(gfx, L"关于", Gdiplus::RectF((REAL)kCX0, 70.0f, 300.0f, 28.0f), 17.0f, ui::kTitle, true);

    const wchar_t* lines[] = {
        L"TopMostDesk 1.6.2 — Windows 窗口置顶助手",
        L"",
        L"· 置顶 / 取消置顶：焦点放到目标窗口后按快捷键",
        L"· 选中窗口透明度：Ctrl+↑ / Ctrl+↓ 每次 ±10%（0~100%），只对当前选中窗口生效",
        L"· 批量选择：按住设定键点窗口，可连续选多个；再按隐藏 / 批量置顶一次性处理",
        L"· 交换切换：快捷键在置顶窗口之间循环",
        L"· 边框标记：普通置顶 / 当前目标 / 批量选中三套配色与粗细",
        L"· 布局分组：保存一整套置顶布局，随时一键恢复",
        L"· 快捷键全部可自定义，键盘与鼠标键（左/右/中/侧键/滚轮）都支持",
        L"",
        L"配置与布局：%APPDATA%\\TopMostDesk\\config.ini",
        L"作者：YG",
    };
    int y = 112;
    for (const wchar_t* ln : lines) {
        ui::Text(gfx, ln, Gdiplus::RectF((REAL)kCX0, (REAL)y, (REAL)(kCX1 - kCX0), 24.0f),
                 12.5f, (y == 112) ? ui::kTitle : ui::kSubText, y == 112);
        y += 26;
    }
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    const int pxW = PX(kWinW);
    const int pxH = PX(kWinH);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, pxW, pxH);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    {
        Gdiplus::Graphics gfx(mem);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gfx.ScaleTransform(g_scale, g_scale);
        gfx.Clear(ui::C(ui::kCard));

        // 侧栏
        Gdiplus::SolidBrush sb(ui::C(RGB(248, 249, 251)));
        gfx.FillRectangle(&sb, 0, kTitleH, kSideW, kWinH - kTitleH);
        Gdiplus::Pen sep(ui::C(RGB(232, 235, 240)), 1.0f);
        gfx.DrawLine(&sep, (REAL)kSideW, (REAL)kTitleH, (REAL)kSideW, (REAL)kWinH);

        // 标题栏
        ui::Text(gfx, L"TopMostDesk 设置", Gdiplus::RectF(22.0f, 14.0f, 300.0f, 26.0f),
                 15.0f, ui::kTitle, true);
        DrawCloseBtn(gfx, s.rcClose, s.closeHover);

        // 导航
        for (int i = 0; i < kNavCount; ++i) {
            RECT nr = NavRect(i);
            Gdiplus::RectF nrF((REAL)nr.left, (REAL)nr.top, (REAL)(nr.right - nr.left),
                               (REAL)(nr.bottom - nr.top));
            const bool cur = (s.page == i);
            if (cur) {
                ui::FillRound(gfx, nrF, 8.0f, RGB(255, 255, 255));
                ui::StrokeRound(gfx, nrF, 8.0f, RGB(228, 232, 238), 1.0f);
            } else if (s.hotNav == i) {
                ui::FillRound(gfx, nrF, 8.0f, RGB(240, 242, 246));
            }
            DrawNavIcon(gfx, i, Gdiplus::RectF(nrF.X + 14.0f, nrF.Y + 10.0f, 18.0f, 18.0f),
                        cur ? ui::kAccent : RGB(120, 128, 140));
            ui::Text(gfx, kNav[i].label,
                     Gdiplus::RectF(nrF.X + 42.0f, nrF.Y, nrF.Width - 52.0f, nrF.Height),
                     13.0f, cur ? ui::kTitle : ui::kSubText, cur);
        }

        // 内容
        switch (s.page) {
        case 0: PaintPageHotkeys(gfx); break;
        case 1: PaintPageLook(gfx);    break;
        case 2: PaintPageNotify(gfx);  break;
        case 3: PaintPageBehavior(gfx); break;
        default: PaintPageAbout(gfx);  break;
        }

        // 底部
        gfx.DrawLine(&sep, (REAL)kSideW, (REAL)kFootTop, (REAL)kWinW, (REAL)kFootTop);

        std::wstring hint;
        if (!s.error.empty()) {
            hint = s.error;
        } else if (s.recRow >= 0) {
            hint = L"正在录制：按键盘组合，或按住修饰键点鼠标键；Esc 取消。";
        } else if (s.page == 0) {
            if (s.hotRow >= 0) {
                const Binding& b = s.work.*(kRows[s.hotRow].field);
                hint = (b.key && b.device == TMD_DEV_MOUSE && b.mods == 0)
                     ? L"这一项没有修饰键：该鼠标键会被本软件全局接管，其他程序收不到。"
                     : kRows[s.hotRow].desc;
            } else {
                hint = L"点输入框即可直接按键；鼠标右键/中键/滚轮也能单独绑定。";
            }
        } else if (s.page == 1) {
            hint = L"颜色与粗细会写进配置，下次启动保持一致。";
        } else if (s.page == 2) {
            hint = L"分类开关独立保存，随时可改。";
        } else if (s.page == 3) {
            hint = L"同步拖动只在「选中 2 个以上窗口」时生效，单选拖动不受影响。";
        } else {
            hint = L"按 Esc 或点右上角关闭窗口。";
        }
        ui::Text(gfx, hint,
                 Gdiplus::RectF((REAL)kCX0, (REAL)kFootTop + 16, 396.0f, 36.0f),
                 11.5f, s.error.empty() ? ui::kSubText : ui::kDanger);

        ui::DrawButton(gfx, s.cancelBtn);
        ui::DrawButton(gfx, s.saveBtn);
    }
    BitBlt(dc, 0, 0, pxW, pxH, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ---------------- 交互 ----------------

void Close(bool ok)
{
    if (s.done) return;
    s.ok = ok;
    s.done = true;
    if (s.hwnd) PostMessageW(s.hwnd, WM_NULL, 0, 0);
}

bool ComboFree(HWND hwnd, UINT mods, UINT vk)
{
    if (!hwnd) return false;
    if (RegisterHotKey(hwnd, 0x7FFD, mods | MOD_NOREPEAT, vk)) {
        UnregisterHotKey(hwnd, 0x7FFD);
        return true;
    }
    return false;
}

bool TrySave()
{
    s.error.clear();

    for (int i = 0; i < kRowCount; ++i) {
        const Binding& a = s.work.*(kRows[i].field);
        if (a.key == 0) continue;
        for (int j = i + 1; j < kRowCount; ++j) {
            const Binding& b = s.work.*(kRows[j].field);
            if (b.key == 0) continue;
            if (a.device == b.device && a.mods == b.mods && a.key == b.key) {
                s.error = tmd::Format(L"「%s」和「%s」用了同一个组合键，请错开。",
                                      kRows[i].label, kRows[j].label);
                Repaint(s.hwnd);
                return false;
            }
        }
    }

    std::vector<UINT> held;
    for (int i = 0; i < kRowCount; ++i) {
        const Binding& b = s.work.*(kRows[i].field);
        if (b.key == 0 || b.device != TMD_DEV_KEYBOARD) continue;
        const UINT id = 0x7100u + (UINT)i;
        if (RegisterHotKey(s.hwnd, id, b.mods | MOD_NOREPEAT, b.key)) {
            held.push_back(id);
        } else {
            s.error = tmd::Format(L"「%s」的 %s 已被系统或其他程序占用。",
                                  kRows[i].label, tmd::DescribeBinding(b).c_str());
            for (UINT h : held) UnregisterHotKey(s.hwnd, h);
            Repaint(s.hwnd);
            return false;
        }
    }
    for (UINT h : held) UnregisterHotKey(s.hwnd, h);
    return true;
}

void StopRecord() { s.recRow = -1; s.recMods = 0; }

// 我们会在录制期间吞掉修饰键的 keydown，系统的按键状态因此不可信；
// 这里自己记账：只要钩子看到过按下，就在 g_held 里挂着，直到看到抬起。
// 这样"先按住 Ctrl 再点输入框"也能拿到正确的修饰键。
UINT g_held = 0;

UINT ModBitOf(UINT vk)
{
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return MOD_CONTROL;
    case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:   return MOD_SHIFT;
    case VK_MENU:    case VK_LMENU:    case VK_RMENU:    return MOD_ALT;
    case VK_LWIN:    case VK_RWIN:                       return MOD_WIN;
    default: return 0;
    }
}

// 钩子记账 + 系统按键状态，两者取并集
UINT LiveMods()
{
    UINT m = g_held;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) m |= MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) m |= MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
        m |= MOD_WIN;
    return m;
}

void OnKeyDown(UINT vk, bool down)
{
    auto isMod = [](UINT k) { return ModBitOf(k) != 0; };

    // 修饰键状态由钩子自己记账（录制期间我们吞了 keydown，系统状态不可信）
    if (isMod(vk)) {
        const UINT bit = ModBitOf(vk);
        if (down) g_held |= bit; else g_held &= ~bit;
        if (s.recRow >= 0) {
            s.recMods = LiveMods();
            if (down) Repaint(s.hwnd);
        }
        return;
    }

    if (s.recRow < 0) return;

    if (!down) return;

    if (vk == VK_ESCAPE) { StopRecord(); s.error.clear(); Repaint(s.hwnd); return; }
    if (vk == VK_RETURN) { StopRecord(); Repaint(s.hwnd); return; }
    if (vk == VK_BACK) {
        s.work.*(kRows[s.recRow].field) = Binding{};
        s.error.clear();
        Repaint(s.hwnd);
        return;
    }
    if (s.recMods == 0) {
        s.error = L"至少要有一个修饰键（Ctrl / Alt / Shift / Win），否则会和正常输入冲突。";
        Repaint(s.hwnd);
        return;
    }
    if ((s.recMods & MOD_WIN) && vk == 'L') {
        s.error = L"Win+L 是系统保留组合，换一个。";
        Repaint(s.hwnd);
        return;
    }

    s.work.*(kRows[s.recRow].field) = Binding{ TMD_DEV_KEYBOARD, s.recMods, vk };
    s.error.clear();
    StopRecord();
    Repaint(s.hwnd);
}

void OnMouseDown(UINT mouseKey, POINT pt, UINT mods)
{
    if (s.recRow < 0) return;

    // 鼠标键可以不带修饰键（右键 / 中键 / 侧键 / 滚轮），但“裸左键”会抢掉全系统的点击，拒绝
    if (mods == 0 && mouseKey == TMD_MOUSE_LEFT) {
        s.error = L"左键必须搭配修饰键（例如 Ctrl + 鼠标左键），否则会抢掉全系统的左键点击。";
        Repaint(s.hwnd);
        return;
    }

    s.work.*(kRows[s.recRow].field) = Binding{ TMD_DEV_MOUSE, mods, mouseKey };
    s.error.clear();
    StopRecord();
    Repaint(s.hwnd);
}

LRESULT CALLBACK LLKey(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) {
        const bool down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
        const UINT vk = ((KBDLLHOOKSTRUCT*)l)->vkCode;
        const bool mod = ModBitOf(vk) != 0;

        // 修饰键永远记账：即使不在录制，也要知道自己按了什么，否则
        // "先按住 Ctrl 再点输入框"时页面拿不到 Ctrl
        if (mod) OnKeyDown(vk, down);

        if (s.recRow >= 0) {
            if (!mod) OnKeyDown(vk, down);
            // 只吞 keydown：keyup 放行，避免系统里的修饰键状态卡住
            return down ? 1 : CallNextHookEx(g_kb, code, w, l);
        }
    }
    return CallNextHookEx(g_kb, code, w, l);
}

LRESULT CALLBACK LLMouse(int code, WPARAM w, LPARAM l)
{
    if (code != HC_ACTION || s.recRow < 0) return CallNextHookEx(g_ms, code, w, l);

    const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)l;
    POINT pt = { ms->pt.x, ms->pt.y };
    const UINT mods = LiveMods();

    // 滚轮没有"点击落点"的概念，光标停在设置窗口上也要能录
    if (w == WM_MOUSEWHEEL) {
        const short d = (short)HIWORD(ms->mouseData);
        if (d != 0) {
            OnMouseDown(d > 0 ? TMD_MOUSE_WHEEL_UP : TMD_MOUSE_WHEEL_DOWN, pt, mods);
            return 1;
        }
        return CallNextHookEx(g_ms, code, w, l);
    }

    // 录制时区分「点窗口 UI」和「录鼠标组合键」：
    //   · 带修饰键的点击 → 一定是想录组合键（点按钮/输入框不会按 Ctrl/Alt/Shift/Win），吞掉录入；
    //     之前这里一律按落点放行，导致"按住 Ctrl 点右键"在窗口内被窗口吃掉，鼠标键永远录不到。
    //   · 裸点击落在窗口内 → 交给窗口自己处理（切输入框、点按钮、点关闭）；
    //   · 裸点击落在窗口外 → 没有 UI 会收到，直接录入（支持裸右键/中键/侧键）。
    // 注：点输入框"开始录制"的那一下发生在 recRow 还是 -1 时，第一行就放行了，不受此处影响。
    RECT wr = {};
    if (s.hwnd) GetWindowRect(s.hwnd, &wr);
    if (mods == 0 && InRect(wr, pt)) return CallNextHookEx(g_ms, code, w, l);

    switch (w) {
    case WM_LBUTTONDOWN: OnMouseDown(TMD_MOUSE_LEFT, pt, mods);   return 1;
    case WM_RBUTTONDOWN: OnMouseDown(TMD_MOUSE_RIGHT, pt, mods);  return 1;
    case WM_MBUTTONDOWN: OnMouseDown(TMD_MOUSE_MIDDLE, pt, mods); return 1;
    case WM_XBUTTONDOWN:
        OnMouseDown(HIWORD(ms->mouseData) == XBUTTON1 ? TMD_MOUSE_X1 : TMD_MOUSE_X2, pt, mods);
        return 1;
    default: break;
    }
    return CallNextHookEx(g_ms, code, w, l);
}

void UpdateHover(POINT p)
{
    bool dirty = false;
    auto upd = [&](int& slot, int val) { if (slot != val) { slot = val; dirty = true; } };

    const bool inClose = PtInRect(&s.rcClose, p) != FALSE;
    if (inClose != s.closeHover) { s.closeHover = inClose; dirty = true; }
    upd(s.hotNav, HitNav(p));
    upd(s.hotRow, s.page == 0 ? HitRow(p) : -1);
    upd(s.hotClear, s.page == 0 ? HitClear(p) : -1);
    upd(s.hotSwatch, s.page == 1 ? HitSwatch(p) : -1);
    upd(s.hotCustom, s.page == 1 ? HitCustom(p) : -1);
    upd(s.hotThick, s.page == 1 ? HitThick(p) : -1);
    upd(s.hotSwitch, s.page == 2 ? HitSwitch(p)
                    : (s.page == 3 ? HitBehaveSwitch(p) : -1));

    if (s.saveBtn.Hit(p) != s.saveBtn.hover)   { s.saveBtn.hover = !s.saveBtn.hover; dirty = true; }
    if (s.cancelBtn.Hit(p) != s.cancelBtn.hover) { s.cancelBtn.hover = !s.cancelBtn.hover; dirty = true; }

    if (dirty) Repaint(s.hwnd);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    // 客户区坐标是物理像素，布局是逻辑像素，统一换算
    auto ToLogical = [](LPARAM lp) -> POINT {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        p.x = (LONG)(p.x / g_scale + 0.5f);
        p.y = (LONG)(p.y / g_scale + 0.5f);
        return p;
    };

    switch (msg) {
    case WM_PAINT:      Paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        POINT p = ToLogical(l);
        UpdateHover(p);
        return 0;
    }

    case WM_MOUSELEAVE:
        s.closeHover = false;
        s.hotNav = s.hotRow = s.hotClear = s.hotSwatch = s.hotCustom = s.hotThick = s.hotSwitch = -1;
        s.saveBtn.hover = s.cancelBtn.hover = false;
        s.saveBtn.pressed = s.cancelBtn.pressed = false;
        Repaint(hwnd);
        return 0;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;

    case WM_LBUTTONUP: {
        POINT p = ToLogical(l);
        if (GetCapture() == hwnd) ReleaseCapture();
        if (s.saveBtn.pressed) {
            s.saveBtn.pressed = false;
            if (s.saveBtn.Hit(p)) { if (TrySave()) Close(true); else Repaint(hwnd); return 0; }
        }
        if (s.cancelBtn.pressed) {
            s.cancelBtn.pressed = false;
            if (s.cancelBtn.Hit(p)) { Close(false); return 0; }
        }
        Repaint(hwnd);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT p = ToLogical(l);

        if (PtInRect(&s.rcClose, p)) { Close(false); return 0; }
        if (s.saveBtn.Hit(p))   { s.saveBtn.pressed = true; SetCapture(hwnd); Repaint(hwnd); return 0; }
        if (s.cancelBtn.Hit(p)) { s.cancelBtn.pressed = true; SetCapture(hwnd); Repaint(hwnd); return 0; }

        if (s.recRow >= 0) { StopRecord(); Repaint(hwnd); }

        const int nav = HitNav(p);
        if (nav >= 0) { s.page = nav; s.error.clear(); Repaint(hwnd); return 0; }

        if (s.page == 0) {
            const int ci = HitClear(p);
            if (ci >= 0) {
                s.work.*(kRows[ci].field) = Binding{};
                s.error.clear();
                Repaint(hwnd);
                return 0;
            }
            const int ri = HitRow(p);
            if (ri >= 0) {
                s.recRow = ri;
                // 用户可能"先按住 Ctrl 再点输入框"，此时 keydown 早于录制开始，
                // 钩子没记到，只能从当前按键状态里补
                s.recMods = LiveMods();
                s.error.clear();
                Repaint(hwnd);
                return 0;
            }
        } else if (s.page == 1) {
            const int sw = HitSwatch(p);
            if (sw >= 0) {
                ColOf(s.work, sw / 10) = kPresets[sw % 10].color;
                Repaint(hwnd);
                return 0;
            }
            const int cu = HitCustom(p);
            if (cu >= 0) {
                COLORREF c = ColOf(s.work, cu);
                if (tmd::ChooseColorTopmost(hwnd, c)) ColOf(s.work, cu) = c;
                Repaint(hwnd);
                return 0;
            }
            const int th = HitThick(p);
            if (th >= 0) { s.work.markerThickness = th; Repaint(hwnd); return 0; }
        } else if (s.page == 2) {
            const int sw = HitSwitch(p);
            if (sw >= 0) {
                bool& v = s.work.notify.*(kNotifyRows[sw].field);
                v = !v;
                Repaint(hwnd);
                return 0;
            }
        } else if (s.page == 3) {
            const int sw = HitBehaveSwitch(p);
            if (sw == 0) { s.work.syncDrag = !s.work.syncDrag; Repaint(hwnd); return 0; }
            if (sw == 1) { s.work.showMarker = !s.work.showMarker; Repaint(hwnd); return 0; }
        }

        if (p.y < kTitleH) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (w == VK_ESCAPE) {
            if (s.recRow >= 0) { StopRecord(); Repaint(hwnd); }
            else Close(false);
            return 0;
        }
        if (w == VK_RETURN && s.recRow < 0) {
            if (TrySave()) Close(true);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        Close(false);
        return 0;

    default: break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

} // namespace

bool ShowSettingsWindow(HWND owner, AppConfig* cfg)
{
    if (!cfg) return false;
    ui::StartGdiplus();

    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    if (!inst) inst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SettingsProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCls;
    RegisterClassExW(&wc);

    s = St();
    s.inst = inst;
    s.work = *cfg;
    g_held = 0;              // 重新开始记账
    g_held = LiveMods();

    const int btnW = 92, btnH = 38, gap = 12, margin = 34;
    s.saveBtn.rc   = { kWinW - margin - btnW, kFootTop + 16, kWinW - margin, kFootTop + 16 + btnH };
    s.cancelBtn.rc = { kWinW - margin - btnW - gap - btnW, kFootTop + 16,
                       kWinW - margin - btnW - gap, kFootTop + 16 + btnH };
    s.saveBtn.text = L"保存";
    s.cancelBtn.text = L"取消";
    s.saveBtn.primary = true;
    s.rcClose = { kWinW - 48, 8, kWinW - 8, 48 };

    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    {
        HDC sdc = GetDC(nullptr);
        const int dpi = sdc ? GetDeviceCaps(sdc, LOGPIXELSX) : 96;
        if (sdc) ReleaseDC(nullptr, sdc);
        g_scale = (dpi > 0) ? (dpi / 96.0f) : 1.0f;
        if (g_scale < 0.75f) g_scale = 0.75f;
        if (g_scale > 3.0f)  g_scale = 3.0f;
    }
    const int pw = PX(kWinW);
    const int ph = PX(kWinH);

    int x = work.left + ((work.right - work.left) - pw) / 2;
    int y = work.top + ((work.bottom - work.top) - ph) / 2;

    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kCls, L"TopMostDesk 设置",
                             WS_POPUP, x, y, pw, ph, owner, nullptr, inst, nullptr);
    if (!h) return false;
    s.hwnd = h;
    ui::ApplyRoundedWindow(h, pw, ph, (int)(12 * g_scale + 0.5f));

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    ui::ForceForeground(h);

    g_kb = SetWindowsHookExW(WH_KEYBOARD_LL, LLKey,   GetModuleHandleW(nullptr), 0);
    g_ms = SetWindowsHookExW(WH_MOUSE_LL,    LLMouse, GetModuleHandleW(nullptr), 0);

    MSG msg;
    while (!s.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_kb) { UnhookWindowsHookEx(g_kb); g_kb = nullptr; }
    if (g_ms) { UnhookWindowsHookEx(g_ms); g_ms = nullptr; }
    if (owner) EnableWindow(owner, TRUE);
    if (IsWindow(h)) DestroyWindow(h);
    s.hwnd = nullptr;

    if (s.ok) {
        *cfg = s.work;
        return true;
    }
    return false;
}
