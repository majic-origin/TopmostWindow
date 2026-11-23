#include "winmgr.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

bool WindowManager::Alive(HWND h)
{
    return h != nullptr && IsWindow(h) != FALSE;
}

bool WindowManager::IsTopMost(HWND h)
{
    if (!Alive(h)) return false;
    return (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

std::wstring WindowManager::Title(HWND h)
{
    if (!Alive(h)) return L"";
    wchar_t buf[512] = {};
    int n = GetWindowTextW(h, buf, 512);
    if (n <= 0) {
        // 某些 UWP / 无标题窗口退回到类名
        return L"<无标题>";
    }
    return buf;
}

std::wstring WindowManager::ClassName(HWND h)
{
    if (!Alive(h)) return L"";
    wchar_t buf[256] = {};
    GetClassNameW(h, buf, 256);
    return buf;
}

std::wstring WindowManager::ExeName(HWND h)
{
    if (!Alive(h)) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid) return L"";

    std::wstring name;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc) {
        wchar_t path[MAX_PATH * 2] = {};
        DWORD sz = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(proc, 0, path, &sz))
            name = tmd::FileNameOf(path);
        CloseHandle(proc);
    }
    return name;
}

bool WindowManager::OwnWindow(HWND h)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

void WindowManager::ApplyTopMost(HWND h, bool on)
{
    if (!Alive(h)) return;
    SetWindowPos(h, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

bool WindowManager::GetFrameRect(HWND h, RECT& rc)
{
    if (!Alive(h)) return false;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))) {
        if (!GetWindowRect(h, &rc)) return false;
    }
    return true;
}

Managed* WindowManager::Ensure(HWND h)
{
    auto it = m_map.find(h);
    if (it != m_map.end()) return &it->second;
    Managed m;
    m.hwnd = h;
    m.pinned = IsTopMost(h);
    auto res = m_map.emplace(h, m);
    m_order.push_back(h);
    return &res.first->second;
}

void WindowManager::Erase(HWND h)
{
    m_map.erase(h);
    m_order.erase(std::remove(m_order.begin(), m_order.end(), h), m_order.end());
}

Managed* WindowManager::Find(HWND h)
{
    auto it = m_map.find(h);
    return it == m_map.end() ? nullptr : &it->second;
}

bool WindowManager::Toggle(HWND h)
{
    if (!Alive(h) || OwnWindow(h)) return false;
    Managed* m = Ensure(h);
    if (IsTopMost(h)) {
        ApplyTopMost(h, false);
        m->pinned = false;
    } else {
        ApplyTopMost(h, true);
        m->pinned = true;
    }
    return m->pinned;
}

bool WindowManager::Pin(HWND h, bool on)
{
    if (!Alive(h) || OwnWindow(h)) return false;
    Managed* m = Ensure(h);
    ApplyTopMost(h, on);
    m->pinned = on;

    // 重新置顶时把透明度也带回来，避免"看不见的置顶窗口"
    if (on && m->alpha < 255) {
        LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
        if (!(ex & WS_EX_LAYERED)) {
            SetWindowLongPtrW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            m->layeredAdded = true;
            SetWindowPos(h, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        SetLayeredWindowAttributes(h, 0, m->alpha, LWA_ALPHA);
    }
    return m->pinned;
}

bool WindowManager::IsPinned(HWND h)
{
    Managed* m = Find(h);
    return m && m->pinned && IsTopMost(h);
}

BYTE WindowManager::GetAlpha(HWND h)
{
    Managed* m = Find(h);
    return m ? m->alpha : 255;
}

void WindowManager::SetAlpha(HWND h, BYTE a)
{
    if (!Alive(h) || OwnWindow(h)) return;
    Managed* m = Ensure(h);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);

    if (a < 255) {
        if (!(ex & WS_EX_LAYERED)) {
            SetWindowLongPtrW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            m->layeredAdded = true;
            SetWindowPos(h, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        SetLayeredWindowAttributes(h, 0, a, LWA_ALPHA);
    } else {
        // 恢复不透明：只剥掉我们自己加上的分层样式，保留应用原有的
        if (m->layeredAdded) {
            SetWindowLongPtrW(h, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
            SetWindowPos(h, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            m->layeredAdded = false;
        } else {
            SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
        }
    }
    m->alpha = a;
}

void WindowManager::HideWindow(HWND h)
{
    if (!Alive(h) || OwnWindow(h)) return;
    Managed* m = Ensure(h);
    if (!IsWindowVisible(h)) return;
    GetWindowRect(h, &m->savedRect);
    ShowWindow(h, SW_HIDE);
    m->hiddenByUs = true;
}

bool WindowManager::ToggleHide(HWND h)
{
    if (!Alive(h) || OwnWindow(h)) return false;
    Managed* m = Ensure(h);
    if (IsWindowVisible(h)) {
        HideWindow(h);
        return true;                     // 现在已隐藏
    }
    ShowWindowAgain(h);
    (void)m;
    return false;
}

void WindowManager::ShowWindowAgain(HWND h)
{
    Managed* m = Find(h);
    if (!Alive(h)) return;
    ShowWindow(h, SW_SHOWNOACTIVATE);
    ShowWindow(h, SW_SHOWNORMAL);
    if (m && m->pinned) ApplyTopMost(h, true);
    if (m) m->hiddenByUs = false;
}

void WindowManager::RestoreAllHidden()
{
    for (auto& kv : m_map) {
        if (kv.second.hiddenByUs && Alive(kv.first))
            ShowWindowAgain(kv.first);
    }
}

int WindowManager::HiddenCount()
{
    int n = 0;
    for (auto& kv : m_map)
        if (kv.second.hiddenByUs && Alive(kv.first)) ++n;
    return n;
}

void WindowManager::Purge()
{
    std::vector<HWND> dead;
    for (auto& kv : m_map) {
        HWND h = kv.first;
        if (!Alive(h)) { dead.push_back(h); continue; }
        // 外部程序自己取消了置顶 —— 同步状态
        if (kv.second.pinned && !IsTopMost(h))
            kv.second.pinned = false;
        if (kv.second.hiddenByUs && IsWindowVisible(h))
            kv.second.hiddenByUs = false;
    }
    for (HWND h : dead) Erase(h);
}

std::vector<HWND> WindowManager::List() const
{
    std::vector<HWND> out;
    for (HWND h : m_order) {
        auto it = m_map.find(h);
        if (it != m_map.end() && it->second.pinned && IsWindow(h))
            out.push_back(h);
    }
    return out;
}

std::vector<HWND> WindowManager::AllManaged() const
{
    std::vector<HWND> out;
    for (HWND h : m_order)
        if (IsWindow(h)) out.push_back(h);
    return out;
}

HWND WindowManager::Next(HWND after) const
{
    std::vector<HWND> list = List();
    if (list.empty()) return nullptr;
    if (!after) return list.front();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == after)
            return list[(i + 1) % list.size()];
    }
    return list.front();
}

void WindowManager::BeginCapture(HWND h)
{
    if (Alive(h) && !OwnWindow(h)) Ensure(h);
}

void WindowManager::ApplySnapshot(HWND h, const RECT& rc, bool topmost, BYTE alpha)
{
    if (!Alive(h)) return;
    Managed* m = Ensure(h);
    ShowWindow(h, SW_SHOWNOACTIVATE);
    SetWindowPos(h, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ApplyTopMost(h, topmost);
    m->pinned = topmost;
    if (alpha < 255) SetAlpha(h, alpha);
}
