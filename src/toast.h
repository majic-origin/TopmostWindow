#pragma once
#include "common.h"

// 单实例通知弹窗。
// 全局只创建一个窗口：每次通知只改文字、重置自动关闭计时、把窗口提到最前，
// 不再走 Shell_NotifyIcon 的气泡（那是队列制，会堆出好几个要逐个关）。
namespace toast {

// 显示 / 覆盖一条通知。key 相同且距上次 <= 1s 视为"同一事件的连续触发"，
// 合并成一次（窗口不变，内容取最后一次），随后自动淡出。
void Show(const std::wstring& key, const std::wstring& title,
          const std::wstring& text, DWORD flags = NIIF_INFO);

void Hide();
void Destroy();
bool Visible();

} // namespace toast
