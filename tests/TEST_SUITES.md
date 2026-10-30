# 测试分类与运行

日常每次 commit 前运行仿真套件；推送前运行实模式套件，集中修复发现的问题并重新验证，通过后再推送。用户明确要求时也可提前运行实模式测试。

CTest 标签提供两份入口：

| 分类 | 命令 | 内容和影响 |
| --- | --- | --- |
| 仿真 | `ctest --test-dir build -L simulation --output-on-failure` | 日志测试，以及若干混合程序中经检查的逻辑与控件事件函数；使用临时文件和独立注册表子树，不操作真实桌面。 |
| 实模式 | `ctest --test-dir build -L real --output-on-failure` | 其余 19 个完整测试程序，可能创建或遮盖窗口、改变前台窗口、移动鼠标、安装热键钩子或运行打包脚本；执行前确认桌面可供测试使用。 |

仿真包含 `test_win_preview_events`（Win/Tab 原始事件顺序与会话清理）、`test_log`，以及 `simulation_hotkeys`、`simulation_settings`、`simulation_monitors`、`simulation_thumbnail`、`simulation_window_tracker`、`simulation_flash_overlay`、`simulation_space_card`、`simulation_window_placement`。这些仅覆盖各程序中列入 CMake 的函数，不代表对应实模式测试已全部仿真。实模式包含：

- `test_cloak`、`test_monitors`、`test_space_manager`、`test_window_tracker`、`test_thumbnail`
- `test_hotkeys`、`test_settings`、`test_space_card`、`test_overview`、`test_overview_spaces`、`test_overview_staging`、`test_overview_host`、`test_window_placement`
- `test_flash_overlay`、`test_space_switch_animation`、`test_taskbar_switch`、`test_tray`、`test_package`、`test_integration_flow`

现有程序按功能而不是运行环境组织，许多混合了逻辑测试和真实系统操作。为保留覆盖，实模式运行完整程序，因此其中的热键和设置逻辑函数也会重复运行。未逐函数核实为无干扰的测试保守归入实模式；这不意味着它们每个函数都操作桌面。增加仿真函数前应检查测试及其初始化、清理路径，确保不干扰用户桌面。

`test_integration_flow` 仍须设置 `SPACEWM_IT=1` 才执行；原生任务栏点击仍须设置 `SPACEWM_TASKBAR_IT=1`。两者都需要退出运行中的 SpaceWM；任务栏测试还需要解锁桌面。未设置时相应测试会跳过，普通实模式命令的通过不能证明这些可选路径已验证。详细前置条件、完整集成步骤及推送前要求见仓库根目录 `AGENTS.md` §2.5、§4、§5。默认不带标签的 `ctest` 仍会运行两类测试，不适合当作无干扰套件。

可用 `ctest --test-dir build -L simulation -N` 和 `ctest --test-dir build -L real -N` 查看当前注册项。Qt Test 详细结果位于 `build/tests/*.out.txt`。

当前仿真缺口：完整 cloak 状态转换、SpaceManager 的窗口归属和跨屏迁移、overview 多屏准备及生命周期、截图缓存和失败处理、任务栏输入桥时序、托盘、打包及实模式集成流程，尚未建立对应的可隔离替身和完整行为断言。现有仿真只覆盖部分 DPI 换算、非法窗口、窗口分类、动画几何、卡片交互、预览控件、热键、设置和日志；不能把套件通过视为实模式覆盖完整。
