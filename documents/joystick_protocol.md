# 手控盒 (joystick) ↔ 主控固件协议

> **日期**：2026-05-18
> **协议版本**：v1.1（CRC 兼容性闸门 @ commit fa625d1）
> **相关代码**：
> - joystick 侧：`firmware/joystick/{octoaxes,octoaxesplus}/control_panel_teensyLC.ino`
> - firmware 侧：`firmware/{octoaxes,octoaxesplus}/joystick.{cpp,h}`
> - CRC 实现：`firmware/octoaxes/serial.cpp:23-46` (`CRC_TABLE`) + `:79-90` (`crc8ccitt`)

---

## 1. 物理层与传输

| 项 | 值 |
|---|---|
| joystick 端串口 | Teensy LC `Serial1`，波特率 115200 |
| firmware 端串口 | Teensy 4.1 `Serial5`，波特率 115200 |
| 帧定界 | PacketSerial 库（COBS 编码 + 0x00 分隔符） |
| 方向 | **单向：joystick → firmware**（firmware 不向 joystick 发任何数据） |
| 发送周期 | joystick 主循环 `delayMicroseconds(2000)`，名义 ~500 Hz；实际受 ADC/编码器中断负载影响 |
| firmware 消费周期 | 收包即解析；XY 速度更新 30 ms 节流（`JOYSTICK_UPDATE_INTERVAL_US`），焦点轮无节流 |

> COBS 已经在帧边界提供"包未截断"保证，但**不检测包内 bit-flip**——这是
> 本协议为何需要包级 CRC 的根因。

---

## 2. 10 字节包字段

```
byte:  [0]  [1]  [2]  [3] | [4]  [5] | [6]  [7] | [8]  | [9]
       └──── focus enc ──┘ └─ joy x ┘ └─ joy y ┘ btn   sentinel/CRC
```

| 偏移 | 内容 | 类型 | 单位/语义 |
|---|---|---|---|
| `byte[0..3]` | 焦点轮绝对编码器位置 | int32，**大端**，二进制补码 | joystick 内部累加器，分辨率 = encoder_pos / 4，对齐到 16 的整数倍（焦点轮最小步进粒度） |
| `byte[4..5]` | X 摇杆增量 | int16，**大端** | 经死区 + 灵敏度系数（cn `/8` 或 overseas `/4`）+ 平方加权后的 [-32767, 32767] 偏移量 |
| `byte[6..7]` | Y 摇杆增量 | int16，**大端** | 同 X |
| `byte[8]` | 摇杆按钮电平 | uint8 | `digitalRead(pin_joystick_btn)`，0=松开 / 非 0=按下 |
| `byte[9]` | **CRC 兼容性 sentinel** | uint8 | 见 §3 |

### 字节序约定

所有多字节字段一律 **MSB-first（大端）**。补码编码通过 joystick 侧
`twos_complement(signedLong, N)` 实现：负数 → `signedLong + 256^N`。

---

## 3. byte[9] 双语义（**本协议核心约定**）

byte[9] 这一字节同时承担**协议版本指示**和**CRC 载荷**两个角色：

| `byte[9]` 取值 | 语义 | firmware 行为 |
|---|---|---|
| **`0x00`** | "我是 legacy 协议（老 joystick），不带 CRC" | 直通解析，不校验，`joystick_legacy_count++` |
| **`0x01` ~ `0xFF`** | "我是新协议，此字节即 CRC-8-CCITT 值" | 计算 `crc8_ccitt(buffer[0..8])`，比对相等 → `crc_ok++`；不等 → `crc_fail++` 并**丢包** |

### 为什么用 0 当 sentinel？

老 joystick 代码（commit fa625d1 之前的
`firmware/joystick/control_panel_teensyLC.ino`）硬编码 `packet[9] = 0`，
留有 `// CRC to be added` 注释——这个字节在历史上**从未承载过有效数据**。
把"== 0"定义成兼容入口，老代码无需任何修改即可与新 firmware 共存。

### 为什么新 joystick 要把"算出来的 0"强制改成 1？

`crc8_ccitt` 输入空间是 2^72 种，输出 256 种，因此约 **1/256** 概率算出
`0x00`。若不处理，这种包会被 firmware 误判为 legacy 直通。为闭合这个
1/256 漏洞，joystick 侧约定：

```c
uint8_t crc = crc8_ccitt(packet, 9);
if (crc == 0x00) crc = 0x01;        // 与 firmware 侧 byte[9]==0 sentinel 不冲突
packet[9] = crc;
```

firmware 侧做对称处理：

```c
uint8_t calc = serialProtocol.crc8ccitt(buffer, 9);
if (calc == 0x00) calc = 0x01;
if (calc != buffer[9]) { drop; }
```

代价：理论错误检出率从 256 个等概率值降为 255 个（约 0.39% 损失，可接受）。

---

## 4. CRC-8-CCITT 算法定义

| 参数 | 值 |
|---|---|
| 多项式 | `0x07`（x⁸ + x² + x + 1） |
| 初始值 | `0x00` |
| 输入反射 | 否 |
| 输出反射 | 否 |
| 输出异或 | `0x00` |
| 复用 | 与 firmware ↔ 上位机协议 CRC 完全相同；CRC_TABLE 直接复制使用 |
| 表存放 | `firmware/octoaxes/serial.cpp:23-46` 与 joystick `.ino` 顶部各一份副本 |
| 覆盖范围 | `byte[0..8]`（9 个字节，**不**包含 byte[9] 自己） |

### 参考实现

```c
static uint8_t crc8_ccitt(const uint8_t *data, uint8_t n) {
    uint8_t v = 0;
    for (uint8_t i = 0; i < n; i++) v = CRC_TABLE[v ^ data[i]];
    return v;
}
```

---

## 5. 兼容性矩阵

| joystick 侧代码 | firmware 侧代码 | 行为 |
|---|---|---|
| 老 (`byte[9] = 0`) | 老（fa625d1 之前 / 旧 Squid） | byte[9] 完全不被读取 [^1] |
| 老 (`byte[9] = 0`) | 新（fa625d1 及之后） | 命中 legacy 分支，跳过 CRC，照常解析；`legacy_count++` |
| 新 (`byte[9] = CRC≠0`) | 老（旧 Squid） | 老 fw 不看 byte[9]，**零影响**（新 joystick 完全可接入老 fw）[^1] |
| 新 | 新 | 校验 CRC：通过 → `crc_ok++`；失败 → `crc_fail++` 丢包 |

[^1]: **源码已核实**：旧 Squid firmware 的 joystick 接收函数
`onJoystickPacketReceived` 位于
`/home/hds/github.com/veerwang/lihongquan/Squid/firmware/controller/src/functions.cpp:509-546`，
函数体只读 `buffer[0..3]`（焦点编码器）、`buffer[4..5]`（X 摇杆）、
`buffer[6..7]`（Y 摇杆）、`buffer[8]`（按钮）四段，**`buffer[9]` 一字节都未引用**。
该函数与 `firmware/octoaxes/joystick.cpp` 的接收逻辑结构同构（同源遗产），
因此新 joystick (`byte[9]=CRC≠0`) 接入旧 Squid fw 在源码层面**确定无副作用**，
无需硬件验证即可放心混用。

> **一致性约束**：byte[9] 的"0 = legacy"约定一旦确立，**永久保留，不可改用作别的语义**。
> 任何未来扩展（如增加状态位、加握手帧）都必须避开 byte[9] = 0 这个特殊值。

---

## 6. 诊断与可观测

### `S:JOYSTICK_STATS` 串口调试命令

firmware 侧通过 USB 串口收到 ASCII 行 `S:JOYSTICK_STATS\n` 后，回送：

```
JOYSTICK_STATS legacy=<N> crc_ok=<N> crc_fail=<N>
```

| 计数器 | 含义 |
|---|---|
| `legacy` | byte[9] == 0 包的累计数（来自老 joystick / 或字段写 0 的中间版本） |
| `crc_ok` | byte[9] != 0 且 CRC 校验通过的包数 |
| `crc_fail` | byte[9] != 0 但 CRC 校验失败的包数（已丢） |

### 现场诊断速查

| 观测 | 推断 |
|---|---|
| `legacy>0, crc_ok=0, crc_fail=0` | 对端是**老 joystick**（或写 0 的中间版本） |
| `legacy=0, crc_ok>0, crc_fail=0` | 对端是**新 joystick**，链路正常 |
| `legacy=0, crc_ok=N, crc_fail=M (M ≪ N)` | 新 joystick，链路偶有干扰（个位数 fail 属正常） |
| `legacy=0, crc_ok=0, crc_fail>>0` | **报警**：对端代码可能写错 CRC，或 byte[9] 被污染 |
| 三者全 0 但 joystick 已通电 | Serial5 物理层/接线问题，包根本没到 firmware |

---

## 7. 已知限制 / 未来扩展约束

### 当前协议局限

1. **legacy 包仍无完整性保护**：老 joystick 包在新 firmware 上和老 firmware
   上行为完全一致——若 byte[0..8] 中某 bit 翻转但 byte[9] 仍为 0，新 firmware
   照单全收。只有升级 joystick 代码才能获得 CRC 保护。

2. **CRC 字节空间从 256 缩为 255**：0x00 被占作 sentinel。错误检出率名义下降 0.39%。

3. **单向通道**：firmware 不向 joystick 反馈状态（如限位告警、错误码）。
   joystick 端 `onPacketReceived` 函数体为空。若未来需要反馈通道，是**全新协议**
   而非本协议扩展。

### 升级路径建议

如未来确需提升保护强度或加状态反馈，**不要**复用 byte[9] 的其他取值，应：

- **方案 A**：扩包到 11 字节或更长，新 firmware 同时识别 10/11 字节两种长度
  （PacketSerial 帧自带长度）。byte[10..] 承载新字段。老 fw 收到 11 字节包会
  因 `size != 10` 直接丢弃——**不向后兼容**，需配套升级 fw。
- **方案 B**：新增握手命令（joystick 启动时发一个 magic 包），firmware 据此
  开关增强协议。复杂度高，仅在协议稳定后再考虑。

### 不可改动项

- byte[9] == 0 永远等价于"legacy 包"
- CRC-8-CCITT 参数（poly/init/reflect/xor）一旦发布不再变更
- 0x00 → 0x01 映射规则两端必须严格对称

---

## 8. 文件清单

```
documents/joystick_protocol.md                        本文档
firmware/joystick/octoaxes/control_panel_teensyLC.ino  joystick 主线固件（teensyLC）
firmware/joystick/octoaxesplus/control_panel_teensyLC.ino  joystick 双相机变体（byte-identical 同步）
firmware/octoaxes/joystick.cpp                        主控 joystick 接收 + CRC 闸门
firmware/octoaxes/joystick.h                          公开 API（含 joystick_print_stats）
firmware/octoaxesplus/joystick.{cpp,h}                byte-identical 同步
firmware/{octoaxes,octoaxesplus}/serial.cpp           S:JOYSTICK_STATS 调试命令分发
```

## 9. 变更历史

| 日期 | commit | 改动 |
|---|---|---|
| 协议 v1.0（pre-history）| — | 10 字节包，byte[9] 写死 0，无 CRC |
| 2026-05-18 | `fa625d1` | 协议 v1.1：byte[9] 引入兼容性闸门 + CRC-8-CCITT 校验 |
| 2026-05-18 | （本提交）| 协议落地文档 |
