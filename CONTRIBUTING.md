# 参与贡献 / Contributing

感谢愿意为 USB-Switch 出力。这份文档说明如何在本机把项目跑起来、提交前该检查什么。编码代理的操作指令见 [AGENTS.md](AGENTS.md)，面向用户的功能说明见 [README.md](README.md)。

**English:** thanks for considering a contribution. This page covers the local setup and the checks to run before opening a pull request.

## 行为准则 / Code of conduct

本项目尚未通过成文的行为准则。在所有者正式采纳之前，请以尊重和专业的方式交流；如果今后采纳了准则，会在这里链接。

**English:** no code of conduct has been adopted yet. Until the owner adopts one, please keep interactions respectful and professional.

## 提交 issue / Reporting issues

报告问题请附上：Windows 版本、程序版本（`usb-switch.exe --version`）、设备类型（U 盘 / 移动硬盘 / 读卡器及型号）、复现步骤，以及 `%LOCALAPPDATA%\USB-Switch\logs\` 下的日志。日志可能包含设备名称和实例 ID，分享前请自行检查。

安全性问题请不要公开提交——见下方「安全报告」。

**English:** include the Windows build, the app version, the device type, the steps to reproduce, and the logs from `%LOCALAPPDATA%\USB-Switch\logs\`. Do not post security issues publicly.

## 本地环境 / Prerequisites

| 工具 | 版本 | 用途 |
|---|---|---|
| CMake | ≥ 3.16 | 配置与构建 |
| C++17 编译器 | MSVC 2022 或 MinGW-w64 | 编译 |
| Python | 3 | 检查脚本与打包 |
| Pillow | 任意近期版本 | 仅 `tools/make_icon.py` 需要 |
| Inno Setup 6 | 可选 | 仅构建安装包需要 |

Windows 10 版本 1607 或更新——可执行文件直接导入 `GetDpiForWindow`，更早的系统加载不了。

所有命令都在仓库根目录执行。

## 构建 / Build

```powershell
.\build.ps1                    # 自动探测 MSVC / MinGW，产物在 dist\
.\build.ps1 -Config Debug      # 调试构建
```

## 提交前检查 / Checks before a pull request

```powershell
.\tools\run-tests.ps1                  # 单元、托盘、分区与 SMART 直通测试
.\tools\run-tests.ps1 -Runtime         # 追加真实设备枚举（可选，需要插着 USB 设备）
python tools\check-strings.py          # 字符串表与 CMake 源码清单一致
python tools\check-resources.py dist\usb-switch.exe   # 图标、版本信息与清单已嵌入
python tests\cli.py dist\usb-switch.exe                # 命令行参数与退出码
python tests\cli-output.py dist\usb-switch.exe         # Unicode 输出
```

改了 `src/ui/` 且有交互桌面时，另跑 `.\tests\gui-smoke.ps1`。自绘几何（按钮圆角、通知卡片、跨 DPI 重排）只能编译检查，视觉效果需要人工确认。

CI 在 MinGW 与 MSVC 两条流水线上以上述检查为基础，MSVC 那条用 `-DQE_WARNINGS_AS_ERRORS=ON` 把警告当错误。请至少保证 `-Wall -Wextra` 下零警告。

## 提交规范 / Commit messages

使用 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/v1.0.0/)：`feat(scope):`、`fix(scope):`、`docs:`、`test:`、`build(ci):`、`chore:`。一句话说清做了什么，与既有历史保持一致。

## 文档同步 / Keeping docs in sync

行为、配置或公开接口变化时，请同步：

- 用户可见的变化 → `CHANGELOG.md` 的 `未发布` 一节（不要写版本号和日期，那要等可验证的发布记录）
- 功能、限制、快速上手 → `README.md`
- 命令行参数、退出码、JSON 字段、`settings.json` 键 → `docs/command-line.md`
- 组件边界或数据流 → `docs/architecture.md`；设计决策 → `docs/adr/`
- 构建、测试或发布流程 → `CONTRIBUTING.md` 与 `docs/release-verification.md`

文档写作遵循仓库的 Markdown 规范；不要凭空添加没有实际内容的文档文件。

## 拉取请求 / Pull requests

一个拉取请求只做一件事，并在描述里写清：改了什么、为什么、验证过什么（跑了上面哪些命令）、是否需要同步文档。CI 需要全绿。

评审节奏与合并时限没有成文承诺——维护者会在有余力时处理。

**English:** keep each pull request to one change, and describe what changed, why, what you verified, and whether docs need updating. There is no committed review turnaround.

## 安全报告 / Security

仓库尚未公布私密的漏洞报告渠道。如果你认为发现了安全问题，请不要直接开公开 issue——先通过 GitHub 上的私有渠道联系维护者，或询问应采用哪种方式。在渠道确认之前，本节不提供具体的报告地址或响应时限。

**English:** no private vulnerability-reporting channel has been published yet. Please do not open a public issue for a suspected security problem; contact the maintainer privately and ask which channel to use. No address or response window is promised until one is confirmed.
