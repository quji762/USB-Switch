# 测试与发布验证 / Testing and release verification

本文件面向开发者。日常使用见 [README](../README.md)。

This document is for developers. Everyday usage is in the [README](../README.md).

## 自动化检查 / Automated checks

```powershell
.\tools\run-tests.ps1              # 单元、托盘、分区与 SMART 直通测试
.\tools\run-tests.ps1 -Runtime     # 追加真实设备枚举
python tools\check-strings.py      # i18n 表与枚举一致、占位符匹配、CMake 源码清单无遗漏
```

测试运行器自动选择编译器：有 `g++` 用 MinGW-w64，没有则回退 `cl.exe`（Visual Studio 开发环境）。所有编译共用一套宏与包含目录，与 `CMakeLists.txt` 一致，因此同一个源文件不会用两套配置编译。`AppInfo.h` 缺失时由脚本自行生成，干净检出上也能直接运行。

GUI 冒烟测试需要交互桌面：

```powershell
.\tests\gui-smoke.ps1              # 启动、单实例与优雅退出
```

## 持续集成 / CI

`.github/workflows/windows.yml` 定义了两条流水线（MinGW 与 MSVC），但**自动触发已关闭**：目前只在 Actions 页面手动运行时才会执行。日常验证靠本节的自动化检查加上 `.\tools\release.ps1`。

两条流水线的内容：

- **MinGW** —— 构建、字符串检查、可执行文件资源检查、单元测试、设备枚举，外加托盘 GUI 冒烟和安装包测试。
- **MSVC** —— Ninja 生成器加 `-DQE_WARNINGS_AS_ERRORS=ON`，覆盖 `/W4 /WX`、静态 MSVC 运行时，以及只有 MSVC 才报的警告（有符号/无符号不匹配、整型到 `HANDLE` 的转换、局部变量遮蔽）。用 Ninja 而不是 `Visual Studio 17 2022` 生成器：后者配上 `CMakeLists.txt` 里的 `/MANIFESTINPUT` 会链接失败（`CVT1100`，清单资源 id 1 重复），而 `.\build.ps1` 走的就是 Ninja 这条已验证的路。

手动运行：GitHub 仓库的 Actions 页面 → *Windows build and tests* → *Run workflow*。

## 发布 / Release

```powershell
.\tools\release.ps1                # 构建、测试，生成便携包、安装包、源码包与 SHA-256
.\tools\release.ps1 -GuiSmoke      # 有交互桌面的机器上追加托盘启动与退出检查
```

正式构建要求 Git 工作区干净；`-AllowDirty` 仅用于本地审阅，并会在构建信息中标注。该脚本不签名、不发布文件。

**发布前还应在干净的 Windows 10/11 用户账户中检查**：安装、升级、卸载、托盘重启恢复、语言切换、开机自启，并用真实 USB 设备验证弹出成功以及被占用时的提示。

签名证书、发布渠道和更新策略由发行方决定。仓库不提供签名证书，也没有实现自动更新（`src/app/Updater.h` 是预留的扩展点）。未完成这些验证的构建不应视为正式发行版。

## 安装包 / Installer

```powershell
iscc installer\usb-switch.iss      # 先跑 .\build.ps1，版本号从 exe 读取
.\tests\installer.ps1              # 安装包构建后测试当前用户的安装、升级和卸载
.\tests\installer.ps1 -LegacyInstaller C:\path\to\quickeject-setup.exe   # 用旧版安装包做升级测试
```

需要 [Inno Setup 6](https://jrsoftware.org/isinfo.php)。安装器使用 Inno Setup 6.7.4 的简体中文翻译，翻译文件保留原作者署名及随附许可（`installer/languages/INNO-LICENSE.txt`）。Inno Setup 有独立的使用和分发条款，商业使用者请参阅其许可说明。

## 真机验收 / Hardware acceptance

以下脚本都需要插入真实 USB 设备，**不会由 CI 执行**。每个脚本都先做只读预检，确认目标设备和预期状态后才继续。

### 安全弹出 / Safe removal

```powershell
.\tests\hardware-eject.ps1 -Drive D -VolumeLabel 'ESD-USB' -PreflightOnly
```

把盘符和卷标换成专用测试盘的实际值。加上 `-PreflightOnly` 时只读检查。去掉后，脚本会在测试盘上新建临时文件、确认 Windows 拒绝弹出；关闭并确认文件已删除后，再按原实例 ID 请求安全弹出。

### 读卡器换卡 / Card swap

```powershell
.\tests\hardware-card-swap.ps1 -Exe .\usb-switch.exe -Drive E -FirstLabel 'CARD-A' -SecondLabel 'CARD-B'
```

准备一个 USB 读卡器和两张卷标不同的测试卡，把第一张插入读卡器。预检通过后，在同一命令末尾加 `-RunSwap` 才会请求退卡。看到换卡提示时只换卡片，保持读卡器连接。

脚本分别检查：原卷消失、同一 USB 存储节点持续在线、第二张卡能在该读卡器中读取。新卡不必沿用原盘符。当前脚本适用于 Windows 报告为 USBSTOR 或 UASPStor 的直连存储节点；不能确认设备身份时不会给出通过结论。

### 占用场景 / Busy volume

```powershell
.\tests\media-busy.ps1 -Exe .\usb-switch.exe -Drive E -ExpectedLabel 'CARD-A'
.\tests\hardware-open-files.ps1 -Exe .\usb-switch.exe -Drive E -ExpectedLabel 'CARD-A'
```

`media-busy.ps1` 只适用于驱动报告介质可移除的卷：它占住该卷上的一个临时文件，确认 `--eject-media` 因无法锁定卷而失败，释放后卷仍可写、临时文件已删除。

`hardware-open-files.ps1` 同时占住测试盘和另一块盘上的同名文件，确认 `--eject --files` 只报告测试盘上的路径。

两者都要传入程序路径、盘符和卷标，且都不会真正弹出设备。
