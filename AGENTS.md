# SpaceWM 开发与测试规范

本文件是本仓库的 **agent / 开发者契约**（曾用名 `agent.md`、`AGENT.md`，均已废弃）。任何修改必须遵守下列规则。

**如何被加载：** 本文件**不会**由 MiMoCode 每轮自动注入。生效方式：
1. 项目 `MEMORY.md` Rules 写明「讨论问题 / 改代码前必读 `SpaceWM/AGENTS.md`」；
2. 许多 agent 工具约定读取仓库根目录 **`AGENTS.md`**（本文件）；
3. 人工或会话中显式要求阅读。

---

## 0. TR / EH 运行日志（第三方 spdlog）

用户约定的缩写（**不是** markdown 问题单）：

| 缩写 | 含义 | 文件 | 级别 | 库 |
|------|------|------|------|-----|
| **TR** | Trace：软件**运行时**日志 | `<projectRoot>/TR/trace.log` | info+ | [spdlog](https://github.com/gabime/spdlog) |
| **EH** | Error：软件**错误 / 崩溃**日志 | `<projectRoot>/EH/error.log` | warn+（含 critical/crash） | 同上 |

- **默认根目录 = 仓库根**（从 exe 上溯找 `AGENTS.md` + `CMakeLists.txt` + `src/`）；`spacelog::init(dir)` 覆盖时在 `dir` 下建 `TR/`、`EH/`。
- **禁止**再维护 `docs/EH.md` / `docs/TR.md` 这类手工问题流水；排障时读上述两个 log。
- 代码入口：`src/core/log/Log.h`（`spacelog::init/info/trace/warn/error/critical/installCrashHandlers`）。
- `main` 启动：`spacelog::init()` + `installCrashHandlers()`；正常退出 `shutdown()`。
- **日志不入库**（`.gitignore` 忽略 `/TR/`、`/EH/`、`*.log`、`logs/`）。

### 0.1 排障时的固定流程

1. **先读** 最近的 `EH/error.log`，再读 `TR/trace.log` 对应时间窗。
2. 对照代码与既有架构不变量，判断是已知模式还是新问题。
3. 修复后在 commit 中写明根因；**不要**再往 markdown 里写 EH/TR 条目。

---

## 1. 技术栈与构建

| 项 | 值 |
|----|-----|
| 语言 | C++20（**不要用 PyQt**） |
| UI | Qt 6.8.3 Widgets |
| 构建 | CMake + Ninja + MSVC |
| 测试 | Qt Test + CTest |
| 日志 | **spdlog**（FetchContent `v1.15.0`） |
| 平台 | Windows 10/11 x64 |

### 构建

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cmake -S C:\Users\jtz18\workspace\SpaceWM -B C:\Users\jtz18\workspace\SpaceWM\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 && cmake --build C:\Users\jtz18\workspace\SpaceWM\build --parallel"
```

产物：`build/SpaceWM.exe`（POST_BUILD `windeployqt` → `lib/` → 拷回 `build/`）。

**`build/` 不入库**；**`lib/` 入库**。重建前：`scripts\stop-spacewm.ps1` 或 `SpaceWM.exe --quit`（事件 `SpaceWM-quit` → `showAllHidden`）。

---

## 2. 测试策略（强制）

### 2.1 规则

1. **新增功能** → 必须新增 test，注册到 `tests/CMakeLists.txt` 的 `SPACEWM_TEST_NAMES`。
2. **修改功能** → 已有 test 必须同步改；无 test 必须新增。
3. **任意源码修改后** → 全量 `ctest`，**100% 通过**才允许 commit。
4. 禁止只跑子集后声称完成；禁止无 test 覆盖的无行为变更合并。

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cmake --build C:\Users\jtz18\workspace\SpaceWM\build --parallel && ctest --test-dir C:\Users\jtz18\workspace\SpaceWM\build --output-on-failure"
```

### 2.2 功能 ↔ 测试（摘要）

当前 **13** 个测试二进制：`cloak` / `monitors` / `space_manager` / `window_tracker` / `thumbnail` / `hotkeys` / `settings` / `log` / `space_card` / `overview` / `overview_host` / `window_placement` / `flash_overlay`。

| 能力（本会话 + 近期 commit） | 测试 | 状态 |
|------------------------------|------|------|
| spdlog TR/EH（`TR/trace.log` / `EH/error.log`） | `test_log` | 有 |
| space add/remove/move（+ / × / 拖动排序） | `test_window_placement` | 有 |
| × 悬停 2s 显示、点击删除、不误触 activated | `test_space_card` | 有 |
| **+ 按钮点击** `addRequested` | `test_window_placement` | 有 |
| **+ 拖入窗口新建 space** | 模型：`addSpace`+`assign`；UI drop | **模型有；UI drop 弱** |
| 软预览 / 外缘 leave 才回 current | `test_window_placement` | 有 |
| 预览缓存 / 打开全量 space 图 | `test_window_placement` · `test_thumbnail` | 有 |
| work area / logical DPI / DWM 可见框 | `test_monitors` | 有 |
| System 热键解析 / 自启 / Win 键策略 | `test_settings` · `test_hotkeys` | 有 |
| showAllHidden 退出恢复 | `test_cloak` | 有 |
| 拖拽热点 mapPressToHotSpot；点击 tile → activated | `test_window_placement` | 有 |
| 渲染 Z 序 / 源截图刷新 | `test_space_manager` | 有 |
| 托盘 UI / SettingsDialog 交互 / `main` 装配 / LL 吞键端到端 / drag ghost 80% | — | **无单测**；手动冒烟 §5 |

已知缺口优先补：`main` 前台 re-home、`+` 拖入 UI、`windowForeground`、`spacePreviewInvalidated` 直连。

### 2.3 新增 test

1. `tests/test_<name>.cpp`（`QTEST_MAIN` + `.moc`）→ `SPACEWM_TEST_NAMES` → 全量 ctest → commit 写明 test。

### 2.4 质量

断言行为；覆盖边界与回归；UI 用 `QSignalSpy`/`QTRY_*`；只创建短命自有窗口。

---

## 3. 架构速览

```text
WindowTracker  → 窗口发现；EVENT_SYSTEM_FOREGROUND → 前台
SpaceManager   → monitor → space[i] → HWND + 渲染截图 + Z 序 + add/remove/move space
Cloak          → hide 只记录自己藏过的；show 只恢复自己
OverviewHost   → 每屏一个 OverviewWindow，同时开关
OverviewWindow → 工作区卡片条（逻辑坐标 + 物理 SetWindowPos）；+ / × / 拖动排序
HotkeyManager  → WH_KEYBOARD_LL（匹配绑定 down+up 吞掉）
spacelog       → spdlog → TR/trace.log + EH/error.log
```

### 关键不变量

- **绝不**对“本进程未 hide 过”的窗口 `ShowWindow(SW_SHOW)`。
- **绝不**管理 `IsIconic` 最小化窗口。
- overview 打开期间 **禁止** BitBlt 整屏。
- 混合 DPI：逻辑坐标给 QWidget，物理 `SetWindowPos`。
- **无跨进程隐藏列表落盘**；仅温和退出 `showAllHidden()`。
- **space 卡片预览一律渲染**（壁纸 + 窗口 PrintWindow），禁止 BitBlt。

---

## 4. Git

- **`build/` 不提交**；**`lib/` 提交**；日志 **不提交**。
- Commit 前：全量 test 通过。格式：`<type>: <summary>`。

---

## 5. 手动冒烟（发布前）

1. 双屏（含不同 DPI）启动 `build\SpaceWM.exe`；确认 `TR/trace.log` 有启动行。
2. `Ctrl+Alt+Space` → 所有屏 overview；**任务栏仍可见**。
3. 悬停 space → 底部为该 space 窗口；移到面板外缘 → 回 current。
4. 点卡片 / Enter 切换；**+** 加 space；有窗口卡片 **×** 删除；拖卡片排序。
5. `Ctrl+Alt+←/→`；System 预设吞 `Win+Tab` / `Ctrl+Win+←/→`。
6. overview 开时点任务栏程序 → 关总览、窗口落当前 space。
7. 托盘 Quit → 本进程 hide 过的窗口全部恢复；`EH/error.log` 无意外 critical。
