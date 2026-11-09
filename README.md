# USB-Switch

Windows 托盘里的 USB 安全弹出工具。选中要拔出的 U 盘、移动硬盘或读卡器，一键安全弹出；Windows 拒绝时，告诉你它为什么拒绝。

A Windows tray tool for safely removing USB storage. Pick the drive, eject it in one click — and when Windows refuses, see the reason it gave.

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

## 系统要求 / Requirements

Windows 10 版本 1607（10.0.14393）或更新版本，包括 Windows 11。可执行文件直接导入 `GetDpiForWindow`，更早的系统连加载都做不到，因此不为它们保留兼容条目。

**English:** Windows 10 version 1607 (build 10.0.14393) or newer, including Windows 11. The executable imports `GetDpiForWindow` directly, so earlier systems cannot load the binary at all.

## 功能 / Features

- **一键安全弹出，拒绝就说清原因** —— 走完整的 PnP 停止流程；系统报告了哪个程序、服务或驱动在占用，就列出来
- **查看谁占着文件** —— 被拒绝时，可按进程列出它当下在目标盘上打开的文件
- **退出存储卡与免拔插重连** —— 只退读卡器里的卡、读卡器保持连接；刚弹出的设备重新上线不用拔插（均为实验功能）
- **插拔通知与设备管理** —— 右下角一张不抢焦点的卡片；别名、隐藏设备、按读卡器分组的设备窗口、空卡槽置灰
- **SMART 健康数据** —— 设备窗口里看温度等读数，可自选显示项
- **中英双语、三种外观、全局快捷键、开机自启、命令行与 JSON 输出**

**English:** one-click safe removal through the full PnP stop flow, with the veto reason Windows reports down to the process, service or driver holding the drive; an on-demand list of the files those processes have open; card-only eject and plug-free reconnect (both experimental); connect/disconnect cards, aliases, hidden devices, a reader-grouped device window and empty-slot detection; SMART readings; Chinese and English; light/dark/system appearance; a global hotkey; run at startup; and a command line with JSON output.

## 安装 / Install

| 版本 | 做法 |
|---|---|
| 安装版 | 运行 `usb-switch-setup-*.exe`。每用户安装到 `%LOCALAPPDATA%\Programs`，不弹 UAC |
| 便携版 | 把 ZIP 解压到电脑本地，双击 `usb-switch.exe` |

**English:** run the setup executable for a per-user install (no UAC prompt), or extract the portable ZIP and run `usb-switch.exe`.

## 快速上手 / Quick start

启动后没有主窗口，图标在任务栏时钟旁；看不到时展开隐藏的托盘图标。

1. 保存并关闭 U 盘上的文件。
2. 点击托盘图标，按卷标和盘符核对设备，选择「点击安全弹出」。
3. 默认会弹确认框，选「是」继续；看到「已安全弹出」再拔出。

**English:** after starting there is no main window — the icon sits next to the clock. Save and close the files on the drive, click the tray icon, match the volume label and drive letter, choose the eject item and confirm. Wait for **Safely removed** before unplugging.

### 退出存储卡 / Ejecting a card

托盘菜单选 **退出存储卡 → 目标设备**，核对卷标和盘符后选「是」（对话框默认选「否」）。要拔下整个设备，请用主菜单的安全弹出。

**English:** choose **Eject card → device** from the tray menu and confirm. To remove the whole device, use the safe-removal item instead.

### 重新连接刚弹出的设备 / Reconnecting

安全弹出后如果还没拔下，托盘菜单会出现「重新连接：设备名」。程序只记住本次运行中最后一次成功弹出的设备，退出程序即清除记录。需要管理员权限时可以选择打开系统授权提示，或取消后手动拔插。

**English:** while the just-ejected device is still plugged in, the tray menu offers **Reconnect**. Only the last successful eject of this run is remembered, and the record clears when the app exits. Elevation is offered when it is needed.

## 设置 / Settings

托盘菜单「设置…」里可以改：界面语言、外观模式（跟随系统/浅色/深色）、全局快捷键、开机自启、插拔通知、弹出前确认、SMART 显示项，以及设备别名与隐藏设备。

设置保存在 `%APPDATA%\USB-Switch\settings.json`，每个键的取值见 [docs/command-line.md](docs/command-line.md#settingsjson)。

**English:** the settings dialog covers language, appearance, the global hotkey, run at startup, notifications, the eject confirmation, which SMART readings to show, and device aliases and hidden devices.

## 命令行 / Command line

```powershell
.\usb-switch.exe --list              # 列出可移动设备
.\usb-switch.exe --eject E:          # 安全弹出
.\usb-switch.exe --eject-media E:    # 退出存储卡
.\usb-switch.exe --settings          # 打开设置（已运行时唤醒已有实例）
.\usb-switch.exe --version
.\usb-switch.exe --help
```

`--list` 的输出长这样：

```text
USB-Switch 1.0.0 — removable devices
[1] CRUZER (E:) — 14.6 GB  [bus=USB, removable=yes, ejectable=yes]
    USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0
```

每次调用只接受一个操作；参数写错显示用法并返回退出码 2，不会碰到设备。加 `--json` 可得到机器可读的结果，`--eject --files` 可在被拒绝时查询占用进程打开的文件。完整的参数、退出码和 JSON 字段见 [docs/command-line.md](docs/command-line.md)。

**English:** one action per invocation; a bad command line prints the usage and exits 2 without touching any device. Add `--json` for machine-readable output. See [docs/command-line.md](docs/command-line.md) for the full reference.

## 排障 / Troubleshooting

| 现象 | 处理 |
|---|---|
| 任务栏时钟旁看不到图标 | 展开隐藏的托盘图标；Explorer 重启后程序会自动重建图标 |
| 菜单显示「暂时无法读取设备列表」 | 枚举暂时失败，程序会自动重试，也可手动选「刷新设备列表」。只有扫描成功且确实没有设备时才显示「没有可移动设备」 |
| 提示文件被占用 | 选「查看打开的文件」看是哪些进程占着，保存并关闭后点「重试弹出」，或等复制完成 |
| 提示系统正在关闭句柄、或设备暂未允许移除 | 等几秒或等文件操作结束后重试 |
| 已安全弹出，但设备还在列表里 | 选「刷新设备列表」核对 |
| 需要日志 | 托盘菜单「打开日志文件夹」 |

**English:** if the icon is missing, expand the hidden tray icons — the app rebuilds it after Explorer restarts. A device list that reads *unavailable* is retrying automatically; *no removable devices* only appears after a successful empty scan. When a drive is busy, use **Show open files**, close what it lists and choose **Retry removal**. *Pending close* and *not allowed to remove* both mean waiting for file operations to finish. Use **Refresh device list** to re-check after a successful eject, and **Open log folder** for the logs.

## 获取帮助 / Getting help

先打开托盘菜单的「打开日志文件夹」看日志（位置与内容说明见 [docs/command-line.md](docs/command-line.md#日志--logs)）。仍无法解决时，请到 [GitHub 仓库的问题页面](https://github.com/quji762/USB-Switch/issues) 反馈并附上日志——日志可能包含设备名称和实例 ID，分享前请自行检查。

**English:** open **Open log folder** from the tray menu first (location and contents are described in [docs/command-line.md](docs/command-line.md#日志--logs)). If that does not resolve it, file an issue on the [GitHub repository](https://github.com/quji762/USB-Switch/issues) with the log attached — it can contain device names and instance IDs, so review it before sharing.

## 已知限制 / Known limitations

- 只管理可移动/USB 磁盘类设备（U 盘、移动硬盘、读卡器）；USB 光驱、手机（MTP）、打印机等不在范围内。
- 打开设备失败的接口会被跳过，不出现在列表里。
- 卷没有任何挂载点（没有盘符、也没有文件夹挂载）时，程序记录不到它：安全弹出不受影响，但「退出存储卡」不会为它出现。给这个卷分配一个盘符即可，机理见 [docs/command-line.md](docs/command-line.md#--eject-media--card-eject)。
- 强制弹出（忽略 veto）故意没有实现——它会绕过硬件的安全停止流程，有数据损坏风险。
- 占用文件检查只覆盖 Windows 为本次拒绝报告的进程，以及当前权限能识别的目标卷句柄，不保证列出所有占用者。
- SMART 只覆盖能通过 ATA PASS-THROUGH 回答的设备；NVMe 通常只有温度。
- 重新连接只记住本次运行中最后一次成功弹出的设备。

**English:** removable/USB disk-class devices only; interfaces that fail to open are skipped; a volume with no mount point at all is invisible to the tool (safe removal still works, card eject does not — give the volume a drive letter); force-eject is deliberately absent; open-file inspection is best-effort; SMART needs a device that answers ATA pass-through; only the last eject of the run can be reconnected.

## 开发者 / For developers

### 从源码构建 / Building

需要 CMake ≥ 3.16 和一个 C++17 编译器（MSVC 或 MinGW-w64 均可）。

```powershell
.\build.ps1                          # 自动探测工具链，产物在 dist\
```

测试、CI 与发布验证见 [docs/release-verification.md](docs/release-verification.md)。想改代码或提 PR，先看 [CONTRIBUTING.md](CONTRIBUTING.md)；编码代理的操作指令在 [AGENTS.md](AGENTS.md)。

**English:** CMake 3.16+ and any C++17 compiler. `.\build.ps1` detects the toolchain and collects the artifacts in `dist\`. Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request; [AGENTS.md](AGENTS.md) holds the instructions for coding agents.

### 架构 / Architecture

组件边界、从枚举到弹出的数据流、持久化与信任边界、以及已经付出的取舍，见 [docs/architecture.md](docs/architecture.md)；设计决策的理由见 [docs/adr/](docs/adr/)。

**English:** component boundaries, the enumeration-to-eject data flow, persistence, trust boundaries and the trade-offs already paid are in [docs/architecture.md](docs/architecture.md), with the rationale in [docs/adr/](docs/adr/).

## 许可证 / License

GNU General Public License **version 3 only**（GPL-3.0-only），完整条款见 [LICENSE](LICENSE)。发布二进制版本须同时遵守该许可证，包括提供相应源代码的义务。

版本变更记录见 [CHANGELOG.md](CHANGELOG.md)。

**English:** licensed under GPL-3.0-only; see [LICENSE](LICENSE). Release history is in [CHANGELOG.md](CHANGELOG.md).
