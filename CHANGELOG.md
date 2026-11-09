# 变更记录 / Changelog

本项目遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 的格式；版本号取自 `CMakeLists.txt` 的 `project()` 版本。当前版本的行为以 [README](README.md) 为准，命令行的完整参考见 [docs/command-line.md](docs/command-line.md)。

This project follows the [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format. The version number comes from the `project()` version in `CMakeLists.txt`. Current behavior is documented in the [README](README.md), and the full command-line reference is in [docs/command-line.md](docs/command-line.md).

## [未发布] / [Unreleased]

### 新增 / Added

- 对话框跨显示器迁移时按新 DPI 重建窗口与控件布局。
- 重连请求需要重启后才生效时给出明确提示，不再按失败报告，也不再空等十秒轮询。
- 设备在授权提示期间自行重现时给出提示，不再显示 "Cannot create a file when that file already exists."
- 退卡遇到跨多个 extent 或跨磁盘的卷时报 `volume_multi_extent`，不再冒充成设备被更换。

### 变更 / Changed

- 退卡跳过默认不挂卷的分区类型：新增 EFI 系统分区与 LDM 元数据分区；用户给这类分区加了盘符时，出现的卷仍被接受。
- 隐藏设备不再触发插拔通知，托盘提示里的设备计数也按同一套隐藏名单过滤。
- 抢不到主日志文件名的实例写到带 PID 的日志，每个日志家族最多保留 8 个。
- JSON 解析失败时给出偏移位置、期待的记号和实际的记号。
- 设置保存改为原子写入，同一时刻的临时文件不再撞名。

### 移除 / Removed

- `--minimized` 参数：此前被接受但没有任何效果；无法识别的参数现在按用法错误处理（退出码 2）。

### 修复 / Fixed

- 高对比度下按钮恢复系统原生样式（此前该分支永不可能执行）。
- 高对比度下自绘按钮的键盘焦点环改用系统调色板，不再可能看不见。
- `SetTimer` 失败后插拔通知不再全部静默丢失，也不再留下常驻的透明窗口。
- 模态对话框打开期间点击托盘图标不再重入弹出流程。
- 设备事件流持续高频时重扫不再被无限推迟。
- `TaskbarCreated` 在窗口创建期间到达时不再被丢弃。
- `--settings` 等第二个实例不再一行日志都不留。
- 日志写入失败时不再被静默吞掉。
- SMART 读取失败时记录 sense 数据；温度描述符补上版本与长度校验。
- `GetMessageW` 出错时托盘进程不再以退出码 0 结束。

## [1.0.0]

第一个使用重设后版本号的版本，功能涵盖下列全部早期开发记录。

The first release under the reset version number. Its feature set covers all of the early development records below.

## 早期开发记录 / Before the 1.0.0 reset

版本号在 1.0.0 发布时重设。以下各条为当时的开发记录摘要。

The version number was reset at the 1.0.0 release. The entries below summarise the records as they were written.

- **1.5.1** 托盘菜单与窗口布局：日常弹出与退卡收入托盘主菜单，「管理设备」收入别名、隐藏与恢复；设置窗口按用途分组。
- **1.5.0** 隐藏设备与空卡槽识别：按设备实例 ID 隐藏或恢复设备；向驱动查询介质是否就绪，明确无介质的槽位置灰。
- **1.4.0** 查看占用文件：弹出被拒时按 PID 列出进程当下在目标盘上打开的文件。
- **1.3.1** 免拔插重连（实验功能）：安全弹出后让原设备重新上线；可定位到集线器端口时只重连对应端口。
- **1.3.0** 退出存储卡（实验功能）：只退读卡器里的卡，读卡器保持连接。
- **1.2.0** 全局快捷键与设备别名：Ctrl+Alt+U 打开设备菜单；别名按 Windows 实例 ID 匹配。
- **1.1.0** 弹出占用诊断：列出 Windows 为本次移除记录的关联程序名与 PID。
- **1.0.1** 修复部分单磁盘 USB 设备可以识别却无法弹出的问题，以及托盘菜单的定位和键盘操作。
