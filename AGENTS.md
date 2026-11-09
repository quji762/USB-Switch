# Agent Instructions

面向在本仓库工作的编码代理。这里是可执行的操作指令，不是产品说明——面向用户的文档见 [README.md](README.md)，变更历史见 [CHANGELOG.md](CHANGELOG.md)。

## 仓库地图

USB-Switch 是 Windows 托盘里的 USB 安全弹出工具，C++17，无第三方运行时依赖（只用 Windows SDK 导入库）。`src/` 约 8100 行，入口是 `src/main.cpp`。

| 目录 | 负责什么 |
|---|---|
| `src/main.cpp` | 参数解析、CLI 分发、消息循环入口 |
| `src/app/` | 应用外壳：启动顺序、单实例、设置、日志、开机自启 |
| `src/core/` | 与 UI 无关的设备逻辑：枚举、弹出、退卡、占用诊断、SMART、端口重连 |
| `src/ui/` | 托盘、隐藏窗口与消息循环、对话框主题、通知卡片 |
| `src/i18n/` | 中英字符串表与占位符格式化 |
| `src/util/` | UTF-8/UTF-16 与格式化、JSON 解析、Win32 RAII |

组件边界、数据流与设计权衡见 [docs/architecture.md](docs/architecture.md)；为什么这样设计见 [docs/adr/](docs/adr/)。

## 工作环境

- Windows 10 1607 或更新（`GetDpiForWindow` 是直接导入，更早的系统加载不了）。
- CMake ≥ 3.16 加一个 C++17 编译器：MSVC（Visual Studio 2022 Build Tools，含「使用 C++ 的桌面开发」工作负载）或 MinGW-w64（MSYS2 UCRT64 或独立的 mingw-w64 构建）。
- Python 3：`tools/check-strings.py`、`tools/check-resources.py`、`tools/package-release.py`、`tools/make_icon.py` 需要它；`make_icon.py` 另需 Pillow。
- 可选：Inno Setup 6（只有要构建安装包时才需要）。
- 所有命令都在仓库根目录执行。

## 命令

| 目的 | 命令 |
|---|---|
| 构建 | `.\build.ps1`（自动探测 MSVC/MinGW，产物在 `dist\`） |
| 构建（指定配置） | `.\build.ps1 -Config Debug` |
| 手动配置 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_RC_COMPILER=windres` |
| 测试 | `.\tools\run-tests.ps1` |
| 测试（含真机枚举） | `.\tools\run-tests.ps1 -Runtime` |
| GUI 冒烟 | `.\tests\gui-smoke.ps1`（需要交互桌面与系统托盘） |
| 字符串表检查 | `python tools\check-strings.py` |
| 可执行文件资源检查 | `python tools\check-resources.py <exe 路径>` |
| CLI 契约检查 | `python tests\cli.py <exe 路径>`、`python tests\cli-output.py <exe 路径>` |
| 重新生成图标 | `python tools\make_icon.py` |

CI 用 `-DQE_WARNINGS_AS_ERRORS=ON` 把警告当错误；本地改动后请至少保证 `-Wall -Wextra` 下零警告。

## 改动约定

- 提交信息用 Conventional Commits（`feat(scope):`、`fix(scope):`、`docs:`、`test:`、`build(ci):`、`chore:`），与既有历史一致。
- 工作区行尾是 CRLF（git `autocrlf` 管理）；新文件沿用仓库现状，不要单独改成 LF。
- `src/i18n/Strings.cpp` 的两列必须同时改：中英都非空、`%1/%2/%3` 占位符一致，否则 `check-strings.py` 会失败。新增语言不止加一列，还要改 `I18n.cpp` 的识别和校验脚本。
- 版本号只有一个来源：`CMakeLists.txt` 的 `project()`。`src/app/AppInfo.h.in` 与 `resources/version.rc.in` 由 CMake 注入，不要手改生成物。
- `tools/build-no-cmake.ps1` 从 `AppInfo.h.in` 读产品标识，与 CMake 路径保持一致；两处漂移时 `check-resources.py` 会发现。

## 测试与完成定义

改哪块就跑哪块，全部改完再跑一次完整门控：

| 改动范围 | 至少运行 |
|---|---|
| `src/util/Json.*`、`src/util/Str.*`、`src/i18n/*` | `.\tools\run-tests.ps1`（`unit.exe` 覆盖解析器、字符串与属性解码） |
| `src/core/MediaEject.*` | `.\tools\run-tests.ps1`（`media-partitions.exe` 用 mock 覆盖分区/卷匹配） |
| `src/core/SmartInfo.*` | `.\tools\run-tests.ps1`（`smart-passthrough.exe` 逐字节断言两条 CDB） |
| `src/ui/*` | `.\tests\gui-smoke.ps1`；自绘几何只能编译检查，视觉效果需人工确认 |
| `src/app/*`、`src/main.cpp` | `.\tools\run-tests.ps1 -Runtime` 加 `python tests\cli.py`、`cli-output.py`、`migration.ps1` |
| `tools/*.py` | 对应的检查脚本本身，加 `.\tools\run-tests.ps1` |
| `installer/` | `iscc installer\usb-switch.iss` 后跑 `.\tests\installer.ps1`（需要 Inno Setup） |

行为、配置或公开接口变化时，同步更新对应文档：README 的功能/限制/快速上手、`CHANGELOG.md` 的 `Unreleased`（只记用户可见的变化）、`docs/command-line.md`（命令行与 JSON 契约）。不要凭提交信息宣称发布——没有可验证的标签或发布记录就不要写版本号和日期。

## 安全与边界

- 不要改 `LICENSE`，不要新增安全或社区政策文件（`SECURITY.md`、`CODE_OF_CONDUCT.md`、`SUPPORT.md`）——这些需要仓库所有者确认，见 [CONTRIBUTING.md](CONTRIBUTING.md) 的说明。
- 不要提交密钥、令牌、私有地址；日志可能包含设备名称与实例 ID，写进文档或 issue 前先检查。
- `tests/hardware-*.ps1` 与 `tests/media-busy.ps1` 会真实操作 USB 设备：只在插入专用测试盘时运行，不要对系统盘或他人数据盘执行。
- 不要实现强制弹出（忽略 veto）：README 的已知限制里记了这是有意不为，改它前先读 [docs/adr/0001-no-force-eject.md](docs/adr/0001-no-force-eject.md)。
- 发布流程见 [docs/release-verification.md](docs/release-verification.md)；不要自行打标签或宣称发布。
