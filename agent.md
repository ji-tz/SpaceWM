# SpaceWM 开发与测试规范

本文件是本仓库的 **agent / 开发者契约**。任何修改必须遵守下列规则。

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
# 管理员无关；需已安装 Qt 与 MSVC
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cmake -S D:\workspace\SpaceWM -B D:\workspace\SpaceWM\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 && cmake --build D:\workspace\SpaceWM\build --parallel"
```

产物：`build/SpaceWM.exe`（POST_BUILD 自动 `windeployqt` → `lib/` → 拷回 `build/`）。

**`build/` 不入库**；**`lib/` 入库**（可移植运行时：Qt DLL + 插件 + MSVC CRT，约 60MB）。克隆后若缺 `build/` 内 exe，可用本机 Qt 重编；`lib/` 可直接与 `build/SpaceWM.exe` 并列运行。

---

## 2. 测试策略（强制）

### 2.1 规则（必须遵守）

1. **新增功能** → 必须新增对应 test（`tests/test_<feature>.cpp`），并注册到 `tests/CMakeLists.txt` 的 `SPACEWM_TEST_NAMES`。
2. **修改功能** →  
   - 若已有相关 test → **必须同步修改**该 test，使断言匹配新行为。  
   - 若无相关 test → **必须新增** test。  
3. **任意源码修改后** → 必须跑 **全量回归**：

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cmake --build D:\workspace\SpaceWM\build --parallel && ctest --test-dir D:\workspace\SpaceWM\build --output-on-failure"
```

4. **全量测试必须 100% 通过** 才允许 commit。  
5. 禁止只跑“相关子集”后声称完成。  
6. 禁止在无 test 覆盖的情况下合并行为变更。

### 2.2 功能 ↔ 测试对照表

| 功能模块 | 源码 | 测试 |
|----------|------|------|
| Cloak 多后端（DWM / ShowWindow / 只恢复自己藏过的） | `core/CloakController.*` | `tests/test_cloak.cpp` |
| 显示器枚举 / 逻辑 DPI 几何 / physRect | `core/MonitorInfo.*` | `tests/test_monitors.cpp` |
| Per-monitor space 切换、归属、Z 序、截图 seed | `core/SpaceManager.*` | `tests/test_space_manager.cpp` |
| 窗口发现 / 本进程排除 / 最小化排除 | `core/WindowTracker.*` | `tests/test_window_tracker.cpp` |
| 整屏截图 / 壁纸兜底 / 窗口 PrintWindow | `core/ThumbnailCapture.*` | `tests/test_thumbnail.cpp` |
| 全局热键 LL hook | `hotkeys/HotkeyManager.*` | `tests/test_hotkeys.cpp` |
| Space 卡片、竖屏真实宽高比、无 “No preview” | `ui/SpaceCardWidget.*` | `tests/test_space_card.cpp` |
| 单屏 overview 开关 / 空卡片按键 / 快速连开 | `ui/OverviewWindow.*` | `tests/test_overview.cpp` |
| **多屏同时 overview（Mission Control 式）** | `ui/OverviewHost.*` | `tests/test_overview_host.cpp` |
| **窗口拖入 space（顶部 space 条 + 底部窗口预览）** | `ui/WindowPreviewWidget.*` + `OverviewWindow` drop | `tests/test_window_placement.cpp` |
| **悬停/键盘 live 预览桌面 + 底部只显示该 space 窗口 + 合成截图** | `SpaceManager::previewSpace/rebuildSpaceScreenshot` + `OverviewWindow::previewSpace` | `tests/test_space_manager.cpp` · `tests/test_window_placement.cpp` |
| 切换 flash 动画 | `ui/SwitchFlashOverlay.*` | `tests/test_flash_overlay.cpp` |
| 托盘 | `ui/TrayIcon.*` | （可选；UI 弱依赖，暂无独立 test） |
| 主程序装配 `main.cpp` | `src/main.cpp` | （集成路径靠手动 + 上述单测） |

### 2.3 新增 test 的步骤

1. 在 `tests/` 写 `test_<name>.cpp`（`QTEST_MAIN` + `#include "test_<name>.moc"`）。  
2. 在 `tests/CMakeLists.txt` 的 `SPACEWM_TEST_NAMES` 加入 `test_<name>`。  
3. 配置 + 全量构建 + **全量 `ctest`**。  
4. Commit message 写明：功能、新/改了哪个 test、回归结果。

### 2.4 测试质量要求

- 断言行为，不只断言“没崩”。  
- 覆盖：正常路径、边界（0 长度、空集、非法句柄）、回归过的 bug。  
- UI 测试用 `QSignalSpy` / `QTRY_*`，避免盲目 sleep。  
- 测试不得依赖用户桌面长期状态；只创建短命自有窗口。

---

## 3. 架构速览

```text
WindowTracker  → 发现可管理窗口（可见、非本进程、非最小化、非 shell）
SpaceManager   → monitor → space[i] → HWND + 截图 + Z 序
Cloak          → hide 只记录自己藏过的；show 只恢复自己
OverviewHost   → 所有屏各一个 OverviewWindow，同时开关
OverviewWindow → 单屏全屏卡片条（DPI 逻辑坐标 + 物理 SetWindowPos）
HotkeyManager  → WH_KEYBOARD_LL（Ctrl+Alt+←/→/Space/1-4）
```

### 关键不变量

- **绝不**对“本进程未 hide 过”的窗口 `ShowWindow(SW_SHOW)`。  
- **绝不**管理 `IsIconic` 最小化窗口。  
- overview 打开期间 **禁止** BitBlt 整屏（会截到自己）。  
- 混合 DPI：`QWidget` 用逻辑坐标，落地用物理 `SetWindowPos`。

---

## 4. Git

- **`build/` 不提交**（本地生成）；**`lib/` 提交**（可移植运行时）；`.vs/` 等不提交。  
- Commit 前：全量 test 通过。  
- 信息格式：`<type>: <summary>`（fix/feat/test/chore）。

---

## 5. 手动冒烟（发布前）

1. 双屏（含不同 DPI / 竖屏）启动 `build\SpaceWM.exe`。  
2. `Ctrl+Alt+Space` → **所有屏同时**出 overview，各屏自己的 spaces。  
3. 点卡片 / Enter → 只切该屏；约 0.2s 后全部淡出。  
4. Esc → 全部取消。  
5. `Ctrl+Alt+←/→` 只切光标所在屏；最小化窗口不被弹出。
