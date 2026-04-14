# TMC2240 数据手册 - 精华摘要

**来源**: `tmc2240_datasheet.pdf`
**总页数**: 127 页
**提取目标**: 提取精华摘要
**处理时间**: 2026-03-16

---

## 1. 芯片概述

**TMC2240** — 36V 2A RMS+ 智能集成步进驱动器

### 核心特性

- 两个完全集成的 36V、5.0A 峰值 H 桥驱动器
- 非耗散集成电流传感 (ICS)，无需外部检流电阻
- 256 微步插值 (MicroPlyer)
- SPI (40-bit) + UART 单线接口
- StealthChop2 静音斩波 + SpreadCycle 高动态控制
- StallGuard2/4 无传感器失速检测
- CoolStep 负载自适应电流控制（节能 75%）
- 增量式编码器接口 (ABN)
- 内置温度/电压 ADC

### 关键参数

| 参数 | 值 |
|------|-----|
| 电源电压 | 4.5V ~ 36V |
| 峰值电流 (IMAX) | 5.0A (H 桥) |
| RMS 电流 (IRMS) | 2.1A (4 层 PCB) |
| 满量程电流 (IFS) | 最大 3.0A |
| 总 RON (HS+LS) | 0.23Ω 典型 |
| 微步分辨率 | 2 ~ 256 微步/全步 |
| SPI 最大时钟 | 10 MHz |
| 内部时钟 | 12.5 MHz |
| 外部时钟 | 8 ~ 20 MHz |
| 封装 | TQFN32 (5×5mm) / TSSOP38 (9.7×4.4mm) |
| 工作温度 | -40°C ~ 125°C |

---

## 2. SPI 通信协议

### 帧格式 (40-bit)

| 位范围 | 内容 | 说明 |
|--------|------|------|
| Bit 39 | W (WRITE_notREAD) | 1=写, 0=读 |
| Bit 38:32 | 地址 (7 bit) | 寄存器地址 0x00~0x7F |
| Bit 31:0 | 数据 (32 bit) | 寄存器数据 |
| 返回 Bit 39:32 | SPI_STATUS | 状态标志 |
| 返回 Bit 31:0 | 数据 (32 bit) | 读回数据 |

- **写操作**: 地址 OR 0x80
- **读操作**: 地址直接使用（需两次传输：第一次发请求，第二次取数据）
- **SPI 模式**: MODE 3
- **CSN**: 低电平有效，整个帧传输期间保持低

### SPI 状态字 (bits 39:32)

| Bit | 标志 | 说明 |
|-----|------|------|
| 3 | standstill | 电机静止 |
| 2 | sg2 | StallGuard2 标志 |
| 1 | driver_error | 驱动器错误 |
| 0 | reset_flag | 复位标志 |

### 关键时序

| 参数 | 值 |
|------|-----|
| SCK 最大频率 | 10 MHz |
| SDI 建立时间 | 10 ns (SCK 上升沿前) |
| SDI 保持时间 | 27 ns (SCK 上升沿后) |
| SDO 输出延迟 | 40 ns (SCK 下降沿后) |
| CSN 有效时间 | 20 ns |

---

## 3. 电流控制

### 电流计算公式

```
I_RMS = (CS_ACTUAL + 1) / 32 × (GLOBALSCALER / 256) × I_FS
```

其中：
- `CS_ACTUAL` = IRUN 或 IHOLD 值 (0-31)
- `GLOBALSCALER` = 全局缩放 (0=256 即满量程, 32-255)
- `I_FS` = 满量程电流，由 CURRENT_RANGE 决定

### 满量程电流设置 (DRV_CONF.CURRENT_RANGE)

| CURRENT_RANGE[1:0] | KIFS | IFS (最大) |
|---------------------|------|-----------|
| 00 (默认) | 11.75 | 1.0A |
| 01 | 24 | 2.0A |
| 10 或 11 | 36 | 3.0A |

外部电阻公式: `IFS = KIFS / RREF` (RREF = 12k~60kΩ)

### 关键寄存器

**IHOLD_IRUN (0x10)**:

| 位域 | 字段 | 默认值 | 说明 |
|------|------|--------|------|
| [4:0] | IHOLD | 8 | 静止电流 (0-31) |
| [12:8] | IRUN | 31 | 运行电流 (0-31) |
| [19:16] | IHOLDDELAY | 1 | 电流下降延迟 |
| [27:24] | IRUNDELAY | 4 | 电流上升延迟 |

**GLOBAL_SCALER (0x0B)**:

| 值 | 效果 |
|-----|------|
| 0 | 满量程 (等效 256) |
| 1-31 | 不允许 |
| 32-255 | 32/256 ~ 255/256 缩放，推荐 >128 |

---

## 4. 斩波器配置 (CHOPCONF 0x6C)

### 关键位域

| 位域 | 字段 | 默认 | 说明 |
|------|------|------|------|
| [3:0] | TOFF | 0 | 关闭时间 (0=禁用, 1-15: NCLK=24+32×TOFF) |
| [6:4] | HSTRT_TFD210 | 5 | 迟滞起始 (chm=0) |
| [10:7] | HEND_OFFSET | 2 | 迟滞终止 (-3~+12, chm=0) |
| [14] | CHM | 0 | 0=SpreadCycle, 1=恒定关闭时间 |
| [16:15] | TBL | 2 | 空白时间 (0=16, 1=24, 2=36, 3=54 clk) |
| [23:20] | TPFD | 4 | 被动快速衰减时间 |
| [27:24] | MRES | 0 | 微步: 0=256, 1=128, ..., 8=全步 |
| [28] | INTPOL | 1 | 256 微步插值使能 |
| [30] | DISS2G | 0 | 禁用短路 GND 保护 |
| [31] | DISS2VS | 0 | 禁用短路 VS 保护 |

### 推荐配置

**SpreadCycle 基础**: `TOFF=5, TBL=2, HSTRT=0, HEND=0`

**StealthChop2 基础**: `TOFF=3, TBL=2, HSTRT=4, HEND=0`

---

## 5. StealthChop2 (PWMCONF 0x70)

### GCONF 使能

| 位 | 字段 | 说明 |
|----|------|------|
| 2 | en_pwm_mode | 1=启用 StealthChop2 |
| 3 | multistep_filt | 1=启用步长输入滤波 |

### PWMCONF 关键参数

| 位域 | 字段 | 默认 | 说明 |
|------|------|------|------|
| [7:0] | PWM_OFS | 29 | PWM 幅度偏移 |
| [15:8] | PWM_GRAD | 0 | 速度相关梯度 |
| [17:16] | PWM_FREQ | 0 | PWM 频率选择 |
| [18] | pwm_autoscale | 1 | 自动幅度缩放 |
| [19] | pwm_autograd | 1 | 自动梯度调谐 |
| [21:20] | FREEWHEEL | 0 | IHOLD=0 空闲模式 |
| [27:24] | PWM_REG | 4 | 调节循环梯度 |
| [31:28] | PWM_LIM | 12 | PWM_SCALE_AUTO 上限 |

---

## 6. StallGuard4

### SG4_THRS (0x74)

| 位域 | 字段 | 说明 |
|------|------|------|
| [7:0] | SG4_THRS | 失速阈值 (SG4_RESULT < SG4_THRS → 失速) |
| [8] | sg4_filt_en | SG4 滤波使能 |
| [9] | sg_angle_offset | 自动相位补偿 |

### SG4_RESULT (0x75) — 只读

- 10 位结果，值越高→负载越低
- 每个全步更新一次
- 用于 StealthChop2 模式

---

## 7. CoolStep (COOLCONF 0x6D)

| 位域 | 字段 | 说明 |
|------|------|------|
| [3:0] | SEMIN | 0=禁用, 1-15: SG < SEMIN×32 → 增电流 |
| [6:5] | SEUP | 电流上升步宽 (0=1x, 1=2x, 2=4x, 3=8x) |
| [14:13] | SEDN | 电流下降速率 |
| [15] | SEIMIN | 最小电流 (0=IRUN/2, 1=IRUN/4) |
| [22:16] | SGT | StallGuard2 阈值 (-64~+63, 带符号) |
| [24] | SFILT | SG2 滤波 (1=每 4 全步更新) |

工作条件: `TCOOLTHRS < TSTEP < THIGH`

---

## 8. DRV_STATUS (0x6F) — 驱动状态

| 位 | 标志 | 说明 |
|----|------|------|
| [9:0] | SG_RESULT | StallGuard 结果 |
| [12] | S2VSA | 短路到 VS (A 相) |
| [13] | S2VSB | 短路到 VS (B 相) |
| [14] | STEALTH | StealthChop2 活跃 |
| [15] | FSACTIVE | 全步模式活跃 |
| [20:16] | CS_ACTUAL | 实际电流缩放 (0-31) |
| [24] | STALLGUARD | 失速检测标志 |
| [25] | OT | 过温关闭 |
| [26] | OTPW | 过温预警 |
| [27] | S2GA | 短路到 GND (A 相) |
| [28] | S2GB | 短路到 GND (B 相) |
| [29] | OLA | 开路 A 相 |
| [30] | OLB | 开路 B 相 |
| [31] | STST | 待机指示 |

---

## 9. 寄存器地址总表

| 地址 | 寄存器 | R/W | 功能 |
|------|--------|-----|------|
| 0x00 | GCONF | R/W | 全局配置 |
| 0x01 | GSTAT | R/WC | 全局状态 |
| 0x02 | IFCNT | R | 接口计数 |
| 0x03 | NODECONF | R/W | UART 节点配置 |
| 0x04 | IOIN | R | I/O 输入状态 |
| 0x0A | DRV_CONF | R/W | 驱动配置 (CURRENT_RANGE, SLOPE) |
| 0x0B | GLOBAL_SCALER | R/W | 全局电流缩放 |
| 0x10 | IHOLD_IRUN | R/W | 运行/静止电流 |
| 0x11 | TPOWERDOWN | R/W | 掉电延迟 |
| 0x12 | TSTEP | R | 实测步长时间 |
| 0x13 | TPWMTHRS | R/W | StealthChop↔SpreadCycle 切换阈值 |
| 0x14 | TCOOLTHRS | R/W | CoolStep 速度下限 |
| 0x15 | THIGH | R/W | CoolStep 速度上限 |
| 0x2D | DIRECT_MODE | R/W | 直接线圈控制 |
| 0x38 | ENCMODE | R/W | 编码器模式 |
| 0x60-0x69 | MSLUT | R/W | 微步查找表 |
| 0x6C | CHOPCONF | R/W | 斩波器配置 |
| 0x6D | COOLCONF | R/W | CoolStep 配置 |
| 0x6F | DRV_STATUS | R | 驱动状态 |
| 0x70 | PWMCONF | R/W | PWM 配置 |
| 0x71 | PWM_SCALE | R | PWM 缩放状态 |
| 0x72 | PWM_AUTO | R | PWM 自动值 |
| 0x74 | SG4_THRS | R/W | StallGuard4 阈值 |
| 0x75 | SG4_RESULT | R | StallGuard4 结果 |
| 0x76 | SG4_IND | R | StallGuard4 滤波 |

---

## 10. 电气特性

### 电源

| 参数 | 最小 | 典型 | 最大 | 单位 |
|------|------|------|------|------|
| VS 范围 | 4.5 | - | 36 | V |
| 睡眠电流 | - | 4 | 18 | μA |
| 静态工作电流 | 3.5 | - | 5.5 | mA |

### 输出 MOSFET

| 参数 | 典型值 | 单位 |
|------|--------|------|
| 低端 RON | 0.11~0.15 | Ω |
| 高端 RON | 0.12~0.28 | Ω |
| 总 RON | 0.23 | Ω |
| 输出转换速率 | 100~800 (可编程) | V/μs |

### 保护

| 功能 | 阈值 |
|------|------|
| 过温关闭 | 165°C |
| 欠压 (VS) | 4.15V |
| 欠压 (VCC_IO) | 1.95V |
| 过流保护 | 可配置 (12μs~2.3s 消隐) |

---

## 11. 封装

| 封装 | 尺寸 | 引脚 | 热阻 θJA (4层) | 热阻 θJC |
|------|------|------|----------------|----------|
| TQFN32 | 5×5mm | 32 | 1.7°C/W | 29°C/W |
| TSSOP38 | 9.7×4.4mm | 38 | 25°C/W | 1°C/W |

---

## 12. 与 TMC2660 对比 (Octoaxes 项目关注点)

| 方面 | TMC2660 | TMC2240 |
|------|---------|---------|
| SPI 帧长度 | 20-bit (3 字节) | 40-bit (5 字节) |
| 电流控制 | CS 0-31, V_FS=0.310V | IRUN 0-31 + GLOBALSCALER, V_FS 由 CURRENT_RANGE 决定 |
| 斩波模式 | SpreadCycle only | SpreadCycle + StealthChop2 |
| StallGuard | SG2 (7-bit SGT) | SG2 + SG4 (支持 StealthChop) |
| CoolStep | 有 | 有 (增强版) |
| 电流传感 | 外部检流电阻 | 集成 ICS (无需外部电阻) |
| SDOFF 模式 | 有 (SPI 微步控制) | 无 (始终 STEP/DIR) |
| 寄存器宽度 | 20-bit | 32-bit |
| Cover 接口 | COVER_LOW (24-bit) | COVER_HIGH + COVER_LOW (40-bit) |

---

## 处理详情

- 处理块数: 4 块 (pdftotext 转换后分块读取)
- 有效内容块: 4/4 块
