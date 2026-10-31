# MarkerCycle / 一键循环标记

一个 Guild Wars 2 [Nexus](https://github.com/RaidcoreGG/Nexus) 插件：给「循环列表」配一个快捷键，
按一次执行第 1 项，再按一次第 2 项，……最后一项之后回到第 1 项。

列表默认是公会团队标记 1→2→…→8→1，但项数不限、可增删可排序，每一项可以是「游戏动作」或「按键」。
v1.3 起**固定 8 个循环常驻**：8 个绑定标识在加载时就全部注册好（不必先新建循环），每个循环有自己的触发键、自己的列表、自己的进度；列表留空的循环按它的键不做事。

Each cycle has its own hotkey, action list and progress (default list: squad markers 1→2→…→8→1).

- GitHub: <https://github.com/Yunfung/MarkerCycle>
- Gitee: <https://gitee.com/vru/gw2-marker-cycle>
- 下载（Release 附件）：<https://github.com/Yunfung/MarkerCycle/releases> / <https://gitee.com/vru/gw2-marker-cycle/releases>

用户安装 / 使用说明见 [说明.txt](说明.txt)。

---

## How it works

| 环节 | 做法 | 原因 |
| --- | --- | --- |
| 取键 | 加载时给 **8 个循环各注册 1 个绑定**（`InputBinds_RegisterWithString`，标识 `MarkerCycle-循环1` … `MarkerCycle-循环8`；第一个默认 `CTRL+E`，其余默认未绑定，Nexus 里显示为未绑定项），只在 `aIsRelease == false` 时触发，并做 100 ms 去抖 | 一个循环一个键；标识常驻，玩家随时可以在「输入绑定」里给任意一个设键 |
| 分发 | `FindCycleByIdentifier` 按绑定标识找到对应循环，游标 / 去抖 / 空闲重置都各循环独立 | 多个循环互不干扰 |
| 执行「游戏动作」 | `GameBinds_InvokeAsync((EGameBinds)value, PressDurationMs)` | 按**游戏绑定项**触发，与玩家把该动作绑到哪个键无关；且国服客户端屏蔽 `SendInput`，走 gamebind 通道最稳 |
| 执行「按键」 | `WndProc_SendToGameOnly(hwnd, WM_KEYDOWN/WM_KEYUP, vk, lParam)`，修饰键用 `WM_SYSKEYDOWN` 方式先行按下/最后抬起 | 国产客户端只认这条投递路径；`lParam` 按扫描码 + Extended 位构造 |
| 找窗口 | `EnumWindows` → 类名 `ArenaNet_Dx_Window_Class` 且进程号 = 本进程 | 不 hook WndProc，避免与其它插件的消息处理互相干扰 |
| 面板 | `GUI_Register(RT_OptionsRender, cb)`，回调里直接摆 ImGui 控件（不需要 Begin/End） | Nexus 官方模板的做法；卸载时 `GUI_Deregister(cb)` |
| ImGui | 静态链接 **1.80**（`RaidcoreGG/imgui` @ `58075c4`） | Nexus 2026.5.20 的 `d3d11.dll` 与 Keyboard_Overlay / Pie_UI / Serenade 内嵌的都是 1.80，版本必须一致才能共用 `ImGuiContext` |
| 面板文字 | 只用 ASCII + 常用标点 + 汉字（`tools/glyphcheck.py` 把关） | Nexus 的 ImGui 字体只给常用范围建字形，箭头 `→` 这类会画成 “?” |
| 配置 | INI：`addons\MarkerCycle\MarkerCycle.ini`，面板每次改动后立即整文件重写 | 可手改、可备份、不留二进制状态 |
| 长按连发 | 按下时记录 `held` + `holdNext = now + RepeatDelayMs`；`GUI_Register(RT_Render, TickRender)` 每帧检查 `ShouldRepeat(held, 开关, now, holdNext)`，到点就 `FireNext()` 并把 `holdNext` 推到 `now + RepeatIntervalMs`；抬起事件把 `held` 清掉 | 只用 Nexus 自带的每帧渲染回调做定时，不额外开线程、不碰游戏主线程以外的东西；轻按（起手延迟内松手）仍然只走一项 |
| 配置管理 | `profiles\` 目录下每份 `.ini` 就是一套配置；保存=复制当前文件，加载=覆盖当前文件后 `LoadConfig()` 重读，导出/导入走 `SHGetFolderPathW` + `CopyFileW` | 和 GW2-QuickChat 的配置管理一致；切换、备份、分享都用文件，不引入额外状态 |

## 目录结构

```
MarkerCycle/
├─ addons/MarkerCycle.dll          产物（安装包内容）
├─ src/MarkerCycle.cpp             插件实现（入口、INI、绑定回调、按键投递、设置面板、配置管理）
├─ src/cycle.h                     纯逻辑：Action 解析/格式化、序列解析、循环游标（无 Windows 依赖）
├─ src/gamebinds.inc               由 Nexus.h 的 EGameBinds 生成的 176 项动作表（含中文名）
├─ src/imgui/                      ImGui 1.80（RaidcoreGG fork），只编译 4 个 .cpp
├─ src/nexus/Nexus.h               Nexus 官方 API 头（API v6，pin 在 9b2c53d）
├─ tests/test_cycle.cpp            逻辑单元测试（含多循环、Action 解析/循环）
├─ tests/test_export.cpp           加载 DLL、调用 GetAddonDef 校验元数据
├─ tools/glyphcheck.py             检查源码字面量里有没有 Nexus 字体画不出的字符
├─ tools/normalize_text.py         把文档统一成 UTF-8 BOM + CRLF
├─ tools/inicheck.cpp              用 Windows 的 INI 接口把配置文件读一遍（离线排查用）
├─ tools/patch_*.py                本次迭代里改源码/文档的一次性补丁脚本（留档）
├─ tools/iniroundtrip.cpp          端到端：把 SerializeConfig 的输出落盘再用 INI 接口读回校验
├─ build.bat                       编译（自动定位 vcvars64.bat）
├─ 一键循环标记安装.ps1            安装 / 回退 / 状态
└─ 一键循环标记安装-双击运行-….bat  双击入口
```

## Build

需要 Visual Studio 2022 Build Tools（含 C++ 桌面工作负载）。

```bat
build.bat
```

内部命令：

```bat
cl /nologo /W3 /O2 /MT /EHsc /std:c++17 /utf-8 /DNDEBUG /LD ^
   /I"src" /I"src\imgui" src\MarkerCycle.cpp ^
   src\imgui\imgui.cpp src\imgui\imgui_draw.cpp src\imgui\imgui_tables.cpp src\imgui\imgui_widgets.cpp ^
   /Fe:build\MarkerCycle.dll
```

* x64；`/MT` 静态 CRT，游戏环境不依赖 VC 运行库；`/utf-8` 保证中文 `Name` 与界面文案以 UTF-8 交给 Nexus / ImGui。
* 依赖只有 `KERNEL32.dll` + `USER32.dll`，导出只有 `GetAddonDef`。

## Tests

`test_cycle.cpp` 还覆盖了长按连发的纯逻辑（`ClampRepeatDelay` / `ClampRepeatInterval` / `ShouldRepeat` / `NextRepeatTime`）以及三个新配置键的序列化。

```bat
build\_runtests.bat
```

（内部就是编 `tests\test_cycle.cpp`、`tests\test_export.cpp` 再依次运行；不需要游戏。）

`test_cycle` 覆盖：序列解析（分隔符、越界丢弃、去重、空串）、TargetMode 解析、auto 探测决策、
1..8 三圈循环、子集循环、空序列、`ParseAction`/`FormatAction` 往返、Action 列表循环、旧配置迁移、
多循环（绑定标识命名、默认/标记目标列表、两循环游标互不影响、空列表、列表变短后的游标收敛）、固定 8 槽位（`EnsureCycleSlots`：缺的补空循环、已有按槽位保留、乱序/重复槽位归位）、配置文本格式（`SerializeConfig`：小节头必须带方括号、8 个小节齐全、不允许出现裸 `CycleN` 行）。

配置读写的端到端校验（编完跑一次，不需要游戏）：

```bat
build\\_iniroundtrip.bat
build\\iniroundtrip.exe
```

它把 `SerializeConfig` 的结果按系统 ANSI 落盘，再用 `GetPrivateProfileStringW/IntW` 读回来逐项比对——
v1.2 曾把小节头写成裸 `Cycle1`，导致列表写出去就再也读不回来，这一条专门防它复发。

`test_export` 加载真实 DLL，校验 `Signature == 1296236545`、`APIVersion == 6`、
`Name == 一键循环标记`、`Load/Unload` 非空。

面板文字检查（可选，改文案后跑一次）：

```bat
python tools\glyphcheck.py
```

## Metadata

| 字段 | 值 |
| --- | --- |
| Name | 一键循环标记 |
| Signature | `1296236545` (`0x4D430001`) |
| APIVersion | `6` |
| Version | `1.5.2.0` |
| DLL | `addons\MarkerCycle.dll` |
| Config | `addons\MarkerCycle\MarkerCycle.ini` |
| Author | 摸鱼哥 |
| Bind identifiers | `MarkerCycle-循环1` … `MarkerCycle-循环8` |

## 配置管理（`DrawConfigManager`）

面板底部「配置管理」折叠段，功能与 GW2-QuickChat 的同名模块一致：

| 控件 | 行为 | 实现 |
| --- | --- | --- |
| 配置列表 + 刷新 | 列出 `addons\MarkerCycle\profiles\*.ini` | `ListProfiles()`（`FindFirstFileW`，按名字排序） |
| 保存到选中 | 当前配置覆盖写进选中项 | `CopyFileUtf8(g_iniPath, ProfilePathUtf8(name))` |
| 加载选中 | 选中项覆盖当前配置并立即重读 | `ApplyConfigFile()` = `CopyFileW` → 清 UI 临时状态 → `LoadConfig()` |
| 删除选中 | 删文件（先弹确认 modal） | `DeleteFileUtf8()` |
| 重命名 | 改文件名 | `MoveFileUtf8(ProfilePathUtf8(old), ProfilePathUtf8(new))` |
| 另存为新配置 | 当前配置复制成新名字 | `CopyFileUtf8` |
| 导出到该路径 / 导出到桌面 | 把当前配置复制成 `.ini` | `NormalizeConfigPath(raw, true)`；桌面用 `SHGetFolderPathW(CSIDL_DESKTOPDIRECTORY)` |
| 从该路径导入 | `.ini` 直接导入；目录则遍历 `*.ini`，只导入 `LooksLikeMarkerCycleIni()` 认可的，最后载入最新那份 | `FindNewestIniInDir()` + `TimestampSuffix()` |

约定：

- profiles 目录：`<游戏目录>\addons\MarkerCycle\profiles\`（`ConfigDirUtf8()` 自动建）。
- 格式判定 `LooksLikeMarkerCycleIni()`：文件里出现 `[Cycle1]` 或 `PressDurationMs=` 才算本插件的配置。
- 路径输入会做 `CleanPath()`：去掉首尾引号/空白、展开 `%USERPROFILE%`、把 `/` 归一成 `\`。
- 为了 `SHGetFolderPathW`，`src/MarkerCycle.cpp` 里有 `#pragma comment(lib, "Shell32.lib")`，
  所以 DLL 的导入表比 v1.3 多一个 `SHELL32.dll`（系统自带，无额外依赖）。

## Config keys (`[MarkerCycle]` + `[Cycle<slot>]`)

```ini
[MarkerCycle]
PressDurationMs=50
ResetAfterSeconds=0
ShowAlert=0
HoldRepeat=0
RepeatDelayMs=300
RepeatIntervalMs=150

[Cycle1]
Name=循环1
ActionCount=8
Action1=bind:86
...

[Cycle2]
Name=循环2
ActionCount=8
Action1=bind:94
...
```

## Config keys (`[MarkerCycle]`)

| Key | Default | 说明 |
| --- | --- | --- |
| `[CycleN] Name` | `循环N` | 面板下拉里显示的名字 |
| `[CycleN] ActionCount` | `8` | 该循环的列表长度（0–512） |
| `[CycleN] ActionM` | — | 第 M 项，`bind:<EGameBinds>` 或 `key:<VK>[+ctrl][+alt][+shift]` |
| `PressDurationMs` | `50` | 触发/按键按下时长（5–500） |
| `ResetAfterSeconds` | `0` | 空闲重置到第 1 项，0 = 不重置 |
| `ShowAlert` | `0` | 1 = 执行时屏幕提示是第几项（ASCII，避免提示条缺中文字形） |

旧版 v1.0 的 `Sequence` / `TargetMode`、v1.1 的单列表 `ActionCount/ActionN`
都会在缺现代格式时自动迁移成多循环格式（`[Cycle1]`），并立即以新格式写回。

## EGameBinds 速查（`src\gamebinds.inc` 由头文件生成）

| 值 | 含义 |
| --- | --- |
| 86–93 | `GB_SquadMarkerPlaceWorld1..8` 团队标记 1–8（箭头/圆圈/爱心/方块/星星/旋涡/三角/叉） |
| 94–101 | `GB_SquadMarkerSetAgent1..8` 标记目标 1–8 |
| 119 / 118 | `GB_SquadMarkerClearAllWorld` / `GB_SquadMarkerClearAllAgent` |
| 131 | `GB_TargetAlert` 标记目标（警告） |
| 6 / 17 / 23 / 27 | 翻滚 / 切武器 / 治疗 / 精英 |

完整 176 项见 `src\gamebinds.inc`（`{ Value, Name, Chinese }`），面板下拉框里可直接筛选。

## 升级 ImGui 的前提

若将来 Nexus 换用新的 ImGui 版本，面板会在打开「设置」时崩溃。此时：
1. 用新版本替换 `src\imgui\`（保持与 Nexus 同一个 commit / 版本号）；
2. 重新 `build.bat` 并覆盖安装。
在没重编之前，删掉 `addons\MarkerCycle.dll` 即可完全恢复（本插件不写游戏内存、不 hook 游戏函数）。
