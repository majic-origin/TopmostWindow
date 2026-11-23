#pragma once
#include "config.h"

// 大窗口设置面板：左侧分类导航（快捷键 / 边框外观 / 弹窗提醒 / 关于），
// 右侧逐项就地设置——快捷键直接点输入框录制，不用一层层翻菜单。
// 返回 true 表示用户点了「保存」，此时 *cfg 已被更新（调用方负责落盘与重新注册热键）。
bool ShowSettingsWindow(HWND owner, AppConfig* cfg);
