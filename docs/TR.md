# TR — Trouble Report（根因与修复）

倒序维护。与 [`EH.md`](EH.md) 通过 ID 关联。规范见 [`../AGENT.md`](../AGENT.md) §0。

---

### TR-20260925-05
- **关联 EH**: EH-20260925-05
- **根因**: 缩略图先缩到 **Qt 逻辑像素**，高 DPI 下合成器再拉到物理像素；另有二次 `scaled` 与 GDI `GetDIBits` 转换。
- **修复**: `applyPixmap` 按 `devicePixelRatioF` 缩到物理尺寸并 `setDevicePixelRatio`；strip 传入全分辨率 `windowShot`；`CreateDIBSection` 顶向下捕获。
- **验证**: 12/12 ctest；人工 150% 屏目视。
- **模式标签**: DPI | 预览语义

### TR-20260925-04
- **关联 EH**: EH-20260925-04
- **根因**: 卡片只有 `enterEvent`，无 `leaveEvent`；`m_selected` 停在悬停 space。
- **修复**: `SpaceCardWidget::leaveEvent` → `hoverLeft`；`OverviewWindow` 收到后 `previewSpace(currentIndex)`。
- **验证**: `hoverLeaveRestoresCurrentSpaceStrip`；12/12。
- **模式标签**: 预览语义 | UI布局

### TR-20260925-03
- **关联 EH**: EH-20260925-03
- **根因**: 总览期间无前台窗口回调；已托管在其它 space 的窗口被任务栏激活后不会 re-home；soft hover 也不应被 commit。
- **修复**: `EVENT_SYSTEM_FOREGROUND` → 若 overview 开：归入该屏 **current** space + `closeAll(false)`（保持 origin）。
- **验证**: 手动双屏；own-process 前台不误关总览。
- **模式标签**: 预览语义 | cloak

### TR-20260925-02
- **关联 EH**: EH-20260925-02
- **根因**: overview `setGeometry`/`SetWindowPos` 使用整屏 `rcMonitor`/`geometry`，盖住任务栏。
- **修复**: `monitors::logicalWorkArea`（`rcWork`）；pin 与 open 均用工作区。
- **验证**: `workAreaFitsInsideFullGeometry`；人工总览下任务栏可见。
- **模式标签**: UI布局 | DPI

### TR-20260925-01
- **关联 EH**: EH-20260925-01
- **根因**: LL hook 仅吞 `KEYDOWN`；`KEYUP` 仍进系统，任务视图/VD 可被抬起触发。
- **修复**: 匹配绑定时 down+up 均 `return 1`；down 才 `emitAction`。
- **验证**: System 预设下 `Win+Tab` 不再打开任务视图（人工）；`test_settings` 解析 Win 组合。
- **模式标签**: 热键

### TR-20260924-05
- **关联 EH**: EH-20260924-05
- **根因**: `Stop-Process -Force` = `TerminateProcess`，不跑 `aboutToQuit`/`showAllHidden`；托盘程序无主窗，不带 Force 的 `CloseMainWindow` 也不可靠。
- **修复**: 命名事件 `SpaceWM-quit` + `--quit` + `scripts\stop-spacewm.ps1`（先信号再超时 Force）；退出路径 `showAllHidden`。**不做**跨进程隐藏列表落盘（用户明确否决）。
- **验证**: 构建前 stop 脚本；`test_cloak::showAllHiddenRestoresOnlyOurs`。
- **模式标签**: 构建/退出 | cloak

### TR-20260924-04
- **关联 EH**: EH-20260924-04
- **根因**: 三层断裂——`trackWindow` 已拥有直接 return 不重绘；同屏 `windowMoved` 不 rebuild；总览卡片不监听模型失效。
- **修复**: `refreshWindowAfterUpdate`；同屏 move 也刷新；`spacePreviewInvalidated` + `OverviewWindow` 订阅。
- **验证**: 12/12；副屏新窗后卡片更新。
- **模式标签**: 缓存 | 预览语义

### TR-20260924-03
- **关联 EH**: EH-20260924-03
- **根因**: `placeWindowInSpace` 调用 `previewSpace(目标)` 把 UI/语义绑到错误的「必须切 space」。
- **修复**: 拖放后 `m_selected = stay`（原 currentIndex）；仅刷新源/目标卡片与底部条。
- **验证**: `placeKeepsCurrentSpace`。
- **模式标签**: 预览语义

### TR-20260924-02
- **关联 EH**: EH-20260924-02
- **根因**: 物理 `GetWindowRect` 当逻辑用；tile `scale` 上界 4×；独立 min clamp；strip 在 `show()` 前算可用区；高 DPI 二次缩放。
- **修复**: `logicalWindowSize` + `scale≤1` 相对逻辑尺寸；工作区一致；DPR 显示；全窗 PrintWindow 再缩放。
- **验证**: `windowTilesNeverExceedReal…`、`toLogicalSize…`、DPI 测试。
- **模式标签**: DPI | 预览语义

### TR-20260924-01
- **关联 EH**: EH-20260924-01
- **根因**: 打开时对每 space×每窗反复 PrintWindow 且 host/panel 重复 seed；HEIC 壁纸 `QImage` 失败只落纯色；缺省图链路不完整。
- **修复**: 进入时一次 `buildAllSpacePreviews`+`warmWindowShots`；`TranscodedWallpaper` 回退；卡片三级兜底；内容变化才重绘。
- **验证**: `openBuildsAllSpacePreviews`、`windowShotCachesOnce…`；12/12。
- **模式标签**: 缓存 | 预览语义 | 构建/退出
