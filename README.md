# QuickEject

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
| **失败原因** | 解析 `PNP_VETO_TYPE`,直接告诉你是哪个程序占用（而不是“设备正在使用中”） |
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
│   ├── Settings             %APPDATA%\QuickEject\settings.json
│   ├── Logger               %LOCALAPPDATA%\QuickEject\logs\quickeject.log(1 MiB 轮转)
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
4. 只保留 **USB 或可移动** 的设备——内置固定磁盘永远不会出现在列表里。

弹出走 `CM_Request_Device_Eject`，被拒绝时读取 `PNP_VETO_TYPE` 与 veto 程序名，
给出“Word 占用了 E: 上的文件”这类可操作的提示，而不是一句“设备正在使用中”。

## 构建 / Building

需要 CMake ≥ 3.16 和一个 C++17 编译器（MSVC 或 MinGW-w64 均可）。

```powershell
# 一键构建（自动探测 MSVC / MinGW，产物在 dist\）
.\build.ps1

# 手动
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

没有工具链时（任选其一）：

```powershell
winget install MSYS2.MSYS2      # 然后 pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake
# 或安装 Visual Studio 2022 Build Tools（“使用 C++ 的桌面开发”工作负载）
```

安装包（可选，需要 [Inno Setup 6](https://jrsoftware.org/isinfo.php)）：

```powershell
iscc installer\quickeject.iss     # 先跑 .\build.ps1，版本号从 exe 读取
```

## 使用 / Usage

```powershell
.\quickeject.exe                # 启动托盘程序
.\quickeject.exe --list         # 列出可移动设备（脚本/排障用）
.\quickeject.exe --eject E:     # 从命令行安全弹出
.\quickeject.exe --version
```

`--list` 输出示例：

```
QuickEject 1.0.0 — removable devices
[1] CRUZER (E:) — 14.6 GB  [bus=USB, removable=yes, ejectable=yes]
    USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0
```

## 设置 / Settings

`%APPDATA%\QuickEject\settings.json`：

```json
{
  "language": "auto",
  "runAtStartup": false,
  "showNotifications": true,
  "confirmBeforeEject": true
}
```

文件损坏或缺失时自动回退默认值，不会阻止启动。

## 产品化说明 / Productization notes

代码按“可发布”的标准搭好了架子，剩下的是产品决策而不是代码问题：

- **更新**：`app/Updater.h` 是扩展点。建议实现为 WinHTTP 拉取版本清单 +
  `WinVerifyTrust` 校验签名后再执行安装包，或直接集成
  [WinSparkle](https://winsparkle.org)。
- **代码签名**：发布前用 Authenticode 证书签名，否则 SmartScreen 会拦截。
  安装包脚本里已留好 `SignTool` 的位置。
- **开机自启**：目前用 HKCU Run 键（无需管理员）。要更“干净”可以改为计划任务，
  逻辑集中在 `app/AutoStart.cpp` 一处。
- **安装包**：`installer/quickeject.iss` 为每用户安装（`%LOCALAPPDATA%\Programs`，
  不弹 UAC）；要改成整机安装把 `DefaultDirName` 换成 `{autopf}` 并提升
  `PrivilegesRequired`。
- **本地化**：加语言 = 在 `src/i18n/Strings.cpp` 的表里加一列。

## 已知限制 / Known limitations

- 只管理**可移动/USB 磁盘类设备**（U 盘、移动硬盘、读卡器、USB 光驱）。
  手机（MTP）、打印机等非存储设备不在范围内。
- 打开设备失败（极少见，如被安全软件拦截）的接口会被跳过，不会出现在列表里。
- 强制弹出（忽略 veto）**故意没有实现**：它会绕过硬件的安全停止流程，有数据
  损坏风险。要做的话应作为独立的、有明显警告的选项，并先尝试
  `FSCTL_DISMOUNT_VOLUME`。
- 占用进程检测只报告 PnP 给出的 veto 程序名；要列出“所有打开句柄的进程”
  需要枚举系统句柄（`NtQuerySystemInformation` + 提权），那是 2.0 的事。

## 开发 / Development

```powershell
python tools\make_icon.py        # 重新生成 resources\quickeject.ico
python tools\check-strings.py    # 校验 i18n 表与枚举一致、占位符匹配、CMake 源码清单无遗漏
.\tools\build-no-cmake.ps1       # 只有 gcc 没有 CMake 时的直接构建（版本号仍从 CMakeLists.txt 读）
```

提交前建议过一遍：`--list` 冒烟、插拔一个 U 盘看气泡、设置里切换语言和开机自启。
