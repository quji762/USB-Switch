# USB-Switch

USB-Switch 是 Windows 托盘中的 USB 安全弹出工具。在菜单中选择要拔出的 U 盘、移动硬盘或读卡器；如果 Windows 拒绝弹出，工具会显示系统给出的原因。

A Windows tray tool for safely removing USB storage. Select a drive from the tray menu to eject it. If Windows refuses, USB-Switch shows the reason it received.

## 系统要求 / Requirements

Windows 10 版本 1607（10.0.14393）或更新版本，包括 Windows 11。可执行文件直接导入 `GetDpiForWindow`——自绘按钮和通知卡片按它度量 DPI——更早的系统连加载都做不到；`resources/app.manifest` 因此只声明 Windows 10 1607 / Windows 11 这一个 supportedOS，没有为更早的系统保留条目。

**English:** Windows 10 version 1607 (build 10.0.14393) or newer, including Windows 11. The executable imports `GetDpiForWindow` directly — the owner-drawn buttons and the notification card are measured with it — so earlier systems cannot load the binary at all. `resources/app.manifest` therefore declares only that one supportedOS.

## 功能 / Features

| 功能 | 说明 |
|---|---|
| 设备枚举 | SetupAPI 枚举磁盘设备接口，关联卷/盘符/卷标/容量/总线类型 |
| 一键安全弹出 | `CM_Request_Device_Eject`，完整走 PnP 停止流程 |
| **失败原因** | 解析 `PNP_VETO_TYPE`，显示系统返回的程序、服务、驱动或设备信息（如果有） |
| 占用诊断 | 弹出被拒绝且系统报告了关联程序时，可查看这些进程当下在目标卷上打开的文件/文件夹（`--eject --files --json`） |
| 退出存储卡 | 只退出读卡器中的卡片、保持读卡器连接（`--eject-media`，实验功能） |
| 重新连接 | 安全弹出后免拔插恢复原设备（实验功能，可能需要管理员） |
| 插拔通知 | 插入/断开时主显示器工作区右下角出现自绘通知卡片（可关闭；已隐藏设备不提示，托盘计数同样跳过）；高对比度、系统不宜通知或卡片无法显示时回退 Windows 原生气泡 |
| 隐藏设备 | 「管理设备」中按设备隐藏或恢复显示，只影响菜单，不卸载设备 |
| 设备别名 | 「管理设备」中为设备设置显示名，按 Windows 实例 ID 匹配 |
| 空卡槽识别 | 向驱动查询介质是否就绪，明确无介质显示“空卡槽”并置灰菜单项 |
| 全局快捷键 | Ctrl+Alt+U 打开设备菜单（可改键或关闭），快捷键本身不弹出设备 |
| 托盘常驻 | 单实例、`TaskbarCreated` 后自动重建图标、Explorer 重启不掉线 |
| 命令行 | `--list` / `--eject <目标>` / `--eject-media <目标>` / `--files` / `--json` / `--settings` / `--version` / `--help` |
| 外观模式 | 跟随系统/浅色/深色；自绘圆角按钮、对话框淡入与标题高亮 |
| 设置 | JSON 持久化：语言、外观、快捷键、别名、隐藏设备、开机自启、通知、弹出前确认、SMART 显示项 |
| 中英双语 | 跟随系统或手动指定 |
| 读卡器 | 同一台读卡器的卡槽收进子菜单，不再和 U 盘平铺 |
| 设备窗口 | 托盘菜单「已连接的设备」按读卡器分组列出当前设备，可从那里安全弹出或退出存储卡 |
| SMART | 设备窗口可显示温度等 SMART 数据，并可以选择要显示的项目。通电时长等 ATA 数值在部分系统上需要以管理员身份运行 |

### 界面与通知 / Appearance & notifications

在「设置 → 界面与输入 → 外观模式」可选择跟随系统、浅色或深色。切换会即时预览，点击保存后应用到其他应用内对话框。对话框内按钮为圆角自绘（悬停、按下、禁用与键盘焦点态），默认按钮带主题强调色；Windows 11 上对话框窗口呈圆角并带系统投影，Windows 10 保持方角。打开窗口或切换外观时，标题区会短暂亮起，窗口淡入出现；Windows 关闭动画或启用高对比度时不会播放，高对比度下按钮恢复系统原生样式，焦点环和禁用态文字改用系统调色板（`COLOR_HIGHLIGHT` / `COLOR_GRAYTEXT`），避免自定义配色在高对比度色面上不可读。托盘弹出菜单及系统提示框沿用 Windows 样式。

对话框模板按 DLU 创建时一次性换算成像素。跨显示器移动到另一 DPI 时，`ThemeDialog` 按系统建议的矩形重摆窗口，按创建时快照等比重建每个控件的位置和对话框字体，自绘按钮的圆角、内边距和焦点环也随之按新 DPI 度量，绘制与布局保持一致。

打开设备通知后，插入或断开设备会在主显示器工作区右下角出现一张短暂的通知卡片，显示设备名称。卡片为 14px 圆角并带柔和投影，淡入淡出显示；卡片不抢焦点；关闭系统动画时静态显示。高对比度或系统报告当前不宜显示通知时，交给 Windows 原生通知处理。

## 安装与使用 / Install & usage

### 第一次使用 / Quick start

1. **启动：** 安装向导可选简体中文或 English。应用首次启动跟随 Windows 界面语言，之后可在托盘的“设置”中更改。使用安装版时，从开始菜单打开 USB-Switch；使用便携版时，先将 ZIP 解压到电脑本地，再双击 `usb-switch.exe`。启动后没有主窗口，图标在任务栏时钟旁；看不到时，展开隐藏的托盘图标。
2. **选择设备：** 保存并关闭 U 盘上的文件，点击托盘图标，按卷标和盘符核对设备，再选择“点击安全弹出”。默认会弹出确认框；选择“是”才会继续。
3. **确认结果：** 默认设置下，看到“已安全弹出”即可拔出。若关闭了通知，可在菜单中刷新设备列表核对结果。若提示文件被占用，先保留设备连接，关闭相关文件或等待复制完成，再点“重试”。若错误框只有“确定”，请先按提示处理原因，再从菜单重新选择设备。

**English:** Install the app or extract the portable ZIP to your PC and run `usb-switch.exe`. Open the icon next to the clock (check the hidden icons if needed), match the drive letter, then select the device and confirm. Wait for **Safely removed** before unplugging. If the drive is busy, close its files or let the transfer finish, then choose **Retry**.

### 命令行 / Command line

```powershell
.\usb-switch.exe                # 启动托盘程序
.\usb-switch.exe --settings     # 打开设置；已运行时使用已有实例
.\usb-switch.exe --list         # 列出可移动设备（脚本/排障用）
.\usb-switch.exe --eject E:     # 从命令行安全弹出
.\usb-switch.exe --version
```

每次调用只接受一个操作。多个弹出目标或空目标会显示用法并返回退出码 2，不会弹出设备。无法识别的参数同样按用法错误处理（退出码 2）；早期版本接受但忽略的 `--minimized` 已删除。也可以将 `--list` 输出的完整 PnP 实例 ID 传给 `--eject`；含有 `&` 时请加引号。

脚本可用 `--eject <目标> --json` 执行弹出并读取结果。`status` 为 `ejected`（退出码 0）、`not_found`（2），或 `vetoed`、`failed`、`ineligible`、`enumeration_failed`（1）。系统拒绝时会提供 `vetoType`、`vetoName` 和 `retryable`；该标记仅表示稍后重试可能有用，程序不会自动重试。

被拒绝且系统报告了关联程序时，可加 `--files`（`usb-switch --eject F: --files --json`）查询这些进程当下打开的路径。这是实际弹出请求，仅在 Windows 拒绝后才检查文件。结果中的 `fileInspection` 按进程给出 `pid`、`name`、`paths` 和 `status`；状态包括 `inspected`（已检查）、`partial`（结果不完整）、`access_denied`、`timeout`、`process_changed`、`device_unavailable` 和 `unavailable`。检查共享约 3.5 秒时间预算，每个进程最多返回 32 个路径。返回的每个路径都必须落在本设备某个卷的挂载点之下，不符时该进程报 `unavailable`；本设备此刻枚举不出挂载卷时，所有进程都报 `device_unavailable`，不会再启动子进程。没有相关进程时数组为空；普通 `--json` 不查询路径。

脚本可运行 `--list --json`。成功时输出 UTF-8 JSON 数组，没有设备时为 `[]`；枚举失败时返回退出码 1，并输出 `{"error":"enumeration_failed"}`。字段名不随界面语言变化，每项包含 `instanceId`、`displayName`、`readerId`、`readerName`、`volumeLabel`、`bus`、`removable`、`ejectable`、`mediaState`、`driverMediaRemovable`、`mountedVolumeBytes`、`driveLetters` 和 `mountPaths`。`mediaState` 为 `present`、`empty`、`not_ready` 或 `unknown`：明确报告无介质的槽位是 `empty`，仅报告未就绪的是 `not_ready`，无法查询是 `unknown`。`driverMediaRemovable` 为驱动报告的介质可移除属性（`true` / `false` / `null`），普通 U 盘也可能返回 `true`，不能单独用来判定读卡器或允许只弹卡。`mountPaths` 列出已识别的挂载路径（含盘符根目录和文件夹），`driveLetters` 仅列出设备自身的盘符，`mountedVolumeBytes` 是已识别挂载卷的容量之和（十进制字节字符串，未知为 `null`），不代表整块物理磁盘的标称容量。

命令行运行拿不到输出句柄时（没有可附加的父控制台，stdout 也没有重定向），`--list`、`--help`、`--version` 返回 1 而不是 0——什么都没印出来的成功运行不算成功；写入中途失败同理。已经成功的弹出请求不在此列：确认打不出来仍返回 0，因为设备已经弹出来了。

```powershell
$devices = .\usb-switch.exe --list --json | Out-String | ConvertFrom-Json
$devices | Select-Object displayName, driveLetters, instanceId
```

`--list` 输出示例：

```
USB-Switch 1.0.0 — removable devices
[1] CRUZER (E:) — 14.6 GB  [bus=USB, removable=yes, ejectable=yes]
    USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0
```

排查问题时，从托盘菜单选择“打开日志文件夹”。托盘和命令行分别写入 `usb-switch.log` 和 `usb-switch-cli.log`，各自保留上一份 `.old`。日志文件以仅共享读的方式独占打开，因此第二个实例（例如只带 `--settings` 的启动）抢不到主文件名时会退到带 PID 的名字 `usb-switch.<进程号>.log`（每个日志家族最多保留 8 个这样的文件），不会静悄悄地什么都不记。日志可能包含设备名称和实例 ID，分享前请检查内容。

### 退出存储卡 / Ejecting a card

要换读卡器里的卡，可在托盘选择 **退出存储卡 → 目标设备**，核对卷标、盘符后选“是”。对话框默认选“否”。这个菜单按驱动报告的介质属性列出候选项，普通 U 盘也可能出现；若要拔下整个设备，请用主菜单的安全弹出。

脚本可以使用 `usb-switch --eject-media E: --json`。把 `E:` 换成实际盘符。命令核对分区和卷，尝试锁住全部卷，再向驱动请求退出介质。GPT 的 Microsoft 保留分区、EFI 系统分区、LDM 元数据分区和 MBR 的扩展容器分区默认不会挂卷，不计入“必须对应到卷”的分区；用户给这类分区加了盘符时，出现的卷仍被接受。跨多个 extent 或跨磁盘的卷报 `volume_multi_extent`（`ERROR_NOT_SUPPORTED`），不会冒充成运行中发生变化或 IOCTL 失败。如果分区不能全部对应到卷、文件正在使用、权限不足或驱动不支持操作，会返回失败及所在步骤（JSON 的 `status` 即步骤名，另附 `win32Error` 和 `instanceId`），不会改为停止读卡器。返回码为 0 且状态为 `media_ejected` 表示驱动接受了退卡请求。

### 重新连接刚弹出的设备 / Reconnecting a just-ejected device

用托盘安全弹出设备后，如果还没有拔下，可打开托盘菜单选择“重新连接：设备名”。程序只记住本次运行中最后一次成功弹出的设备；退出程序即清除记录。Windows 必须仍将原节点标记为已准备弹出。若需要管理员权限，可以选择打开系统授权提示，或取消并手动拔插。

对于可定位到集线器端口的 USB 存储设备，系统授权提示显示 USB-Switch；程序在授权后重新核对端口上的驱动标识及设备停止状态，仅重新连接对应端口。不能确认端口时，仍只尝试按原节点重启，授权提示同样显示 USB-Switch；授权完成后会重新枚举磁盘，核对原磁盘与目标节点的关系，并在重启前再次检查节点是否仍处于已弹出状态。如果磁盘已恢复在线或身份无法核实，会停止重连。授权命令运行期间不能重复提交。

重启命令返回 `ERROR_SUCCESS_REBOOT_REQUIRED`（3010）时按成功处理，但只会提示“重新连接的请求已排入队列，将在系统重启后生效”：SetupApi 只是标记了需要重启，系统要稍后才应用，因此不会继续等待设备出现，菜单项保留以便重启后重试。若重启执行时磁盘已经自行重现（错误 183），程序不再弹错误框，而是提示“设备已自行恢复连接，无需重新连接”，丢弃失效的重连记录并刷新列表。其他错误会在托盘显示 Windows 错误及编号；若命令成功但原磁盘十秒内仍未出现在列表中，托盘会提示刷新或手动拔插。

### 设置文件 / Settings file

`%APPDATA%\USB-Switch\settings.json`：

```json
{
  "version": 1,
  "language": "auto",
  "appearance": "auto",
  "menuHotkey": "U",
  "deviceAliases": {},
  "hiddenDevices": {},
  "runAtStartup": false,
  "showNotifications": true,
  "confirmBeforeEject": true,
  "smartDisplay": {
    "temperature": true,
    "powerOnHours": false,
    "powerCycles": false,
    "reallocated": false,
    "pending": false
  }
}
```

程序写出的顶层键共 10 个：`version`（结构版本，读取方暂不校验）、`language`（`auto` / `zh` / `en`）、`appearance`（`auto` / `light` / `dark`）、`menuHotkey`（Ctrl+Alt 加该字母，留空关闭）、`deviceAliases`（实例 ID → 显示名）、`hiddenDevices`（小写实例 ID → 上次显示名）、`runAtStartup`、`showNotifications`、`confirmBeforeEject` 和 `smartDisplay`（温度、通电时长、通电次数、重映射扇区、待映射扇区五个开关）。未知的顶层键和 `smartDisplay` 内的未知成员会在保存时原样保留。文件损坏或缺失时自动回退默认值，不会阻止启动；损坏的文件会另存为 `settings.json.corrupt`。

安装器只清理版本信息能确认为旧版 QuickEject 的 `quickeject.exe`。如果安装目录下有同名文件但身份无法确认，安装会停止并保留该文件。

从旧版 QuickEject 升级时，仅在新位置没有设置文件的情况下读取旧设置，并尝试将其保存到新位置；旧文件保留作为备份。如果启用了开机自启，只有旧启动项指向同一安装目录中的旧程序时，才会在新版启动项创建成功后清除它；创建失败时旧启动项原地保留、设置里仍显示已开启，日志中会留下一条警告。设置窗口按当前用户 Run 启动项是否指向本程序来显示勾选状态。普通启动不会重新创建已被删除的启动项；同名项指向其他程序时会保留。卸载时也只清除指向该安装目录的启动项，设置文件会保留。

## 构建 / Building

需要 CMake ≥ 3.16 和一个 C++17 编译器（MSVC 或 MinGW-w64 均可）。

```powershell
# 一键构建（自动探测 MSVC / MinGW，产物在 dist\）
.\build.ps1

# 手动（默认生成器）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --clean-first

# 手动（Visual Studio 生成器；-A x64 只对这类生成器有效，MinGW 等生成器不接受它）
cmake -S . -B build -A x64
cmake --build build --config Release --clean-first
```

构建脚本对 MSVC 执行干净重编译，以规避部分非英语系统上 Ninja 对本地化 `/showIncludes` 输出识别不一致导致的头文件依赖遗漏。

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

Inno Setup 有独立的使用和分发条款；商业使用者请参阅其[许可说明](https://jrsoftware.org/isorder.php)。

## 架构 / Architecture

```
src/
├── main.cpp                  入口：参数解析、CLI 分发、消息循环
├── app/                      应用外壳（与设备无关）
│   ├── App.h/.cpp            启动顺序、单实例、消息循环
│   ├── AppInfo.h.in          产品名/版本/公司，CMake 注入
│   ├── AutoStart.h/.cpp      HKCU ...\Run 开机自启
│   ├── Logger.h/.cpp         %LOCALAPPDATA%\USB-Switch\logs\usb-switch.log(1 MiB 轮转)
│   ├── Settings.h/.cpp       %APPDATA%\USB-Switch\settings.json
│   ├── SingleInstance.h/.cpp Local\ 互斥体 + 唤醒已有实例
│   └── Updater.h/.cpp        更新检查接口（空实现，见“产品化说明”）
├── core/                     与 UI 无关的设备逻辑
│   ├── Device.h              设备模型
│   ├── DeviceJson.h          --list --json 的设备序列化
│   ├── DeviceManager.h/.cpp  枚举 + 卷关联（见下）
│   ├── DeviceNotifier.h/.cpp RegisterDeviceNotification + WM_DEVICECHANGE
│   ├── EjectJson.h           --eject --json 的结果序列化
│   ├── Ejector.h/.cpp        CM_Request_Device_Eject + veto 原因本地化
│   ├── MediaEject.h/.cpp     退卡（IOCTL_STORAGE_EJECT_MEDIA）与分区/卷核对
│   ├── OpenFiles.h/.cpp      占用诊断：PSS 句柄快照 + 卷根过滤
│   ├── RemovalTarget.h       沿 USBSTOR/UASPStor 传输节点定位可安全移除的节点
│   ├── RestartProcess.h      读取提权重启进程的退出码
│   ├── SmartInfo.h/.cpp      ATA PASS-THROUGH 读 SMART 数据
│   ├── UsbPort.h/.cpp        集线器端口重连（IOCTL_USB_HUB_CYCLE_PORT）
│   └── VetoEvents.h/.cpp     Kernel-PnP 事件里取本次拒绝报告的进程
├── ui/
│   ├── MainWindow.h/.cpp     隐藏窗口：托盘菜单、防抖重扫、弹出流程
│   ├── TrayIcon.h/.cpp       Shell_NotifyIcon(NOTIFYICON_VERSION_4)
│   ├── ConnectionToast.h     插入/断开通知卡片（队列、淡入淡出、回退气泡）
│   ├── ConnectionFrame.h     通知卡片图标的托盘分辨率绘制（插座与插头）
│   ├── DialogHeader.h        对话框标题区高亮
│   ├── DialogTheme.h         自绘按钮、高对比度、外观模式、跨 DPI 重布局（仅头文件）
│   └── Dialogs.h/.cpp        关于 / 设置 / 设备 / SMART（资源模板 + 运行时设文案）
├── i18n/                     中英字符串表 + %1/%2/%3 占位符
│   ├── I18n.h/.cpp           语言选择（auto/zh/en）与格式化
│   ├── Strings.h             StrId 枚举
│   └── Strings.cpp           中英两列表
└── util/                     Str(UTF-8/UTF-16、格式化)、Json、Win32 RAII
    ├── Str.h/.cpp
    ├── Json.h/.cpp
    └── Win32.h
```

### 设备枚举是怎么做的

1. `SetupDiGetClassDevs(GUID_DEVINTERFACE_DISK)` 枚举所有磁盘设备接口；
2. 以 **0 访问权限** 打开设备路径，发 `IOCTL_STORAGE_GET_DEVICE_NUMBER` 和
   `IOCTL_STORAGE_QUERY_PROPERTY`，拿到设备号、总线类型、是否可移动、厂商/产品字符串；
3. `FindFirstVolume` 枚举卷，同样取 `STORAGE_DEVICE_NUMBER`，用 **设备号** 把卷的盘符、
   卷标、容量贴回对应的磁盘设备；
4. 只保留报告为 **USB、可移动介质，或挂载卷为 DRIVE_REMOVABLE** 的磁盘；USB 总线上的固定磁盘也会显示，但承载当前 Windows 卷的磁盘会排除（包括它的其他分区）。第三条用于兜底存储描述符 IOCTL 失败的设备。无法识别 Windows 卷的磁盘号时，程序不会发出弹出请求。

弹出走 `CM_Request_Device_Eject`，被拒绝时读取 `PNP_VETO_TYPE` 与 veto 程序名，根据系统实际返回的信息解释失败原因；系统未提供占用程序名时无法列出该程序。

## 开发 / Development

```powershell
python tools\make_icon.py        # 重新生成 resources\usb-switch.ico
python tools\check-strings.py    # 校验 i18n 表与枚举一致、占位符匹配、CMake 源码清单无遗漏
.\tools\run-tests.ps1            # 编译并运行单元、托盘、分区与 SMART 直通测试（缺 g++ 时回退 cl.exe；-Runtime 追加真实设备枚举）
.\tools\build-no-cmake.ps1       # 只有 gcc 没有 CMake 时的直接构建（版本号读 CMakeLists.txt，产品名/公司/版权读 AppInfo.h.in）
.\tests\installer.ps1            # 安装包构建后，测试当前用户的安装、升级和卸载
.\tests\installer.ps1 -LegacyInstaller C:\path\to\quickeject-setup.exe  # 使用旧版安装包做升级测试
.\tests\gui-smoke.ps1           # 有 Windows 桌面和系统托盘时，测试启动、单实例与退出
```

提交前建议过一遍：`--list` 冒烟、插拔一个 U 盘看通知卡片、设置里切换语言和开机自启。

界面语言目前只有简体中文和英语两种：文案集中在 `src/i18n/Strings.cpp` 的两列表里，`tools/check-strings.py` 按这两种翻译逐条校验（非空且 `%1/%2/%3` 占位符一致），`src/i18n/I18n.cpp` 只识别 `auto` / `zh` / `en`。新增一种语言需要同时改这张表、语言识别和校验脚本，不是只加一列就行。

### 发布前验证 / Release verification

运行 `.\tools\release.ps1` 可在本机依次构建、测试并生成便携包、安装包、对应源码包和 SHA-256 校验文件；它不会签名或发布文件。正式构建要求 Git 工作区干净；`-AllowDirty` 仅用于本地审阅，并会在构建信息中标注。可附加 `-GuiSmoke` 在有交互桌面的机器上检查托盘启动与退出。

仓库中的 Windows CI 在 MinGW 和 MSVC 两条流水线上检查构建、字符串、可执行文件资源、单元测试和设备枚举；MinGW 流水线另外跑托盘 GUI 冒烟测试和安装包测试，MSVC 流水线用 Visual Studio 17 2022 生成器并以 `-DQE_WARNINGS_AS_ERRORS=ON` 把警告当错误，覆盖 `/W4 /WX`、静态运行时和清单合并这些 MinGW 走不到的路径。
发布前还应在干净的 Windows 10/11 用户账户中检查安装、升级、卸载、托盘重启恢复、语言切换和开机自启，并使用真实 USB 设备验证弹出成功及被占用时的提示。
发行方还需要确定签名证书、发布渠道和更新策略；仓库目前没有提供签名证书，也没有实现自动更新。请勿将未完成这些验证的构建视为正式发行版。

实机验收可先运行 `.\tests\hardware-eject.ps1 -Drive D -VolumeLabel 'ESD-USB' -PreflightOnly`，将盘符和卷标替换为专用测试盘的实际值。去掉 `-PreflightOnly` 后，脚本会在测试盘上新建临时文件，确认 Windows 拒绝弹出；关闭并确认文件已删除后，再按原实例 ID 请求安全弹出。它不属于常规构建步骤，不会由 CI 自动执行。

**USB 读卡器换卡验收：** 准备一个 USB 读卡器和两张卷标不同的测试卡，把第一张插入读卡器。用下面的命令做只读预检，将程序路径、盘符和卷标换成实际值。预检通过后，在同一命令末尾加 `-RunSwap` 才会请求退卡。看到换卡提示时，只换卡片，保持读卡器连接。

```powershell
.\tests\hardware-card-swap.ps1 -Exe .\usb-switch.exe -Drive E -FirstLabel 'CARD-A' -SecondLabel 'CARD-B'
```

脚本分别检查原卷消失、同一 USB 存储节点持续在线、第二张卡能在该读卡器中读取；新卡不必沿用原盘符。当前脚本适用于 Windows 报告为 USBSTOR 或 UASPStor 的直连存储节点；不能确认设备身份时不会给出通过结论。

另外两项同样不会由 CI 执行，也不会真正弹出设备。`tests\media-busy.ps1` 只适用于驱动报告介质可移除的卷：它占住该卷上的一个临时文件，确认 `--eject-media` 因无法锁定卷而失败，释放后卷仍可写、临时文件已删除。`tests\hardware-open-files.ps1` 同时占住测试盘和另一块盘上的同名文件，确认 `--eject --files` 只报告测试盘上的路径。两者都要传入程序路径、盘符和卷标。

```powershell
.\tests\media-busy.ps1 -Exe .\usb-switch.exe -Drive E -ExpectedLabel 'CARD-A'
.\tests\hardware-open-files.ps1 -Exe .\usb-switch.exe -Drive E -ExpectedLabel 'CARD-A'
```

## 已知限制 / Known limitations

- 只管理**可移动/USB 磁盘类设备**（U 盘、移动硬盘、读卡器）。USB 光驱不在当前磁盘接口枚举范围内。手机（MTP）、打印机等非存储设备不在范围内。
- 打开设备失败（极少见，如被安全软件拦截）的接口会被跳过，不会出现在列表里。
- 没有盘符、只挂载到文件夹的设备会显示挂载路径。可从 JSON 的 `mountPaths` 复制完整挂载路径，例如 `--eject "C:\Mounts\USB"`，也可以使用设备实例 ID。文件夹名称按列表中的大小写匹配，末尾反斜杠可省略；文件或子目录不会匹配所在设备。未挂载的卷不计入已挂载容量。
- 强制弹出（忽略 veto）**故意没有实现**：它会绕过硬件的安全停止流程，有数据损坏风险。要做的话应作为独立的、有明显警告的选项，并先尝试 `FSCTL_DISMOUNT_VOLUME`。
- 占用文件检查仅覆盖 Windows 为本次拒绝报告的进程，以及当前权限可识别的目标卷文件句柄；返回的路径还必须落在本设备某个卷的挂载点之下。不保证列出所有占用者。
- SMART 只覆盖能通过 ATA PASS-THROUGH 回答的设备：温度优先取自存储栈（`IOCTL_STORAGE_QUERY_PROPERTY`），取不到时读 ATA 属性表（属性 190/194）——后者与通电时长等数值走同一条 PASS-THROUGH，在部分系统上同样需要管理员权限；NVMe 设备通常只有温度。设备不报告时对应项不显示。
- 重新连接只记住本次运行中最后一次成功弹出的设备，且要求 Windows 仍将原节点标记为已准备弹出；退出程序即清除记录。
- 部分读卡器在卡片已退出但尚未取走时，与物理空槽返回同一状态，此时会显示为空卡槽。

### 产品化说明 / Productization notes

代码按“可发布”的标准搭好了架子，剩下的是产品决策而不是代码问题：

- **更新**：`app/Updater.h` 是扩展点。建议实现为 WinHTTP 拉取版本清单 + `WinVerifyTrust` 校验签名后再执行安装包，或直接集成 [WinSparkle](https://winsparkle.org)。
- **代码签名**：发布前可用 Authenticode 证书签名；未签名程序可能触发 SmartScreen 警告。安装包脚本里已留好 `SignTool` 的位置。
- **开机自启**：目前用 HKCU Run 键（无需管理员）。要更“干净”可以改为计划任务，逻辑集中在 `app/AutoStart.cpp` 一处。
- **安装包**：`installer/usb-switch.iss` 为每用户安装（`%LOCALAPPDATA%\Programs`，不弹 UAC）；要改成整机安装把 `DefaultDirName` 换成 `{autopf}` 并提升 `PrivilegesRequired`。

## 许可证 / License

本项目以 GNU General Public License **version 3 only**（GPL-3.0-only）授权，完整条款见 [LICENSE](LICENSE)。发布二进制版本时须同时遵守该许可证，包括提供相应源代码的义务。项目使用的构建工具及其运行库可能另有各自的许可条款。

版本变更记录见 [CHANGELOG.md](CHANGELOG.md)。
