# 命令行与 JSON / Command line and JSON

本文件是 USB-Switch 命令行的完整参考。日常用法见 [README](../README.md#命令行--command-line)。

This is the full reference for the command line. Everyday usage is in the [README](../README.md#命令行--command-line).

## 参数 / Switches

| 参数 | 说明 |
|---|---|
| `--list`，`-l` | 列出可移动设备 |
| `--eject <目标>`，`-e <目标>` | 安全弹出 |
| `--eject-media <目标>` | 退出存储卡（只退介质，不停读卡器） |
| `--files` | 仅与 `--eject` 连用：被拒绝时查询占用进程打开的文件 |
| `--json` | 机器可读输出 |
| `--settings` | 打开设置窗口；程序已运行时唤醒已有实例 |
| `--version`，`-v` | 打印版本 |
| `--help`，`-h`，`/?` | 打印用法 |

`--eject=` 和 `--eject-media=` 也接受等号形式（`--eject=E:`）。

**每次调用只接受一个操作。** 多个操作、多个弹出目标、空目标或无法识别的参数都显示用法并返回退出码 2，不会触碰任何设备。

### 弹出目标 / Eject target

`--eject` 和 `--eject-media` 接受三种写法：

- 盘符：`E:` 或 `E`（大小写均可）
- 挂载路径：`C:\Mounts\USB`（按 `--list --json` 里 `mountPaths` 的大小写匹配，末尾反斜杠可省略；文件或子目录不会匹配所在设备）
- 完整的 PnP 实例 ID：`USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0`

命令行里含有 `&` 时必须加引号。

## 退出码 / Exit codes

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 失败（枚举失败、设备未找到、弹出被拒、IOCTL 失败等） |
| 2 | 用法错误 |

两种特殊情况：

- **拿不到输出句柄时**（没有可附加的父控制台，stdout 也没有重定向），`--list`、`--help`、`--version` 返回 1 而不是 0——什么都没印出来的成功运行不算成功。写入中途失败同理。
- **已经成功的弹出请求不在此列**：确认信息打不出来仍返回 0，因为设备已经弹出来了。

## `--list` / Inventory

```powershell
.\usb-switch.exe --list
.\usb-switch.exe --list --json
```

文本输出示例（标题行随界面语言变化）：

```text
USB-Switch 1.0.0 — removable devices
[1] CRUZER (E:) — 14.6 GB  [bus=USB, removable=yes, ejectable=yes]
    USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0
```

JSON 输出是一个数组，没有设备时为 `[]`；枚举失败时返回退出码 1 并输出 `{"error":"enumeration_failed"}`。字段名不随界面语言变化。

| 字段 | 类型 | 说明 |
|---|---|---|
| `instanceId` | string | PnP 实例 ID |
| `displayName` | string | 显示名（含别名） |
| `readerId` | string | 所属读卡器的 ID；普通 U 盘为空 |
| `readerName` | string | 读卡器名；非读卡器槽位为空 |
| `volumeLabel` | string | 卷标 |
| `bus` | string | 总线类型，如 `USB` |
| `removable` | bool | 存储描述符报告的可移除标志 |
| `mediaState` | string | `present` / `empty` / `not_ready` / `unknown` |
| `driverMediaRemovable` | bool \| null | 驱动报告的介质可移除属性。普通 U 盘也可能为 `true`，不能单独用来判定读卡器或允许只弹卡 |
| `ejectable` | bool | 是否可以请求弹出 |
| `mountedVolumeBytes` | string \| null | 已识别挂载卷的容量之和，十进制字节字符串；未知为 `null`。不代表整块物理磁盘的标称容量 |
| `driveLetters` | string[] | 设备自身的盘符 |
| `mountPaths` | string[] | 已识别的挂载路径（盘符根目录和文件夹挂载） |

`mediaState` 的判定：明确报告无介质的槽位是 `empty`，仅报告未就绪的是 `not_ready`，无法查询是 `unknown`。程序不会因容量未知、没有卷标或没有盘符而断言空槽。

```powershell
$devices = .\usb-switch.exe --list --json | Out-String | ConvertFrom-Json
$devices | Select-Object displayName, driveLetters, instanceId
```

## `--eject` / Safe removal

```powershell
.\usb-switch.exe --eject E: --json
```

| 字段 | 出现条件 | 说明 |
|---|---|---|
| `status` | 总是 | `ejected` / `not_found` / `vetoed` / `failed` / `ineligible` / `enumeration_failed` |
| `instanceId` | 总是 | 目标设备的实例 ID |
| `retryable` | 总是 | 稍后重试是否可能有用。程序不会自动重试 |
| `reportedProcesses` | `vetoed` | Windows 为本次移除记录的关联程序：`[{ "pid", "name" }]` |
| `vetoType` | `vetoed` | `PNP_VETO_TYPE` 数值 |
| `vetoName` | `vetoed` | 系统返回的程序/服务/驱动名或设备路径（可能为空） |
| `configRet` | `failed` | `CONFIGRET` 数值 |
| `win32Error` | `failed` | Win32 错误码 |

`reportedProcesses` 只匹配本次请求期间、属于该磁盘或本次实际停止节点的 Kernel-PnP 225 事件；没有匹配记录或没有权限时数组为空。这里的名字是程序名，不是它打开的文档。

### `--files` / Open files

```powershell
.\usb-switch.exe --eject F: --files --json
```

这是实际弹出请求，仅在 Windows 拒绝后才检查文件。结果中追加 `fileInspection`，按进程给出 `pid`、`name`、`paths` 和 `status`。

`status` 取值：`inspected`（已检查）、`partial`（结果不完整）、`access_denied`、`timeout`、`process_changed`、`device_unavailable`、`unavailable`。

约束：检查共享约 3.5 秒时间预算，每个进程最多返回 32 个路径；返回的每个路径都必须落在本设备某个卷的挂载点之下，不符时该进程报 `unavailable`；本设备此刻枚举不出挂载卷时，所有进程都报 `device_unavailable`，不会再启动子进程。没有相关进程时数组为空；普通 `--json` 不查询路径。

这个列表是排查线索，不能证明每个列出的文件都是阻塞原因。文件路径不写入应用日志。

## `--eject-media` / Card eject

```powershell
.\usb-switch.exe --eject-media E: --json
```

| 字段 | 说明 |
|---|---|
| `status` | 步骤名。`media_ejected` 表示驱动接受了退卡请求 |
| `win32Error` | Win32 错误码，0 表示成功 |
| `instanceId` | 目标设备的实例 ID |

失败时的步骤名：`unsupported_media`、`no_media`、`not_ready`、`open_disk`、`disk_identity`、`disk_changed`、`disk_layout`、`open_volume`、`volume_extent`、`volume_multi_extent`、`volume_changed`、`incomplete_volume_set`、`lock_volume`、`eject_media`。

- GPT 的 Microsoft 保留分区、EFI 系统分区、LDM 元数据分区和 MBR 的扩展容器分区默认不会挂卷，不计入「必须对应到卷」的分区；用户给这类分区加了盘符时，出现的卷仍被接受。
- 跨多个 extent 或跨磁盘的卷（跨区卷、带区卷、镜像卷）报 `volume_multi_extent`（`ERROR_NOT_SUPPORTED`），不会冒充成运行中发生变化或 IOCTL 失败。
- 卷没有任何挂载点时程序记录不到该卷，此时 `--eject-media` 报 `unsupported_media`。安全弹出不受影响。

## `settings.json`

`%APPDATA%\USB-Switch\settings.json`。程序写出的顶层键共 10 个：

| 键 | 取值 |
|---|---|
| `version` | 结构版本，读取方暂不校验 |
| `language` | `auto` / `zh` / `en` |
| `appearance` | `auto` / `light` / `dark` |
| `menuHotkey` | Ctrl+Alt 加该字母；留空关闭 |
| `deviceAliases` | 实例 ID → 显示名 |
| `hiddenDevices` | 小写实例 ID → 上次显示名 |
| `runAtStartup` | bool |
| `showNotifications` | bool |
| `confirmBeforeEject` | bool |
| `smartDisplay` | `temperature` / `powerOnHours` / `powerCycles` / `reallocated` / `pending` 五个开关 |

未知的顶层键和 `smartDisplay` 内的未知成员会在保存时原样保留，因此新版程序的设置不会被旧版覆盖丢失。文件损坏或缺失时自动回退默认值，不会阻止启动；损坏的文件会另存为 `settings.json.corrupt`。

从旧版 QuickEject 升级时，仅在新位置没有设置文件的情况下读取旧设置并保存到新位置，旧文件保留作为备份。

## 日志 / Logs

托盘和命令行分别写入 `%LOCALAPPDATA%\USB-Switch\logs\usb-switch.log` 和 `usb-switch-cli.log`，各自保留上一份 `.old`（1 MiB 轮转）。

日志文件以仅共享读的方式独占打开。第二个实例（例如只带 `--settings` 的启动）抢不到主文件名时，会退到带 PID 的名字 `usb-switch.<进程号>.log`，每个日志家族最多保留 8 个这样的文件，不会静悄悄地什么都不记。

日志可能包含设备名称和实例 ID，分享前请检查内容。排障时从托盘菜单选择「打开日志文件夹」。
