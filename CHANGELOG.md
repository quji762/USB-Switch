# 变更记录 / Changelog

版本号在 1.0.0 发布时重设。本文档按时间倒序记录：最上面是尚未发布的变更，其后是 1.0.0，最后是重设版本之前的开发记录（1.5.1 至 1.0.1，按当时的中文记录原文保留）。当前版本的行为以 [README](README.md) 为准。

The version number was reset at the 1.0.0 release. This file lists changes newest first: unreleased work, then 1.0.0, then the development records from before the reset (1.5.1 through 1.0.1, kept as originally written in Chinese). Current behavior is documented in [README](README.md).

## 未发布 / Unreleased

- **退卡跳过默认不挂卷的分区：** GPT 的 EFI 系统分区和 LDM 元数据分区不再计入“必须对应到卷”的分区（与 Microsoft 保留分区、MBR 扩展容器分区相同）；用户给这类分区加了盘符时，出现的卷仍被接受。
  **Card eject skips partitions that carry no volume by default:** the EFI system partition and the LDM metadata partition of a GPT disk no longer count as partitions that must map to a volume (same as the Microsoft reserved partition and MBR extended-partition containers); if the user gave such a partition a drive letter, the volume that appears is still accepted.
- **重连需要重启时给出明确提示：** 重启请求返回 `ERROR_SUCCESS_REBOOT_REQUIRED`（3010）时，提示请求已排入队列、将在系统重启后生效，不再按失败报告，也不再空等十秒轮询。
  **A queued reconnect is reported as such:** when the restart request returns `ERROR_SUCCESS_REBOOT_REQUIRED` (3010), the tool says the request is queued for the next reboot instead of reporting a failure and polling for ten seconds.
- **设备自行重现时给出提示：** 授权期间设备已自行恢复连接（错误 183）时，提示无需重新连接，并丢弃失效的重连记录。
  **A device that came back on its own is reported as such:** if the disk reappeared while the elevation prompt was up (error 183), the tool says no reconnect is needed and drops the stale record.
- **删除 `--minimized` 参数：** 该参数此前被接受但没有任何效果；现在无法识别的参数按用法错误处理（退出码 2）。
  **The `--minimized` switch is gone:** it used to be accepted with no effect; an unrecognized switch is now a usage error (exit code 2).
- **隐藏设备不再触发通知：** 插入/断开通知卡片和托盘提示中的设备计数按同一套隐藏名单过滤，与菜单保持一致。
  **Hidden devices no longer notify:** connect/disconnect cards and the device count in the tray tooltip follow the same hidden list as the menu.
- **对话框跨显示器迁移时按新 DPI 重建布局：** 移到另一 DPI 的显示器后，对话框按系统建议的矩形重摆，控件位置和对话框字体按创建时快照等比重建，自绘几何随之更新。
  **Dialogs relayout at the new DPI:** after a move to a monitor with another scale, the dialog takes the system-suggested rectangle, control positions and the dialog font are rebuilt from the creation-time snapshot at the new ratio, and the owner-drawn geometry follows.
- **日志文件有上限：** 抢不到主日志文件名的进程写出的带 PID 的日志，每个家族最多保留 8 个。
  **Per-process logs are bounded:** at most eight PID-tagged log files are kept per log family for processes that lost the race for the shared log name.

## 1.0.0

第一个使用重设后版本号的版本，功能涵盖下列全部重设前的开发记录；各项行为的当前细节见 [README](README.md)。

The first release under the reset version number. Its feature set covers all of the pre-reset development records below; see [README](README.md) for current behavior.

## 重设版本前的开发记录 / Development history before the 1.0.0 reset

### 1.5.1：托盘菜单与窗口布局

日常弹出和退卡操作保留在托盘主菜单。展开“管理设备”可设置别名、隐藏设备或恢复显示。设置窗口把启动、通知和弹出确认放在一组，语言和快捷键放在另一组；选择“保存”后生效。

### 1.5.0：隐藏设备和识别空卡槽

托盘菜单中选择 **管理设备 → 隐藏设备 → 设备名**，该设备便从主列表、设置别名和退卡列表中隐藏。在同一子菜单选择 **重新显示设备** 可恢复；设备已断开时也可以按上次显示的名称恢复。设置按当前用户和设备实例 ID 保存，重启后仍有效，不按盘符匹配。隐藏仅影响托盘菜单，不会卸载设备，也不会改变命令行清单。托盘提示里的设备计数和插入/断开通知卡片按同一套隐藏名单过滤，保存隐藏或恢复后计数立即更新。

对报告支持可移动介质的设备，程序向驱动查询介质是否就绪。明确报告无介质的槽位显示“空卡槽”；仅报告未就绪的设备显示“无可用介质（空槽或未就绪）”。两种情况下菜单项均置灰，也不提供退卡。部分读卡器在卡片已退出但尚未取走时与物理空槽返回同一状态。程序不会因容量未知、没有卷标或没有盘符而断言空槽。`--list --json` 的 `mediaState` 分别为 `present`、`empty`、`not_ready` 或 `unknown`。

### 1.4.0：查看阻止弹出的程序打开了哪些文件

如果 Windows 拒绝弹出并报告了关联程序，在托盘提示中选择 **查看打开的文件**。程序会按 PID 列出当下仍在目标盘上打开的文件或文件夹；保存并关闭相关文档后可手动重试。这个列表是排查线索，不能证明每个列出的文件都是阻塞原因。权限不足、进程已退出、仅有内存映射或文件已关闭时，可能没有路径。文件路径不写入应用日志，也不会自动提权或关闭文件。

命令行使用 `usb-switch --eject F: --files --json`。这是实际弹出请求；仅在 Windows 拒绝后才检查文件。结果中的 `fileInspection` 按进程给出 `pid`、`name`、`paths` 和 `status`。状态包括 `inspected`（已检查）、`partial`（结果不完整）、`access_denied`、`timeout`、`process_changed`、`device_unavailable` 和 `unavailable`。检查共享约 3.5 秒时间预算，每个进程最多返回 32 个路径。返回的每个路径都必须落在本设备某个卷的挂载点之下，不符时该进程报 `unavailable`；本设备此刻枚举不出挂载卷时，所有进程都报 `device_unavailable`，不会再启动子进程。没有相关进程时数组为空；普通 `--json` 不查询路径。

### 1.3.1：重新连接刚弹出的设备（实验功能）

用托盘安全弹出设备后，如果还没有拔下，可打开托盘菜单选择“重新连接：设备名”。程序只记住本次运行中最后一次成功弹出的设备；退出程序即清除记录。Windows 必须仍将原节点标记为已准备弹出。若需要管理员权限，可以选择打开系统授权提示，或取消并手动拔插。对于可定位到集线器端口的 USB 存储设备，系统授权提示显示 USB-Switch；程序在授权后重新核对端口上的驱动标识及设备停止状态，仅重新连接对应端口。不能确认端口时，仍只尝试按原节点重启，授权提示同样显示 USB-Switch；授权完成后会重新枚举磁盘，核对原磁盘与目标节点的关系，并在重启前再次检查节点是否仍处于已弹出状态。如果磁盘已恢复在线或身份无法核实，会停止重连。授权命令运行期间不能重复提交。重启命令返回 `ERROR_SUCCESS_REBOOT_REQUIRED`（3010）时按成功处理：SetupApi 只是标记了需要重启，系统会稍后才应用。若重启执行时磁盘已经自行重现（错误 183），程序不再弹错误框，而是丢弃失效的重连记录并刷新列表。其他错误会在托盘显示 Windows 错误及编号；若命令成功但原磁盘十秒内仍未出现在列表中，托盘会提示刷新或手动拔插。此功能尚未包含在 1.2.0 安装包内。

已在 ASolid ESD-USB 实测完整托盘流程：普通权限安全弹出 → 管理员授权 → 原磁盘实例和原卷重新出现，文件目录可读；同一台机器上的双槽读卡器仍可读。1.3.0 在这只 U 盘上返回 3010（当时按错误报告），未能免拔插恢复。

### 1.3.0 测试版：退出存储卡（实验功能）

要换读卡器里的卡，可在托盘选择 **退出存储卡 → 目标设备**，核对卷标、盘符后选“是”。对话框默认选“否”。这个菜单按驱动报告的介质属性列出候选项，普通 U 盘也可能出现；若要拔下整个设备，请用主菜单的安全弹出。此功能尚未包含在 1.2.0 安装包内。

脚本可以使用 `usb-switch --eject-media E: --json`。把 `E:` 换成实际盘符。命令核对分区和卷，尝试锁住全部卷，再向驱动请求退出介质。GPT 的 Microsoft 保留分区和 MBR 的扩展容器永远不会挂卷，不计入“必须对应到卷”的分区；跨多个 extent 或跨磁盘的卷报 `volume_multi_extent`（`ERROR_NOT_SUPPORTED`），不会冒充成运行中发生变化或 IOCTL 失败。如果分区不能全部对应到卷、文件正在使用、权限不足或驱动不支持操作，会返回失败及所在步骤，不会改为停止读卡器。返回码为 0 且状态为 `media_ejected` 表示驱动接受了退卡请求。已用 1.3.0 便携包在一台双槽 USB 读卡器（VID 05E3 / PID 0764，两张 exFAT 卡）上实测：仅退出 D: 后原卷不可读，E: 保持可读；将原卡插回同槽后，D: 恢复原卷并可读取，读卡器未重新连接。这个结果仅覆盖该型号和原卡重插。

### 1.2.0：全局快捷键和设备别名

在其他应用里按 **Ctrl+Alt+U** 可打开设备菜单，方向键选设备，Esc 取消。快捷键本身不会弹出设备。在“设置”中可改为 Ctrl+Alt 加其他字母或关闭；组合被其他程序占用时会提示。打开“管理设备 → 设置设备别名”，选择设备并输入名称；留空保存可恢复原名。别名保存在当前用户配置中，菜单和确认框同时保留原卷标及盘符。别名按 Windows 提供的实例 ID 匹配，盘符变化不会单独改变绑定；如果换接口后 Windows 分配了不同 ID，需要重新命名。

### 1.1.0：弹出占用诊断

弹出被拒绝时，托盘错误框和命令行会同时列出 Windows 为本次设备移除记录的关联程序名和 PID。错误框可用 Ctrl+C 复制，脚本可读 `--eject <目标> --json` 的 `reportedProcesses` 数组。只匹配本次请求期间、属于该磁盘或本次实际停止节点的 Kernel-PnP 225 事件；没有匹配记录或没有权限时，结果为空。这里的文件名是程序名，不是它打开的文档；不会读取命令行、关闭进程或强制解锁文件。

### 1.0.1 更新

修复部分单磁盘 USB 设备可以识别、却无法弹出的问题。设备被占用时保留系统的拒绝原因；关闭占用文件后可重试。此版本也修复了托盘菜单的定位和键盘操作。

若菜单提示“暂时无法读取设备列表”，旧设备不会作为可弹出选项显示。程序会自动重试，也可手动选择“刷新设备列表”。只有扫描成功且列表为空时才显示“没有可移动设备”。
