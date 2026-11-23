#pragma once
#include "common.h"

// 弹出式「按键录制」框：支持键盘组合与鼠标键（左/右/中/侧键/滚轮）。
// b 既是初值（当前绑定），也是回填结果。
bool CaptureBinding(HWND owner, Binding* b, const wchar_t* title, const wchar_t* hint);

// 简易文本输入框
bool InputBox(HWND owner, const wchar_t* title, const wchar_t* prompt,
              std::wstring& value);
