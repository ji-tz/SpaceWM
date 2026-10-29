# 测试分类与运行

CTest 标签提供两份入口：

| 分类 | 命令 | 内容和影响 |
| --- | --- | --- |
| 仿真 | `ctest --test-dir build -L simulation --output-on-failure` | 日志测试，以及热键、设置程序中经检查的逻辑函数；使用临时文件和独立注册表子树，不操作真实桌面。 |
| 实模式 | `ctest --test-dir build -L real --output-on-failure` | 其余 19 个完整测试程序，可能创建或遮盖窗口、改变前台窗口、移动鼠标、安装热键钩子或运行打包脚本；执行前确认桌面可供测试使用。 |

仿真包含 `test_log`、`simulation_hotkeys`、`simulation_settings`。实模式包含：

- `test_cloak`、`test_monitors`、`test_space_manager`、`test_window_tracker`、`test_thumbnail`
- `test_hotkeys`、`test_settings`、`test_space_card`、`test_overview`、`test_overview_spaces`、`test_overview_staging`、`test_overview_host`、`test_window_placement`
- `test_flash_overlay`、`test_space_switch_animation`、`test_taskbar_switch`、`test_tray`、`test_package`、`test_integration_flow`

现有程序按功能而不是运行环境组织，许多混合了逻辑测试和真实系统操作。为保留覆盖，实模式运行完整程序，因此其中的热键和设置逻辑函数也会重复运行。未逐函数核实为无干扰的测试保守归入实模式；这不意味着它们每个函数都操作桌面。增加仿真函数前应检查测试及其初始化、清理路径，确保不干扰用户桌面。

`test_integration_flow` 仍须设置 `SPACEWM_IT=1` 才执行；原生任务栏点击仍须设置 `SPACEWM_TASKBAR_IT=1`。两者都需要退出运行中的 SpaceWM；任务栏测试还需要解锁桌面。未设置时相应测试会跳过，普通实模式命令的通过不能证明这些可选路径已验证。详细前置条件、完整集成步骤及推送前要求见仓库根目录 `AGENTS.md` §2.5、§4、§5。默认不带标签的 `ctest` 仍会运行两类测试，不适合当作无干扰套件。

可用 `ctest --test-dir build -L simulation -N` 和 `ctest --test-dir build -L real -N` 查看当前注册项。Qt Test 详细结果位于 `build/tests/*.out.txt`。
