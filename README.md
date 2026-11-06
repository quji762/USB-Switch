# USB-Switch

一个用 Win32 C++ 写的 USB 安全弹出工具：托盘常驻、一键安全弹出、插拔通知。
对标 USB Safely Remove 的核心体验，但不使用其任何商标、名称或界面素材。

A lightweight USB safe-removal tray tool written in Win32 C++: tray resident,
one-click safe removal, hot-plug notifications. It targets the same core
experience as USB Safely Remove without using its name, trademarks or artwork.

---

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
4. 只保留报告为 **USB 或可移动** 的磁盘；USB 总线上的固定磁盘也会显示。

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

Inno Setup 有独立的使用和分发条款；商业使用者请参阅其
[许可说明](https://jrsoftware.org/isorder.php)。

## 使用 / Usage

```powershell
.\usb-switch.exe                # 启动托盘程序
.\usb-switch.exe --list         # 列出可移动设备（脚本/排障用）
.\usb-switch.exe --eject E:     # 从命令行安全弹出
.\usb-switch.exe --version
```

`--list` 输出示例：

```
USB-Switch 1.0.0 — removable devices
[1] CRUZER (E:) — 14.6 GB  [bus=USB, removable=yes, ejectable=yes]
    USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0
```

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

从旧版 QuickEject 升级时，仅在新位置没有设置文件的情况下读取旧设置，
并尝试将其保存到新位置；旧文件保留作为备份。如果启用了开机自启，
只有旧启动项指向同一安装目录中的旧程序时，才会在新版启动项创建成功后清除它。
卸载时也只清除指向该安装目录的启动项，设置文件会保留。

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
- 仅识别直接挂载为盘符的卷；挂载到文件夹而没有盘符的卷不会显示盘符、卷标和容量。
- 强制弹出（忽略 veto）**故意没有实现**：它会绕过硬件的安全停止流程，有数据
  损坏风险。要做的话应作为独立的、有明显警告的选项，并先尝试
  `FSCTL_DISMOUNT_VOLUME`。
- 占用进程检测只报告 PnP 给出的 veto 程序名；要列出“所有打开句柄的进程”
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

## 许可证 / License

本项目以 GNU General Public License **version 3 only**（GPL-3.0-only）授权，
完整条款见 [LICENSE](LICENSE)。发布二进制版本时须同时遵守该许可证，包括提供
相应源代码的义务。项目使用的构建工具及其运行库可能另有各自的许可条款。
