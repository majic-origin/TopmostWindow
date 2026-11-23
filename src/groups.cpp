#include "groups.h"
#include "config.h"
#include <cstdlib>

static const wchar_t* kSectionPrefix = L"layout:";

// 字段内可能含 '|'，做最小转义，保证 INI 单行可解
static std::wstring Esc(const std::wstring& s)
{
    std::wstring o;
    o.reserve(s.size() + 4);
    for (wchar_t c : s) {
        if (c == L'\\')      o += L"\\\\";
        else if (c == L'|')  o += L"\\p";
        else if (c == L'\r' || c == L'\n') o += L' ';
        else                 o += c;
    }
    return o;
}

static std::wstring Unesc(const std::wstring& s)
{
    std::wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            if (n == L'p') o += L'|';
            else if (n == L'\\') o += L'\\';
            else { o += L'\\'; o += n; }
        } else {
            o += s[i];
        }
    }
    return o;
}

static std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep)
{
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

void GroupStore::Load()
{
    m_groups.clear();
    const std::wstring file = GetConfigPath();

    // 枚举所有 [layout:*] 段
    std::vector<wchar_t> names(8192, L'\0');
    DWORD n = GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), file.c_str());
    if (n == 0) return;

    for (const wchar_t* p = names.data(); *p; p += wcslen(p) + 1) {
        std::wstring sec = p;
        if (sec.rfind(kSectionPrefix, 0) != 0) continue;

        SavedGroup g;
        g.name = sec.substr(wcslen(kSectionPrefix));

        int count = GetPrivateProfileIntW(sec.c_str(), L"count", 0, file.c_str());
        for (int i = 0; i < count; ++i) {
            std::wstring key = tmd::Format(L"w%d", i);
            wchar_t buf[4096] = {};
            GetPrivateProfileStringW(sec.c_str(), key.c_str(), L"", buf, 4095, file.c_str());
            if (!buf[0]) continue;

            auto f = Split(buf, L'|');
            if (f.size() < 10) continue;

            SavedWindow w;
            w.exe     = Unesc(f[0]);
            w.cls     = Unesc(f[1]);
            w.title   = Unesc(f[2]);
            w.rect.left   = _wtoi(f[3].c_str());
            w.rect.top    = _wtoi(f[4].c_str());
            w.rect.right  = _wtoi(f[5].c_str());
            w.rect.bottom = _wtoi(f[6].c_str());
            w.topmost = _wtoi(f[7].c_str()) != 0;
            w.alpha   = (BYTE)_wtoi(f[8].c_str());
            w.hidden  = _wtoi(f[9].c_str()) != 0;
            g.windows.push_back(w);
        }
        if (!g.windows.empty()) m_groups.push_back(g);
    }
}

void GroupStore::Save() const
{
    const std::wstring file = GetConfigPath();

    // 先清空旧段
    std::vector<wchar_t> names(8192, L'\0');
    DWORD n = GetPrivateProfileSectionNamesW(names.data(), (DWORD)names.size(), file.c_str());
    if (n > 0) {
        for (const wchar_t* p = names.data(); *p; p += wcslen(p) + 1) {
            std::wstring sec = p;
            if (sec.rfind(kSectionPrefix, 0) == 0)
                WritePrivateProfileStringW(sec.c_str(), nullptr, nullptr, file.c_str());
        }
    }

    for (const auto& g : m_groups) {
        std::wstring sec = kSectionPrefix + g.name;
        WritePrivateProfileStringW(sec.c_str(), L"count",
            std::to_wstring(g.windows.size()).c_str(), file.c_str());

        for (size_t i = 0; i < g.windows.size(); ++i) {
            const SavedWindow& w = g.windows[i];
            std::wstring line =
                Esc(w.exe) + L"|" + Esc(w.cls) + L"|" + Esc(w.title) + L"|" +
                std::to_wstring(w.rect.left) + L"|" +
                std::to_wstring(w.rect.top) + L"|" +
                std::to_wstring(w.rect.right) + L"|" +
                std::to_wstring(w.rect.bottom) + L"|" +
                (w.topmost ? L"1" : L"0") + L"|" +
                std::to_wstring((int)w.alpha) + L"|" +
                (w.hidden ? L"1" : L"0");
            std::wstring key = tmd::Format(L"w%u", (unsigned)i);
            WritePrivateProfileStringW(sec.c_str(), key.c_str(), line.c_str(), file.c_str());
        }
    }

    // 立刻落盘，避免 profile 缓存导致外部读到旧布局
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
}

const SavedGroup* GroupStore::Find(const std::wstring& name) const
{
    for (const auto& g : m_groups)
        if (g.name == name) return &g;
    return nullptr;
}

std::vector<std::wstring> GroupStore::Names() const
{
    std::vector<std::wstring> out;
    for (const auto& g : m_groups) out.push_back(g.name);
    return out;
}

bool GroupStore::Capture(const std::wstring& name, WindowManager& wm)
{
    if (name.empty()) return false;

    std::vector<HWND> targets = wm.List();          // 优先抓置顶窗口
    if (targets.empty()) targets = wm.AllManaged(); // 没有置顶则抓已管理窗口
    if (targets.empty()) return false;

    SavedGroup g;
    g.name = name;
    for (HWND h : targets) {
        if (!IsWindow(h)) continue;
        SavedWindow w;
        w.exe   = WindowManager::ExeName(h);
        w.cls   = WindowManager::ClassName(h);
        w.title = WindowManager::Title(h);
        WindowManager::GetFrameRect(h, w.rect);
        w.topmost = WindowManager::IsTopMost(h);
        w.alpha   = wm.GetAlpha(h);
        w.hidden  = !IsWindowVisible(h);
        g.windows.push_back(w);
    }
    if (g.windows.empty()) return false;

    Remove(name);
    m_groups.push_back(g);
    Save();
    return true;
}

bool GroupStore::Restore(const std::wstring& name, WindowManager& wm)
{
    const SavedGroup* g = Find(name);
    if (!g) return false;

    // 枚举当前所有顶层窗口作为候选池
    struct Ctx {
        std::vector<HWND> hwnds;
    } ctx;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        Ctx* c = (Ctx*)lp;
        if (!IsWindow(h)) return TRUE;
        if (WindowManager::OwnWindow(h)) return TRUE;
        LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
        if (ex & WS_EX_TOOLWINDOW) return TRUE;
        RECT r;
        if (!GetWindowRect(h, &r)) return TRUE;
        if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return TRUE;
        if (!IsWindowVisible(h) && WindowManager::Title(h).empty()) return TRUE;
        c->hwnds.push_back(h);
        return TRUE;
    }, (LPARAM)&ctx);

    std::vector<bool> used(ctx.hwnds.size(), false);
    int restored = 0;

    for (const auto& sw : g->windows) {
        HWND best = nullptr;
        int bestScore = -1;

        for (size_t i = 0; i < ctx.hwnds.size(); ++i) {
            if (used[i]) continue;
            HWND h = ctx.hwnds[i];
            if (WindowManager::ExeName(h) != sw.exe) continue;

            int score = 1;
            if (WindowManager::ClassName(h) == sw.cls) score += 2;
            if (WindowManager::Title(h) == sw.title)   score += 4;
            if (score > bestScore) { bestScore = score; best = h; }
        }
        if (!best) continue;

        for (size_t i = 0; i < ctx.hwnds.size(); ++i)
            if (ctx.hwnds[i] == best) { used[i] = true; break; }

        wm.ApplySnapshot(best, sw.rect, sw.topmost, sw.alpha);
        if (sw.hidden) {
            ShowWindow(best, SW_HIDE);
            if (Managed* m = wm.Find(best)) m->hiddenByUs = true;
        }
        ++restored;
    }
    return restored > 0;
}

bool GroupStore::Remove(const std::wstring& name)
{
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        if (it->name == name) {
            m_groups.erase(it);
            Save();
            return true;
        }
    }
    return false;
}
