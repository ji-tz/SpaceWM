# SpaceWM 开发与测试规范

本文件是本仓库的 **agent / 开发者契约**（原 `agent.md`）。任何修改必须遵守下列规则。

**如何被加载：** 本文件**不会**由 MiMoCode 每轮自动注入。生效方式：
1. 项目 `MEMORY.md` Rules 写明「讨论问题 / 改代码前必读 `SpaceWM/AGENT.md`」；
2. 仓库根目录 `AGENTS.md` 作为常见 agent 约定的入口，内容指向本文件；
3. 人工或会话中显式要求阅读。

---

## 0. TR / EH 问题记录体系（强制）

讨论、修复任何缺陷时，**先查 EH，再结合 TR 归因**；修完必须回写。

### 0.1 术语

| 缩写 | 全称 | 含义 | 落盘位置 |
|------|------|------|----------|
| **EH** | Error History | **问题现象**流水：何时、怎么坏的、影响范围 | [`docs/EH.md`](docs/EH.md) |
| **TR** | Trouble Report | **根因 + 修复 + 验证**报告，一条 EH 可对应 0..n 条 TR | [`docs/TR.md`](docs/TR.md) |

### 0.2 何时写

| 事件 | 动作 |
|------|------|
| 用户报障 / 测试失败 / 自己发现异常 | 在 `docs/EH.md` **顶部**追加一条 EH（新 ID），状态 `open` |
| 定位到原因并完成修复 | 在 `docs/TR.md` **顶部**追加 TR，写回对应 EH 的 `TR` 与状态 `fixed` |
| 仅澄清、无代码变更 | 可只更新 EH 状态为 `wontfix` / `by-design`，并简短注释 |

### 0.3 讨论问题时的固定流程

1. **先读** `docs/EH.md` 最近条目（倒序，至少近 10 条或主题相关的全部）。
2. **再读** 对应或历史相似的 `docs/TR.md`，提取**同类根因模式**（DPI、cloak 生命周期、缓存失效、软/硬预览边界等）。
3. **结合 TR 分析**当前现象：是已知模式复发，还是新问题。
4. 修复后按 0.2 回写；commit message 可引用 `EH-…` / `TR-…`。

### 0.4 ID 与模板

- ID：`EH-YYYYMMDD-NN`、`TR-YYYYMMDD-NN`（同日递增，两位序号）。
- 日期用本地日 `YYYYMMDD`。

**EH 模板：**

```markdown
### EH-20260925-01
- **现象**:
- **复现**:
- **环境**:
- **影响**:
- **TR**: TR-20260925-01
- **状态**: open | fixed | wontfix | by-design
```

**TR 模板：**

```markdown
### TR-20260925-01
- **关联 EH**: EH-20260925-01
- **根因**:
- **修复**:
- **验证**:（ctest / 手动步骤）
- **模式标签**: DPI | cloak | 缓存 | 预览语义 | 热键 | 生命周期 | 构建/退出 | UI布局
```

**模式标签**用于跨 TR 检索同类问题；新 TR 必须带至少一个标签。

---

## 1. 技术栈与构建

| 项 | 值 |
|----|-----|
| 语言 | C++20（**不要用 PyQt**） |
| UI | Qt 6.8.3 Widgets |
| 构建 | CMake + Ninja + MSVC |
| 测试 | Qt Test + CTest |
| 平台 | Windows 10/11 x64 |

### 构建

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cmake -S C:\Users\jtz18\workspace\SpaceWM -B C:\Users\jtz18\workspace\SpaceWM\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 && cmake --build C:\Users\jtz18\workspace\SpaceWM\build --parallel"
```

产物：`build/SpaceWM.exe`（POST_BUILD 自动 `windeployqt` → `lib/` → 拷回 `build/`）。

**`build/` 不入库**；**`lib/` 入库**（可移植运行时：Qt DLL + 插件 + MSVC CRT，约 60MB）。

**重建 exe 前：** 用 `scripts\stop-spacewm.ps1` 或 `SpaceWM.exe --quit`（事件 `SpaceWM-quit` → `showAllHidden`）；勿默认 `Stop-Process -Force`。

---

## 2. 测试策略（强制）

### 2.1 规则（必须遵守）

1. **新增功能** → 必须新增对应 test（`tests/test_<feature>.cpp`），并注册到 `tests/CMakeLists.txt` 的 `SPACEWM_TEST_NAMES`。
2. **修改功能** → 已有 test 必须同步改；无 test 必须新增。
3. **任意源码修改后** → 全量回归：

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cmake --build C:\Users\jtz18\workspace\SpaceWM\build --parallel && ctest --test-dir C:\Users\jtz18\workspace\SpaceWM\build --output-on-failure"
```

4. **全量测试必须 100% 通过** 才允许 commit。  
5. 禁止只跑“相关子集”后声称完成。  
6. 禁止在无 test 覆盖的情况下合并行为变更。

### 2.2 功能 ↔ 测试对照表

| 功能模块 | 源码 | 测试 | 覆盖 |
|----------|------|------|------|
| Cloak、showAllHidden | `core/window/CloakController.*` | `test_cloak` | 有 |
| 显示器 / 逻辑 DPI / **work area** | `core/monitor/MonitorInfo.*` | `test_monitors` | 有 |
| Space 切换 / 独占 / 渲染预览 / Z 序 / 源刷新 | `core/space/SpaceManager.*` | `test_space_manager` | 有 |
| 窗口发现规则 | `core/window/WindowTracker.*` | `test_window_tracker` | 有；**无** FOREGROUND 专项 |
| 捕获 / windowShot 缓存 / wallpaperFilled | `core/capture/ThumbnailCapture.*` | `test_thumbnail` | 有 |
| 热键解析 / System 预设 / 自启 | `HotkeyManager` + `AppSettings` | `test_hotkeys` · `test_settings` | 有；**LL 吞键→系统 VD** 仅人工 |
| Space 卡片 | `ui/preview/SpaceCardWidget.*` | `test_space_card` | 有 |
| 单屏 / 多屏 overview | `OverviewWindow` / `OverviewHost` | `test_overview` · `test_overview_host` | 有 |
| **软预览 / 悬停离开 / 拖放停原 space / 全量预览 / tile DPI** | `OverviewWindow` 等 | `test_window_placement` | 有 |
| **space 增删排序（+ / × / 拖动重排）** | `SpaceManager::{add,remove,move}Space` + `AddSpaceButton` + `SpaceCardWidget` | `test_window_placement` · `test_space_card` | 有 |
| **悬停离开卡片不重置；外缘/整面板 leave 才回 current** | `OverviewWindow::{eventFilter,leaveEvent}` | `test_window_placement` | 有 |
| 拖拽热点 | `mapPressToHotSpot` | `test_window_placement` | 有；**ghost 80%** 未断言 |
| 切换 flash | `SwitchFlashOverlay` | `test_flash_overlay` | 有 |
| 托盘 | `TrayIcon` | — | **无**（弱依赖） |
| 设置对话框 UI | `SettingsDialog` | — | **无**（仅测数据层） |
| **main 装配**（`--quit`、前台关总览、任务栏 re-home） | `main.cpp` | — | **无单测**；手动冒烟 §5 |
| **spacePreviewInvalidated→卡片** | 信号订阅 | 间接 | **无直接测试** |
| `stop-spacewm.ps1` | 脚本 | — | **无** |

### 2.3 新增 test 的步骤

1. `tests/test_<name>.cpp`（`QTEST_MAIN` + `.moc`）。  
2. 注册到 `SPACEWM_TEST_NAMES`。  
3. 全量构建 + **全量 `ctest`**。  
4. Commit message 写明功能、test、回归结果。

### 2.4 测试质量要求

- 断言行为，不只断言“没崩”。  
- 覆盖正常路径、边界、回归过的 bug。  
- UI 用 `QSignalSpy` / `QTRY_*`；测试只创建短命自有窗口。

---

## 3. 架构速览

```text
WindowTracker  → 窗口发现；EVENT_SYSTEM_FOREGROUND → 前台
SpaceManager   → monitor → space[i] → HWND + 渲染截图 + Z 序
Cloak          → hide 只记录自己藏过的；show 只恢复自己
OverviewHost   → 每屏一个 OverviewWindow，同时开关
OverviewWindow → 工作区卡片条（逻辑坐标 + 物理 SetWindowPos）
HotkeyManager  → WH_KEYBOARD_LL（匹配绑定 down+up 吞掉）
```

### 关键不变量

- **绝不**对“本进程未 hide 过”的窗口 `ShowWindow(SW_SHOW)`。  
- **绝不**管理 `IsIconic` 最小化窗口。  
- overview 打开期间 **禁止** BitBlt 整屏。  
- 混合 DPI：逻辑坐标给 QWidget，物理 `SetWindowPos`。  
- **无跨进程隐藏列表落盘**；仅温和退出 `showAllHidden()`。

---

## 4. Git

- **`build/` 不提交**；**`lib/` 提交**。  
- Commit 前：全量 test 通过。  
- 信息格式：`<type>: <summary>`（fix/feat/test/chore）。

---

## 5. 手动冒烟（发布前）

1. 双屏（含不同 DPI / 竖屏）启动 `build\SpaceWM.exe`。  
2. `Ctrl+Alt+Space` → 所有屏同时出 overview；**任务栏仍可见**。  
3. 悬停 space → 底部为该 space 窗口；移开 → 回到 **current** space 窗口。  
4. 点卡片 / Enter → 只切该屏；Esc 取消。  
5. `Ctrl+Alt+←/→` 只切光标所在屏；最小化窗口不被弹出。  
6. System 热键预设：`Win+Tab` / `Ctrl+Win+←/→` **不**打开系统任务视图/虚拟桌面。  
7. overview 打开时点任务栏程序 → 关总览、窗口落在 **当前** space。  
8. 托盘 Quit → 本进程 hide 过的窗口全部恢复可见。
