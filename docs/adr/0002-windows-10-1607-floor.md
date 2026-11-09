# 2. 系统要求收敛到 Windows 10 1607 / Windows 10 1607 floor

- 状态：**已接受**（accepted）——当前清单即如此
- 日期：2026-11-09
- 影响面：`resources/app.manifest`、README 的系统要求

## 背景 / Context

自绘按钮和通知卡片按每显示器 DPI 度量几何，代码直接调用 `GetDpiForWindow`——该函数自 Windows 10 版本 1607（10.0.14393）起才有。而 `resources/app.manifest` 的 `supportedOS` 一直列着 Win7/8/8.1，注释还写着「更早的系统仍可运行，只是没有 opt-in」。

那句话是假的：直接导入意味着加载器在更早的系统上找不到入口点，进程根本起不来。

## 决策 / Decision

清单只声明 Windows 10 1607 与 Windows 11，README 把同一要求写在第一节。不再为更早的系统保留兼容条目。

## 考虑过的替代方案 / Alternatives considered

- **运行时 `GetProcAddress` 取 `GetDpiForWindow`，失败回退 96 DPI**。没有采纳：需要一条完整的降级绘制路径（按钮圆角、内边距、焦点环、通知卡片全部要能在固定 DPI 下成立），而这条路径没有任何真机可以验证——本机只有 Windows 11。为三个已经停止支持、且本来就跑不起来的系统维持一条未测代码路径，代价高于收益。
- **保留清单条目、只改注释**。没有采纳：那会让一份已知错误的支持声明继续留在发布物里。

## 后果 / Consequences

- 正面：README、清单和实际行为一致；没有未测试的降级代码要维护。
- 负面：Win7/8/8.1 用户无法使用——但这些系统上程序本来就连加载都做不到，所以实际损失为零。
- 注意：MSVC 侧的清单合并与 MinGW 侧的清单遮蔽是两条不同路径，两者都由 `tools/check-resources.py` 静态检查。

## 相关 / Related

- `resources/app.manifest`
- [docs/architecture.md](../architecture.md) 的系统边界
