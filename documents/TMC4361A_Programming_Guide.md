# TMC4361A 编程手册

本手册基于 TMC4361 Datasheet Rev3.10 编写，用于 Octoaxes 项目的固件开发参考。

> **注意**: 对于新设计，建议使用升级版 TMC4361A。本手册内容同时适用于 TMC4361 和 TMC4361A。

---

## 目录

1. [概述](#1-概述)
2. [引脚定义](#2-引脚定义)
3. [SPI 通信接口](#3-spi-通信接口)
4. [输入滤波](#4-输入滤波)
5. [状态标志和事件](#5-状态标志和事件)
6. [斜坡配置](#6-斜坡配置)
7. [高级斜坡配置](#7-高级斜坡配置)
8. [斜坡发生器单位](#8-斜坡发生器单位)
9. [外部步进控制和电子齿轮](#9-外部步进控制和电子齿轮)
10. [参考开关](#10-参考开关)
11. [虚拟停止开关](#11-虚拟停止开关)
12. [原点参考配置](#12-原点参考配置)
13. [目标到达和位置比较](#13-目标到达和位置比较)
14. [重复和循环运动](#14-重复和循环运动)
15. [斜坡定时和同步](#15-斜坡定时和同步)
16. [影子寄存器设置](#16-影子寄存器设置)
17. [目标管线](#17-目标管线)
18. [无主同步](#18-无主同步)
19. [SPI 输出接口](#19-spi-输出接口)
20. [电流缩放](#20-电流缩放)
21. [紧急停止 (NFREEZE)](#21-紧急停止-nfreeze)
22. [可控 PWM 输出](#22-可控-pwm-输出)
23. [dcStep 支持](#23-dcstep-支持)
24. [编码器接口](#24-编码器接口)
25. [编码器反馈调节](#25-编码器反馈调节)
26. [闭环操作详解](#26-闭环操作详解)
27. [寄存器详细定义（补充）](#27-寄存器详细定义补充)
28. [电气特性](#28-电气特性)
29. [封装信息](#29-封装信息)

---

## 1. 概述

### 1.1 功能特性

TMC4361 是一款高性能步进电机运动控制器，专为快速、无抖动的运动曲线应用设计。

**主要特性：**
- SPI 接口与微控制器通信（易用协议）
- SPI 接口与步进电机驱动器通信
- 编码器接口（增量式 ABN 或串行 SSI/SPI）
- 闭环操作支持
- 集成 ChopSync™ 和 dcStep™ 支持
- 内部斜坡发生器（S 形斜坡或 sixPoint™ 斜坡，支持实时修改）
- 可控 PWM 输出
- 参考开关处理
- 硬件和虚拟停止开关

### 1.2 应用场景

- 纺织、缝纫机
- CCTV、安防
- 打印机、扫描仪
- ATM、现金循环机
- 办公自动化
- POS 系统
- 工厂自动化
- 实验室自动化
- 泵和阀门
- 太阳能跟踪器
- CNC 机床
- 机器人

### 1.3 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         主控 CPU (SPI)                           │
├─────────────────────────────────────────────────────────────────┤
│                          TMC4361A                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐ │
│  │ 寄存器块  │  │ 斜坡发生器│  │  状态/   │  │   SPI 输出接口   │ │
│  │          │  │ S-ramp   │  │ 中断控制 │  │  (驱动器通信)    │ │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘ │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐ │
│  │ 参考开关 │  │ 编码器   │  │ 闭环控制 │  │   Step/Dir 输出  │ │
│  │  处理    │  │ 解码器   │  │          │  │                  │ │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│              TMC26x / TMC2660 / TMC2130 步进驱动器               │
└─────────────────────────────────────────────────────────────────┘
```

### 1.4 封装信息

| 订购代码 | 描述 | 尺寸 |
|---------|------|------|
| TMC4361-LA | 带闭环和 dcStep 功能的运动控制器，QFN40 | 6 x 6 mm² |

---

## 2. 引脚定义

### 2.1 引脚分布图 (QFN40, 6mm x 6mm, 0.5mm pitch)

```
                    40 39 38 37 36 35 34 33 32 31
                   ┌──┴──┴──┴──┴──┴──┴──┴──┴──┴──┐
        ANEG_NSCLK │1                          30│ NSCSDRV_SDO
            NSCSIN │2                          29│ SCKDRV_NSDO
             SCKIN │3                          28│ SDIDRV_NSCLK
             SDIIN │4      TMC4361             27│ SDODRV_SCLK
               VCC │5      QFN 40              26│ VCC
               GND │6      6mm x 6mm           25│ GND
             SDOIN │7      0.5 pitch           24│ STPOUT_PWMA
               MP1 │8                          23│ DIROUT_PWMB
               MP2 │9                          22│ NNEG
             B_SDI │10                         21│ N
                   └──┬──┬──┬──┬──┬──┬──┬──┬──┬──┘
                    11 12 13 14 15 16 17 18 19 20

Pin 11: BNEG_NSDI      Pin 16: VDD1V8
Pin 12: STOPL          Pin 17: STPIN
Pin 13: HOME_REF       Pin 18: DIRIN
Pin 14: STOPR          Pin 19: NFREEZE
Pin 15: GND            Pin 20: START

Pin 40: A_SCLK         Pin 35: VDD1V8
Pin 39: NRST           Pin 34: TEST_MODE
Pin 38: CLK_EXT        Pin 33: INTR
Pin 37: VCC            Pin 32: STDBY_CLK
Pin 36: GND            Pin 31: TARGET_REACHED
```

### 2.2 引脚功能表

#### 电源引脚

| 引脚名 | 引脚号 | 类型 | 功能 |
|--------|--------|------|------|
| GND | 6, 15, 25, 36 | 电源地 | IO 和数字电路的地 |
| VCC | 5, 26, 37 | 电源 | IO 和数字电路电源 (3.3V ~ 5V) |
| VDD1V8 | 16, 35 | 电源 | 内部产生的 1.8V 核心电压连接 |
| CLK_EXT | 38 | 输入 | 外部时钟输入，频率 fCLK |
| NRST | 39 | 输入(上拉) | 低电平有效复位，内部上拉 |
| TEST_MODE | 34 | 输入 | 测试模式：VCC=3.3V 时接地；VCC=5V 时接 VDD1V8 |
| NFREEZE | 19 | 输入(上拉) | 低电平有效，立即冻结输出操作 |

#### 微控制器接口引脚

| 引脚名 | 引脚号 | 类型 | 功能 |
|--------|--------|------|------|
| NSCSIN | 2 | 输入 | SPI 片选（低电平有效） |
| SCKIN | 3 | 输入 | SPI 时钟 |
| SDIIN | 4 | 输入 | SPI 数据输入 |
| SDOIN | 7 | 输出 | SPI 数据输出（NSCSIN=1 时高阻） |
| INTR | 33 | 输出 | 中断输出，可配置上拉/下拉 |
| TARGET_REACHED | 31 | 输出 | 目标到达输出 |
| STDBY_CLK | 32 | 输出 | 待机信号或内部时钟输出或 ChopSync 输出 |

#### 参考开关引脚

| 引脚名 | 引脚号 | 类型 | 功能 |
|--------|--------|------|------|
| STOPL | 12 | 输入(下拉) | 左限位开关，停止斜坡的外部信号 |
| HOME_REF | 13 | 输入(下拉) | 原点参考信号输入 |
| STOPR | 14 | 输入(下拉) | 右限位开关 |
| STPIN | 17 | 输入(下拉) | 外部步进控制的步进输入 |
| DIRIN | 18 | 输入(下拉) | 外部步进控制的方向输入 |
| START | 20 | 双向 | 启动信号输入/输出 |

#### Step/Dir 输出引脚

| 引脚名 | 引脚号 | 类型 | 功能 |
|--------|--------|------|------|
| STPOUT_PWMA | 24 | 输出 | 步进输出 / PWM 正弦信号 / DAC 输出 |
| DIROUT_PWMB | 23 | 输出 | 方向输出 / PWM 余弦信号 / DAC 输出 |

#### 驱动器接口引脚

| 引脚名 | 引脚号 | 类型 | 功能 |
|--------|--------|------|------|
| NSCSDRV_SDO | 30 | 输出 | 驱动器 SPI 片选 / 编码器 SDO |
| SCKDRV_NSDO | 29 | 输出 | 驱动器 SPI 时钟 / MDBN |
| SDODRV_SCLK | 27 | 双向 | 驱动器 SPI 数据输出 / 编码器时钟 |
| SDIDRV_NSCLK | 28 | 输入(下拉) | 驱动器 SPI 数据输入 |
| MP1 | 8 | 输入(下拉) | dcStep 输入控制信号 |
| MP2 | 9 | 双向 | dcStep 输出控制信号 |

#### 编码器接口引脚

| 引脚名 | 引脚号 | 类型 | 功能 |
|--------|--------|------|------|
| A_SCLK | 40 | 双向 | 增量编码器 A 信号 / SSI/SPI 时钟 |
| ANEG_NSCLK | 1 | 双向 | 编码器 A 反相 / SSI 反相时钟 / SPI 编码器片选 |
| B_SDI | 10 | 输入(下拉) | 增量编码器 B 信号 / SSI/SPI 数据输入 |
| BNEG_NSDI | 11 | 双向 | 编码器 B 反相 / SSI 反相数据 / SPI 编码器数据输出 |
| N | 21 | 输入(下拉) | 增量编码器 N（索引）信号 |
| NNEG | 22 | 输入(下拉) | 编码器 N 反相信号 |

---

## 3. SPI 通信接口

### 3.1 SPI 数据报结构

TMC4361 使用 **40 位 SPI 数据报** 与微控制器通信。

```
┌─────────────────────────────────────────────────────────────────┐
│                    40 位 SPI 数据报                              │
├────────────┬────────────────────────────────────────────────────┤
│  bit 39-32 │                    bit 31-0                        │
├────────────┼────────────────────────────────────────────────────┤
│  发送: 地址 │                    发送: 32位数据                   │
│  接收: 状态 │                    接收: 32位数据                   │
└────────────┴────────────────────────────────────────────────────┘
```

**地址字节格式：**
- bit 39 (MSB): 读/写选择位
  - `0` = 读操作
  - `1` = 写操作
- bit 38-32: 7 位寄存器地址

**关键要点：**
- NSCSIN 必须在整个数据传输期间保持低电平
- 数据在 SCKIN 上升沿采样，在下降沿输出
- MSB 先发送
- 最少需要 40 个时钟周期

### 3.2 读写操作示例

```c
// 写操作：地址需要加 0x80
// 读取 XACTUAL (地址 0x21)
spi_transfer(0x21, 0x00000000);  // 第一次：发送读请求
data = spi_transfer(0x21, 0x00000000);  // 第二次：获取数据

// 写入 VMAX (地址 0x24)
spi_transfer(0x80 | 0x24, velocity_value);  // 地址 = 0xA4
```

| 操作 | 发送数据 | 接收数据 |
|------|----------|----------|
| 读 XACTUAL | `0x2100000000` | `0xSS` + 无效数据 |
| 读 XACTUAL | `0x2100000000` | `0xSS` + XACTUAL |
| 写 VMAX=0x00ABCDEF | `0xA400ABCDEF` | `0xSS` + XACTUAL |
| 写 VMAX=0x00123456 | `0xA400123456` | `0xSS00ABCDEF` |

> `SS` 是 8 位 SPI 状态位

### 3.3 SPI 时序参数

| 参数 | 符号 | 最小值 | 典型值 | 单位 |
|------|------|--------|--------|------|
| SCKIN 频率 (fCLK=16MHz) | fSCK | - | fCLK/4 = 4 | MHz |
| SCKIN 变化前后 NSCSIN 有效时间 | tCC | 10 | - | ns |
| SDIIN 建立时间 | tDU | 10 | - | ns |
| SDIIN 保持时间 | tDH | 10 | - | ns |

### 3.4 代码示例

```c
// TMC4361 SPI 读写函数
uint32_t tmc4361_read(uint8_t address) {
    uint8_t tx[5] = {address & 0x7F, 0, 0, 0, 0};
    uint8_t rx[5];

    // 第一次传输：发送读请求
    spi_transfer(tx, rx, 5);

    // 第二次传输：获取数据
    spi_transfer(tx, rx, 5);

    return (rx[1] << 24) | (rx[2] << 16) | (rx[3] << 8) | rx[4];
}

void tmc4361_write(uint8_t address, uint32_t data) {
    uint8_t tx[5] = {
        address | 0x80,           // 写标志
        (data >> 24) & 0xFF,
        (data >> 16) & 0xFF,
        (data >> 8) & 0xFF,
        data & 0xFF
    };
    uint8_t rx[5];

    spi_transfer(tx, rx, 5);
}
```

---

## 4. 输入滤波

### 4.1 滤波组分配

TMC4361 提供数字滤波功能，输入引脚分为以下几组：

| 滤波组 | 引脚 | 参数 |
|--------|------|------|
| 编码器输入 | A, B, N, ANEG, BNEG, NNEG | SR_ENC_IN, FILT_L_ENC_IN |
| 参考输入 | STOPL, HOME_REF, STOPR | SR_REF, FILT_L_REF |
| START 输入 | START | SR_S, FILT_L_S |
| 串行编码器时钟 | SDODRV_SCLK, SDIDRV_NSCLK | SR_ENC_OUT, FILT_L_ENC_OUT |
| Step/Dir 输入 | STPIN, DIRIN | 可分配到上述任一组 |

### 4.2 滤波配置寄存器

**INPUT_FILT_CONF (0x03)** 寄存器结构：

```
bit 31-24: [保留][SR_ENC_OUT][FILT_L_ENC_OUT]
bit 23-16: [FILT_L_S][SR_S]
bit 15-8:  [FILT_L_REF][SR_REF]
bit 7-0:   [FILT_L_ENC_IN][SR_ENC_IN]
```

### 4.3 采样率配置 (SR)

采样率 = fCLK / 2^SR

| SR 值 | 采样率 |
|-------|--------|
| 0 | fCLK |
| 1 | fCLK/2 |
| 2 | fCLK/4 |
| 3 | fCLK/8 |
| 4 | fCLK/16 |
| 5 | fCLK/32 |
| 6 | fCLK/64 |
| 7 | fCLK/128 |

### 4.4 滤波长度配置 (FILT_L)

| FILT_L 值 | 滤波长度 |
|-----------|----------|
| 0 | 无滤波 |
| 1 | 2 个相同位 |
| 2 | 3 个相同位 |
| 3 | 4 个相同位 |
| 4 | 5 个相同位 |
| 5 | 6 个相同位 |
| 6 | 7 个相同位 |
| 7 | 8 个相同位 |

---

## 5. 状态标志和事件

### 5.1 相关寄存器

| 寄存器名 | 地址 | 读写 | 功能 |
|----------|------|------|------|
| GENERAL_CONF | 0x00 | RW | 通用配置，包含状态相关位 |
| STATUS_FLAGS | 0x0F | R | 32 个状态标志 |
| EVENTS | 0x0E | R+C | 32 个事件（读后清除） |
| SPI_STATUS_SELECTION | 0x0B | RW | 选择 8 个事件用于 SPI 状态 |
| EVENT_CLEAR_CONF | 0x0C | RW | 事件清除例外配置 |
| INTR_CONF | 0x0D | RW | 选择触发 INTR 的事件 |

### 5.2 状态标志与事件的区别

- **状态标志 (STATUS_FLAGS)**: 反映当前状态
- **状态事件 (EVENTS)**: 指示状态变化，读取后自动清除

### 5.3 SPI 状态位配置

每次 SPI 传输的前 8 位返回选定的状态事件：

```c
// 选择 TARGET_REACHED 事件作为 SPI 状态位
tmc4361_write(SPI_STATUS_SELECTION, (1 << TARGET_REACHED_EVENT));
```

### 5.4 中断配置

```c
// 配置中断触发事件
tmc4361_write(INTR_CONF, (1 << TARGET_REACHED_EVENT) | (1 << STOPL_EVENT));

// 配置中断极性 (GENERAL_CONF bit 15)
// 0 = 低电平有效（默认）
// 1 = 高电平有效
```

### 5.5 防止事件丢失

如果事件在读取 EVENTS 寄存器时触发，可能会丢失。解决方法：

```c
// 设置 EVENT_CLEAR_CONF 对应位为 1，防止该事件被清除
tmc4361_write(EVENT_CLEAR_CONF, (1 << important_event));

// 需要清除时：
tmc4361_write(EVENT_CLEAR_CONF, 0);  // 允许清除
tmc4361_read(EVENTS);                 // 读取并清除
tmc4361_write(EVENT_CLEAR_CONF, (1 << important_event));  // 恢复保护
```

---

## 6. 斜坡配置

### 6.1 Step/Dir 输出配置

#### 相关寄存器

| 寄存器名 | 地址 | 功能 |
|----------|------|------|
| GENERAL_CONF | 0x00 | 斜坡发生器配置 (bit 5:0) |
| STP_LENGTH_ADD | 0x10 (低16位) | 步进脉冲附加长度（时钟周期） |
| DIR_SETUP_TIME | 0x10 (高16位) | 方向改变后的建立时间 |

#### 步进脉冲长度

```c
// 步进脉冲长度 = STP_LENGTH_ADD + 1 个时钟周期
tmc4361_write(0x10, (dir_setup << 16) | stp_length);
```

#### 极性配置

```c
// GENERAL_CONF 寄存器相关位
// bit 3: step_inactive_pol - 步进输出极性
//        0 = 上升沿表示步进（默认）
//        1 = 下降沿表示步进
// bit 4: toggle_step - 每次电平变化都表示步进
// bit 5: pol_dir_out - 方向输出极性
//        0 = 高电平表示正方向
//        1 = 高电平表示负方向（默认）
// bit 28: reverse_motor_dir - 反转电机方向
```

### 6.2 运动模式和斜坡类型

#### RAMPMODE 寄存器 (0x20)

| RAMPMODE[2:0] | 运动模式 | 斜坡类型 | 说明 |
|---------------|----------|----------|------|
| b'000 | 速度模式 | 无斜坡 | 直接跳到 VMAX |
| b'001 | 速度模式 | 梯形斜坡 | 使用加速度和减速度值 |
| b'010 | 速度模式 | S 形斜坡 | 使用弓形值平滑加减速 |
| b'100 | 定位模式 | 无斜坡 | 直接以 VMAX 运动到目标 |
| b'101 | 定位模式 | 梯形斜坡 | 平滑定位 |
| b'110 | 定位模式 | S 形斜坡 | 最平滑的定位 |

### 6.3 斜坡相关寄存器

| 寄存器名 | 地址 | 位宽 | 说明 |
|----------|------|------|------|
| RAMPMODE | 0x20 | 3 | 运动模式和斜坡类型 |
| XACTUAL | 0x21 | 32 (有符号) | 当前位置 |
| VACTUAL | 0x22 | 24 (有符号) | 当前速度（只读） |
| AACTUAL | 0x23 | 24 (有符号) | 当前加速度（只读） |
| VMAX | 0x24 | 32 (24+8) | 最大/目标速度 |
| VSTART | 0x25 | 31 (23+8) | 起始速度 |
| VSTOP | 0x26 | 31 (23+8) | 停止速度 |
| VBREAK | 0x27 | 31 (23+8) | 加速度切换点速度 |
| AMAX | 0x28 | 24 (22+2) | 最大加速度 |
| DMAX | 0x29 | 24 (22+2) | 最大减速度 |
| ASTART | 0x2A | 24 (22+2) | 起始加速度 |
| DFINAL | 0x2B | 24 (22+2) | 最终减速度 |
| BOW1 | 0x2D | 24 | 第一弓形值 |
| BOW2 | 0x2E | 24 | 第二弓形值 |
| BOW3 | 0x2F | 24 | 第三弓形值 |
| BOW4 | 0x30 | 24 | 第四弓形值 |
| CLK_FREQ | 0x31 | 25 | 外部时钟频率 |
| XTARGET | 0x37 | 32 (有符号) | 目标位置 |

### 6.4 无斜坡模式

速度立即跳到 VMAX，无加减速过程。

```c
// 速度模式 - 无斜坡
tmc4361_write(RAMPMODE, 0b000);
tmc4361_write(VMAX, target_velocity);

// 定位模式 - 无斜坡
tmc4361_write(RAMPMODE, 0b100);
tmc4361_write(VMAX, max_velocity);
tmc4361_write(XTARGET, target_position);
```

### 6.5 梯形斜坡

```
v(t)
  ^
  │      ┌────────────┐
VMAX ----│            │
  │     /│            │\
  │    / │     A2     │ \
  │   /  │            │  \
  │  / A1│            │A3 \
  │ /    │            │    \
  └──────┴────────────┴──────────> t
```

#### 简单梯形斜坡（无断点）

```c
tmc4361_write(RAMPMODE, 0b001);
tmc4361_write(VBREAK, 0);           // 无断点
tmc4361_write(AMAX, acceleration);   // 加速度
tmc4361_write(DMAX, deceleration);   // 减速度
tmc4361_write(VMAX, target_velocity);
```

#### 带断点的梯形斜坡（sixPoint 斜坡）

```
v(t)
  ^
VMAX ----┌────────────┐
         │            │
VBREAK --├────┐  ┌────┤
         │    │  │    │
         │    │  │    │
         └────┴──┴────┴────> t
         A1L A1 A2 A3 A3L
```

```c
tmc4361_write(RAMPMODE, 0b001);
tmc4361_write(VBREAK, break_velocity);  // 加速度切换点
tmc4361_write(AMAX, acceleration_high);  // VBREAK 以上的加速度
tmc4361_write(DMAX, deceleration_high);  // VBREAK 以上的减速度
tmc4361_write(ASTART, acceleration_low); // VBREAK 以下的加速度
tmc4361_write(DFINAL, deceleration_low); // VBREAK 以下的减速度
tmc4361_write(VMAX, target_velocity);
```

### 6.6 S 形斜坡

S 形斜坡提供无抖动的运动曲线，加速度平滑变化。

```
v(t)
  ^
  │        ┌──────────────────┐
VMAX ─────│                  │
  │      ╱│                  │╲
  │    ╱  │       B23        │  ╲
  │  ╱    │                  │    ╲
  │╱  B1  │B12  B2      B3 B34│ B4  ╲
  └───────┴──────────────────┴───────> t
```

**斜坡阶段：**
- **B1**: BOW1 增加加速度（从 ASTART 到 AMAX）
- **B12**: 保持 AMAX
- **B2**: BOW2 减小加速度（从 AMAX 到 0）
- **B23**: 匀速段
- **B3**: BOW3 增加减速度（从 0 到 -DMAX）
- **B34**: 保持 -DMAX
- **B4**: BOW4 减小减速度（从 -DMAX 到 -DFINAL）

#### 标准 S 形斜坡配置

```c
tmc4361_write(RAMPMODE, 0b010);
tmc4361_write(ASTART, 0);            // 从 0 加速度开始
tmc4361_write(DFINAL, 0);            // 减速到 0
tmc4361_write(AMAX, max_acceleration);
tmc4361_write(DMAX, max_deceleration);
tmc4361_write(BOW1, bow1_value);     // 加速度增加率
tmc4361_write(BOW2, bow2_value);     // 加速度减小率
tmc4361_write(BOW3, bow3_value);     // 减速度增加率
tmc4361_write(BOW4, bow4_value);     // 减速度减小率
tmc4361_write(VMAX, target_velocity);
```

#### 带初始/最终加速度的 S 形斜坡

```c
tmc4361_write(RAMPMODE, 0b010);
tmc4361_write(ASTART, start_acceleration);  // 非零起始加速度
tmc4361_write(DFINAL, final_deceleration);  // 非零最终减速度
// ... 其他参数同上
```

### 6.7 起始和停止速度

可以配置非零的起始和停止速度：

```c
tmc4361_write(VSTART, start_velocity);  // 起始速度
tmc4361_write(VSTOP, stop_velocity);    // 停止速度
```

**注意事项：**
- 在定位模式下，确保 XACTUAL 和 XTARGET 之间有足够的距离
- VSTOP > VSTART 时需要特别注意

### 6.8 定位模式注意事项

```c
// 在定位模式下停止运动的正确方法：
// 1. 首先将 VMAX 设为 1（不是 0！）
tmc4361_write(VMAX, 1);

// 2. 等待 VEL_REACHED_F 标志
while(!(tmc4361_read(STATUS_FLAGS) & VEL_REACHED_F));

// 3. 然后设置 VMAX = 0
tmc4361_write(VMAX, 0);
```

### 6.9 AACTUAL 值对照表

#### 梯形斜坡

| 斜坡阶段 | A1L | A1 | A2 | A3 | A3L |
|----------|-----|----|----|----|----|
| v > 0 时 | ASTART | AMAX | 0 | -DMAX | -DFINAL |
| v < 0 时 | -ASTART | -AMAX | 0 | DMAX | DFINAL |

#### S 形斜坡

| 斜坡阶段 | B1 | B12 | B2 | B23 | B3 | B34 | B4 |
|----------|-------|------|--------|-----|--------|------|---------|
| v > 0 时 AACTUAL | ASTART→AMAX | AMAX | AMAX→0 | 0 | 0→-DMAX | -DMAX | -DMAX→-DFINAL |
| v > 0 时 BOWACTUAL | BOW1 | 0 | -BOW2 | 0 | -BOW3 | 0 | BOW4 |
| v < 0 时 AACTUAL | -ASTART→-AMAX | -AMAX | -AMAX→0 | 0 | 0→DMAX | DMAX | DMAX→DFINAL |
| v < 0 时 BOWACTUAL | -BOW1 | 0 | BOW2 | 0 | BOW3 | 0 | -BOW4 |

---

## 附录 A: 寄存器地址速查表

| 地址 | 寄存器名 | 读写 |
|------|----------|------|
| 0x00 | GENERAL_CONF | RW |
| 0x01 | REFERENCE_CONF | RW |
| 0x02 | START_CONF | RW |
| 0x03 | INPUT_FILT_CONF | RW |
| 0x04 | SPI_OUT_CONF | RW |
| 0x05 | CURRENT_CONF | RW |
| 0x06 | SCALE_VALUES | RW |
| 0x07 | ENC_IN_CONF | RW |
| 0x08 | ENC_IN_DATA | RW |
| 0x09 | ENC_OUT_DATA | RW |
| 0x0A | STEP_CONF | RW |
| 0x0B | SPI_STATUS_SELECTION | RW |
| 0x0C | EVENT_CLEAR_CONF | RW |
| 0x0D | INTR_CONF | RW |
| 0x0E | EVENTS | R+C |
| 0x0F | STATUS_FLAGS | R |
| 0x10 | STP_LENGTH_ADD / DIR_SETUP_TIME | RW |
| 0x20 | RAMPMODE | RW |
| 0x21 | XACTUAL | RW |
| 0x22 | VACTUAL | R |
| 0x23 | AACTUAL | R |
| 0x24 | VMAX | RW |
| 0x25 | VSTART | RW |
| 0x26 | VSTOP | RW |
| 0x27 | VBREAK | RW |
| 0x28 | AMAX | RW |
| 0x29 | DMAX | RW |
| 0x2A | ASTART | RW |
| 0x2B | DFINAL | RW |
| 0x2D | BOW1 | RW |
| 0x2E | BOW2 | RW |
| 0x2F | BOW3 | RW |
| 0x30 | BOW4 | RW |
| 0x31 | CLK_FREQ | RW |
| 0x37 | XTARGET | RW |

---

## 7. 高级斜坡配置

### 7.1 带初始速度的 S 形斜坡

S 形斜坡可以从非零速度开始。

**配置步骤：**

```c
// 设置 S 形斜坡模式
tmc4361_write(RAMPMODE, 0b010);

// 设置初始速度
tmc4361_write(VSTART, start_velocity);  // VSTART > 0
tmc4361_write(VSTOP, 0);

// 其他斜坡参数
tmc4361_write(AMAX, acceleration);
tmc4361_write(DMAX, deceleration);
tmc4361_write(BOW1, bow1);
// ... 设置其他 BOW 值
```

**原理：**
- 初始加速度值等于 AMAX，不考虑 ASTART 参数
- 因此，斜坡阶段 B1 不执行

```
v(t)
  ^
  │        ┌────────────────┐
VMAX ─────│                │
  │      ╱│                │╲
  │    ╱  │      B23       │  ╲
  │  ╱    │                │    ╲
VSTART ──│ B12  B2         B3 B34│B4
  │      │                │
  └──────┴────────────────┴──────> t
         跳过 B1
```

> **注意**：定位模式下使用 VSTART 时，确保 XACTUAL 和 XTARGET 之间有足够距离，否则可能导致定位过冲。

### 7.2 带停止速度的斜坡

斜坡可以以非零速度结束（VSTOP > 0）。

#### 梯形斜坡带停止速度

```c
tmc4361_write(RAMPMODE, 0b001);
tmc4361_write(VSTART, 0);
tmc4361_write(VSTOP, stop_velocity);  // VSTOP > 0
```

> **注意**：使用 VBREAK > 0 时，确保 VBREAK > VSTOP 且 VSTART < VSTOP。

#### S 形斜坡带停止速度

```c
tmc4361_write(RAMPMODE, 0b010);
tmc4361_write(VSTART, 0);
tmc4361_write(VSTOP, stop_velocity);  // VSTOP > 0
```

**原理：**
- 最终减速度值等于 DMAX，不考虑 DFINAL 参数
- 因此，斜坡阶段 B4 不执行

### 7.3 VSTART 和 VSTOP 的交互

- VSTART 和 VSTOP 仅用于启动或结束速度斜坡
- 如果斜坡进行中改变速度方向，不使用 VSTART 或 VSTOP
- VSTOP 在定位模式下可用于到达目标位置时
- 在速度模式下，VSTOP 仅当 VACTUAL ≠ 0 且目标速度 VMAX = 0 时可用
- VSTART 和 VSTOP 是无符号值，对两个速度方向都有效

### 7.4 VSTART 和 ASTART 联合使用

对于某些应用，需要从定义的速度开始但不使用最大加速度 AMAX。

```c
tmc4361_write(RAMPMODE, 0b010);
tmc4361_write(VSTART, start_velocity);
tmc4361_write(VSTOP, stop_velocity);

// 启用 ASTART 和 VSTART 联合使用
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 0);  // use_astart_and_vstart = 1
tmc4361_write(GENERAL_CONF, general_conf);
```

**结果：**
- 阶段 B1 会执行，即使使用了 VSTART
- 可以从零加速度开始（ASTART = 0）
- 或从小于 AMAX 的加速度开始（0 < ASTART < AMAX）

### 7.5 sixPoint 斜坡

sixPoint 斜坡是带有初始和停止速度值的梯形斜坡，同时使用两个加速度和两个减速度值。

```c
tmc4361_write(RAMPMODE, 0b001);
tmc4361_write(VSTART, start_velocity);   // VSTART > 0
tmc4361_write(VSTOP, stop_velocity);     // VSTOP > 0
tmc4361_write(VBREAK, break_velocity);   // VBREAK > 0
tmc4361_write(AMAX, acceleration_high);   // VBREAK 以上的加速度
tmc4361_write(ASTART, acceleration_low);  // VBREAK 以下的加速度
tmc4361_write(DMAX, deceleration_high);   // VBREAK 以上的减速度
tmc4361_write(DFINAL, deceleration_low);  // VBREAK 以下的减速度
```

```
v(t)
  ^
VMAX ────┌────────────┐
         │            │
VBREAK ──├────┐  ┌────┤
         │    │  │    │
VSTOP ───┤    │  │    ├───
VSTART ──┤    │  │    │
         └────┴──┴────┴────> t
         A1L A1 A2 A3 A3L
```

> **注意**：确保 VBREAK > VSTOP 且 VSTART < VSTOP。

---

## 8. 斜坡发生器单位

### 8.1 时钟频率

必须设置 CLK_FREQ 寄存器 (0x31) 为外部时钟频率 fCLK 的值（Hz）。

- 支持范围：4.2 MHz ~ 32 MHz
- 默认配置：16 MHz

```c
tmc4361_write(CLK_FREQ, 16000000);  // 16 MHz
```

### 8.2 速度值单位

速度值定义为每秒脉冲数 [pps]。

| 寄存器 | 位宽 | 格式 |
|--------|------|------|
| VACTUAL | 32 位有符号 | 无小数位 |
| VSTART, VSTOP, VBREAK | 31 位无符号 | 23 位整数 + 8 位小数 |
| VMAX | 32 位有符号 | 24 位整数 + 8 位小数 |

**最大速度限制：**

```
VMAX ≤ ½ · fCLK
```

> **注意**：如果 VACTUAL 超过此限制，STPOUT 输出将产生错误的步进脉冲。

### 8.3 加速度值单位

加速度值默认定义为每秒²脉冲数 [pps²]。

| 寄存器 | 位宽 | 格式 |
|--------|------|------|
| AMAX, DMAX, ASTART, DFINAL, DSTOP | 24 位无符号 | 22 位整数 + 2 位小数 |
| AACTUAL | 32 位有符号 | 无小数位 |

#### 直接加速度模式（用于短而陡峭的斜坡）

```c
// 启用直接加速度值模式
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 1);  // direct_acc_val_en = 1
tmc4361_write(GENERAL_CONF, general_conf);
```

**计算公式：**
```
AMAX [pps²] = AMAX / 2³⁷ · fCLK²
```

**限制：** AMAX, DMAX, ASTART, DFINAL, DSTOP ≤ 65535

### 8.4 弓形值单位

弓形值 (BOW1...BOW4) 默认定义为每秒³脉冲数 [pps³]。

#### 直接弓形模式

```c
// 启用直接弓形值模式
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 2);  // direct_bow_val_en = 1
tmc4361_write(GENERAL_CONF, general_conf);
```

**计算公式：**
```
BOWx [pps³] = BOWx / 2⁵³ · fCLK³
```

**限制：** BOW1...4 ≤ 4095

### 8.5 最小和最大值概览

#### 频率模式（默认）

| 参数类型 | 速度 | 加速度 | 弓形 | 时钟 |
|----------|------|--------|------|------|
| 最小值 | 3.906 mpps | 0.25 mpps² | 1 mpps³ | 4.194 MHz |
| 最大值 | 8.388 Mpps 且 ≤ ½·fCLK | 4.194 Mpps² | 16.777 Mpps³ | 32 MHz |

#### 直接模式（fCLK = 16MHz 示例）

| 参数类型 | 加速度 (direct_acc_val_en=1) | 弓形 (direct_bow_val_en=1) |
|----------|------------------------------|---------------------------|
| 最小值 | ~1.86 kpps² | ~454.75 kpps³ |
| 最大值 | ~122.07 Mpps² | ~1.86 Gpps³ |

---

## 9. 外部步进控制和电子齿轮

### 9.1 相关引脚和寄存器

| 引脚名 | 类型 | 功能 |
|--------|------|------|
| STPIN | 输入 | 步进输入信号 |
| DIRIN | 输入 | 方向输入信号 |

| 寄存器名 | 地址 | 说明 |
|----------|------|------|
| GENERAL_CONF | 0x00 | bit 9:6, 26 |
| GEAR_RATIO | 0x12 | 电子齿轮比，有符号，32位 = 8位整数 + 24位小数 |

### 9.2 启用外部步进控制

```c
// 选项1: 高电平有效外部步进
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf &= ~(0b11 << 6);
general_conf |= (0b01 << 6);  // sdin_mode = b'01
tmc4361_write(GENERAL_CONF, general_conf);

// 选项2: 低电平有效外部步进
general_conf |= (0b10 << 6);  // sdin_mode = b'10

// 选项3: 翻转外部步进（每次电平变化都是一步）
general_conf |= (0b11 << 6);  // sdin_mode = b'11
```

> **注意**：选择任一选项后，内部步进发生器将被禁用。

### 9.3 输入方向极性

默认情况下，DIRIN = 0 表示负方向。

```c
// 反转方向极性
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 26);  // pol_dir_in = 1
tmc4361_write(GENERAL_CONF, general_conf);
```

### 9.4 电子齿轮

GEAR_RATIO 寄存器（0x12）是有符号参数，包含 8 位整数和 24 位小数。

```c
// 齿轮比 = 1.5 (即 1.5 * 2^24 = 0x01800000)
tmc4361_write(GEAR_RATIO, 0x01800000);

// 齿轮比 = 0.5 (即 0.5 * 2^24 = 0x00800000)
tmc4361_write(GEAR_RATIO, 0x00800000);
```

**工作原理：**
- 每个外部步进将 GEAR_RATIO 值添加到内部累加寄存器
- 当累加器溢出时，生成一个内部步进
- 余数保留用于下一个外部步进
- 绝对齿轮值范围：2⁻²⁴ ~ 127
- 负齿轮比会反转方向解释

### 9.5 间接外部控制

可以将外部 S/D 接口与内部斜坡发生器结合使用。

```c
// 设置间接外部控制
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (0b01 << 6);  // sdin_mode ≠ b'00
general_conf |= (1 << 10);    // sd_indirect_control = 1
tmc4361_write(GENERAL_CONF, general_conf);
```

**结果：**外部步进脉冲会修改 XTARGET 而不是直接影响 XACTUAL。

### 9.6 从外部切换到内部控制

```c
// 前提条件：外部直接控制已激活
// sdin_mode ≠ b'00, sd_indirect_control = 0, ASTART = 0

// 1. 设置自动切换
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 11);  // automatic_direct_sdin_switch_off = 1
tmc4361_write(GENERAL_CONF, general_conf);

// 2. 持续更新 VSTART 为当前速度（在 µC 中计算）
tmc4361_write(VSTART, current_velocity);

// 3. 切换时，禁用外部模式
general_conf &= ~(0b11 << 6);  // sdin_mode = b'00
tmc4361_write(GENERAL_CONF, general_conf);
```

**结果：**内部斜坡从 VSTART 值开始，方向根据之前的外部步进自动设置。

---

## 10. 参考开关

### 10.1 相关引脚和寄存器

| 引脚名 | 类型 | 功能 |
|--------|------|------|
| STOPL | 输入 | 左参考开关 |
| STOPR | 输入 | 右参考开关 |
| HOME_REF | 输入 | 原点开关 |
| TARGET_REACHED | 输出 | 指示 XACTUAL = XTARGET |

| 寄存器名 | 地址 | 说明 |
|----------|------|------|
| REFERENCE_CONF | 0x01 | 参考引脚交互配置 |
| HOME_SAFETY_MARGIN | 0x1E | X_HOME 周围的不确定区域 |
| DSTOP | 0x2C | 停止开关触发时的减速度值 |
| POS_COMP | 0x32 | 自由配置的比较位置 |
| VIRT_STOP_LEFT | 0x33 | 虚拟左停止位置 |
| VIRT_STOP_RIGHT | 0x34 | 虚拟右停止位置 |
| X_HOME | 0x35 | 原点参考位置 |
| X_LATCH | 0x36 | 在不同条件下存储 XACTUAL |

### 10.2 硬件停止开关

#### 启用 STOPL

```c
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
// 设置 STOPL 极性（0=低电平有效，1=高电平有效）
// ref_conf |= (1 << 0);  // pol_stop_left
// 启用 STOPL
ref_conf |= (1 << 2);  // stop_left_en = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

#### 启用 STOPR

```c
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
// 设置 STOPR 极性
// ref_conf |= (1 << 1);  // pol_stop_right
// 启用 STOPR
ref_conf |= (1 << 3);  // stop_right_en = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

### 10.3 停止斜坡配置

#### 硬停止（默认）

```c
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf &= ~(1 << 10);  // soft_stop_en = 0
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**结果：**触发停止开关时，VACTUAL 立即设为 0。

#### 线性停止斜坡

```c
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 10);  // soft_stop_en = 1
tmc4361_write(REFERENCE_CONF, ref_conf);

// 设置停止减速度
tmc4361_write(DSTOP, deceleration_value);
```

**结果：**触发停止开关时，以 DSTOP 减速度线性减速至 VACTUAL = 0。

### 10.4 停止事件处理

当停止开关激活时：
- STATUS_FLAGS (0x0F) 中的相关状态标志被设置
- EVENTS (0x0E) 中的相关事件被释放

**恢复运动：**

```c
// 前提：停止开关不再激活 或 已禁用停止开关
// 清除事件
tmc4361_read(EVENTS);
```

### 10.5 位置锁存

可以选择四种不同的事件来将 XACTUAL 存储到 X_LATCH：

| 配置 | pol_stop_left=0 | pol_stop_left=1 |
|------|-----------------|-----------------|
| latch_x_on_inactive_l=1 | STOPL: 0→1 | STOPL: 1→0 |
| latch_x_on_active_l=1 | STOPL: 1→0 | STOPL: 0→1 |

| 配置 | pol_stop_right=0 | pol_stop_right=1 |
|------|------------------|------------------|
| latch_x_on_inactive_r=1 | STOPR: 0→1 | STOPR: 1→0 |
| latch_x_on_active_r=1 | STOPR: 1→0 | STOPR: 0→1 |

### 10.6 交换参考开关方向

```c
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 4);  // invert_stop_direction = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**结果：**STOPL 成为右参考开关，STOPR 成为左参考开关。

---

## 11. 虚拟停止开关

### 11.1 启用虚拟停止

#### 左虚拟停止

```c
tmc4361_write(VIRT_STOP_LEFT, left_limit_position);

uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 5);  // virtual_left_limit_en = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**触发条件：**XACTUAL ≤ VIRT_STOP_LEFT

#### 右虚拟停止

```c
tmc4361_write(VIRT_STOP_RIGHT, right_limit_position);

uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 6);  // virtual_right_limit_en = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**触发条件：**XACTUAL ≥ VIRT_STOP_RIGHT

### 11.2 虚拟停止斜坡配置

```c
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);

// 选项1: 硬停止
ref_conf &= ~(0b11 << 11);
ref_conf |= (0b01 << 11);  // virt_stop_mode = b'01

// 选项2: 线性停止斜坡
ref_conf |= (0b10 << 11);  // virt_stop_mode = b'10

tmc4361_write(REFERENCE_CONF, ref_conf);
```

> **注意：**invert_stop_direction 对虚拟停止开关没有影响。

---

## 12. 原点参考配置

### 12.1 回原点流程

```c
// 1. 配置斜坡参数
tmc4361_write(RAMPMODE, 0b001);  // 梯形斜坡
tmc4361_write(AMAX, homing_acceleration);
tmc4361_write(VMAX, homing_velocity);

// 2. 启用原点跟踪模式
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 16);  // start_home_tracking = 1
tmc4361_write(REFERENCE_CONF, ref_conf);

// 3. 设置正确的 home_event
// home_event 配置见下表

// 4. 向原点开关方向启动斜坡
tmc4361_write(VMAX, -homing_velocity);  // 或正向
```

**结果：**
- 当识别到原点事件时，XACTUAL 被锁存到 X_HOME
- start_home_tracking 自动禁用
- XLATCH_DONE 事件被释放

### 12.2 原点事件选择

| home_event | 描述 | X_HOME 位置 |
|------------|------|-------------|
| b'0000 | 使用增量编码器的 N 信号 | - |
| b'0011 | HOME_REF=0 表示负方向 | 边缘左侧 |
| b'1100 | HOME_REF=0 表示正方向 | 边缘右侧 |
| b'0110 | HOME_REF=1 表示原点位置 | 中心 |
| b'0010 | HOME_REF=1 表示原点位置 | 左侧 |
| b'0100 | HOME_REF=1 表示原点位置 | 右侧 |
| b'1001 | HOME_REF=0 表示原点位置 | 中心 |
| b'1011 | HOME_REF=0 表示原点位置 | 右侧 |
| b'1101 | HOME_REF=0 表示原点位置 | 左侧 |

### 12.3 原点安全边距

为避免因机械误差导致的误报，可以设置原点周围的不确定范围：

```c
tmc4361_write(HOME_SAFETY_MARGIN, margin_in_usteps);
```

**结果：**在以下范围内不评估 HOME_ERROR_F：
```
X_HOME - HOME_SAFETY_MARGIN ≤ XACTUAL ≤ X_HOME + HOME_SAFETY_MARGIN
```

### 12.4 使用 STOPL 或 STOPR 作为原点开关

```c
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);

// 选项1: STOPL 作为原点开关
ref_conf |= (1 << 17);  // stop_left_is_home = 1

// 选项2: STOPR 作为原点开关
ref_conf |= (1 << 18);  // stop_right_is_home = 1

tmc4361_write(REFERENCE_CONF, ref_conf);
```

---

## 13. 目标到达和位置比较

### 13.1 TARGET_REACHED 输出

TARGET_REACHED 引脚在 XACTUAL = XTARGET 时激活。

#### 反转极性

```c
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 16);  // invert_pol_target_reached = 1
tmc4361_write(GENERAL_CONF, general_conf);
```

#### Wired-Or / Wired-And 配置

```c
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 17);  // intr_tr_pu_pd_en = 1

// Wired-Or（默认）
general_conf &= ~(1 << 18);  // tr_as_wired_and = 0

// Wired-And
general_conf |= (1 << 18);  // tr_as_wired_and = 1

tmc4361_write(GENERAL_CONF, general_conf);
```

### 13.2 位置比较

#### 基本比较设置

```c
// 比较 XACTUAL 与 POS_COMP
tmc4361_write(POS_COMP, compare_position);

uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf &= ~(1 << 19);  // pos_comp_source = 0 (使用 XACTUAL)
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**结果：**当 XACTUAL = POS_COMP 时，POS_COMP_REACHED_Flag 被设置。

#### 使用编码器位置比较

```c
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 19);  // pos_comp_source = 1 (使用 ENC_POS)
tmc4361_write(REFERENCE_CONF, ref_conf);
```

#### 比较选择网格

| modified_pos_compare | pos_comp_source=0 | pos_comp_source=1 |
|----------------------|-------------------|-------------------|
| b'00 | XACTUAL vs POS_COMP | ENC_POS vs POS_COMP |
| b'01 | XACTUAL vs X_HOME | ENC_POS vs X_HOME |
| b'10 | XACTUAL vs X_LATCH | ENC_POS vs ENC_LATCH |
| b'11 | REV_CNT vs POS_COMP | REV_CNT vs POS_COMP |

---

## 14. 重复和循环运动

### 14.1 重复运动到 XTARGET

```c
// 前提：定位模式激活
tmc4361_write(RAMPMODE, 0b101);

// 启用到达目标后清除位置
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 20);  // clr_pos_at_target = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**结果：**到达 XTARGET 后，XACTUAL 被重置为 0，斜坡重新启动。

### 14.2 循环运动

```c
// 设置位置范围（只写）
tmc4361_write(X_RANGE, range_value);  // 0x36 地址

// 启用循环运动
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 21);  // circular_motion = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**结果：**
- XACTUAL 范围限制为：-X_RANGE ≤ XACTUAL < X_RANGE
- 当 XACTUAL 到达 (X_RANGE - 1) 并继续正向运动时，XACTUAL 变为 -X_RANGE
- 定位模式下自动选择最短路径

### 14.3 非整数微步/转

使用 CIRCULAR_DEC 寄存器 (0x7C) 扩展每转微步数：

```c
// 例：每转 601 微步（X_RANGE = 300 提供 600 微步）
// CIRCULAR_DEC = 0x80000000 (= 2^31 / 2^31 = 1)
tmc4361_write(CIRCULAR_DEC, 0x80000000);
```

**平均微步/转：**
```
微步/转 = (2 · X_RANGE) + CIRCULAR_DEC / 2³¹
```

### 14.4 圈数计数器

```c
// 启用圈数计数器读取
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 19);  // circular_cnt_as_xlatch = 1
tmc4361_write(GENERAL_CONF, general_conf);

// 读取圈数
int32_t rev_cnt = (int32_t)tmc4361_read(X_LATCH);  // 0x36
```

> **注意：**循环运动禁用时 (circular_motion = 0)，REV_CNT 重置为 0。

### 14.5 阻挡区域

在循环运动期间，虚拟停止可用于设置阻挡区域：

```c
// 设置阻挡区域
tmc4361_write(VIRT_STOP_LEFT, blocking_left);
tmc4361_write(VIRT_STOP_RIGHT, blocking_right);

// 启用虚拟限制
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 5) | (1 << 6);  // 启用两个虚拟限制
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**阻挡区域定义：**
- 如果 VIRT_STOP_LEFT < VIRT_STOP_RIGHT：满足任一条件即在阻挡区域内
- 如果 VIRT_STOP_LEFT > VIRT_STOP_RIGHT：必须同时满足两个条件

---

## 15. 斜坡定时和同步

### 15.1 相关引脚和寄存器

| 引脚名 | 类型 | 功能 |
|--------|------|------|
| START | 输入/输出 | 外部启动信号输入或指示内部启动事件的输出 |

| 寄存器名 | 地址 | 说明 |
|----------|------|------|
| START_CONF | 0x02 | 同步单元配置寄存器 |
| START_OUT_ADD | 0x11 | 外部启动信号的附加有效输出长度 |
| START_DELAY | 0x13 | 启动触发和启动信号之间的延迟时间 |
| X_PIPE0...7 | 0x38...0x3F | 目标位置管道和/或参数管道 |

### 15.2 启动触发选择

```c
uint32_t start_conf = tmc4361_read(START_CONF);

// 清除触发事件位
start_conf &= ~(0b1111 << 5);

// 设置触发源
// bit 5: 外部启动触发（START 引脚为输入）
// bit 6: TARGET_REACHED 事件
// bit 7: VELOCITY_REACHED 事件
// bit 8: POSCOMP_REACHED 事件

start_conf |= (0b0010 << 5);  // 示例：TARGET_REACHED 触发

tmc4361_write(START_CONF, start_conf);
```

### 15.3 启动使能开关

```c
uint32_t start_conf = tmc4361_read(START_CONF);

// 清除启动使能位
start_conf &= ~0b11111;

// 设置使能
// bit 0: XTARGET 在启动信号后更改
// bit 1: VMAX 在启动信号后更改
// bit 2: RAMPMODE 在启动信号后更改
// bit 3: GEAR_RATIO 在启动信号后更改
// bit 4: 影子寄存器在启动信号后激活

start_conf |= 0b00001;  // 示例：XTARGET 延迟更改

tmc4361_write(START_CONF, start_conf);
```

### 15.4 启动延迟

```c
tmc4361_write(START_DELAY, delay_clock_cycles);
```

### 15.5 优先处理外部输入

```c
uint32_t start_conf = tmc4361_read(START_CONF);
start_conf |= (1 << 9);  // immediate_start_in = 1
tmc4361_write(START_CONF, start_conf);
```

**结果：**外部启动触发立即生成内部启动信号，忽略定义的延迟。

### 15.6 START 引脚极性

```c
uint32_t start_conf = tmc4361_read(START_CONF);
start_conf |= (1 << 10);  // pol_start_signal = 1 (高电平有效)
tmc4361_write(START_CONF, start_conf);
```

### 15.7 START 输出配置

```c
// 延长有效输出电平持续时间
tmc4361_write(START_OUT_ADD, additional_clock_cycles);
```

**有效电平持续时间：**(START_OUT_ADD + 1) 个时钟周期

---

## 16. 影子寄存器设置

### 16.1 启用影子寄存器

```c
uint32_t start_conf = tmc4361_read(START_CONF);
start_conf |= (1 << 4);  // start_en(4) = 1
start_conf |= (0b0010 << 5);  // 选择触发事件
tmc4361_write(START_CONF, start_conf);
```

### 16.2 循环影子寄存器

```c
uint32_t start_conf = tmc4361_read(START_CONF);
start_conf |= (1 << 12);  // cyclic_shadow_regs = 1
tmc4361_write(START_CONF, start_conf);
```

**结果：**内部启动信号时，当前运动参数与影子寄存器交换。

### 16.3 影子寄存器配置选项

| shadow_option | 说明 | 影响的参数 |
|---------------|------|-----------|
| b'00 | 完整斜坡寄存器 | 所有 13 个参数 |
| b'01 | S 形斜坡双级 | VMAX, AMAX, DMAX, BOW1-4 |
| b'10 | 梯形斜坡双级 (VSTART) | VMAX, AMAX, DMAX, ASTART, DFINAL, VBREAK, VSTART |
| b'11 | 梯形斜坡双级 (VSTOP) | VMAX, AMAX, DMAX, ASTART, DFINAL, VBREAK, VSTOP |

### 16.4 影子寄存器映射

#### 选项 1：完整斜坡寄存器 (shadow_option = b'00)

| 活动寄存器 | 影子寄存器 |
|-----------|-----------|
| 0x20 RAMPMODE | 0x4C SH_REG12 |
| 0x24 VMAX | 0x40 SH_REG0 |
| 0x25 VSTART | 0x46 SH_REG6 |
| 0x26 VSTOP | 0x47 SH_REG7 |
| 0x27 VBREAK | 0x45 SH_REG5 |
| 0x28 AMAX | 0x41 SH_REG1 |
| 0x29 DMAX | 0x42 SH_REG2 |
| 0x2A ASTART | 0x43 SH_REG3 |
| 0x2B DFINAL | 0x44 SH_REG4 |
| 0x2D BOW1 | 0x48 SH_REG8 |
| 0x2E BOW2 | 0x49 SH_REG9 |
| 0x2F BOW3 | 0x4A SH_REG10 |
| 0x30 BOW4 | 0x4B SH_REG11 |

### 16.5 延迟影子传输

```c
uint32_t start_conf = tmc4361_read(START_CONF);

// 设置跳过的启动信号数量（0-15）
start_conf &= ~(0b1111 << 16);
start_conf |= (skip_count << 16);  // SHADOW_MISS_CNT

tmc4361_write(START_CONF, start_conf);
```

**结果：**影子传输在第 (SHADOW_MISS_CNT + 1) 个启动信号时执行。

### 16.6 重要注意事项

> **BOW 值计算延迟：**写入影子 BOW 值后，需要等待最多 (320 / fCLK) 秒才能触发影子传输。

> **建议在静止时传输：**强烈建议在 VACTUAL = 0 时执行影子寄存器传输，特别是当 RAMPMODE 改变时。

> **S 形斜坡传输延迟：**
> ```
> t_SHADOW_TRANSFER = sqrt(max(BOW3, BOW4) · VMAX) / (56 · BOW3)
> ```
> 在此延迟期间，VACTUAL 必须保持恒定。

---

## 17. 目标管线

TMC4361 提供目标管线功能，用于排序从属目标，以便轻松安排复杂的目标结构。

### 17.1 相关寄存器

| 寄存器名 | 地址 | 说明 |
|----------|------|------|
| START_CONF | 0x02 | 管线启用配置 |
| X_PIPE0 | 0x38 | 管线寄存器 0 |
| X_PIPE1 | 0x39 | 管线寄存器 1 |
| X_PIPE2 | 0x3A | 管线寄存器 2 |
| X_PIPE3 | 0x3B | 管线寄存器 3 |
| X_PIPE4 | 0x3C | 管线寄存器 4 |
| X_PIPE5 | 0x3D | 管线寄存器 5 |
| X_PIPE6 | 0x3E | 管线寄存器 6 |
| X_PIPE7 | 0x3F | 管线寄存器 7 |
| XPIPE_REWRITE_REG | - | 循环管线回写配置 |

### 17.2 目标管线配置和激活

将目标值分配到 X_PIPE0...7 寄存器。当管线启用时，每次生成内部启动信号时会启动新的分配周期。

**处理流程：**
1. 新的 XTARGET 值取自 X_PIPE0 的值
2. 每个 X_PIPEn 寄存器取自其后继寄存器的值：X_PIPEn = X_PIPEn+1

```c
// 激活目标管线
uint32_t start_conf = tmc4361_read(START_CONF);
start_conf |= (0b0001 << 0);  // pipeline_en = b'0001
tmc4361_write(START_CONF, start_conf);
```

**结果：**每次新的内部启动信号提示时执行上述流程。

### 17.3 循环目标管线

可以将 XTARGET 的值重新分配到一个或多个管线寄存器 X_PIPE0...7，从而创建循环目标管线。

```c
// 启用循环目标管线
uint32_t start_conf = tmc4361_read(START_CONF);
start_conf |= (0b0001 << 0);  // pipeline_en = b'0001
tmc4361_write(START_CONF, start_conf);

// 设置回写寄存器（例如回写到 X_PIPE4）
tmc4361_write(XPIPE_REWRITE_REG, 0x00010000);  // bit4 = 1
```

**结果：**执行上述流程，且 XTARGET 被写回到选定的 X_PIPEx 寄存器。

### 17.4 管线用于不同内部寄存器

管线（寄存器 0x38...0x3F）可配置为最多分成四个段，用于馈送以下内部参数：

| 目标寄存器 | 地址 | 用途 |
|-----------|------|------|
| XTARGET | 0x37 | 目标位置 |
| POS_COMP | 0x32 | 位置比较值 |
| GEAR_RATIO | 0x12 | 齿轮比 |
| GENERAL_CONF | 0x00 | 通用配置 |

### 17.5 管线激活选项

| pipeline_en(3:0) | 说明 |
|------------------|------|
| b'xxx1 | XTARGET 管线启用 |
| b'xx1x | POS_COMP 管线启用 |
| b'x1xx | GEAR_RATIO 管线启用 |
| b'1xxx | GENERAL_CONF 管线启用 |

### 17.6 管线映射表

| pipeline_en | 排列 | GENERAL_CONF | GEAR_RATIO | POS_COMP | XTARGET |
|-------------|------|--------------|------------|----------|---------|
| b'0000 | 无管线 | - | - | - | - |
| b'0001 | 一个 8 级管线 | - | - | - | X_PIPE0 |
| b'0010 | 一个 8 级管线 | - | - | X_PIPE0 | - |
| b'0100 | 一个 8 级管线 | - | X_PIPE0 | - | - |
| b'1000 | 一个 8 级管线 | X_PIPE0 | - | - | - |
| b'0011 | 两个 4 级管线 | - | - | X_PIPE4 | X_PIPE0 |
| b'0101 | 两个 4 级管线 | - | X_PIPE4 | - | X_PIPE0 |
| b'1001 | 两个 4 级管线 | X_PIPE4 | - | - | X_PIPE0 |
| b'0110 | 两个 4 级管线 | - | X_PIPE4 | X_PIPE0 | - |
| b'1010 | 两个 4 级管线 | X_PIPE4 | - | X_PIPE0 | - |
| b'1100 | 两个 4 级管线 | X_PIPE4 | X_PIPE0 | - | - |
| b'0111 | 两个 3 级 + 一个 2 级 | - | X_PIPE6 | X_PIPE3 | X_PIPE0 |
| b'1011 | 两个 3 级 + 一个 2 级 | X_PIPE6 | - | X_PIPE3 | X_PIPE0 |
| b'1101 | 两个 3 级 + 一个 2 级 | X_PIPE6 | X_PIPE3 | - | X_PIPE0 |
| b'1110 | 两个 3 级 + 一个 2 级 | X_PIPE6 | X_PIPE3 | X_PIPE0 | - |
| b'1111 | 四个 2 级管线 | X_PIPE6 | X_PIPE4 | X_PIPE2 | X_PIPE0 |

### 17.7 管线示例

#### 示例 A：POS_COMP 循环管线（8 级）

```c
// POS_COMP 8级循环管线
tmc4361_write(START_CONF, 0x02);           // pipeline_en = b'0010
tmc4361_write(XPIPE_REWRITE_REG, 0x80);    // 回写到 X_PIPE7
```

#### 示例 C：XTARGET 和 POS_COMP 循环管线（各 4 级）

```c
// XTARGET 和 POS_COMP 各4级循环管线
tmc4361_write(START_CONF, 0x03);           // pipeline_en = b'0011
tmc4361_write(XPIPE_REWRITE_REG, 0x88);    // 回写到 X_PIPE3 和 X_PIPE7
```

#### 示例 G：四个 2 级循环管线

```c
// 四个2级循环管线
tmc4361_write(START_CONF, 0x0F);           // pipeline_en = b'1111
tmc4361_write(XPIPE_REWRITE_REG, 0xAA);    // 回写到所有奇数管线寄存器
```

---

## 18. 无主同步

### 18.1 概述

START 引脚可配置为三态输入，以便无主同步多个运动控制器。

### 18.2 三态 START 引脚工作原理

1. START 配置为三态
2. 启用忙碌状态期间，START 设置为具有强驱动非活动极性的输出
3. 当内部启动信号生成后（启动定时器过期后），START 引脚配置为输入
4. 同时，弱输出信号以活动启动极性发出
5. 当 START 输入检测到活动极性（所有成员就绪），START 输出保持活动（强驱动）START_OUT_ADD 个时钟周期
6. 然后忙碌状态再次激活直到下一个启动信号

### 18.3 激活三态 START 引脚

```c
uint32_t start_conf = tmc4361_read(START_CONF);
start_conf |= (1 << 11);  // busy_en = 1
tmc4361_write(START_CONF, start_conf);
```

### 18.4 START 引脚连接

> **注意：**当 START 引脚与其他 TMC4361 设备的 START 引脚连接时，建议在设备之间连接串联电阻（如 220Ω），以限制配置阶段不同电压电平时可能流动的短路电流。

---

## 19. SPI 输出接口

TMC4361 提供 SPI 接口用于电机驱动器的初始化和配置（除了 Step/Dir 输出）。

### 19.1 SPI 接口引脚

| 引脚名 | 类型 | 说明 |
|--------|------|------|
| NSCSDRV_SDO | 输出 | 片选输出到电机驱动器，低电平有效 |
| SCKDRV_NSDO | 输出 | SPI 时钟输出到电机驱动器 |
| SDODRV_SCLK | 输入/输出 | 串行数据输出到电机驱动器 |
| SDIDRV_NSCLK | 输入 | 从电机驱动器接收的串行数据输入 |
| STDBY_CLK | 输出 | 时钟输出、待机输出或 ChopSync 时钟输出 |

### 19.2 SPI 输出相关寄存器

| 寄存器名 | 地址 | 说明 |
|----------|------|------|
| GENERAL_CONF | 0x00 | 通用配置 |
| REFERENCE_CONF | 0x01 | 参考配置 |
| SPIOUT_CONF | 0x04 | SPI 输出通信配置 |
| STEP_CONF | 0x0A | 微步配置 |
| DAC_ADDR | 0x1D | DAC 地址/命令 |
| SPI_SWITCH_VEL | 0x1F | 自动 Cover 数据报速度阈值 |
| FS_VEL | 0x60 | 全步驱动速度阈值 |
| COVER_LOW | 0x6C | Cover 寄存器低 32 位 |
| COVER_HIGH | 0x6D | Cover 寄存器高 32 位 |
| COVER_DRV_LOW | 0x6E | Cover 响应寄存器低 32 位 |
| COVER_DRV_HIGH | 0x6F | Cover 响应寄存器高 32 位 |

### 19.3 启用 SPI 输出通信

```c
// 启用 SPI 输出（默认已启用）
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf &= ~(1 << 24);  // serial_enc_out_enable = 0
tmc4361_write(GENERAL_CONF, general_conf);
```

### 19.4 SPI 输出时序配置

```c
uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);

// 设置 SPI 时钟低电平时间（2-15 个时钟周期）
spiout_conf &= ~(0x0F << 20);
spiout_conf |= (low_time << 20);  // SPI_OUT_LOW_TIME

// 设置 SPI 时钟高电平时间（2-15 个时钟周期）
spiout_conf &= ~(0x0F << 24);
spiout_conf |= (high_time << 24);  // SPI_OUT_HIGH_TIME

// 设置数据报之间的阻塞时间
spiout_conf &= ~(0x0F << 28);
spiout_conf |= (block_time << 28);  // SPI_OUT_BLOCK_TIME

tmc4361_write(SPIOUT_CONF, spiout_conf);
```

**SPI 时钟频率范围：**fCLK / 30 ≤ fSPI_CLK ≤ fCLK / 2

### 19.5 Cover 数据报

Cover 数据报允许微控制器通过 TMC4361 直接与电机驱动器通信。

#### 设置 Cover 数据报长度

```c
uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);
spiout_conf &= ~(0x7F << 13);
spiout_conf |= (cover_length << 13);  // COVER_DATA_LENGTH (0-64位)
tmc4361_write(SPIOUT_CONF, spiout_conf);
```

> **注意：**对于 TMC 电机驱动器，可设置 COVER_DATA_LENGTH = 0，长度会根据选择的驱动器自动设置。

#### 发送 Cover 数据报（≤32 位）

```c
tmc4361_write(COVER_LOW, cover_data);
// 等待 COVER_DONE 事件
// 读取响应
uint32_t response = tmc4361_read(COVER_DRV_LOW);
```

#### 发送 Cover 数据报（>32 位）

```c
tmc4361_write(COVER_HIGH, cover_data_high);
tmc4361_write(COVER_LOW, cover_data_low);
// 等待 COVER_DONE 事件
```

### 19.6 自动 Cover 数据报

```c
// 设置触发速度
tmc4361_write(SPI_SWITCH_VEL, switch_velocity);

// 设置低速 cover 数据
tmc4361_write(COVER_LOW, low_speed_cover_data);

// 设置高速 cover 数据
tmc4361_write(COVER_HIGH, high_speed_cover_data);

// 启用自动 cover
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 28);  // automatic_cover = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**结果：**
- |VACTUAL| < SPI_SWITCH_VEL 时发送 COVER_LOW
- |VACTUAL| ≥ SPI_SWITCH_VEL 时发送 COVER_HIGH

### 19.7 正弦波查找表（MSLUT）

TMC4361 提供可编程查找表用于存储电流波形。默认为正弦波。

#### MSLUT 寄存器

| 寄存器名 | 地址 | 说明 |
|----------|------|------|
| MSLUT[0] | 0x70 | 微步表段 0 (ofs31...ofs00) |
| MSLUT[1] | 0x71 | 微步表段 1 (ofs63...ofs32) |
| MSLUT[2] | 0x72 | 微步表段 2 (ofs95...ofs64) |
| MSLUT[3] | 0x73 | 微步表段 3 (ofs127...ofs96) |
| MSLUT[4] | 0x74 | 微步表段 4 (ofs159...ofs128) |
| MSLUT[5] | 0x75 | 微步表段 5 (ofs191...ofs160) |
| MSLUT[6] | 0x76 | 微步表段 6 (ofs223...ofs192) |
| MSLUT[7] | 0x77 | 微步表段 7 (ofs255...ofs224) |
| MSLUTSEL | 0x78 | 波形段边界和基波倾斜值 |
| MSCNT | 0x79 | 当前微步位置（只读） |
| CURRENTA/CURRENTB | 0x7A | 当前电流值（只读） |
| CURRENTA_SPI/CURRENTB_SPI | 0x7B | 缩放后的电流值（只读） |
| START_SIN/START_SIN90_120/DAC_OFFSET | 0x7E | 起始值和 DAC 偏移 |

#### MSLUT 工作原理

- 内部微步波表将 0° 到 90° 映射为 256 个微步
- 自动对称扩展到 360°，共 1024 个微步
- MSCNT 范围：0 到 1023
- 采用增量编码，仅需 256 位存储四分之一波

#### 配置 MSLUT 段

```c
// 设置段边界和基波倾斜值
// X1, X2, X3: 段边界（8位）
// W0, W1, W2, W3: 基波倾斜值（2位，0-3）
uint32_t mslutsel = (X3 << 24) | (X2 << 16) | (X1 << 8) |
                    (W3 << 6) | (W2 << 4) | (W1 << 2) | W0;
tmc4361_write(MSLUTSEL, mslutsel);
```

**波形段特性：**

| 段 | 基波倾斜 | 范围 |
|----|---------|------|
| 0 | W0 | 0 ... X1 |
| 1 | W1 | X1 ... X2 |
| 2 | W2 | X2 ... X3 |
| 3 | W3 | X3 ... 255 |

**增量计算：**INC = Wx + (ofs - 1)

#### 设置起始电流值

```c
uint32_t start_values = (DAC_OFFSET << 24) | (START_SIN90_120 << 16) | START_SIN;
tmc4361_write(0x7E, start_values);
```

### 19.8 TMC 电机驱动器配置

#### TMC 驱动器输出格式选项

| TMC 驱动器 | spi_output_format | Cover 长度 | 说明 |
|-----------|-------------------|-----------|------|
| 关闭 | b'0000 | 0 | SPI 输出关闭 |
| TMC23x | b'1000 | 12 | SPI 电流传输 |
| TMC24x | b'1001 | 12 | SPI 电流传输 + stallGuard |
| TMC26x/389 SPI | b'1010 | 20 | SPI 电流传输 |
| TMC26x/389 S/D | b'1011 | 20 | Step/Dir 输出 |
| TMC2130 SPI | b'1101 | 40 | SPI 电流传输 |
| TMC2130 S/D | b'1100 | 40 | Step/Dir 输出 |

#### 配置 TMC26x（SPI 模式）

```c
uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);
spiout_conf &= ~0x0F;
spiout_conf |= 0x0A;  // spi_output_format = b'1010
spiout_conf &= ~(0x7F << 13);  // COVER_DATA_LENGTH = 0
tmc4361_write(SPIOUT_CONF, spiout_conf);
```

#### 配置 TMC26x（S/D 模式）

```c
uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);
spiout_conf &= ~0x0F;
spiout_conf |= 0x0B;  // spi_output_format = b'1011
spiout_conf &= ~(0x7F << 13);  // COVER_DATA_LENGTH = 0
tmc4361_write(SPIOUT_CONF, spiout_conf);

// 设置 Step/Dir 时序
tmc4361_write(STP_LENGTH_ADD, step_timing);
```

#### 配置 TMC2130

```c
// SPI 模式
uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);
spiout_conf &= ~0x0F;
spiout_conf |= 0x0D;  // spi_output_format = b'1101
tmc4361_write(SPIOUT_CONF, spiout_conf);

// S/D 模式
spiout_conf &= ~0x0F;
spiout_conf |= 0x0C;  // spi_output_format = b'1100
tmc4361_write(SPIOUT_CONF, spiout_conf);
```

### 19.9 TMC 驱动器状态位映射

#### TMC26x/TMC2130 状态位

| STATUS 位 | 标志名 | 说明 |
|----------|--------|------|
| STATUS(24) | SG | stallGuard2 状态 |
| STATUS(25) | OT | 过温标志 |
| STATUS(26) | OTPW | 温度预警标志 |
| STATUS(27) | S2GA | 线圈 A 高侧接地短路 |
| STATUS(28) | S2GB | 线圈 B 高侧接地短路 |
| STATUS(29) | OLA | 线圈 A 开路 |
| STATUS(30) | OLB | 线圈 B 开路 |
| STATUS(31) | STST | 静止标志 |

### 19.10 堵转检测和 Stop-on-Stall

```c
// 设置堵转速度阈值
tmc4361_write(VSTALL_LIMIT, stall_velocity);

// 启用 Stop-on-Stall
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 26);  // stop_on_stall = 1
ref_conf &= ~(1 << 27); // drive_after_stall = 0
tmc4361_write(REFERENCE_CONF, ref_conf);
```

**结果：**当检测到堵转且 |VACTUAL| > VSTALL_LIMIT 时，内部斜坡速度立即设为 0。

#### 堵转后恢复

```c
// 清除 STOP_ON_STALL 事件
tmc4361_read(EVENTS);

// 启用堵转后驱动
uint32_t ref_conf = tmc4361_read(REFERENCE_CONF);
ref_conf |= (1 << 27);  // drive_after_stall = 1
tmc4361_write(REFERENCE_CONF, ref_conf);
```

### 19.11 自动全步切换

```c
// 设置全步切换速度
tmc4361_write(FS_VEL, fullstep_velocity);

// 启用全步切换
uint32_t general_conf = tmc4361_read(GENERAL_CONF);
general_conf |= (1 << 19);  // fs_en = 1
tmc4361_write(GENERAL_CONF, general_conf);
```

**结果：**
- |VACTUAL| ≥ FS_VEL 时切换到全步
- |VACTUAL| < FS_VEL 时切换回微步

### 19.12 非 TMC 驱动器/SPI-DAC 连接

#### 非 TMC 数据传输选项

| 输出格式 | spi_output_format | 说明 |
|---------|-------------------|------|
| SPI 输出关闭 | b'0000 | SPI 输出驱动引脚关闭 |
| 仅 Cover 输出 | b'1111 | 仅发送 Cover 数据报 |
| 无符号缩放因子 | b'0100 | 输出 8 位缩放因子 |
| 有符号电流数据 | b'0101 | 输出 18 位电流值 |
| DAC 缩放因子 | b'0110 | 带 DAC 地址的缩放因子 |
| DAC 绝对值 (相位=0为正) | b'0011 | 带 DAC 地址的绝对电流值 |
| DAC 绝对值 (相位=1为正) | b'0010 | 带 DAC 地址的绝对电流值 |
| DAC 映射值 | b'0001 | 映射到 0-255 的电流值 |

#### 配置 SPI-DAC

```c
// 设置 DAC 命令长度
uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);
spiout_conf &= ~(0x1F << 7);
spiout_conf |= (dac_cmd_length << 7);  // DAC_CMD_LENGTH
tmc4361_write(SPIOUT_CONF, spiout_conf);

// 设置 DAC 地址
// DAC_ADDR_A: bit15:0, DAC_ADDR_B: bit31:16
tmc4361_write(DAC_ADDR, (dac_addr_b << 16) | dac_addr_a);
```

---

## 20. 电流缩放

### 20.1 概述

微步查找表（MSLUT）的电流值代表最大 9 位有符号值。在速度斜坡的大多数阶段，不需要以全电流幅度驱动电机。TMC4361 提供多种可能性来适应实际电流值。

### 20.2 缩放参数

| 参数 | 说明 |
|------|------|
| HOLD | 保持电流缩放值 |
| BOOST | 启动电流缩放值 |
| DRV1 | 驱动电流缩放值 1 |
| DRV2 | 驱动电流缩放值 2 |

### 20.3 缩放计算

```
MULT_SCALE = (actual_SCALE_VAL + 1) / 256
```

其中 actual_SCALE_VAL = {HOLD, BOOST, DRV1, DRV2}

MULT_SCALE 范围：0 < MULT_SCALE ≤ 1

**电流输出计算：**
```
CURRENTA_SPI = CURRENTA × MULT_SCALE
CURRENTB_SPI = CURRENTB × MULT_SCALE
```

### 20.4 相关寄存器

| 寄存器名 | 地址 | 说明 |
|----------|------|------|
| CURRENT_CONF | 0x05 | 电流缩放配置 |
| SCALE_VALUES | 0x06 | 缩放值（HOLD/DRV1/DRV2/BOOST） |
| STDBY_DELAY | 0x15 | 待机延迟时间 |
| FREEWHEEL_DELAY | 0x16 | 自由滚动延迟时间 |
| VDRV_SCALE_LIMIT | 0x17 | 缩放切换速度 |
| UP_SCALE_DELAY | 0x18 | 增大缩放延迟 |
| HOLD_SCALE_DELAY | 0x19 | 保持缩放延迟 |
| DRV_SCALE_DELAY | 0x1A | 驱动缩放延迟 |
| BOOST_TIME | 0x1B | 启动增强时间 |
| SCALE_PARAM | 0x7C | 实际缩放参数（只读） |

### 20.5 配置缩放值

```c
// 设置缩放值
// HOLD: bit7:0, DRV1: bit15:8, DRV2: bit23:16, BOOST: bit31:24
uint32_t scale_values = (boost << 24) | (drv2 << 16) | (drv1 << 8) | hold;
tmc4361_write(SCALE_VALUES, scale_values);
```

### 20.6 TMC26x/TMC2130 S/D 模式注意事项

> **重要：**在 S/D 模式下使用 TMC26x 和 TMC2130 步进驱动器时，缩放值仅包含 5 位，因为直接适配 TMC26x 的 CS 值和 TMC2130 的 IHOLD、IRUN 值。
>
> 此时 MULT_SCALE 计算略有不同：
> ```
> MULT_SCALE = (actual_SCALE_VAL + 1) / 32
> ```

---

## 附录 A: 寄存器地址速查表（续）

### SPI 输出和电流相关寄存器

| 地址 | 寄存器名 | 读写 | 说明 |
|------|----------|------|------|
| 0x04 | SPIOUT_CONF | RW | SPI 输出配置 |
| 0x05 | CURRENT_CONF | RW | 电流缩放配置 |
| 0x06 | SCALE_VALUES | RW | 缩放值 |
| 0x15 | STDBY_DELAY | RW | 待机延迟 |
| 0x16 | FREEWHEEL_DELAY | RW | 自由滚动延迟 |
| 0x17 | VDRV_SCALE_LIMIT | RW | 缩放切换速度 |
| 0x18 | UP_SCALE_DELAY | RW | 增大缩放延迟 |
| 0x19 | HOLD_SCALE_DELAY | RW | 保持缩放延迟 |
| 0x1A | DRV_SCALE_DELAY | RW | 驱动缩放延迟 |
| 0x1B | BOOST_TIME | RW | 启动增强时间 |
| 0x1D | DAC_ADDR | RW | DAC 地址 |
| 0x1F | SPI_SWITCH_VEL / CHOPSYNC_DIV | RW | Cover 切换速度 / ChopSync 分频 |

### 管线和影子寄存器

| 地址 | 寄存器名 | 读写 | 说明 |
|------|----------|------|------|
| 0x11 | START_OUT_ADD | RW | START 输出延长 |
| 0x12 | GEAR_RATIO | RW | 齿轮比 |
| 0x13 | START_DELAY | RW | 启动延迟 |
| 0x1E | HOME_SAFETY_MARGIN | RW | 原点安全边界 |
| 0x2C | DSTOP | RW | 停止减速度 |
| 0x32 | POS_COMP | RW | 位置比较 |
| 0x33 | VIRT_STOP_LEFT | RW | 虚拟左停止 |
| 0x34 | VIRT_STOP_RIGHT | RW | 虚拟右停止 |
| 0x35 | X_HOME | RW | 原点位置 |
| 0x36 | X_LATCH / X_RANGE / REV_CNT | R/W | 锁存位置 |
| 0x38-0x3F | X_PIPE0...7 | RW | 管线寄存器 |
| 0x40-0x4D | SH_REG0...13 | RW | 影子寄存器 |

### SPI 驱动器通信寄存器

| 地址 | 寄存器名 | 读写 | 说明 |
|------|----------|------|------|
| 0x60 | FS_VEL | W | 全步速度阈值 |
| 0x67 | VSTALL_LIMIT | RW | 堵转速度阈值 |
| 0x6C | COVER_LOW | W | Cover 低 32 位 |
| 0x6D | COVER_HIGH | W | Cover 高 32 位 |
| 0x6E | COVER_DRV_LOW | R | Cover 响应低 32 位 |
| 0x6F | COVER_DRV_HIGH | R | Cover 响应高 32 位 |

### 微步查找表寄存器

| 地址 | 寄存器名 | 读写 | 说明 |
|------|----------|------|------|
| 0x70-0x77 | MSLUT[0..7] | W | 微步查找表 |
| 0x78 | MSLUTSEL | W | 波形段配置 |
| 0x79 | MSCNT | R | 当前微步位置 |
| 0x7A | CURRENTA/CURRENTB | R | 当前电流值 |
| 0x7B | CURRENTA_SPI/CURRENTB_SPI | R | 缩放后电流值 |
| 0x7C | SCALE_PARAM / CIRCULAR_DEC | R/RW | 缩放参数 / 循环减速 |
| 0x7E | START_SIN/START_SIN90_120/DAC_OFFSET | RW | 起始值和 DAC 偏移 |

---

## 21. 紧急停止 (NFREEZE)

当系统发生故障时，某些应用需要立即终止当前操作的策略。TMC4361 提供低电平有效的安全引脚 NFREEZE。

### 21.1 NFREEZE 工作原理

NFREEZE 是低电平有效的紧急停止引脚：

- 当 NFREEZE 从高电平变为低电平时，立即以用户配置的方式停止当前斜坡
- 触发 FROZEN 事件（EVENTS 位 10）
- FROZEN 状态保持到 TMC4361 复位

**重要**: NFREEZE 必须保持低电平至少 3 个时钟周期（输入滤波需要 3 个连续采样点）。

### 21.2 FREEZE 寄存器配置

| 寄存器 | 地址 | 位域 | 说明 |
|--------|------|------|------|
| DFREEZE | 0x4E | 23:0 | 紧急减速值 |
| IFREEZE | 0x4E | 31:24 | 紧急电流缩放值 |

**注意**: FREEZE 寄存器只能在复位后、斜坡启动前写入一次，之后无法修改直到下次复位。

### 21.3 DFREEZE 配置

DFREEZE 用于配置紧急停止时的减速方式：

- **DFREEZE = 0**: 硬停止（立即停止）
- **DFREEZE ≠ 0**: 线性减速斜坡

减速值计算：
```
d_freeze [pps²] = DFREEZE / 2³⁷ · fCLK²
```

### 21.4 IFREEZE 配置

IFREEZE 用于配置紧急事件时的电流缩放：

- **IFREEZE = 0**: 保持紧急事件前的最后缩放值
- **IFREEZE ≠ 0**: 使用指定的电流缩放值

### 21.5 配置示例

```c
// 配置紧急停止（复位后立即配置）
void configure_freeze(void) {
    // 设置减速值（硬停止）
    uint32_t freeze_val = 0;  // DFREEZE = 0

    // 设置电流缩放（保持当前值）
    freeze_val |= (0 << 24);  // IFREEZE = 0

    tmc4361_write(0x4E, freeze_val);
}

// 带减速斜坡的配置
void configure_freeze_with_ramp(uint32_t decel_value, uint8_t current_scale) {
    uint32_t freeze_val = (decel_value & 0xFFFFFF);
    freeze_val |= ((uint32_t)current_scale << 24);
    tmc4361_write(0x4E, freeze_val);
}
```

---

## 22. 可控 PWM 输出

TMC4361 可在 STPOUT 和 DIROUT 输出引脚提供可控 PWM（脉宽调制）信号，用于电压模式驱动。

### 22.1 PWM 输出引脚

| 引脚名 | 类型 | 说明 |
|--------|------|------|
| STPOUT_PWMA | 输出 | 线圈 A 的 PWM 输出 |
| DIROUT_PWMB | 输出 | 线圈 B 的 PWM 输出 |
| SDODRV | 输出 | TMC23x/24x 时的 PWM A |
| NSCSDRV | 输出 | TMC23x/24x 时的 PWM B |

### 22.2 PWM 相关寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| GENERAL_CONF | 0x00 | 位 21: pwm_out_en |
| CURRENT_CONF | 0x05 | pwm_scale_en (位 8), PWM_AMPL (31:16) |
| PWM_VMAX | 0x17 | PWM 缩放达到最大值的速度 |
| PWM_FREQ | 0x1F | PWM 周期的时钟周期数 |

### 22.3 启用 PWM 输出

```c
// 启用 PWM 输出
void enable_pwm_output(uint16_t pwm_freq) {
    // 设置 PWM 周期
    tmc4361_write(PWM_FREQ, pwm_freq);

    // 启用 PWM 输出
    uint32_t general_conf = tmc4361_read(GENERAL_CONF);
    general_conf |= (1 << 21);  // pwm_out_en = 1
    tmc4361_write(GENERAL_CONF, general_conf);
}
```

PWM 频率计算：
```
fPWM = fCLK / PWM_FREQ
```

### 22.4 PWM 占空比缩放

在低速时必须启用 PWM 电压缩放，以避免电机过热。

```c
// 配置 PWM 缩放
void configure_pwm_scaling(uint16_t pwm_ampl, uint32_t pwm_vmax) {
    // 设置起始 PWM 缩放值
    uint32_t current_conf = tmc4361_read(CURRENT_CONF);
    current_conf &= ~(0xFFFF << 16);
    current_conf |= ((uint32_t)pwm_ampl << 16);  // PWM_AMPL
    current_conf |= (1 << 8);  // pwm_scale_en = 1
    tmc4361_write(CURRENT_CONF, current_conf);

    // 设置达到最大缩放的速度
    tmc4361_write(PWM_VMAX, pwm_vmax);
}
```

缩放计算：
- VACTUAL = 0 时: `PWM_SCALE = (PWM_AMPL + 1) / 2¹⁷`
- VACTUAL = PWM_VMAX 时: `PWM_SCALE = 0.5`（最大）
- 最小占空比: `DUTY_MIN = 0.5 - PWM_SCALE`
- 最大占空比: `DUTY_MAX = 0.5 + PWM_SCALE`

### 22.5 TMC23x/24x PWM 模式

TMC4361 可自动将 PWM 信号路由到 SPI 输出接口引脚，用于 TMC23x/24x 电压 PWM 模式。

```c
// 配置 TMC23x/24x PWM 模式
void configure_tmc23x_pwm(uint16_t pwm_freq) {
    // 设置 PWM 周期
    tmc4361_write(PWM_FREQ, pwm_freq);

    // 设置 spi_output_format
    uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);
    spiout_conf &= ~0x0F;
    spiout_conf |= 0x08;  // TMC23x: b'1000, TMC24x: b'1001
    tmc4361_write(SPIOUT_CONF, spiout_conf);

    // 启用 PWM 输出
    uint32_t general_conf = tmc4361_read(GENERAL_CONF);
    general_conf |= (1 << 21);  // pwm_out_en = 1
    tmc4361_write(GENERAL_CONF, general_conf);

    // 禁用 SPI 切换
    tmc4361_write(SPI_SWITCH_VEL, 0);
}
```

---

## 23. dcStep 支持

dcStep 是步进电机驱动器的自动换向模式，允许在电机能够承受负载的情况下以标称速度运行。当电机过载时，会自动降速到能够驱动负载的较低速度，避免电机失步。

### 23.1 dcStep 引脚

| 引脚名 | 类型 | 说明 |
|--------|------|------|
| MP1 | 输入 | dcStep 输入信号 |
| MP2 | 输出 | dcStep 输出信号 |

### 23.2 dcStep 寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| GENERAL_CONF | 0x00 | 位 22:21: dc_step_mode |
| DC_VEL | 0x60 | dcStep 启动速度（全步）|
| DC_TIME | 0x61(7:0) | 内部 dcStep 计算的 PWM 开启时间上限 |
| DC_SG | 0x61(15:8) | 失步检测的最大 PWM 开启时间（×16）|
| DC_BLKTIME | 0x61(31:16) | 全步释放后的 dcStep 空白时间 |
| DC_LSPTM | 0x62 | dcStep 低速定时器 |

### 23.3 dcStep 原理

dcStep 扩展了电机的可用工作区域：

- **传统操作**: 受最大速度下所需扭矩限制，需要 50% 的安全裕度
- **dcStep 操作**: 充分利用可用电机扭矩，利用飞轮质量补偿扭矩峰值

最小全步频率计算：
```
fFS = fCLK / DC_LSPTM
```

### 23.4 TMC26x dcStep 配置

TMC26x 硬件连接：
- SG_TST (TMC26x) → MP1 (TMC4361)
- TST_MODE (TMC26x) → VCCIO

```c
// 配置 TMC26x dcStep
void configure_tmc26x_dcstep(uint32_t dc_vel, uint32_t dc_lsptm) {
    // TMC26x 预配置
    // CHM = 1 (constant tOFF-Chopper)
    // HSTRT = 0 (slow decay only)
    // SGTO = 1, SGT1 = 1 (on_state_xy as test signal output)
    // TST = 1 (Test mode on)

    // 设置 spi_output_format
    uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);
    spiout_conf &= ~0x0F;
    spiout_conf |= 0x0A;  // b'1010 或 b'1011
    tmc4361_write(SPIOUT_CONF, spiout_conf);

    // 配置 dcStep 参数
    uint32_t dc_conf = 0;
    dc_conf |= (dc_time & 0xFF);           // DC_TIME
    dc_conf |= ((dc_sg & 0xFF) << 8);      // DC_SG
    dc_conf |= ((dc_blktime & 0xFFFF) << 16);  // DC_BLKTIME
    tmc4361_write(0x61, dc_conf);

    // 设置 dcStep 速度阈值
    tmc4361_write(DC_VEL, dc_vel);

    // 设置低速定时器
    tmc4361_write(DC_LSPTM, dc_lsptm);

    // 启用 dcStep 模式
    uint32_t general_conf = tmc4361_read(GENERAL_CONF);
    general_conf &= ~(0x03 << 21);
    general_conf |= (0x01 << 21);  // dcstep_mode = b'01
    tmc4361_write(GENERAL_CONF, general_conf);
}
```

### 23.5 TMC2130 dcStep 配置

TMC2130 硬件连接：
- DCO (TMC2130) → MP1 (TMC4361)
- DCEN (TMC2130) ← MP2 (TMC4361)

```c
// 配置 TMC2130 dcStep
void configure_tmc2130_dcstep(uint32_t dc_vel, uint32_t dc_lsptm) {
    // 设置 spi_output_format
    uint32_t spiout_conf = tmc4361_read(SPIOUT_CONF);
    spiout_conf &= ~0x0F;
    spiout_conf |= 0x0C;  // b'1100 或 b'1101
    tmc4361_write(SPIOUT_CONF, spiout_conf);

    // 设置 dcStep 速度阈值
    tmc4361_write(DC_VEL, dc_vel);

    // 设置低速定时器
    tmc4361_write(DC_LSPTM, dc_lsptm);

    // 启用 dcStep 模式
    uint32_t general_conf = tmc4361_read(GENERAL_CONF);
    general_conf &= ~(0x03 << 21);
    general_conf |= (0x01 << 21);  // dcstep_mode = b'01
    tmc4361_write(GENERAL_CONF, general_conf);
}
```

---

## 24. 编码器接口

TMC4361 配备编码器输入接口，支持增量式 ABN 编码器、绝对式 SSI 编码器和 SPI 编码器。

### 24.1 编码器引脚

| 引脚名 | 类型 | ABN 模式 | SSI 模式 | SPI 模式 |
|--------|------|----------|----------|----------|
| A_SCLK | 输入/输出 | A 信号 | SCLK | SCLK |
| ANEG_NSCLK | 输入/输出 | ¬A 信号 | ¬SCLK | CS |
| B_SDI | 输入 | B 信号 | SDI | SDI |
| BNEG_NSDI | 输入/输出 | ¬B 信号 | ¬SDI | SDO |
| N | 输入 | N (索引) 信号 | - | - |
| NNEG | 输入 | ¬N 信号 | - | - |

### 24.2 编码器寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| GENERAL_CONF | 0x00 | 位 11:10: serial_enc_in_mode, 位 12: diff_enc_in_disable |
| INPUT_FILT_CONF | 0x03 | 输入滤波配置 |
| ENC_IN_CONF | 0x07 | 编码器配置 |
| ENC_IN_DATA | 0x08 | 串行编码器数据结构 |
| STEP_CONF | 0x0A | 电机配置 |
| ENC_POS | 0x50 | 当前编码器位置（微步） |
| ENC_LATCH | 0x51 | 锁存的编码器位置 |
| ENC_POS_DEV | 0x52 | XACTUAL 与 ENC_POS 的偏差 |
| ENC_CONST | 0x54 | 内部计算的编码器常数 |
| ENC_VEL | 0x65 | 当前编码器速度（无符号） |
| ENC_VEL_FILT | 0x66 | 滤波后编码器速度（有符号） |

### 24.3 选择编码器类型

```c
// 选择增量式 ABN 编码器
void select_abn_encoder(void) {
    uint32_t general_conf = tmc4361_read(GENERAL_CONF);
    general_conf &= ~(0x03 << 10);  // serial_enc_in_mode = b'00
    tmc4361_write(GENERAL_CONF, general_conf);
}

// 选择绝对式 SSI 编码器
void select_ssi_encoder(void) {
    uint32_t general_conf = tmc4361_read(GENERAL_CONF);
    general_conf &= ~(0x03 << 10);
    general_conf |= (0x01 << 10);  // serial_enc_in_mode = b'01
    tmc4361_write(GENERAL_CONF, general_conf);
}

// 选择绝对式 SPI 编码器
void select_spi_encoder(void) {
    uint32_t general_conf = tmc4361_read(GENERAL_CONF);
    general_conf &= ~(0x03 << 10);
    general_conf |= (0x03 << 10);  // serial_enc_in_mode = b'11
    tmc4361_write(GENERAL_CONF, general_conf);
}
```

### 24.4 禁用差分信号

如果使用单端编码器信号：

```c
void disable_differential_encoder(void) {
    uint32_t general_conf = tmc4361_read(GENERAL_CONF);
    general_conf |= (1 << 12);  // diff_enc_in_disable = 1
    tmc4361_write(GENERAL_CONF, general_conf);
}
```

### 24.5 ABN 编码器配置

#### 24.5.1 自动计算编码器常数

```c
// 配置 ABN 编码器（自动计算常数）
void configure_abn_encoder(uint16_t fs_per_rev, uint8_t mstep_per_fs,
                           uint32_t enc_resolution) {
    // 设置电机参数
    uint32_t step_conf = tmc4361_read(STEP_CONF);
    step_conf &= ~0xFFFF;
    step_conf |= (fs_per_rev & 0x0FFF);           // FS_PER_REV
    step_conf |= ((mstep_per_fs & 0x0F) << 12);   // MSTEP_PER_FS
    tmc4361_write(STEP_CONF, step_conf);

    // 设置编码器分辨率（AB 转换数/转）
    tmc4361_write(ENC_IN_RES, enc_resolution);
}
```

编码器常数计算：
```
ENC_CONST = MSTEP_PER_FS × FS_PER_REV / ENC_IN_RES
```

#### 24.5.2 索引信号 (N) 配置

```c
// 配置索引信号极性
void configure_n_channel(bool high_active, uint8_t sensitivity) {
    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);

    if (high_active) {
        enc_conf |= (1 << 0);   // pol_n = 1
    } else {
        enc_conf &= ~(1 << 0);  // pol_n = 0
    }

    // 设置灵敏度
    // b'00: N 电平匹配 pol_n 时有效
    // b'01: N 切换到有效极性时触发
    // b'10: N 切换到无效极性时触发
    // b'11: N 任意边沿触发
    enc_conf &= ~(0x03 << 1);
    enc_conf |= ((sensitivity & 0x03) << 1);

    tmc4361_write(ENC_IN_CONF, enc_conf);
}

// 在 N 事件时清零 ENC_POS
void clear_enc_pos_on_n(uint32_t reset_value, bool continuous) {
    // 设置复位值
    tmc4361_write(ENC_RESET_VAL, reset_value);

    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);

    if (continuous) {
        enc_conf |= (1 << 3);  // clr_latch_cont_on_n = 1
    } else {
        enc_conf &= ~(1 << 3);
        enc_conf |= (1 << 4);  // clr_latch_once_on_n = 1
    }
    enc_conf |= (1 << 5);  // clear_on_n = 1

    tmc4361_write(ENC_IN_CONF, enc_conf);
}
```

### 24.6 SSI 编码器配置

#### 24.6.1 数据结构配置

```c
// 配置 SSI 编码器数据格式
void configure_ssi_data_format(uint8_t single_turn_bits, uint8_t multi_turn_bits,
                               uint8_t status_bits, bool left_aligned) {
    uint32_t enc_in_data = 0;
    enc_in_data |= ((single_turn_bits - 1) & 0x1F);          // SINGLE_TURN_RES
    enc_in_data |= (((multi_turn_bits - 1) & 0x1F) << 5);    // MULTI_TURN_RES
    enc_in_data |= ((status_bits & 0x07) << 10);             // STATUS_BIT_CNT
    tmc4361_write(ENC_IN_DATA, enc_in_data);

    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);
    if (left_aligned) {
        enc_conf |= (1 << 6);  // left_aligned_data = 1
    } else {
        enc_conf &= ~(1 << 6);
    }
    tmc4361_write(ENC_IN_CONF, enc_conf);
}
```

#### 24.6.2 SSI 时钟配置

```c
// 配置 SSI 时钟
void configure_ssi_clock(uint16_t clk_low, uint16_t clk_high,
                        uint16_t clk_delay, uint32_t pause_time) {
    // 设置时钟高低电平周期
    uint32_t ser_clk = ((uint32_t)clk_high << 16) | clk_low;
    tmc4361_write(SER_CLK_IN, ser_clk);

    // 设置时钟延迟
    tmc4361_write(SSI_IN_CLK_DELAY, clk_delay);

    // 设置请求间隔（SSI 标准 > 21µs）
    tmc4361_write(SER_PTIME, pause_time);

    // 启用 SSI 模式
    select_ssi_encoder();
}
```

### 24.7 SPI 编码器配置

```c
// 配置 SPI 编码器
void configure_spi_encoder(uint8_t single_turn_bits, uint32_t addr_to_enc,
                          uint16_t clk_low, uint16_t clk_high) {
    // 配置数据格式
    configure_ssi_data_format(single_turn_bits, 0, 0, false);

    // 设置时钟
    uint32_t ser_clk = ((uint32_t)clk_high << 16) | clk_low;
    tmc4361_write(SER_CLK_IN, ser_clk);

    // 设置请求地址
    tmc4361_write(ADDR_TO_ENC, addr_to_enc);

    // 启用 SPI 模式
    select_spi_encoder();
}

// 通过 TMC4361 配置 SPI 编码器
void spi_encoder_write(uint32_t addr, uint32_t data) {
    // 停止数据请求
    tmc4361_write(DATA_TO_ENC, 0);

    // 设置地址
    tmc4361_write(ADDR_TO_ENC, addr);

    // 发送数据（触发传输）
    tmc4361_write(DATA_TO_ENC, data);

    // 等待完成并读取响应
    uint32_t addr_resp = tmc4361_read(ADDR_FROM_ENC);
    uint32_t data_resp = tmc4361_read(DATA_FROM_ENC);  // 必须读取以恢复数据流
}
```

### 24.8 编码器失准补偿

TMC4361 可通过三角函数补偿编码器安装偏差：

```c
// 配置编码器失准补偿
void configure_encoder_compensation(uint16_t x_offset, uint8_t y_offset,
                                   uint8_t amplitude) {
    uint32_t comp = 0;
    comp |= x_offset;                    // ENC_COMP_XOFFSET
    comp |= ((uint32_t)y_offset << 16);  // ENC_COMP_YOFFSET
    comp |= ((uint32_t)amplitude << 24); // ENC_COMP_AMPL
    tmc4361_write(ENC_COMP, comp);
}
```

---

## 25. 编码器反馈调节

编码器反馈可用于控制运动控制器输出，使内部位置与实际位置匹配。TMC4361 提供 PID 控制和闭环操作两种调节模式。

### 25.1 调节模式选择

| 应用场景 | 推荐模式 |
|----------|----------|
| 编码器直接安装在电机后端，位置数据精确 | 闭环操作 |
| 编码器在驱动端，电机和驱动端无固定连接（如皮带驱动） | PID 控制 |

### 25.2 反馈监控

根据内部位置 XACTUAL 与外部位置 ENC_POS 的偏差 ENC_POS_DEV 自动生成状态标志和事件。

```c
// 设置容许的位置偏差
void set_encoder_tolerance(uint32_t tolerance) {
    tmc4361_write(ENC_POS_DEV_TOL, tolerance);
}
```

判断逻辑：
- 若 |ENC_POS_DEV| ≤ ENC_POS_DEV_TOL: 无编码器故障标志
- 若 |ENC_POS_DEV| > ENC_POS_DEV_TOL: ENC_FAIL_F 标志置位，触发 ENC_FAIL 事件

### 25.3 调节模式下的目标到达

在调节模式下，TARGET_REACHED 事件和标志仅在满足以下条件时触发：

```
XACTUAL = XTARGET 且 |ENC_POS_DEV| ≤ CL_TR_TOLERANCE
```

```c
// 设置目标到达容差
void set_closed_loop_tr_tolerance(uint32_t tolerance) {
    tmc4361_write(CL_TR_TOLERANCE, tolerance);
}
```

### 25.4 PID 控制

PID 控制器根据位置偏差 PID_E = XACTUAL - ENC_POS 计算速度值来最小化位置误差。

#### 25.4.1 PID 计算公式

```
vPID = (PID_P/256) × PID_E × [1/s] + (PID_I/256) × PID_ISUM + PID_D × dPID_E/dt
```

其中：
- PID_P: 比例项
- PID_I: 积分项
- PID_D: 微分项
- PID_ISUM: 积分累加器（更新频率: fCLK/128）

#### 25.4.2 PID 寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| PID_P | 0x59 | 比例增益 |
| PID_VEL | 0x5A | 当前 PID 输出速度 |
| PID_ISUM | 0x5B | 积分累加器 |
| PID_ISUM_LIMIT | 0x5C | 积分限幅 |
| PID_E | 0x5D | 当前位置偏差 |
| PID_I | 0x5E | 积分增益 |
| PID_D | 0x5F | 微分增益 |
| PID_TOLERANCE | 0x60 | PID 容差 |

#### 25.4.3 PID 配置示例

```c
// 配置 PID 控制器
void configure_pid_controller(uint16_t p_gain, uint16_t i_gain,
                             uint16_t d_gain, uint32_t tolerance) {
    // 设置 PID 参数
    tmc4361_write(PID_P, p_gain);
    tmc4361_write(PID_I, i_gain);
    tmc4361_write(PID_D, d_gain);

    // 设置容差
    tmc4361_write(PID_TOLERANCE, tolerance);

    // 设置积分限幅（防止积分饱和）
    tmc4361_write(PID_ISUM_LIMIT, 0x7FFFFFFF);

    // 启用闭环电流缩放
    uint32_t current_conf = tmc4361_read(CURRENT_CONF);
    current_conf |= (1 << 7);  // closed_loop_scale_en = 1
    tmc4361_write(CURRENT_CONF, current_conf);
}

// 读取 PID 状态
void read_pid_status(int32_t *pid_e, int32_t *pid_vel, int32_t *pid_isum) {
    *pid_e = (int32_t)tmc4361_read(PID_E);
    *pid_vel = (int32_t)tmc4361_read(PID_VEL);
    *pid_isum = (int32_t)tmc4361_read(PID_ISUM);
}
```

### 25.5 编码器速度计算

TMC4361 可自动计算编码器速度：

```c
// 读取编码器速度
uint32_t read_encoder_velocity(void) {
    return tmc4361_read(ENC_VEL);  // 无符号
}

int32_t read_filtered_encoder_velocity(void) {
    return (int32_t)tmc4361_read(ENC_VEL_FILT);  // 有符号，滤波后
}
```

---

## 26. 闭环操作详解

闭环操作直接修改 SPI 输出电流和内部步进生成器的 Step/Dir 输出，基于编码器反馈数据进行调节。TMC4361 的两相闭环控制采用与场向量控制（FOC）不同的方法，类似于 PID 控制级联。

> **重要**: 闭环操作只能与 256 微步/全步分辨率配合使用（MSTEPS_PER_FS = 0）。

### 26.1 闭环基本参数

闭环控制通过评估内部位置 XACTUAL 与外部位置 ENC_POS 之间的差异来验证 SPI 输出电流值和 Step/Dir 输出，同时考虑校准偏移参数 CL_OFFSET。

#### 26.1.1 关键寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| CL_OFFSET | 0x59 | 闭环校准偏移值（内外位置差） |
| ENC_POS_DEV | 0x52 | 当前位置偏差（只读） |
| CL_BETA | 0x1C[8:0] | 最大换相角（微步） |
| CL_TOLERANCE | 0x5F[7:0] | 位置偏差容差 |
| CL_DELTA_P | 0x5C | 比例控制器增益 |
| CL_CYCLE | 0x63[31:16] | 调节周期延迟（时钟周期） |

#### 26.1.2 参数说明

**CL_BETA（最大换相角）**
- 用于补偿检测到的位置偏差 ENC_POS_DEV 的最大换相角
- 当偏差达到 CL_BETA 值时，换相角保持稳定并触发 CL_MAX 事件
- 推荐值：255（90°）

**CL_TOLERANCE（位置容差）**
- 若 |ENC_POS_DEV| ≤ CL_TOLERANCE，则 CL_FIT_F 标志置位
- 位置匹配消除时触发 CL_FIT 事件

**CL_DELTA_P（比例控制器）**
- 补偿检测到的内外位置偏差
- 24 位值，后 16 位为小数位
- 实际比例项：p_PID = CL_DELTA_P / 65536
- 值越大，对位置偏差的响应越快
- **注意**：过高的 p_PID 可能导致振荡

```c
// 配置闭环基本参数
void configure_closed_loop_basic(void) {
    // 设置最大换相角（推荐 255 = 90°）
    uint32_t reg = tmc4361_read(0x1C);
    reg = (reg & 0xFFFFFE00) | 255;  // CL_BETA = 255
    tmc4361_write(0x1C, reg);

    // 设置位置容差
    reg = tmc4361_read(0x5F);
    reg = (reg & 0xFFFFFF00) | 10;  // CL_TOLERANCE = 10 微步
    tmc4361_write(0x5F, reg);

    // 设置比例增益（65536 = 1.0）
    tmc4361_write(0x5C, 65536);  // CL_DELTA_P = 1.0
}
```

### 26.2 闭环校准与启用

校准过程建立内部位置与外部编码器位置之间的对应关系。

#### 26.2.1 方法一：自动校准（推荐）

```c
// 闭环校准和启用（自动生成 CL_OFFSET）
void enable_closed_loop_with_calibration(void) {
    // 前提：设置为最佳最大电流缩放

    // 1. 设置为 256 微步/全步
    uint32_t step_conf = tmc4361_read(STEP_CONF);
    step_conf = (step_conf & 0xFFFFFFF0) | 0;  // MSTEP_PER_FS = 0
    tmc4361_write(STEP_CONF, step_conf);

    // 2. 移动到任意全步位置（MSCNT mod 128 = 0）
    // ...（运动代码省略）

    // 3. 设置调节模式为闭环
    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);
    enc_conf = (enc_conf & 0xFF3FFFFF) | (1 << 22);  // regulation_modus = 01
    tmc4361_write(ENC_IN_CONF, enc_conf);

    // 4. 启用校准
    enc_conf = tmc4361_read(ENC_IN_CONF);
    enc_conf |= (1 << 24);  // cl_calibration_en = 1
    tmc4361_write(ENC_IN_CONF, enc_conf);

    // 5. 等待系统稳定
    delay_ms(100);

    // 6. 关闭校准
    enc_conf = tmc4361_read(ENC_IN_CONF);
    enc_conf &= ~(1 << 24);  // cl_calibration_en = 0
    tmc4361_write(ENC_IN_CONF, enc_conf);

    // 闭环操作已启用，CL_OFFSET 已自动设置
}
```

#### 26.2.2 方法二：使用已知偏移值

```c
// 使用预存的 CL_OFFSET 启用闭环
void enable_closed_loop_with_offset(int32_t offset) {
    // 1. 设置为 256 微步/全步
    uint32_t step_conf = tmc4361_read(STEP_CONF);
    step_conf = (step_conf & 0xFFFFFFF0) | 0;
    tmc4361_write(STEP_CONF, step_conf);

    // 2. 设置调节模式为闭环
    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);
    enc_conf = (enc_conf & 0xFF3FFFFF) | (1 << 22);
    tmc4361_write(ENC_IN_CONF, enc_conf);

    // 3. 写入已知偏移值
    tmc4361_write(0x59, offset);  // CL_OFFSET

    // 闭环操作已启用
}
```

### 26.3 闭环追赶速度限制

当需要补偿运动扰动时，可限制追赶速度以避免过冲。

#### 26.3.1 速度限制寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| CL_VMAX_CALC_P | 0x5A | PI 调节器的 P 参数 |
| CL_VMAX_CALC_I | 0x5B | PI 调节器的 I 参数 |
| PID_DV_CLIP | 0x5E | 最大速度偏差限幅 |
| PID_I_CLIP | 0x5D[14:0] | 积分累加器限幅 |

#### 26.3.2 配置示例

```c
// 配置闭环追赶速度限制
void configure_closed_loop_velocity_limit(void) {
    // 设置 PI 参数
    tmc4361_write(0x5A, 256);   // CL_VMAX_CALC_P
    tmc4361_write(0x5B, 64);    // CL_VMAX_CALC_I

    // 设置速度限幅
    tmc4361_write(0x5E, 100000);  // PID_DV_CLIP

    // 设置积分限幅（建议：PID_I_CLIP ≤ PID_DV_CLIP / PID_I）
    uint32_t reg = tmc4361_read(0x5D);
    reg = (reg & 0xFFFF8000) | 0x7FFF;
    tmc4361_write(0x5D, reg);

    // 启用速度限制
    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);
    enc_conf |= (1 << 27);  // cl_vlimit_en = 1
    tmc4361_write(ENC_IN_CONF, enc_conf);
}
```

### 26.4 闭环速度模式

某些应用只需维持指定的速度值，不关心位置偏差。TMC4361 提供闭环速度模式支持。

> **注意**: 闭环速度模式独立于内部斜坡操作模式（速度或定位模式）设置。

```c
// 启用闭环速度模式
void enable_closed_loop_velocity_mode(void) {
    // 1. 先配置追赶速度参数
    configure_closed_loop_velocity_limit();

    // 2. 启用速度模式
    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);
    enc_conf |= (1 << 27);  // cl_vlimit_en = 1
    enc_conf |= (1 << 28);  // cl_velocity_mode_en = 1
    tmc4361_write(ENC_IN_CONF, enc_conf);

    // 若 |ENC_POS_DEV| 超过 768 微步，XACTUAL 自动调整为 ENC_POS ± 768
}
```

### 26.5 闭环电流缩放

为节省能源，闭环操作期间可根据实际负载调整电流缩放。

#### 26.5.1 闭环缩放参数

| 参数 | SCALE_VALUES 位 | 说明 |
|------|-----------------|------|
| CL_IMIN | [7:0] | 最小缩放值（低负载） |
| CL_IMAX | [15:8] | 最大缩放值（高负载） |
| CL_START_UP | [23:16] | 开始增加电流的偏差阈值 |
| CL_START_DOWN | [31:24] | 开始降低电流的偏差阈值（建议设为 0） |

#### 26.5.2 缩放逻辑

1. 若 |ENC_POS_DEV| ≤ CL_START_UP：电流缩放值 = CL_IMIN
2. 若 CL_START_UP < |ENC_POS_DEV| ≤ CL_BETA：电流从 CL_IMIN 线性增加到 CL_IMAX
3. 若 |ENC_POS_DEV| > CL_BETA：电流缩放值 = CL_IMAX

```c
// 配置闭环电流缩放
void configure_closed_loop_scaling(uint8_t imin, uint8_t imax, uint8_t start_up) {
    // 设置缩放值
    uint32_t scale_values = (0 << 24) |        // CL_START_DOWN = 0（自动设为 CL_BETA）
                           (start_up << 16) |   // CL_START_UP
                           (imax << 8) |        // CL_IMAX
                           imin;                // CL_IMIN
    tmc4361_write(SCALE_VALUES, scale_values);

    // 启用闭环缩放（自动禁用开环缩放选项）
    uint32_t current_conf = tmc4361_read(CURRENT_CONF);
    current_conf |= (1 << 7);  // closed_loop_scale_en = 1
    tmc4361_write(CURRENT_CONF, current_conf);
}
```

#### 26.5.3 缩放过渡控制

可配置电流缩放值的平滑过渡：

```c
// 配置缩放过渡延迟
void configure_scale_transition(uint32_t upscale_delay, uint32_t dnscale_delay) {
    // 上升延迟：每 upscale_delay 时钟周期增加一步
    tmc4361_write(0x18, upscale_delay);   // CL_UPSCALE_DELAY

    // 下降延迟：每 dnscale_delay 时钟周期减少一步
    tmc4361_write(0x19, dnscale_delay);   // CL_DNSCALE_DELAY

    // 若设为 0，则立即切换到目标值
}
```

### 26.6 反电动势补偿

高速运动时，电机线圈会产生电流和电压之间的相移。TMC4361 通过 γ 校正来补偿这一效应，在运动方向上添加与速度相关的角度到换相角。

#### 26.6.1 反电动势寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| CL_GAMMA | 0x1C[23:16] | 最大补偿角（默认 255 = 90°） |
| CL_VMIN_EMF | 0x60 | 开始补偿的最小速度 |
| CL_VMAX_EMF | 0x61 | 达到最大补偿的速度（CL_VADD_EMF） |

#### 26.6.2 补偿逻辑

1. 若 |V_ENC_MEAN| ≤ CL_VMIN_EMF：GAMMA = 0
2. 若 CL_VMIN_EMF < |V_ENC_MEAN| ≤ (CL_VMIN_EMF + CL_VADD_EMF)：GAMMA 线性增加
3. 若 |V_ENC_MEAN| > (CL_VMIN_EMF + CL_VADD_EMF)：GAMMA = CL_GAMMA

> **警告**: 启用 γ 校正后，最大可能换相角为 (CL_BETA + CL_GAMMA)，该值不得超过 180°（511 微步），否则会导致运动方向意外改变。

```c
// 配置反电动势补偿
void configure_back_emf_compensation(uint32_t vmin, uint32_t vadd, uint8_t gamma) {
    // 设置 CL_GAMMA
    uint32_t reg = tmc4361_read(0x1C);
    reg = (reg & 0xFF00FFFF) | ((uint32_t)gamma << 16);
    tmc4361_write(0x1C, reg);

    // 设置速度阈值
    tmc4361_write(0x60, vmin);  // CL_VMIN_EMF
    tmc4361_write(0x61, vadd);  // CL_VADD_EMF（实际为 CL_VMAX_EMF）

    // 启用反电动势补偿
    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);
    enc_conf |= (1 << 25);  // cl_emf_en = 1
    tmc4361_write(ENC_IN_CONF, enc_conf);
}
```

### 26.7 编码器速度滤波

编码器速度值存在固有波动。TMC4361 提供滤波选项用于反电动势补偿。

#### 26.7.1 速度读取寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| V_ENC | 0x65 | 实际编码器速度 [pps]（波动） |
| V_ENC_MEAN | 0x66 | 滤波后编码器速度 [pps] |

#### 26.7.2 滤波参数

| 参数 | 地址/位 | 说明 |
|------|---------|------|
| ENC_VMEAN_WAIT | 0x63[7:0] | 速度采样间隔（时钟周期） |
| ENC_VMEAN_FILTER | 0x63[11:8] | 滤波指数（值越小响应越快） |
| ENC_VMEAN_INT | 0x63[31:16] | 速度更新周期 |
| ENC_VEL_ZERO | 0x62 | 零速度检测阈值 |

滤波公式：
```
V_ENC_MEAN = V_ENC_MEAN - V_ENC_MEAN/2^ENC_VMEAN_FILTER + V_ENC/2^ENC_VMEAN_FILTER
```

```c
// 配置编码器速度滤波
void configure_encoder_velocity_filter(uint8_t wait, uint8_t filter_exp) {
    uint32_t reg = tmc4361_read(0x63);
    reg = (reg & 0xFFFFF000) | ((uint32_t)filter_exp << 8) | wait;
    tmc4361_write(0x63, reg);
}

// 设置零速度检测
void set_encoder_velocity_zero_threshold(uint32_t threshold) {
    tmc4361_write(0x62, threshold);  // ENC_VEL_ZERO
    // 若在 threshold 时钟周期内无 AB 信号变化，触发 ENC_VEL0 事件
}
```

### 26.8 PID 控制参数详解

PID 控制器的高级配置参数。

#### 26.8.1 裁剪参数

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| PID_DV_CLIP | 0x5E | 限制 vPID 和 PID_VEL 的最大值 |
| PID_I_CLIP | 0x5D[14:0] | 积分累加器 PID_ISUM 的限幅 |
| PID_D_CLKDIV | 0x5D[23:16] | 微分项的时间缩放 |
| PID_TOLERANCE | 0x5F | 目标位置稳定的迟滞值 |

> **约束条件**: PID_I_CLIP ≤ PID_DV_CLIP / PID_I

#### 26.8.2 调节模式

| regulation_modus | 说明 |
|------------------|------|
| 00 | 无调节 |
| 01 | 闭环操作 |
| 10 | PID 调节，脉冲生成器基速度 = 0 |
| 11 | PID 调节，脉冲生成器基速度 = VACTUAL |

```c
// 配置 PID 裁剪参数
void configure_pid_clipping(uint32_t dv_clip, uint16_t i_clip, uint8_t d_clkdiv) {
    // 设置速度裁剪
    tmc4361_write(0x5E, dv_clip);

    // 设置积分和微分参数
    uint32_t reg = ((uint32_t)d_clkdiv << 16) | i_clip;
    tmc4361_write(0x5D, reg);
}

// 启用 PID 调节（基速度 = VACTUAL）
void enable_pid_regulation_with_vactual(void) {
    uint32_t enc_conf = tmc4361_read(ENC_IN_CONF);
    enc_conf = (enc_conf & 0xFF3FFFFF) | (3 << 22);  // regulation_modus = 11
    tmc4361_write(ENC_IN_CONF, enc_conf);
}
```

---

## 27. 寄存器详细定义（补充）

本章补充前文未详细介绍的寄存器定义。

### 27.1 冻结寄存器 (FREEZE)

冻结寄存器只能在复位后、运动开始前写入一次。始终可读。

| 地址 | 位 | 名称 | 默认值 | 说明 |
|------|-----|------|--------|------|
| 0x4E | [23:0] | DFREEZE | 0x000000 | 冻结减速值。当 NFREEZE 变为低电平时用于线性斜坡停止。设为 0 则硬停止 |
| 0x4E | [31:24] | IFREEZE | 0x00 | NFREEZE 为低时的电流缩放值。若为 0，使用冻结事件时的当前值 |

**DFREEZE 计算**:
- Direct 模式: a[Δv/clk] = DFREEZE / 2³⁷
- 建议: DFREEZE ≤ 65536

### 27.2 编码器寄存器 (Encoder Registers)

| 地址 | 位 | 名称 | R/W | 默认值 | 说明 |
|------|-----|------|-----|--------|------|
| 0x50 | [31:0] | ENC_POS | RW | 0x00000000 | 实际编码器位置 [μsteps] |
| 0x51 | [31:0] | ENC_LATCH | R | 0x00000000 | 锁存的编码器位置 |
| 0x51 | [31:0] | ENC_RESET_VAL | W | 0x00000000 | ENC_POS 清零时的重置值 |
| 0x52 | [31:0] | ENC_POS_DEV | R | 0x00000000 | XACTUAL 与 ENC_POS 的偏差 |
| 0x52 | [31:0] | CL_TR_TOLERANCE | W | 0x00000000 | 闭环时触发 TARGET_REACHED 的容差 |
| 0x53 | [31:0] | ENC_POS_DEV_TOL | W | 0xFFFFFFFF | ENC_POS_DEV 的最大容差（超过则报错） |
| 0x54 | [30:0] | ENC_IN_RES | W | 0x00000000 | 编码器输入分辨率 [每转编码器步数] |
| 0x54 | [30:0] | ENC_CONST | R | 0x00000000 | 编码器常数（15位整数 + 16位小数） |
| 0x54 | [31] | manual_enc_const | W | 0 | 0=自动计算；1=手动定义 |
| 0x55 | [31:0] | ENC_OUT_RES | W | 0x00000000 | 编码器输出分辨率 |

**串行编码器时钟配置**:

| 地址 | 位 | 名称 | 默认值 | 说明 |
|------|-----|------|--------|------|
| 0x56 | [15:0] | SER_CLK_IN_HIGH | 0x00A0 | 串行时钟高电平时间 [时钟周期] |
| 0x56 | [31:16] | SER_CLK_IN_LOW | 0x00A0 | 串行时钟低电平时间 [时钟周期] |
| 0x57 | [15:0] | SSI_IN_CLK_DELAY | 0x0000 | SSI/SPI 时钟延迟 |
| 0x57 | [31:16] | SSI_IN_WTIME | 0x00F0 | 多次数据传输间延迟（tw < 19μs） |
| 0x58 | [19:0] | SER_PTIME | 0x00190 | 连续请求间周期（tp > 21μs） |

### 27.3 PID 和闭环寄存器

| 地址 | 位 | 名称 | R/W | 默认值 | 说明 |
|------|-----|------|-----|--------|------|
| 0x59 | [31:0] | CL_OFFSET | RW | 0x00000000 | 闭环校准后的偏移量 |
| 0x5A | [23:0] | PID_P | W | 0x000000 | PID 比例参数 |
| 0x5A | [23:0] | CL_VMAX_CALC_P | W | 0x000000 | 闭环追赶速度 PI 调节器 P 参数 |
| 0x5A | [31:0] | PID_VEL | R | 0x00000000 | 实际 PID 输出速度 |
| 0x5B | [23:0] | PID_I | W | 0x000000 | PID 积分参数 |
| 0x5B | [23:0] | CL_VMAX_CALC_I | W | 0x000000 | 闭环追赶速度 PI 调节器 I 参数 |
| 0x5B | [31:0] | PID_ISUM_RD | R | 0x00000000 | 实际 PID 积分累加和 |
| 0x5C | [23:0] | PID_D | W | 0x000000 | PID 微分参数 |
| 0x5C | [23:0] | CL_DELTA_P | W | 0x000000 | 位置维持刚度增益（65536 = 1.0） |
| 0x5D | [14:0] | PID_I_CLIP | W | 0x0000 | PID 积分限幅 |
| 0x5D | [23:16] | PID_D_CLKDIV | W | 0x00 | D 部分时钟分频 |
| 0x5D | [31:0] | PID_E | R | 0x00000000 | 实际位置偏差 |
| 0x5E | [30:0] | PID_DV_CLIP | W | 0x00000000 | PID 速度限幅 |
| 0x5F | [19:0] | PID_TOLERANCE | W | 0x00000 | PID 位置容差 |
| 0x5F | [7:0] | CL_TOLERANCE | W | 0x00 | 闭环位置容差 |

### 27.4 杂项寄存器

| 地址 | 位 | 名称 | 功能 | 默认值 |
|------|-----|------|------|--------|
| 0x60 | [23:0] | FS_VEL | 最小全步速度 [pps]（开环） | 0x000000 |
| 0x60 | [23:0] | DC_VEL | 最小 dcStep 速度 [pps] | 0x000000 |
| 0x60 | [23:0] | CL_VMIN_EMF | 反电动势补偿起始速度 | 0x000000 |
| 0x61 | [7:0] | DC_TIME | dcStep PWM 导通时间上限 | 0x00 |
| 0x61 | [15:8] | DC_SG | dcStep 丢步检测阈值 | 0x00 |
| 0x61 | [31:16] | DC_BLKTIME | dcStep 释放后空白时间 | 0x0000 |
| 0x61 | [23:0] | CL_VADD_EMF | 反电动势补偿速度增量 | 0x000000 |
| 0x62 | [31:0] | DC_LSPTM | dcStep 低速定时器 | 0x00FFFFFF |
| 0x62 | [23:0] | ENC_VEL_ZERO | 编码器速度归零延迟 | 0xFFFFFF |
| 0x63 | [7:0] | ENC_VMEAN_WAIT | 编码器平均速度延迟 | 0x00 |
| 0x63 | [7:0] | SER_ENC_VARIATION | 串行编码器变化容差 | 0x00 |
| 0x63 | [11:8] | ENC_VMEAN_FILTER | 编码器速度滤波指数 | 0x0 |
| 0x63 | [31:16] | ENC_VMEAN_INT | 编码器速度更新时间 | 0x0000 |
| 0x63 | [31:16] | CL_CYCLE | 闭环控制周期 | 0x0000 |
| 0x65 | [31:0] | V_ENC | 实际编码器速度 [pps] | 0x00000000 |
| 0x66 | [31:0] | V_ENC_MEAN | 滤波后编码器速度 [pps] | 0x00000000 |
| 0x67 | [23:0] | VSTALL_LIMIT | 堵转停止速度阈值 [pps] | 0x00000000 |
| 0x7C | [31:0] | CIRCULAR_DEC | 循环运动小数部分 | 0x000 |
| 0x7D | [15:0] | ENC_COMP_XOFFSET | 编码器补偿 X 偏移 | 0x0000 |
| 0x7D | [23:16] | ENC_COMP_YOFFSET | 编码器补偿 Y 偏移 | 0x00 |
| 0x7D | [31:24] | ENC_COMP_AMPL | 编码器补偿幅度 | 0x00 |

### 27.5 传输寄存器 (SPI 编码器)

| 地址 | 名称 | R/W | 说明 |
|------|------|-----|------|
| 0x68 | ADDR_TO_ENC | W | 发送到 SPI 编码器的地址数据 |
| 0x69 | DATA_TO_ENC | W | 发送到 SPI 编码器的配置数据 |
| 0x6A | ADDR_FROM_ENC | R | 从 SPI 编码器接收的地址数据 |
| 0x6B | DATA_FROM_ENC | R | 从 SPI 编码器接收的数据 |
| 0x6C | COVER_LOW | W | 发送到电机驱动器的低 32 位 SPI 数据 |
| 0x6D | COVER_HIGH | W | 发送到电机驱动器的高 32 位 SPI 数据 |
| 0x6E | COVER_DRV_LOW | R | 从电机驱动器接收的低 32 位数据 |
| 0x6F | COVER_DRV_HIGH | R | 从电机驱动器接收的高 32 位数据 |

**自动 Cover 数据传输**:
- `automatic_cover = 1` 时:
  - 速度向下越过 SPI_SWITCH_VEL: 发送 COVER_LOW
  - 速度向上越过 SPI_SWITCH_VEL: 发送 COVER_HIGH
- 设置 COVER_DATA_LENGTH ≤ 32

### 27.6 正弦查找表寄存器 (SinLUT)

| 地址 | 名称 | 默认值 | 说明 |
|------|------|--------|------|
| 0x70 | MSLUT[0] | 0xAAAAB554 | 微步查找表 0 |
| 0x71 | MSLUT[1] | 0x4A9554AA | 微步查找表 1 |
| 0x72 | MSLUT[2] | 0x24492929 | 微步查找表 2 |
| 0x73 | MSLUT[3] | 0x10104222 | 微步查找表 3 |
| 0x74 | MSLUT[4] | 0xFBFFFFFF | 微步查找表 4 |
| 0x75 | MSLUT[5] | 0xB5BB777D | 微步查找表 5 |
| 0x76 | MSLUT[6] | 0x49295556 | 微步查找表 6 |
| 0x77 | MSLUT[7] | 0x00404222 | 微步查找表 7 |
| 0x78 | MSLUTSEL | 0xFFFF8056 | 查找表段选择 |
| 0x79 | MSCNT | - | 当前微步位置 (R) |
| 0x79 | MSOFFSET | 0x000 | PWM 模式微步偏移 (W) |
| 0x7A | CURRENTA/B | - | 当前线圈 A/B 电流值 (R) |
| 0x7B | CURRENTA/B_SPI | - | 发送到驱动器的缩放电流值 (R) |
| 0x7C | SCALE_PARAM | - | 当前使用的缩放参数 (R) |
| 0x7E | START_SIN | 0x00 | 正弦波形起始值 |
| 0x7E | START_SIN90_120 | 0xF7 | 余弦波形起始值 |
| 0x7E | DAC_OFFSET | 0x00 | DAC 输出偏移 |

### 27.7 版本寄存器

| 地址 | 位 | 名称 | 说明 |
|------|-----|------|------|
| 0x7F | [15:0] | Version No | TMC4361 版本号（默认 0x0001） |

---

## 28. 电气特性

### 28.1 绝对最大额定值

**任何情况下都不得超过最大额定值。**

#### 3.3V 供电模式 (TEST_MODE = 0V)

| 参数 | 符号 | 最小值 | 最大值 | 单位 |
|------|------|--------|--------|------|
| 供电电压 | V_CC | 3.0 | 3.6 | V |
| IO 输入电压 | V_IN | −0.3 | 3.6 | V |

#### 5.0V 供电模式 (TEST_MODE = 1.8V)

| 参数 | 符号 | 最小值 | 最大值 | 单位 |
|------|------|--------|--------|------|
| 供电电压 | V_CC | 4.8 | 5.2 | V |
| IO 输入电压 | V_IN | −0.3 | 5.2 | V |

#### 温度额定值

| 参数 | 符号 | 最小值 | 最大值 | 单位 |
|------|------|--------|--------|------|
| 工作温度 | T | −40 | 125 | °C |

### 28.2 DC 特性

| 参数 | 符号 | 条件 | 最小值 | 典型值 | 最大值 | 单位 |
|------|------|------|--------|--------|--------|------|
| 扩展温度范围 | T_COM | | −40°C | | 125 | °C |
| 核心电压 | V_DD | | | 1.8 | | V |
| IO 电压 | V_DD | | | 3.3 / 5.0 | | V |
| 输入低电平 | V_INL | V_DD=3.3/5V | −0.3 | | 0.8/1.2 | V |
| 输入高电平 | V_INH | V_DD=3.3/5V | 2.3/3.5 | | 3.6/5.2 | V |
| 下拉输入电流 | | V_IN=V_DD | 5 | 30 | 110 | μA |
| 上拉输入电流 | | V_IN=0V | −110 | −30 | −5 | μA |
| 输出低电平 | V_OUTL | V_DD=3.3/5V | | | 0.4 | V |
| 输出高电平 | V_OUTH | V_DD=3.3/5V | 2.64/4.0 | | | V |
| 输出驱动能力 | I_OUT_DRV | V_DD=3.3/5V | | 4.0 | | mA |

### 28.3 功耗

| 参数 | 符号 | 条件 | 最大值 | 单位 |
|------|------|------|--------|------|
| 静态功耗 | PD_STAT | 所有输入为 V_DD 或 GND | 1.1 / 1.7 | mW |
| 动态功耗 | PD_DYN | f_CLK 可变 | 2.3 / 3.7 | mW/MHz |
| 总功耗 | PD | f_CLK = 16 MHz | 37.9 / 60.3 | mW |

### 28.4 通用 IO 时序参数

| 参数 | 符号 | 条件 | 最小值 | 典型值 | 最大值 | 单位 |
|------|------|------|--------|--------|--------|------|
| 工作频率 | f_CLK | | 4.2¹ | 16 | 30 | MHz |
| 时钟周期 | t_CLK | 上升沿到上升沿 | 33.5 | 62.5 | | ns |
| 时钟低电平 | | | 16.5 | | | ns |
| 时钟高电平 | | | 16.5 | | | ns |
| 输入信号上升时间 | t_RISE_IN | 20%至80% | | | 20 | ns |
| 输入信号下降时间 | t_FALL_IN | 80%至20% | | | 20 | ns |
| 输出信号上升时间 | t_RISE_OUT | 20%至80%，32pF | | 3.5 | | ns |
| 输出信号下降时间 | t_FALL_OUT | 80%至20%，32pF | | 3.5 | | ns |
| SPI 建立时间 | t_SU | 相对上升沿 | 5 | | | ns |
| SPI 保持时间 | t_HD | 相对上升沿 | 5 | | | ns |

> ¹ f_CLK 下限是内部单位转换的限制。芯片在更低频率下也能工作。

---

## 29. 封装信息

### 29.1 封装尺寸

TMC4361 采用 QFN40 封装，尺寸为 6 x 6 mm²。

| 参数 | 符号 | 最小值 | 典型值 | 最大值 | 单位 |
|------|------|--------|--------|--------|------|
| 总厚度 | A | 0.8 | 0.85 | 0.9 | mm |
| 离板高度 | A1 | 0 | 0.035 | 0.05 | mm |
| 模塑厚度 | A2 | - | 0.65 | 0.67 | mm |
| 引脚宽度 | b | 0.2 | 0.25 | 0.3 | mm |
| 本体尺寸 X | D | | 6 BSC | | mm |
| 本体尺寸 Y | E | | 6 BSC | | mm |
| 引脚间距 | e | | 0.5 BSC | | mm |
| 暴露焊盘尺寸 X | J | 4.52 | 4.62 | 4.72 | mm |
| 暴露焊盘尺寸 Y | K | 4.52 | 4.62 | 4.72 | mm |
| 引脚长度 | L | 0.35 | 0.4 | 0.45 | mm |
| 共面性 | ccc | | | 0.08 | mm |

### 29.2 芯片标记

每个芯片上的标记显示：
1. TRINAMIC 徽标
2. 产品代码：TMC4361-LA
3. 日期代码：YYWW LLLL
4. 产地：GERMANY
5. 批号

### 29.3 布局建议

1. **电源去耦**: 在 VCC 引脚附近放置 100nF 陶瓷电容
2. **GND 平面**: 使用完整的内层 GND 平面
3. **暴露焊盘**: 必须焊接到 GND，提供良好的散热路径
4. **信号完整性**: SPI 信号线保持短且平行

---

## 附录 B: STATUS_FLAGS 和 EVENTS 位定义

### STATUS_FLAGS (0x0F) 重要位

| 位 | 名称 | 说明 |
|---|------|------|
| 0 | TARGET_REACHED_F | XACTUAL = XTARGET |
| 1 | POS_COMP_REACHED_F | 位置比较匹配 |
| 2 | VEL_REACHED_F | VACTUAL = VMAX |
| 7 | STOPL_ACTIVE_F | 左停止开关激活 |
| 8 | STOPR_ACTIVE_F | 右停止开关激活 |
| 9 | VSTOPL_ACTIVE_F | 左虚拟停止激活 |
| 10 | VSTOPR_ACTIVE_F | 右虚拟停止激活 |
| 12 | HOME_ERROR_F | 原点位置错误 |

### EVENTS (0x0E) 重要位

| 位 | 名称 | 说明 |
|---|------|------|
| 0 | TARGET_REACHED | 目标到达事件 |
| 1 | POS_COMP_REACHED | 位置比较匹配事件 |
| 2 | VEL_REACHED | 速度到达事件 |
| 11 | STOPL_EVENT | 左停止事件 |
| 12 | STOPR_EVENT | 右停止事件 |
| 13 | VSTOPL_EVENT | 左虚拟停止事件 |
| 14 | VSTOPR_EVENT | 右虚拟停止事件 |
| 22 | XLATCH_DONE | 位置锁存完成事件 |

---

## 修订历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-01-22 | 初始版本，基于 TMC4361 Datasheet 页 1-40 |
| 1.1 | 2026-01-22 | 添加页 41-80 内容：高级斜坡配置、外部步进控制、参考开关、同步 |
| 1.2 | 2026-01-22 | 添加页 81-120 内容：目标管线、无主同步、SPI 输出接口、电流缩放 |
| 1.3 | 2026-01-22 | 添加页 121-160 内容：紧急停止、PWM 输出、dcStep 支持、编码器接口、编码器反馈调节 |
| 1.4 | 2026-01-22 | 添加页 161-200 内容：闭环操作详解（校准、速度模式、电流缩放、反电动势补偿） |
| 1.5 | 2026-01-22 | 添加页 201-224 内容：完整寄存器定义、电气特性、封装信息 |

---

*本文档由 Claude Code 自动生成，用于 Octoaxes 项目。*
