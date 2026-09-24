# SpaceWM

Windows 上的**按显示器独立虚拟桌面（Spaces）管理器**，体验对标 macOS 的 Spaces + Mission Control。

C++20 · Qt 6.8.3 Widgets · CMake + Ninja + MSVC · 仅支持 Windows 10/11 x64。

> 本仓库的开发与测试契约见 [`agent.md`](agent.md)（改代码前必读）。

## 功能

- **按显示器独立的 Spaces**：每块屏幕默认 4 个 space，互不干扰；切换只作用于光标所在屏。
- **窗口隐藏不销毁**：非当前 space 的窗口被 cloak（多后端：Immersive View / DWM 属性 / `ShowWindow`），退出或恢复时**只显示本进程藏过的窗口**，绝不碰 shell 窗口。
- **Mission Control 式总览**（`Ctrl+Alt+Space`）：
  - 所有显示器**同时**打开总览，各屏展示自己的 spaces；
  - 顶部 space 条：点击切换、接收拖放；
  - 底部窗口预览：按真实窗口宽高比缩放，可拖到任意 space 卡片完成归位；
  - 悬停 / 键盘移动时**实时预览桌面**（真实切换 + 合成截图刷新卡片）。
- **最大化窗口独占 space**：最大化窗口绑定独立 space，拒收其它窗口混入。
- **全局热键**：`RegisterHotKey` + 原生事件过滤。
- **切换动画**：方向性半透明 flash，不逐帧抓屏。
- **托盘**：状态显示、上/下一切换、打开总览、刷新显示器、退出（安全 uncloak）。
- **混合 DPI 安全**：进程启用 Per-Monitor V2；`QWidget` 用逻辑坐标，落位用物理 `SetWindowPos`；竖屏显示器预览保持真实宽高比。
- **单实例**：`QSharedMemory` 防止重复挂钩热键。
- **可移植运行时**：`lib/` 已入库（Qt DLL + 插件 + MSVC CRT），克隆即可运行，无需本机安装 Qt。

## 快捷键

| 快捷键 | 动作 |
|--------|------|
| `Ctrl+Alt+←` | 光标所在屏切上一个 space |
| `Ctrl+Alt+→` | 光标所在屏切下一个 space |
| `Ctrl+Alt+Space` | 打开 / 关闭总览（所有屏同时） |
| `Ctrl+Alt+1` ~ `Ctrl+Alt+4` | 跳转到光标所在屏的第 N 个 space |

总览内：单击卡片 / `Enter` 切换该屏，`Esc` 取消（恢复进入总览前的 space），悬停卡片实时预览，拖动底部窗口预览到卡片可移动窗口。

## 构建

依赖：Visual Studio（MSVC x64）、CMake ≥ 3.21、Ninja、Qt 6.8.3（`msvc2022_64`）。

```powershell
# 在已安装 VS 的机器上（路径按本机调整）
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 && cmake --build build --parallel"
```

产物：`build/SpaceWM.exe`。POST_BUILD 自动执行 `windeployqt` → 暂存到 `lib/` → 拷回 `build/`。

- **`build/` 不入库**（本地生成）；**`lib/` 入库**（可移植运行时，约 60MB）。
- 克隆后需本机 Qt 编译出 `build/SpaceWM.exe`；运行时依赖已在 `lib/`，与 exe 并列即可启动，无需再手动部署 Qt DLL。

## 测试

11 个 Qt Test 可执行文件，通过 CTest 统一调度：

```powershell
# 构建 + 全量回归（agent.md 要求：任何源码修改后必须全量通过）
cmd /c "`"$vcvars`" && cmake --build build --parallel && ctest --test-dir build --output-on-failure"

# 或使用封装脚本
.\scripts\run-tests.ps1
```

| 测试 | 覆盖 |
|------|------|
| `test_cloak` | 多后端隐藏 / 只恢复自己藏过的窗口 |
| `test_monitors` | 显示器枚举、逻辑 DPI 几何、物理 RECT |
| `test_space_manager` | per-monitor 切换、归属、Z 序、截图、独占 space |
| `test_window_tracker` | 窗口发现、本进程/最小化排除 |
| `test_thumbnail` | 整屏截图、壁纸兜底、PrintWindow |
| `test_hotkeys` | 全局热键注册与分发 |
| `test_space_card` | 卡片宽高比、紧凑模式、拖放 |
| `test_overview` | 单屏总览开关、按键、快速连开 |
| `test_overview_host` | 多屏同时总览 |
| `test_window_placement` | 拖入 space、live 预览、底部预览比例 |
| `test_flash_overlay` | 切换 flash 动画 |

新增 / 修改功能必须同步测试，详见 [`agent.md` §2](agent.md)。

可选：在 [Windows Sandbox](scripts/enable-windows-sandbox.ps1) 中跑隔离回归，避免测试触碰宿主桌面状态。

## 架构

```text
WindowTracker  → 发现可管理窗口（可见、非本进程、非最小化、非 shell）
SpaceManager   → monitor → space[i] → HWND + 截图 + Z 序 + 独占绑定
Cloak          → hide 只记录自己藏过的；show 只恢复自己
OverviewHost   → 每屏一个 OverviewWindow，同时开关（Mission Control）
OverviewWindow → 单屏：顶部 space 条 + 底部窗口预览（DPI 逻辑坐标 + 物理 SetWindowPos）
HotkeyManager  → RegisterHotKey + WM_HOTKEY（Ctrl+Alt+←/→/Space/1-4）
ThumbnailCapture → PrintWindow / BitBlt / 壁纸兜底
SwitchFlashOverlay → 切换方向性闪光
TrayIcon       → 托盘状态与菜单
```

### 关键不变量

- **绝不**对本进程未 hide 过的窗口调用 `ShowWindow(SW_SHOW)`。
- **绝不**管理 `IsIconic` 最小化窗口。
- 总览打开期间**禁止** BitBlt 整屏（会截到自己）；截图改用壁纸 + 窗口 PrintWindow 合成。
- 混合 DPI：`QWidget` 用逻辑坐标，落地用物理 `SetWindowPos`。

## 目录结构

```text
SpaceWM/
├── agent.md              # 开发与测试契约（必读）
├── CMakeLists.txt        # 主构建：spacewm_core 静态库 + SpaceWM.exe + 测试
├── lib/                  # 可移植运行时（Qt DLL + 插件 + CRT，已入库）
├── cmake/CopyCrt.cmake   # 拷贝 MSVC CRT 到 lib/
├── scripts/              # 测试脚本、Windows Sandbox 启用
├── src/
│   ├── main.cpp          # 装配：DPI、单实例、信号连接、热键、托盘
│   ├── core/             # SpaceManager / Cloak / WindowTracker / Monitor / Thumbnail
│   ├── hotkeys/          # HotkeyManager
│   └── ui/               # OverviewHost / OverviewWindow / SpaceCard /
│                         # WindowPreview / SwitchFlash / TrayIcon
└── tests/                # Qt Test + CTest（每功能域一个二进制）
```

## 手动冒烟（发布前）

1. 双屏（含不同 DPI / 竖屏）启动 `build\SpaceWM.exe`。
2. `Ctrl+Alt+Space` → 所有屏同时出总览，各屏展示自己的 spaces。
3. 点卡片 / `Enter` → 只切该屏；约 0.2s 后全部淡出。
4. `Esc` → 全部取消并恢复原 space。
5. `Ctrl+Alt+←/→` 只切光标所在屏；最小化窗口不被弹出。
