# USB-Switch

USB-Switch 是 Windows 托盘中的 USB 安全弹出工具。在菜单中选择要拔出的 U 盘、移动硬盘或读卡器；如果 Windows 拒绝弹出，工具会显示系统给出的原因。

A Windows tray tool for safely removing USB storage. Select a drive from the tray menu to eject it. If Windows refuses, USB-Switch shows the reason it received.

## 第一次使用 / Quick start

1. **启动：** 安装向导可选简体中文或 English。应用首次启动跟随 Windows 界面语言，之后可在托盘的“设置”中更改。使用安装版时，从开始菜单打开 USB-Switch；使用便携版时，先将 ZIP 解压到电脑本地，再双击 `usb-switch.exe`。启动后没有主窗口，图标在任务栏时钟旁；看不到时，展开隐藏的托盘图标。
2. **选择设备：** 保存并关闭 U 盘上的文件，点击托盘图标，按卷标和盘符核对设备，再选择“点击安全弹出”。默认会弹出确认框；选择“是”才会继续。
3. **确认结果：** 默认设置下，看到“已安全弹出”即可拔出。若关闭了通知，可在菜单中刷新设备列表核对结果。若提示文件被占用，先保留设备连接，关闭相关文件或等待复制完成，再点“重试”。若错误框只有“确定”，请先按提示处理原因，再从菜单重新选择设备。

**English:** Install the app or extract the portable ZIP to your PC and run `usb-switch.exe`. Open the icon next to the clock (check the hidden icons if needed), match the drive letter, then select the device and confirm. Wait for **Safely removed** before unplugging. If the drive is busy, close its files or let the transfer finish, then choose **Retry**.

## 开发版：只退出介质（实验功能）

要换读卡器里的卡，可在托盘选择 **只退出介质 → 目标设备**，核对卷标、盘符后选“是”。对话框默认选“否”。这个菜单按驱动报告的介质属性列出候选项，普通 U 盘也可能出现；若要拔下整个设备，请用主菜单的安全弹出。此功能尚未包含在 1.2.0 安装包内。

脚本可以使用 `usb-switch --eject-media E: --json`。把 `E:` 换成实际盘符。命令核对分区和卷，尝试锁住全部卷，再向驱动请求退出介质。如果分区不能全部对应到卷、文件正在使用、权限不足或驱动不支持操作，会返回失败及所在步骤，不会改为停止读卡器。返回码为 0 且状态为 `media_ejected` 表示驱动接受了退卡请求。真读卡器上的退卡、换卡仍待验证。

## 1.2.0：全局快捷键和设备别名

在其他应用里按 **Ctrl+Alt+U** 可打开设备菜单，方向键选设备，Esc 取消。快捷键本身不会弹出设备。在“设置”中可改为 Ctrl+Alt 加其他字母或关闭；组合被其他程序占用时会提示。打开“设置设备别名”，选择设备并输入名称；留空保存可恢复原名。别名保存在当前用户配置中，菜单和确认框同时保留原卷标及盘符。别名按 Windows 提供的实例 ID 匹配，盘符变化不会单独改变绑定；如果换接口后 Windows 分配了不同 ID，需要重新命名。

## 1.1.0：弹出占用诊断

弹出被拒绝时，托盘错误框和命令行会同时列出 Windows 为本次设备移除记录的关联程序名和 PID。错误框可用 Ctrl+C 复制，脚本可读 `--eject <目标> --json` 的 `reportedProcesses` 数组。只匹配本次请求期间、属于该磁盘或本次实际停止节点的 Kernel-PnP 225 事件；没有匹配记录或没有权限时，结果为空。这里的文件名是程序名，不是它打开的文档；不会读取命令行、关闭进程或强制解锁文件。

## 1.0.1 更新

修复部分单磁盘 USB 设备可以识别、却无法弹出的问题。设备被占用时保留系统的拒绝原因；关闭占用文件后可重试。此版本也修复了托盘菜单的定位和键盘操作。

若菜单提示“暂时无法读取设备列表”，旧设备不会作为可弹出选项显示。程序会自动重试，也可手动选择“刷新设备列表”。只有扫描成功且列表为空时才显示“没有可移动设备”。

## 功能 / Features

| 功能 | 说明 |
|---|---|
| 设备枚举 | SetupAPI 枚举磁盘设备接口，关联卷/盘符/卷标/容量/总线类型 |
| 一键安全弹出 | `CM_Request_Device_Eject`,完整走 PnP 停止流程 |
| **失败原因** | 解析 `PNP_VETO_TYPE`，显示系统返回的程序、服务、驱动或设备信息（如果有） |
| 插拔通知 | 设备插入/拔出时气泡提示（可关闭） |
| 托盘常驻 | 单实例、`TaskbarCreated` 后自动重建图标、 Explorer 重启不掉线 |
| 命令行 | `--list` / `--eject <盘符>` / `--version` / `--help` |
| 设置 | JSON 持久化：语言、开机自启、通知、弹出前确认 |
| 中英双语 | 跟随系统或手动指定 |

## 架构 / Architecture

```
src/
├── main.cpp                 入口：参数解析、CLI 分发、消息循环
├── app/                     应用外壳（与设备无关）
│   ├── App.h/.cpp           启动顺序、单实例、消息循环
│   ├── SingleInstance       Local\ 互斥体 + 唤醒已有实例
│   ├── Settings             %APPDATA%\USB-Switch\settings.json
│   ├── Logger               %LOCALAPPDATA%\USB-Switch\logs\usb-switch.log(1 MiB 轮转)
│   ├── AutoStart            HKCU ...\Run 开机自启
│   ├── Updater              更新检查接口（空实现，见“产品化”）
│   └── AppInfo.h.in         产品名/版本/公司，CMake 注入
├── core/                    与 UI 无关的设备逻辑
│   ├── Device.h             设备模型
│   ├── DeviceManager        枚举 + 卷关联（见下）
│   ├── DeviceNotifier      RegisterDeviceNotification + WM_DEVICECHANGE
│   └── Ejector              CM_Request_Device_Eject + veto 原因本地化
├── ui/
│   ├── MainWindow           隐藏窗口：托盘菜单、防抖重扫、弹出流程
│   ├── TrayIcon             Shell_NotifyIcon(NOTIFYICON_VERSION_4)
│   └── Dialogs              关于 / 设置（资源模板 + 运行时设文案）
├── i18n/                    中英字符串表 + %1/%2/%3 占位符
└── util/                    Str(UTF-8/UTF-16、格式化)、Json、Win32 RAII
```

### 设备枚举是怎么做的

1. `SetupDiGetClassDevs(GUID_DEVINTERFACE_DISK)` 枚举所有磁盘设备接口；
2. 以 **0 访问权限** 打开设备路径，发 `IOCTL_STORAGE_GET_DEVICE_NUMBER` 和
   `IOCTL_STORAGE_QUERY_PROPERTY`，拿到设备号、总线类型、是否可移动、厂商/产品字符串；
3. `FindFirstVolume` 枚举卷，同样取 `STORAGE_DEVICE_NUMBER`，用 **设备号** 把卷的盘符、
   卷标、容量贴回对应的磁盘设备；
4. 只保留报告为 **USB 或可移动** 的磁盘；USB 总线上的固定磁盘也会显示，但承载当前 Windows 卷的磁盘会排除（包括它的其他分区）。无法识别 Windows 卷的磁盘号时，程序不会发出弹出请求。

弹出走 `CM_Request_Device_Eject`，被拒绝时读取 `PNP_VETO_TYPE` 与 veto 程序名，
根据系统实际返回的信息解释失败原因；系统未提供占用程序名时无法列出该程序。

## 构建 / Building

需要 CMake ≥ 3.16 和一个 C++17 编译器（MSVC 或 MinGW-w64 均可）。

```powershell
# 一键构建（自动探测 MSVC / MinGW，产物在 dist\）
.\build.ps1

# 手动
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --clean-first
```

构建脚本对 MSVC 执行干净重编译，以规避部分非英语系统上 Ninja 对本地化
`/showIncludes` 输出识别不一致导致的头文件依赖遗漏。

没有工具链时（任选其一）：

```powershell
winget install MSYS2.MSYS2      # 然后 pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake
# 或安装 Visual Studio 2022 Build Tools（“使用 C++ 的桌面开发”工作负载）
```

安装包（可选，需要 [Inno Setup 6](https://jrsoftware.org/isinfo.php)）：

```powershell
iscc installer\usb-switch.iss     # 先跑 .\build.ps1，版本号从 exe 读取
```

安装器使用 [Inno Setup 6.7.4 的简体中文翻译](https://github.com/jrsoftware/issrc/blob/is-6_7_4/Files/Languages/Unofficial/ChineseSimplified.isl)，翻译文件保留原作者署名及[随附许可](installer/languages/INNO-LICENSE.txt)。

Inno Setup 有独立的使用和分发条款；商业使用者请参阅其
[许可说明](https://jrsoftware.org/isorder.php)。

## 使用 / Usage

```powershell
.\usb-switch.exe                # 启动托盘程序
.\usb-switch.exe --settings     # 打开设置；已运行时使用已有实例
.\usb-switch.exe --list         # 列出可移动设备（脚本/排障用）
.\usb-switch.exe --eject E:     # 从命令行安全弹出
.\usb-switch.exe --version
```

每次调用只接受一个操作。多个弹出目标或空目标会显示用法并返回退出码 2，不会弹出设备。也可以将 `--list` 输出的完整 PnP 实例 ID 传给 `--eject`；含有 `&` 时请加引号。

脚本可用 `--eject <目标> --json` 执行弹出并读取结果。`status` 为 `ejected`（退出码 0）、`not_found`（2），或 `vetoed`、`failed`、`ineligible`、`enumeration_failed`（1）。系统拒绝时会提供 `vetoType`、`vetoName` 和 `retryable`；该标记仅表示稍后重试可能有用，程序不会自动重试。

脚本可运行 `--list --json`。成功时输出 UTF-8 JSON 数组，没有设备时为 `[]`；
枚举失败时返回退出码 1，并输出 `{"error":"enumeration_failed"}`。字段名不随界面语言变化。
`mountPaths` 列出已识别的挂载路径（含盘符根目录和文件夹），`driveLetters` 仅列出设备自身的盘符，`mountedVolumeBytes` 是已识别挂载卷的容量之和（十进制字节字符串，未知为 `null`），不代表整块物理磁盘的标称容量。

开发版的 `--list --json` 另含 `driverMediaRemovable`：驱动报告介质可移除为 `true`，不可移除为 `false`，无法查询为 `null`。普通 U 盘也可能返回 `true`；此字段不能单独用来判定读卡器或允许只弹卡。

```powershell
$devices = .\usb-switch.exe --list --json | Out-String | ConvertFrom-Json
$devices | Select-Object displayName, driveLetters, instanceId
```

`--list` 输出示例：

```
USB-Switch 1.2.0 — removable devices
[1] CRUZER (E:) — 14.6 GB  [bus=USB, removable=yes, ejectable=yes]
    USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0
```

排查问题时，从托盘菜单选择“打开日志文件夹”。托盘和命令行分别写入 `usb-switch.log` 和 `usb-switch-cli.log`，各自保留上一份 `.old`。日志可能包含设备名称和实例 ID，分享前请检查内容。

## 设置 / Settings

`%APPDATA%\USB-Switch\settings.json`：

```json
{
  "language": "auto",
  "runAtStartup": false,
  "showNotifications": true,
  "confirmBeforeEject": true
}
```

文件损坏或缺失时自动回退默认值，不会阻止启动。

安装器只清理版本信息能确认为旧版 QuickEject 的 `quickeject.exe`。如果安装目录下有同名文件但身份无法确认，安装会停止并保留该文件。

从旧版 QuickEject 升级时，仅在新位置没有设置文件的情况下读取旧设置，
并尝试将其保存到新位置；旧文件保留作为备份。如果启用了开机自启，
只有旧启动项指向同一安装目录中的旧程序时，才会在新版启动项创建成功后清除它。
设置窗口按当前用户 Run 启动项是否指向本程序来显示勾选状态。普通启动不会重新创建已被删除的启动项；同名项指向其他程序时会保留。卸载时也只清除指向该安装目录的启动项，设置文件会保留。

## 产品化说明 / Productization notes

代码按“可发布”的标准搭好了架子，剩下的是产品决策而不是代码问题：

- **更新**：`app/Updater.h` 是扩展点。建议实现为 WinHTTP 拉取版本清单 +
  `WinVerifyTrust` 校验签名后再执行安装包，或直接集成
  [WinSparkle](https://winsparkle.org)。
- **代码签名**：发布前可用 Authenticode 证书签名；未签名程序可能触发 SmartScreen 警告。
  安装包脚本里已留好 `SignTool` 的位置。
- **开机自启**：目前用 HKCU Run 键（无需管理员）。要更“干净”可以改为计划任务，
  逻辑集中在 `app/AutoStart.cpp` 一处。
- **安装包**：`installer/usb-switch.iss` 为每用户安装（`%LOCALAPPDATA%\Programs`，
  不弹 UAC）；要改成整机安装把 `DefaultDirName` 换成 `{autopf}` 并提升
  `PrivilegesRequired`。
- **本地化**：加语言 = 在 `src/i18n/Strings.cpp` 的表里加一列。

## 已知限制 / Known limitations

- 只管理**可移动/USB 磁盘类设备**（U 盘、移动硬盘、读卡器）。USB 光驱不在当前磁盘接口枚举范围内。
  手机（MTP）、打印机等非存储设备不在范围内。
- 打开设备失败（极少见，如被安全软件拦截）的接口会被跳过，不会出现在列表里。
- 没有盘符、只挂载到文件夹的设备会显示挂载路径。可从 JSON 的 `mountPaths` 复制完整挂载路径，例如 `--eject "C:\Mounts\USB"`，也可以使用设备实例 ID。文件夹名称按列表中的大小写匹配，末尾反斜杠可省略；文件或子目录不会匹配所在设备。未挂载的卷不计入已挂载容量。
- 强制弹出（忽略 veto）**故意没有实现**：它会绕过硬件的安全停止流程，有数据
  损坏风险。要做的话应作为独立的、有明显警告的选项，并先尝试
  `FSCTL_DISMOUNT_VOLUME`。
- 占用诊断结合 PnP 拒绝原因和本次系统事件记录，无法保证识别全部进程或具体文档；要列出“所有打开句柄的进程”
  需要枚举系统句柄（`NtQuerySystemInformation` + 提权），那是 2.0 的事。

## 开发 / Development

```powershell
python tools\make_icon.py        # 重新生成 resources\usb-switch.ico
python tools\check-strings.py    # 校验 i18n 表与枚举一致、占位符匹配、CMake 源码清单无遗漏
.\tools\build-no-cmake.ps1       # 只有 gcc 没有 CMake 时的直接构建（版本号仍从 CMakeLists.txt 读）
.\tests\installer.ps1            # 安装包构建后，测试当前用户的安装、升级和卸载
.\tests\installer.ps1 -LegacyInstaller C:\path\to\quickeject-setup.exe  # 使用旧版安装包做升级测试
.\tests\gui-smoke.ps1           # 有 Windows 桌面和系统托盘时，测试启动、单实例与退出
```

提交前建议过一遍：`--list` 冒烟、插拔一个 U 盘看气泡、设置里切换语言和开机自启。

## 发布前验证 / Release verification

运行 ` .\tools\release.ps1 ` 可在本机依次构建、测试并生成便携包、安装包、
对应源码包和 SHA-256 校验文件；它不会签名或发布文件。正式构建要求 Git 工作区
干净；`-AllowDirty` 仅用于本地审阅，并会在构建信息中标注。可附加 `-GuiSmoke`
在有交互桌面的机器上检查托盘启动与退出。

仓库中的 Windows CI 检查构建、字符串、可执行文件资源、单元测试和设备枚举。
发布前还应在干净的 Windows 10/11 用户账户中检查安装、升级、卸载、
托盘重启恢复、语言切换和开机自启，并使用真实 USB 设备验证弹出成功及被占用时的提示。
发行方还需要确定签名证书、发布渠道和更新策略；仓库目前没有
提供签名证书，也没有实现自动更新。请勿将未完成这些验证的构建视为正式发行版。

实机验收可先运行 `.\tests\hardware-eject.ps1 -Drive D -VolumeLabel 'ESD-USB' -PreflightOnly`，将盘符和卷标替换为专用测试盘的实际值。去掉 `-PreflightOnly` 后，脚本会在测试盘上新建临时文件，确认 Windows 拒绝弹出；关闭并确认文件已删除后，再按原实例 ID 请求安全弹出。它不属于常规构建步骤，不会由 CI 自动执行。

## 许可证 / License

本项目以 GNU General Public License **version 3 only**（GPL-3.0-only）授权，
完整条款见 [LICENSE](LICENSE)。发布二进制版本时须同时遵守该许可证，包括提供
相应源代码的义务。项目使用的构建工具及其运行库可能另有各自的许可条款。
