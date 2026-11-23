# 贡献指南

先说结论：**开 Issue 说明问题、提 PR 改代码，都欢迎**。这里只写几条让双方都省事的约定。

## 报 Bug 请带上这些

1. Windows 版本（`winver` 里那一串，例如 `Windows 11 家庭版 10.0.26300`）；
2. 程序版本：托盘右键 →「关于」里写着，或看 `src/resource.h` 的版本号；
3. 复现步骤：哪个窗口、按了什么键、期望什么、实际什么；
4. 如果和描边 / 位置有关，附一张截图比十行描述有用。

## 提 PR 之前

```bat
:: 编译（二选一）
build.bat
tools\build-local.bat
```

- 保证能编过再提，**不接受带编译错误的 PR**；
- 只改和这个 PR 主题相关的文件，别顺手重排整个文件（diff 一乱就没人看得懂）；
- 涉及界面 / 交互的改动，请在 PR 描述里放一张截图或 GIF。

## 代码风格

- C++17、4 空格缩进、花括号另起一行（跟现有代码保持一致，看 `src/overlay.cpp` 就懂了）；
- 一律 `W` 版 API（`CreateWindowExW`、`SetWindowTextW`…），字符串用 `std::wstring`，不混用 ANSI；
- 新增菜单命令要在 `src/app.h` 里分一段**不与其他功能重叠的号段**（历史上撞过号段，表现为"点了没反应"）；
- 涉及跨进程窗口操作的地方，记得留一句注释说明为什么这么写——这个项目一半的坑都在 DWM / UIPI / 分层窗口上。

## 提交信息

```
<type>: <一句话说明>

<可选：为什么这么改，踩了什么坑>
```

`type` 用 `feat` / `fix` / `docs` / `refactor` / `chore`。

## 目录速查

| 想改什么 | 去哪 |
| --- | --- |
| 快捷键、配置项 | `src/config.h`、`src/settings.cpp` |
| 描边绘制 | `src/overlay.cpp` |
| 托盘菜单 | `src/menu.cpp` |
| 热键 / 鼠标钩子 | `src/app.cpp`、`src/input_hook.cpp` |
| 弹窗样式 | `src/toast.cpp`、`src/ui_theme.cpp` |

实现细节、历史坑位、设计取舍都写在 [docs/DESIGN.md](docs/DESIGN.md)。
