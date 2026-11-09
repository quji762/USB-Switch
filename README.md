# USB-Switch

Windows 托盘里的 USB 安全弹出工具。选中要拔出的 U 盘、移动硬盘或读卡器，一键安全弹出；Windows 拒绝时，告诉你它为什么拒绝。

A Windows tray tool for safely removing USB storage. Pick the drive, eject it in one click — and when Windows refuses, see the reason it gave.

## 功能 / Features

- **一键安全弹出** —— 走完整的 PnP 停止流程，不假装成功
- **拒绝就说清原因** —— 系统报告了哪个程序、服务或驱动在占用，就列出来
- **查看谁占着文件** —— 被拒绝时，可按进程列出它当下在目标盘上打开的文件
- **退出存储卡** —— 只退读卡器里的卡，读卡器保持连接（实验功能）
- **免拔插重连** —— 刚弹出的设备重新上线，不用拔插（实验功能）
- **插拔通知** —— 右下角一张短暂的卡片，不抢焦点，可整类关闭
- **设备管理** —— 别名、隐藏设备、按读卡器分组的设备窗口
- **空卡槽识别** —— 明确无介质的槽位置灰，不拿「容量未知」冒充空槽
- **SMART** —— 设备窗口里看温度等健康数据，可自选显示项
- **中英双语**、浅色/深色/跟随系统、全局快捷键、开机自启、命令行与 JSON 输出

**English:** one-click safe removal through the full PnP stop flow; the veto reason Windows reports, down to the process, service or driver holding the drive; an on-demand list of the files those processes have open; card-only eject and plug-free reconnect (both experimental); connect/disconnect cards; aliases, hidden devices and a reader-grouped device window; empty-slot detection; SMART readings; Chinese and English; light/dark/system appearance; a global hotkey; run at startup; and a command line with JSON output.

## 安装 / Install

| 版本 | 做法 |
|---|---|
| 安装版 | 运行 `usb-switch-setup-*.exe`。每用户安装到 `%LOCALAPPDATA%\Programs`，不弹 UAC |
| 便携版 | 把 ZIP 解压到电脑本地，双击 `usb-switch.exe` |

**English:** run the setup executable for a per-user install (no UAC prompt), or extract the portable ZIP and run `usb-switch.exe`.

## 使用 / Usage

启动后没有主窗口，图标在任务栏时钟旁；看不到时展开隐藏的托盘图标。

1. 保存并关闭 U 盘上的文件。
2. 点击托盘图标，按卷标和盘符核对设备，选择「点击安全弹出」。
3. 默认会弹确认框，选「是」继续；看到「已安全弹出」再拔出。

关闭了通知时，可在菜单中选择「刷新设备列表」核对结果。提示文件被占用，就先保留连接、关闭相关文件或等复制完成，再点「重试」。

**English:** after starting there is no main window — the icon sits next to the clock. Save and close the files on the drive, click the tray icon, match the volume label and drive letter, choose the eject item and confirm. Wait for **Safely removed** before unplugging. If the drive is busy, close its files or let the transfer finish, then choose **Retry**.

### 退出存储卡 / Ejecting a card

托盘菜单选 **退出存储卡 → 目标设备**，核对卷标和盘符后选「是」（对话框默认选「否」）。要拔下整个设备，请用主菜单的安全弹出。

**English:** choose **Eject card → device** from the tray menu and confirm. To remove the whole device, use the safe-removal item instead.

### 重新连接刚弹出的设备 / Reconnecting

安全弹出后如果还没拔下，托盘菜单会出现「重新连接：设备名」。程序只记住本次运行中最后一次成功弹出的设备，退出程序即清除记录。需要管理员权限时可以选择打开系统授权提示，或取消后手动拔插。

**English:** while the just-ejected device is still plugged in, the tray menu offers **Reconnect**. Only the last successful eject of this run is remembered, and the record clears when the app exits. Elevation is offered when it is needed.

## 设置 / Settings

托盘菜单「设置…」里可以改：界面语言、外观模式（跟随系统/浅色/深色）、全局快捷键、开机自启、插拔通知、弹出前确认、SMART 显示项，以及设备别名与隐藏设备。

设置保存在 `%APPDATA%\USB-Switch\settings.json`，字段说明见 [docs/command-line.md](docs/command-line.md#settingsjson)。

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

每次调用只接受一个操作；参数写错显示用法并返回退出码 2，不会碰到设备。加 `--json` 可得到机器可读的结果，`--eject --files` 可在被拒绝时查询占用进程打开的文件。完整的参数、退出码和 JSON 字段见 [docs/command-line.md](docs/command-line.md)。

**English:** one action per invocation; a bad command line prints the usage and exits 2 without touching any device. Add `--json` for machine-readable output. See [docs/command-line.md](docs/command-line.md) for the full reference.

## 从源码构建 / Building

需要 CMake ≥ 3.16 和一个 C++17 编译器（MSVC 或 MinGW-w64 均可）。

```powershell
.\build.ps1                          # 自动探测工具链，产物在 dist\
```

测试与发布验证见 [docs/release-verification.md](docs/release-verification.md)。

**English:** CMake 3.16+ and any C++17 compiler. `.\build.ps1` detects the toolchain and collects the artifacts in `dist\`.

## 开发者 / For developers

```
src/
├── main.cpp            入口：参数解析、CLI 分发、消息循环
├── app/                应用外壳：启动顺序、单实例、设置、日志、开机自启
├── core/               与 UI 无关的设备逻辑：枚举、弹出、退卡、占用诊断、SMART、端口重连
├── ui/                 托盘、隐藏窗口与消息循环、对话框主题、通知卡片
├── i18n/               中英字符串表与占位符格式化
└── util/               UTF-8/UTF-16 与格式化、JSON 解析、Win32 RAII
```

界面语言目前只有简体中文和英语：文案集中在 `src/i18n/Strings.cpp` 的两列表，`tools/check-strings.py` 逐条校验非空与占位符一致。新增一种语言需要同时改这张表、语言识别和校验脚本。

**English:** the tree above is the layout; `core/` holds everything UI-independent. Adding a language means the string table, the language detection and the checker all change together — it is not just another column.

## 已知限制 / Known limitations

- 只管理可移动/USB 磁盘类设备（U 盘、移动硬盘、读卡器）；USB 光驱、手机（MTP）、打印机等不在范围内。
- 打开设备失败的接口会被跳过，不出现在列表里。
- 卷没有任何挂载点（没有盘符、也没有文件夹挂载）时，程序记录不到它：安全弹出不受影响，但「退出存储卡」不会为它出现。给这个卷分配一个盘符即可。
- 强制弹出（忽略 veto）故意没有实现——它会绕过硬件的安全停止流程，有数据损坏风险。
- 占用文件检查只覆盖 Windows 为本次拒绝报告的进程，以及当前权限能识别的目标卷句柄，不保证列出所有占用者。
- SMART 只覆盖能通过 ATA PASS-THROUGH 回答的设备；NVMe 通常只有温度。
- 重新连接只记住本次运行中最后一次成功弹出的设备。

**English:** removable/USB disk-class devices only; interfaces that fail to open are skipped; a volume with no mount point at all is invisible to the tool (safe removal still works, card eject does not); force-eject is deliberately absent; open-file inspection is best-effort; SMART needs a device that answers ATA pass-through.

## 许可证 / License

GNU General Public License **version 3 only**（GPL-3.0-only），完整条款见 [LICENSE](LICENSE)。发布二进制版本须同时遵守该许可证，包括提供相应源代码的义务。

版本变更记录见 [CHANGELOG.md](CHANGELOG.md)。

**English:** licensed under GPL-3.0-only; see [LICENSE](LICENSE). Release history is in [CHANGELOG.md](CHANGELOG.md).
