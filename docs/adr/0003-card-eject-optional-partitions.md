# 3. 退卡按「可选分区」匹配 / Card eject treats some partitions as optional

- 状态：**已接受**（accepted）——当前实现即如此
- 日期：2026-11-09
- 影响面：`src/core/MediaEject.*`、`tests/media-partitions.cpp`

## 背景 / Context

退卡前要核对「分区」和「卷」是否一一对应，防止卡已被换掉。最初的实现要求**每个**分区都对应到一个已挂载的卷，于是：

- Windows 默认格式化的 GPT 卡带一个 Microsoft 保留分区（MSR），永远不挂卷 → 必然失败；
- 带 MBR 扩展分区容器的卡，容器本身不挂卷 → 必然失败，且被报成误导性的 `volume_changed`。

## 决策 / Decision

`Layout()` 把分区分成两类：**必须有卷的**（`expected`）和**默认不挂卷的**（`optional`：Microsoft 保留分区、EFI 系统分区、LDM 元数据分区、MBR 扩展容器 0x05/0x0F/0x85）。

一个卷的 extent 只要落在 `expected` **或** `optional` 里就算合法；最终只要求 `expected` 全部被观测到。无法支持的布局（跨 extent、跨磁盘）走各自独立的 stage 名，不冒充成「设备被更换」——stage 清单见 [../command-line.md](../command-line.md)。

## 考虑过的替代方案 / Alternatives considered

- **按分区类型无条件跳过**。评审中否掉：EFI 系统分区默认没有盘符，但用户可以给它分配一个。一旦分配，该卷会进入 `volumeInterfaces`，而它的 extent 因为被跳过而匹配不上，于是从「必然失败」变成「用户配置了才失败」——把问题藏得更深。`optional` 集合对两种情况都成立。
- **让 `DeviceManager` 枚举没有挂载点的卷**（对 `\\?\Volume{...}` 直接发 `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` 反查归属盘）。**推迟**：这改变了枚举语义——`volumeInterfaces` 从「有挂载点的卷」变成「本盘全部的卷」，所有消费方都要重新核对，值得单独一轮做。当前后果是「卷存在但没有任何挂载点」时退卡仍不可用（安全弹出不受影响）。
- **LDM 元数据分区暂不纳入**。否掉：它与 MSR 同属「永远不该有卷」，漏掉它会让动态盘上的卡继续失败。LDM **数据**分区（`AF9B60A0-…`）故意不在集合里——动态卷是有盘符的，必须继续对应到卷。

## 后果 / Consequences

- 正面：Windows 默认 GPT 卡、带扩展容器的 MBR 卡、动态盘上的卡都能退卡；用户给 ESP 加了盘符也不会被误判。
- 负面：「卷存在但无任何挂载点」的卡仍无法退卡（见上）；`optional` 集合是一份需要随认知更新的分区类型清单。
- 测试：`tests/media-partitions.cpp` 覆盖 ESP 无卷、LDM 元数据无卷、LDM 数据不跳、ESP 挂载后有卷四种布局。

## 相关 / Related

- [docs/command-line.md](../command-line.md) 的 `--eject-media` 一节
- [docs/architecture.md](../architecture.md) 的退卡流程
