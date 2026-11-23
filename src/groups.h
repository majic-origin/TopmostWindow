#pragma once
#include "common.h"
#include "winmgr.h"

struct SavedWindow
{
    std::wstring exe;
    std::wstring cls;
    std::wstring title;
    RECT         rect      = {};
    bool         topmost    = false;
    BYTE         alpha      = 255;
    bool         hidden     = false;
};

struct SavedGroup
{
    std::wstring             name;
    std::vector<SavedWindow> windows;
};

// 布局分组：以 [layout:<名称>] 段写入 config.ini，每行一条窗口记录。
class GroupStore
{
public:
    void Load();
    void Save() const;

    bool Capture(const std::wstring& name, WindowManager& wm);   // 快照当前置顶窗口
    bool Restore(const std::wstring& name, WindowManager& wm);   // 一键恢复
    bool Remove(const std::wstring& name);

    const std::vector<SavedGroup>& Groups() const { return m_groups; }
    std::vector<std::wstring> Names() const;
    const SavedGroup* Find(const std::wstring& name) const;

private:
    std::vector<SavedGroup> m_groups;
};
