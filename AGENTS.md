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
5. **新增涉及 overview / space / 窗口管理的功能** → 必须在实模式集成流程
   `tests/test_integration_flow.cpp` 中**追加一个 `stepNN_` 插槽**（编号顺延，
   不改写既有 step）并在 §2.5 的步骤表登记；该流程默认跳过、显式运行（见 §2.5），
   但步骤缺失视为该功能缺少端到端回归覆盖。

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cmake --build C:\Users\jtz18\workspace\SpaceWM\build --parallel && ctest --test-dir C:\Users\jtz18\workspace\SpaceWM\build --output-on-failure"
```

### 2.2 功能 ↔ 测试（摘要）

当前 **15** 个测试二进制：`cloak` / `monitors` / `space_manager` / `window_tracker` / `thumbnail` / `hotkeys` / `settings` / `log` / `space_card` / `overview` / `overview_host` / `window_placement` / `flash_overlay` / `tray` / `integration_flow`。

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
| 遮罩前刷新可见截图（screen fallback）/ warm 只补缺不清缓存 | `test_space_manager` | 有 |
| **实模式 12 步集成流程**（真实应用 + 真实切换/ cloak） | `test_integration_flow` | 有（opt-in §2.5） |
| 确认/退出时**先 cloak 再撤遮罩**（防闪现） | `test_space_manager` · `test_overview_host` | 有 |
| 托盘 UI / SettingsDialog 交互 / `main` 装配 / LL 吞键端到端 / drag ghost 80% | — | **无单测**；手动冒烟 §5 |

已知缺口优先补：`main` 前台 re-home、`+` 拖入 UI、`windowForeground`、`spacePreviewInvalidated` 直连。

### 2.3 新增 test

1. `tests/test_<name>.cpp`（`QTEST_MAIN` + `.moc`）→ `SPACEWM_TEST_NAMES` → 全量 ctest → commit 写明 test。

### 2.4 质量

断言行为；覆盖边界与回归；UI 用 `QSignalSpy`/`QTRY_*`；只创建短命自有窗口。

### 2.5 实模式集成流程（opt-in，不进默认回归）

`tests/test_integration_flow.cpp`：用**真实外部应用**（记事本 / 资源管理器 /
浏览器）跑完整个 overview 生命周期——真实追踪、真实 cloak 切换、真实合成截图；
UI 用 Qt Test 事件驱动。默认 `ctest` 因缺 `SPACEWM_IT` 直接 skip（CI 零成本），
**不需要每次回归**；改动 overview/space 主链路时或发布前显式跑一次：

```powershell
.\build\SpaceWM.exe --quit    # 必须先退出正在运行的实例（会 QSKIP）
$env:SPACEWM_IT = '1'
ctest --test-dir .\build -R test_integration_flow -V
$env:SPACEWM_IT = $null
```

**步骤登记表**（与 `stepNN_` 插槽一一对应；新功能追加行 + 追加插槽，不改写旧步骤）：

| # | 步骤 | 插槽 |
|---|------|------|
| 1 | 打开记事本、资源管理器、浏览器（真实进程） | `step01_launchNotepadExplorerBrowser` |
| 2 | 开启软件：三个窗口全部被追踪进冷启动的唯一 space | `step02_softwareTracksAllThree` |
| 3 | 快捷键打开 preview（测试内等价调用 `openAll`，真实按键链路见 §5） | `step03_openPreview` |
| 4 | 点击 **+** 生成第二个 space | `step04_clickAddCreatesSecondSpace` |
| 5 | 拖动记事本到第二个 space | `step05_dropNotepadOntoSecondSpace` |
| 6 | 点击第二个 space 进入 | `step06_clickSecondSpaceCommitsSwitch` |
| 7 | preview 自动退出（cloak 同步：目标空间可见、其余隐藏） | `step07_overviewAutoExited` |
| 8 | 快捷键再次打开 preview（二次打开不闪关 + 遮罩后全量 uncloak） | `step08_reopenPreview` |
| 9 | 拖动第一个 space 到第二个 space（卡片排序） | `step09_dragFirstSpaceCardOntoSecond` |
| 10 | 顺序交换为 [Space 2, Space 1]，归属/当前 space 跟随 | `step10_spaceOrderSwapped` |
| 11 | 点击第二个 space | `step11_clickSecondSpaceCard` |
| 12 | 进入第二个 space，preview 消失 | `step12_enteredSpaceAndPreviewGone` |

约束：
- **热键注入不可用**（LL 钩子按设计忽略 `LLKHF_INJECTED`）——流程内 open 用与
  热键 lambda 完全相同的 `openAll()` 入口；热键解析/防重在 `test_hotkeys`，
  真实按键链路由 §5 手动冒烟覆盖。
- 运行期间会真实接管桌面（遮罩/切换），结束时 `cleanupTestCase` 恢复一切：
  untrack + `showAllHidden` + 仅对我们启动的窗口发 `WM_CLOSE`（绝不 kill
  `explorer.exe`）。
- 每个 step 结束状态是下一个 step 的前提——追加新 step 时保持这条链可读。

---

## 3. 架构速览

```text
WindowTracker  → 窗口发现；EVENT_SYSTEM_FOREGROUND → 前台
SpaceManager   → monitor → space[i] → HWND + 渲染截图 + Z 序 + add/remove/move space
Cloak          → hide 只记录自己藏过的；show 只恢复自己
OverviewHost   → 每屏一个 OverviewWindow，同时开关
OverviewWindow → 工作区卡片条（逻辑坐标 + 物理 SetWindowPos）；+ / × / 拖动排序
HotkeyManager  → WH_KEYBOARD_LL（**独立钩子线程**；绑定 down+up 吞掉；总览开着时吞 Esc）
spacelog       → spdlog → TR/trace.log + EH/error.log
```

### 关键不变量

- **绝不**对“本进程未 hide 过”的窗口 `ShowWindow(SW_SHOW)`。
- **绝不**管理 `IsIconic` 最小化窗口。
- overview 打开期间 **禁止** BitBlt 整屏。
- 混合 DPI：逻辑坐标给 QWidget，物理 `SetWindowPos`。
- **无跨进程隐藏列表落盘**；仅温和退出 `showAllHidden()`。
- **space 卡片预览一律渲染**（壁纸 + 窗口 PrintWindow），禁止 BitBlt。
- **LL 钩子只装在独立线程上**：主线程阻塞超过 LowLevelHooksTimeout 会被系统
  静默卸钩（所有热键无声死亡且无日志），安装/按键在 TR 有 `LL keyboard hook
  installed` / `hotkey action=` 行可排查。

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
8. overview 开着时按 **Esc**（含焦点被弹回前台窗口时）→ 取消退出；TR 有
   `hotkey action=` 行（热键仍活），无 `hotkey action` = 钩子被卸（查 §3 不变量）。
9. 连续快速开合 overview（打开期间主线程做重截图）→ 热键全程可用。
