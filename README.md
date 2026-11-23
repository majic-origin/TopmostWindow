# TopMostDesk — Windows 窗口置顶助手

纯 Win32 / C++17 实现，无第三方依赖，单个 exe（静态链接 CRT）。常驻系统托盘，按全局快捷键把任意窗口置顶 / 取消置顶，并提供置顶列表、透明度、隐藏、分组布局、描边标记等管理能力。

## 编译

需要 Visual Studio 2019/2022（MSVC，含 `rc.exe`）或任意支持 Win32 的 C++17 编译器。

```bat
:: 方式一：VS 开发者命令提示符里执行
build.bat

:: 方式二：CMake
cmake -B out -A x64
cmake --build out --config Release
```

`build.bat` 先跑 `rc /fo app.res ..\src\app.rc` 生成图标与版本资源，再和源码一起链接；`build.bat` 本身必须是纯 ASCII（cmd 按 OEM 代码页读批处理，中文注释会被拆断）。

产物：`build\TopMostDesk.exe`

## 使用方式

| 功能 | 操作 |
| --- | --- |
| 置顶 / 取消置顶 | 激活目标窗口，按 `Win+Shift+T`（默认，可改） |
| 隐藏当前窗口 | 默认 `Ctrl+H`；有批量选中时隐藏全部选中 |
| 显示全部隐藏窗口 | 默认 `Ctrl+Shift+H` |
| 批量选择窗口 | 按住默认的 `Ctrl` 点窗口即可加入 / 移出选择集，可连续选多个（绿框标出） |
| 批量隐藏 / 批量置顶 | 选好后按 `Ctrl+H` 隐藏全部选中；按 `Ctrl+鼠标右键` 批量置顶 / 取消置顶 |
| 取消全部置顶 | 默认 `Ctrl+Shift+鼠标右键`（可改），或在菜单 →「置顶窗口列表」→「全部取消置顶」 |
| 切换置顶窗口 | 默认 `Win+滚轮下滚` 下一个 / `Win+滚轮上滚` 上一个（可改成任意键鼠组合） |
| 快速切换 | 左键单击托盘图标 = 对当前前台窗口切换置顶 |
| 完整管理菜单 | 右键单击托盘图标，或 `Win+Shift+M` |
| 置顶窗口列表 | 菜单 →「置顶窗口列表」，勾选当前目标；点击条目可直接切到该窗口 |
| 透明度 | 菜单 →「窗口透明度」→ 100%~20%（调节时自动置顶，便于观察） |
| 分组布局 | 菜单 →「分组布局」→「保存当前置顶布局…」输入名称；「恢复布局」一键复位 |
| 边框外观 | 菜单 →「置顶标记外观」→ 置顶边框颜色 / 选中窗口边框颜色 / 边框粗细 1~10px / 显示开关 |
| 弹窗提醒 | 菜单 →「弹窗提醒」→ 分类开关：置顶提示 / 切换提示 / 透明度提示 / 布局提示 / 系统与错误提示，另有「全部静音」「全部开启」「全部关闭」 |
| 开机自启 | 菜单 →「开机自动启动」勾选，写入 `HKCU\...\Run` |
| 改快捷键 | 菜单 →「**快捷键与设置…**」打开设置大窗口，一页里改完所有快捷键 |
| 退出 | 菜单 →「退出」 |

### 托盘菜单行为（点设置不关闭）

菜单项分两类：

- **设置项**（边框颜色、粗细、弹窗开关、开机自启、快捷键、取消全部置顶、刷新…）：点完 **菜单保持打开**，可以连着改好几项。
- **动作项**（置顶该窗口、透明度、隐藏、列表跳转、分组恢复、关于、退出）：点完就关闭，因为下一步通常是把焦点交还给目标窗口。

实现方式：`ShowContextMenu()` 里是一个 `for` 循环 —— `TrackPopupMenuEx(TPM_RETURNCMD)` 拿到命令后分发，若命中 `IsStickyCommand()` 就重新 `BuildMenu()` 再弹一次；返回 0（点空白 / Esc）或命中退出才 `break`。菜单每次重建，勾选状态、置顶计数、快捷键文案都是最新的。锚点固定在首次弹出时的鼠标位置，所以重开时菜单不跳动，看起来就是"没关过"。循环上限 128 次防止异常情况下死循环。

### 鼠标键与批量选择

`RegisterHotKey` 天生只认键盘，所以鼠标键走另一条通道：常驻一个 `WH_MOUSE_LL` 全局钩子，命中配置的组合时 `PostMessage` 给主窗口并吞掉这次事件（不会同时触发目标窗口的右键菜单或滚动）。

- 可绑：鼠标左键 / 右键 / 中键 / 侧键1 / 侧键2 / 滚轮上滚 / 滚轮下滚。
- 建议搭配修饰键（`Ctrl + 鼠标右键`、`Alt + 中键`）；右键/中键/侧键/滚轮也允许不带修饰键（左键除外，会抢掉全系统点击）。
- 默认绑定：置顶 `Win+Shift+T`、隐藏 `Ctrl+H`、显示全部 `Ctrl+Shift+H`、下一个 `Win+滚轮下`、上一个 `Win+滚轮上`、取消全部置顶 `Ctrl+Shift+鼠标右键`、批量选择 `Ctrl+左键`、批量置顶 `Ctrl+右键`。
- 注意 `Win + 鼠标键` 松手时系统仍会弹开始菜单，这是 Windows 自身行为；如果嫌碍事，改用 `Ctrl` / `Alt` 组合。

**批量选择**：`bindSelect`（默认 `Ctrl+左键`）命中后，钩子吞掉这次点击，用 `WindowFromPoint` + `GetAncestor(GA_ROOT)` 取光标下的顶层窗口，把它加入 / 移出选择集，绿框标出并弹一条"已选中 N 个窗口"。选中集在 `App::m_selected` 里，动作时按下面的规则联动：

| 操作 | 有选中窗口 | 没有选中窗口 |
| --- | --- | --- |
| `Ctrl+H`（`HK_HIDE`） | 隐藏全部选中，然后清空选择 | 隐藏当前前台窗口 |
| `Ctrl+右键`（`HK_BATCH_PIN`） | 只要有一个没置顶就全部置顶，否则全部取消 | 对当前前台窗口切换置顶 |
| `Ctrl+Shift+H`（`HK_SHOW_ALL`） | 恢复所有被本软件隐藏的窗口 | 同上 |

代价是 `Ctrl+左键` 变成了"本软件的选窗口手势"——在资源管理器里 Ctrl+点选文件也会被拦。不想用就把它改成 `Ctrl+Shift+左键` 之类更少用的组合，或在设置窗口里换一个更少用的组合。

### 设置大窗口（快捷键一页改完）

菜单 →「快捷键与设置…」打开一块 900×640（按系统 DPI 等比放大）的独立窗口，左侧分类导航、右侧就地设置，不用一层层翻菜单：

| 分类 | 内容 |
| --- | --- |
| 快捷键 | 9 项，每行一个输入框：**点一下就开始录制**，按下的组合键直接显示在框里；右侧 ✕ 清除该项 |
| 边框外观 | 三套配色（置顶 / 当前选中 / 批量选中）各 7 个预设色 + 系统取色器，边框粗细 1~10px |
| 弹窗提醒 | 6 个分类开关（全部静音 + 5 类），开关状态就是配置 |
| 关于 | 版本与用法速览 |

交互细节：

- 录制期间同时挂 `WH_KEYBOARD_LL` 与 `WH_MOUSE_LL`，只吞 `keydown`、放行 `keyup`（否则系统里的修饰键状态会卡住）；所以按 `Win` 不会弹开始菜单。
- 鼠标支持左/右/中/侧键/滚轮方向。右键、中键、侧键、滚轮都可以**不带修饰键**单独绑定；只有"裸左键"被拒绝（会抢掉全系统的点击）。不带修饰键的项用橙色文字提示"该鼠标键会被本软件全局接管"。
- **修饰键状态自己记账**：录制期间我们吞了 `keydown`，系统根本不知道你按了 Ctrl，只查 `GetAsyncKeyState` 会出现"先按住 Ctrl 再点输入框、结果记成裸右键"。所以钩子把每个修饰键的按下/抬起记在 `g_held` 里，再与系统状态取并集。
- **滚轮不按落点判断**：按钮类必须点在设置窗口外才算录制（框内的点击要留给 UI）；滚轮没有落点概念，光标停在设置窗口上也能录。
- 打开设置窗口前先 `UnregisterHotKey` 释放自己占用的组合，否则"改回原组合"会被自己挡住。
- 点「保存」时统一校验：**重复组合**（两项绑成一样，比如把"置顶"和"批量置顶"都设成 `Ctrl+右键`）+ **键盘占用**（用 `RegisterHotKey` 逐个试注册，失败立刻红字点名是哪一项）——校验不过就不关闭窗口。
- 布局全部以 96 DPI 的逻辑坐标书写，绘制时 `Graphics::ScaleTransform(dpi/96)`、窗口尺寸同步放大，所以 125% / 150% 缩放下不会变小也不会糊。

### 边框外观

- **置顶边框颜色**：普通置顶窗口的描边颜色，7 个预设 + 「自定义颜色…」（系统取色器 / 设置窗口）。
- **选中窗口边框颜色**：当前操作目标（最近一次置顶、或用滚轮/列表切过去的窗口）用另一种颜色，并且线宽 +2px、内圈多一条细线，一眼能区分"谁是当前选中的"。
- **批量选中边框颜色**：批量选择集的颜色（默认绿），同样 +2px + 内描边；即使窗口没置顶也会画框，方便确认选了谁。
- **边框粗细**：1~10 像素，改完立刻重绘；已置顶的窗口全部同步，不会丢失。
- 全部即时生效并写入配置。

### 弹窗提醒（分类开关）

托盘气泡是最容易被嫌吵的部分，所以拆成独立分类，由用户自己决定看哪些：

| 分类 | 触发场景 | 默认 |
| --- | --- | --- |
| 置顶 / 取消置顶提示 | 每次切换置顶、隐藏窗口 | 开 |
| 切换置顶窗口提示 | `Win+滚轮` 或快捷键在置顶窗口间切换 | 开 |
| 透明度调整提示 | 菜单里选透明度档位 | 开 |
| 分组布局提示 | 保存 / 恢复布局成功或失败 | 开 |
| 系统与错误提示 | 快捷键被占用、注册表写入失败等 | 开 |
| 全部静音 | 一票否决，优先级最高 | 关 |

`全部静音` 勾上后所有气泡都静默（菜单里的分类项同时置灰，表示"当前不会响"）；取消静音后各分类恢复各自的设置。全部开关都落在 `config.ini` 的 `[notify]` 段。

### 弹窗实现：单实例 + 原地覆盖（`toast.h/.cpp`）

系统气泡（`Shell_NotifyIcon` + `NIF_INFO`）是**队列制**：连续触发几次就排几个，每关一个才露出下一个，超时的还会堆进通知中心。所以这里不再用它，改成自绘的单实例弹窗：

- **全局只有一个窗口**：`toast::Show()` 里 `EnsureWindow()` 只在第一次 `CreateWindowEx`，之后永远是同一个 `HWND`；新通知只做三件事——换文字、`InvalidateRect` 重画、`SetWindowPos(HWND_TOPMOST)` 提回最前。
- **覆盖而不是新建**：`s.title / s.text / s.flags` 直接赋值，同一次显示里内容被整体替换，不存在"旧弹窗还挂在屏幕上"。
- **同一事件 1 秒内连续触发只算一次**：`key`（取事件标题）与上次相同、且间隔 ≤ 1000ms 时判定为 burst，只更新文本、把自动关闭的 deadline 顺延到最后一次操作，不新增窗口也不额外闪。
- **不同事件**：同样复用这个窗口，内容立刻覆盖（不排队、不叠放）。
- **关闭方式**：最后一次更新后 2.8 秒自动消失；鼠标悬停时暂停倒计时；**点一下任意键即消失**，不需要找关闭按钮。
- **不会被别的置顶窗口盖住**：200ms 的 watchdog 定时器把窗口反复 `SetWindowPos(HWND_TOPMOST, SWP_NOACTIVATE)`，同时兼做倒计时检查。
- **尺寸按 DPI 走**：380×88 逻辑像素，绘制时 `ScaleTransform(dpi/96)`，圆角用 `SetWindowRgn`，配色按 `NIIF_INFO / WARNING / ERROR` 分成蓝 / 橙 / 红三套（`NIIF_INFO=1`、`NIIF_ERROR=3` 是位关系，必须按低四位取值判断，否则 INFO 会被判成 ERROR）。

## 应用图标

图标源文件是 `assets/logo/png/favicon-512.png`，构建前先转成多尺寸 ICO：

```bat
powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
```

脚本按 16/20/24/32/40/48/64/128/256 九档缩放，把每档编码成 PNG 后拼成标准 ICO（Vista+ 支持 PNG 压缩条目），输出 `src/app.ico`。换 logo 时只替换 PNG 再跑一次脚本即可。

配置方式：`src/app.rc` 里 `IDI_APP ICON "app.ico"`，`IDI_APP` 在 `src/resource.h` 定义为 101；`build.bat` / CMake 都会先用 `rc.exe` 编译 `.rc`，再把 `.res` 交给链接器。

运行期加载（`app.cpp`）：

```cpp
// 窗口类图标（Alt+Tab / 任务栏）
wc.hIcon   = LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
wc.hIconSm = LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
// 托盘图标（按当前 DPI 取小图标尺寸，失败时回退到代码绘制）
m_icon = LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
```

生效范围：

| 位置 | 说明 |
| --- | --- |
| 资源管理器里的 exe 文件图标 | 由 `.rsrc` 里的 `RT_GROUP_ICON` 决定，随文件走 |
| 任务栏 / Alt+Tab / 窗口标题栏 | 来自 `WNDCLASSEXW.hIcon / hIconSm` |
| 系统托盘（通知区域） | `Shell_NotifyIcon` 的 `hIcon` |
| 文件属性 → 详细信息 | 来自同一 `.rc` 的 `VERSIONINFO`（资源名必须是 `1`，否则 Windows 按名字查不到） |
| 快捷方式 / 固定到开始屏幕 | 系统读取 exe 内嵌图标，无需额外配置 |

因为是内嵌资源，单文件拷走到任何机器都带图标，不需要旁边放 ico 文件。

### 隐藏的调试参数

```bat
TopMostDesk.exe --test-settings  :: 启动后直接打开设置大窗口
TopMostDesk.exe --test-hotkey    :: 启动后直接弹出单条快捷键录制框
TopMostDesk.exe --test-input     :: 启动后直接弹出分组名称输入框
TopMostDesk.exe --test-color     :: 启动后直接弹出系统取色器
```

配置与分组布局：`%APPDATA%\TopMostDesk\config.ini`

## 架构

```
main.cpp        wWinMain：单实例互斥、DPI 感知、App 生命周期
app.h/.cpp      App：隐藏主窗口 + 消息循环，动作分发、定时器、托盘事件
menu.cpp        App::BuildMenu / HandleCommand：菜单装配、颜色/粗细/批量选择命令分发
settings.h/.cpp 设置大窗口：左侧分页导航 + 快捷键就地录制 + 外观/提醒开关（GDI+ 自绘，按 DPI 缩放）
winmgr.h/.cpp   WindowManager：被管理窗口状态表（置顶/透明度/隐藏）+ 安全封装
overlay.h/.cpp  Overlay：单块透明点击穿透覆盖层，用逐像素 alpha 画描边（置顶/当前/批量选中三色）
input_hook.h/.cpp  InputHook：全局 WH_MOUSE_LL 钩子，鼠标键映射成动作（带屏幕坐标）
groups.h/.cpp   GroupStore：布局快照的采集/匹配/恢复，持久化到 INI
dialogs.h/.cpp  按键录制框（键盘钩子 + 鼠标钩子 + 占用校验）
inputbox.cpp    分组名称输入框
ui_theme.h/.cpp GDI+ 绘制封装：圆角卡片、按钮、文字、窗口圆角
config.h/.cpp   AppConfig（9 组 Binding + 外观 + 提醒开关）读写、开机自启
tray.h/.cpp     托盘图标（资源优先，缺失时运行时绘制），通知转交 toast
toast.h/.cpp    单实例通知弹窗：内容原地覆盖 + 1s 合并 + 自动消失
common.h/.cpp   公共定义、Binding 结构与名称格式化
app.rc          图标 IDI_APP + VERSIONINFO 版本信息
resource.h      资源 ID 与版本号宏
app.ico         由 assets/logo/png/favicon-512.png 生成的九档多尺寸图标
tools/make-icon.ps1  PNG → ICO 生成脚本
```

### 关键实现

**置顶**：`SetWindowPos(hwnd, HWND_TOPMOST / HWND_NOTOPMOST, ..., SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)`。状态以 `GWL_EXSTYLE & WS_EX_TOPMOST` 为准，定时 `Purge()` 同步外部程序自己造成的状态漂移。

**全局热键**：键盘走 `RegisterHotKey` + `MOD_NOREPEAT`；鼠标走 `WH_MOUSE_LL`。两条通道最终都归一到同一个 `OnAction(id)`（`HK_TOGGLE / HK_HIDE / HK_SHOW_ALL / HK_NEXT / HK_PREV / HK_UNPIN_ALL / HK_SELECT / HK_BATCH_PIN / HK_MENU`），目标窗口统一取 `GetForegroundWindow()`。

**鼠标钩子的坑**：`WH_MOUSE_LL` 的 `wParam` 才是消息号，`lParam` 是 `MSLLHOOKSTRUCT`（不是 `MSG`）。按 `MSG` 解析会读到垃圾 `message`，命中判断永远不成立——滚轮增量在 `mouseData` 的高位，侧键在 `mouseData` 的 `HIWORD`。

**视觉标记**：不注入目标进程。创建一个覆盖整个虚拟屏幕的 `WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST` 弹出窗口，`CreateDIBSection` 建 32bpp 后备缓冲，直接按行写像素画四边条，再 `UpdateLayeredWindow(..., ULW_ALPHA)` 提交。200ms 重绘一次，窗口矩形用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 取真实可视边界（`GetWindowRect` 会多算 8px 不可见边框）。每帧末尾用 `SetWindowPos(HWND_TOPMOST, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)` 重新提到最前——否则其它 topmost 窗口被激活后会盖住描边。

**显隐状态**：覆盖层在"没有可画窗口"时会 `ShowWindow(SW_HIDE)`。这是分层窗口的坑——`UpdateLayeredWindow` 只更新内容、不会把隐藏的窗口显示出来，所以恢复画之前必须显式 `ShowWindow(SW_SHOWNOACTIVATE)`，否则隐藏过一次就再也画不出来（v2 的"全部丢边框"就是这个）。

**菜单命令 ID 分段**：每类功能占独立号段（透明度 200、置顶列表 400、颜色 620/640、粗细 660、通知 700…），分发时用 `id >= base && id < base + 精确数量` 判定。v2 曾把"自定义颜色"（640）和"粗细"（680）放进 620~669 / 650~699 的宽松区间里，被前面的预设色分支整段吞掉——表现为点自定义颜色没反应、改粗细不生效。

**配置落盘**：`WritePrivateProfileString` 内部有缓存，写完必须再调一次 `WritePrivateProfileString(nullptr, nullptr, nullptr, file)` 强制 flush，否则其它进程（包括面板预览、脚本）读到的还是旧值。

**透明度**：动态加 `WS_EX_LAYERED` + `SetLayeredWindowAttributes(..., LWA_ALPHA)`。记录"分层样式是不是我们加的"，恢复 100% 时只移除自己加的那一层，不破坏应用原有的分层属性。样式变更后补一次 `SWP_FRAMECHANGED`。

**视觉标记**：不注入目标进程。创建一个覆盖整个虚拟屏幕的 `WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST` 弹出窗口，`CreateDIBSection` 建 32bpp 后备缓冲，直接按行写像素画四边条，再 `UpdateLayeredWindow(..., ULW_ALPHA)` 提交。250ms 重绘一次，窗口矩形用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 取真实可视边界（`GetWindowRect` 会多算 8px 不可见边框）。

**分组布局**：快照记录 `exe 名 / 类名 / 标题 / 矩形 / 置顶 / 透明度 / 隐藏`。恢复时枚举顶层窗口，按 `exe` 必须相同、`class` 与 `title` 加分的方式打分匹配，逐个 `SetWindowPos` 复位并重放置顶/透明/隐藏状态。字段用 `\|` 分隔、`\p` 转义，写进 INI 的 `[layout:<名称>]` 段。

**托盘**：`Shell_NotifyIcon` + `NOTIFYICON_VERSION_4`；图标由代码绘制（圆角块 + 向上箭头），不依赖 `.ico` 资源；监听 `TaskbarCreated` 消息，Explorer 崩溃重启后自动重新挂载。

## 已知边界

- 以 `asInvoker` 运行。目标窗口若以管理员权限运行，受 UIPI 限制，置顶操作可能无效——需要时以管理员身份启动本程序（清单里 `uiAccess="false"`，未做签名）。
- 全屏独占（Exclusive Fullscreen）游戏上覆盖层不显示，属预期。
- 透明度依赖窗口自身的分层合成能力，个别 DirectComposition 自绘窗口可能忽略 `LWA_ALPHA`。
