# Octoaxes 固件架构文档

> 本文档详细描述固件的技术架构，供未来重构参考。
>
> 最后更新：2026-01-21

---

## 目录

1. [系统概述](#1-系统概述)
2. [硬件通讯架构](#2-硬件通讯架构)
3. [SPI 通讯协议](#3-spi-通讯协议)
4. [TMC4361A 驱动层](#4-tmc4361a-驱动层)
5. [TMC2660 驱动配置](#5-tmc2660-驱动配置)
6. [初始化流程](#6-初始化流程)
7. [轴类继承体系](#7-轴类继承体系)
8. [状态机设计](#8-状态机设计)
9. [命令处理流程](#9-命令处理流程)
10. [单位转换系统](#10-单位转换系统)
11. [关键寄存器参考](#11-关键寄存器参考)
12. [配置参数表](#12-配置参数表)
13. [重构建议](#13-重构建议)

---

## 1. 系统概述

### 1.1 硬件组成

```
┌─────────────────┐     USB/Serial     ┌─────────────────┐
│   PC 上位机     │ ◄───────────────► │   Teensy 4.1    │
│   (PyQt5)       │     115200 baud    │   主控制器      │
└─────────────────┘                    └────────┬────────┘
                                                │ SPI (500kHz)
                                       ┌────────┴────────┐
                                       │  TMC4361A x7    │
                                       │  运动控制器     │
                                       └────────┬────────┘
                                                │ Cover 接口
                                       ┌────────┴────────┐
                                       │  TMC2660 x7     │
                                       │  步进驱动器     │
                                       └────────┬────────┘
                                                │
                                       ┌────────┴────────┐
                                       │  步进电机 x7    │
                                       └─────────────────┘
```

### 1.2 软件分层

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (Application)                  │
│  octoaxes.ino, commandprocessor.cpp, serial.cpp         │
├─────────────────────────────────────────────────────────┤
│                    管理层 (Management)                   │
│  AxisManager (axesmrg.cpp)                              │
├─────────────────────────────────────────────────────────┤
│                    轴控制层 (Axis Control)               │
│  Axis, StepAxis, FilterWheel, Objectives                │
├─────────────────────────────────────────────────────────┤
│                    驱动层 (Driver)                       │
│  TMC4361A.cpp, TMC4361A_TMC2660_Utils.cpp               │
├─────────────────────────────────────────────────────────┤
│                    硬件抽象层 (HAL)                      │
│  Arduino SPI, GPIO                                      │
└─────────────────────────────────────────────────────────┘
```

### 1.3 文件结构

```
firmware/octoaxes/
├── octoaxes.ino              # 主程序入口
├── config.h                  # 系统和轴配置参数
├── axis.h/.cpp               # 轴基类
├── stepaxis.h/.cpp           # 步进轴实现
├── filterwheel.h/.cpp        # 滤光轮控制
├── objectives.h/.cpp         # 物镜切换器
├── axesmrg.h/.cpp            # 轴管理器
├── commandprocessor.h/.cpp   # 命令处理器
├── serial.h/.cpp             # 串口通信协议
├── TMC4361A.h/.cpp           # TMC4361A 寄存器定义和基础操作
├── TMC4361A_Fields.h         # TMC4361A 寄存器位域定义
├── TMC4361A_Register.h       # TMC4361A 寄存器地址定义
├── TMC4361A_Constants.h      # TMC4361A 常量定义
└── TMC4361A_TMC2660_Utils.h/.cpp  # SPI 通讯和初始化工具
```

---

## 2. 硬件通讯架构

### 2.1 通讯链路

MCU (Teensy 4.1) 与 TMC4361A 之间通过 SPI 直接通讯。TMC2660 不直接连接 MCU，而是通过 TMC4361A 的 **Cover 接口** 间接通讯。

```
Teensy 4.1                TMC4361A                 TMC2660
    │                         │                       │
    │◄──── SPI 直接通讯 ─────►│                       │
    │                         │◄── Cover 接口转发 ───►│
    │                         │                       │
```

### 2.2 引脚分配

| 轴 | CS 引脚 | 时钟源 | 说明 |
|----|--------|--------|------|
| X  | 定义在 config.h | 16 MHz (Pin 37) | 标准轴 |
| Y  | 定义在 config.h | 16 MHz (Pin 37) | 标准轴 |
| Z  | 定义在 config.h | 16 MHz (Pin 37) | 标准轴 |
| W  | 定义在 config.h | 16 MHz (Pin 37) | 滤光轮 1 |
| E1 | 定义在 config.h | 16 MHz (Pin 28) | 物镜切换器 |
| E3 | 定义在 config.h | 16 MHz (Pin 28) | 扩展 Z 轴 |
| E4 | 定义在 config.h | 16 MHz (Pin 28) | 滤光轮 2 |

### 2.3 时钟系统

系统使用 Teensy 4.1 的 PWM 输出为 TMC4361A 提供外部时钟：

```cpp
// 标准轴时钟 (Pin 37)
analogWriteFrequency(37, 16000000);  // 16 MHz
analogWrite(37, 128);                 // 50% 占空比

// 扩展轴时钟 (Pin 28)
analogWriteFrequency(28, 16000000);  // 16 MHz
analogWrite(28, 128);                 // 50% 占空比
```

---

## 3. SPI 通讯协议

### 3.1 SPI 参数

| 参数 | 值 |
|------|-----|
| 时钟频率 | 500 kHz |
| 模式 | SPI_MODE0 (CPOL=0, CPHA=0) |
| 位序 | MSB First |
| 数据宽度 | 8 位 |
| CS 极性 | 低电平有效 |

### 3.2 核心传输函数

**文件**: `TMC4361A_TMC2660_Utils.cpp`

```cpp
void tmc4361A_readWriteArray(uint8_t channel, uint8_t *data, size_t length)
{
    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
    digitalWrite(channel, LOW);        // 片选拉低
    delayMicroseconds(100);            // 等待芯片就绪

    for (size_t i = 0; i < length; i++) {
        data[i] = SPI.transfer(data[i]); // 全双工传输
    }

    digitalWrite(channel, HIGH);       // 片选拉高
    SPI.endTransaction();
}
```

### 3.3 数据帧格式

**写操作** (5 字节):

```
┌──────────────┬──────────┬──────────┬──────────┬──────────┐
│ 地址 | 0x80  │ Data[31:24] │ Data[23:16] │ Data[15:8] │ Data[7:0] │
└──────────────┴──────────┴──────────┴──────────┴──────────┘
     Byte 0        Byte 1      Byte 2       Byte 3      Byte 4
```

- Byte 0: 寄存器地址 OR 0x80 (写入标志)
- Byte 1-4: 32 位数据，大端序

**读操作** (需要两次传输):

```
第一次传输: 发送地址，返回值无效
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│  地址    │   0x00   │   0x00   │   0x00   │   0x00   │
└──────────┴──────────┴──────────┴──────────┴──────────┘

第二次传输: 发送任意地址，返回上次请求的数据
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│  地址    │ Data[31:24] │ Data[23:16] │ Data[15:8] │ Data[7:0] │
└──────────┴──────────┴──────────┴──────────┴──────────┘
```

---

## 4. TMC4361A 驱动层

### 4.1 数据结构

**文件**: `TMC4361A.h`

```cpp
// 配置结构体
typedef struct {
    uint8_t channel;                      // CS 引脚
    uint32_t shadowRegister[TMC4361A_REGISTER_COUNT]; // 影子寄存器
} ConfigurationTypeDef;

// TMC4361A 主结构体
typedef struct {
    ConfigurationTypeDef *config;
    int32_t registerResetState[TMC4361A_REGISTER_COUNT];
    uint8_t registerAccess[TMC4361A_REGISTER_COUNT];  // 访问权限

    // 运动参数缓存
    uint32_t clockFrequency;
    float screwPitch;
    uint16_t fullStepsPerRev;
    uint16_t microsteps;
} TMC4361ATypeDef;
```

### 4.2 寄存器写入

**文件**: `TMC4361A.cpp`

```cpp
void tmc4361A_writeInt(TMC4361ATypeDef *tmc4361A, uint8_t address, int32_t value)
{
    uint8_t data[5] = {
        address | TMC4361A_WRITE_BIT,     // 地址 + 写标志
        (value >> 24) & 0xFF,              // 高字节
        (value >> 16) & 0xFF,
        (value >> 8) & 0xFF,
        value & 0xFF                       // 低字节
    };

    tmc4361A_readWriteArray(tmc4361A->config->channel, data, 5);

    // 更新影子寄存器
    address = TMC_ADDRESS(address);
    tmc4361A->config->shadowRegister[address] = value;
    tmc4361A->registerAccess[address] |= TMC_ACCESS_DIRTY;
}
```

### 4.3 寄存器读取

```cpp
int32_t tmc4361A_readInt(TMC4361ATypeDef *tmc4361A, uint8_t address)
{
    uint8_t data[5] = {0};
    address = TMC_ADDRESS(address);

    // 检查是否可读，不可读则返回影子寄存器值
    if (!TMC_IS_READABLE(tmc4361A->registerAccess[address]))
        return tmc4361A->config->shadowRegister[address];

    data[0] = address;

    // 第一次传输：发送地址
    tmc4361A_readWriteArray(tmc4361A->config->channel, data, 5);

    // 第二次传输：获取数据
    tmc4361A_readWriteArray(tmc4361A->config->channel, data, 5);

    return ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 8) | data[4];
}
```

### 4.4 影子寄存器机制

影子寄存器的作用：

1. **减少 SPI 通讯**: 对于只写寄存器，读取时返回缓存值
2. **故障恢复**: 芯片复位后可快速恢复配置
3. **调试支持**: 可随时查看当前配置状态
4. **脏标记**: 追踪哪些寄存器被修改过

```cpp
// 访问权限定义
#define TMC_ACCESS_NONE        0x00  // 不可访问
#define TMC_ACCESS_READ        0x01  // 可读
#define TMC_ACCESS_WRITE       0x02  // 可写
#define TMC_ACCESS_DIRTY       0x08  // 已修改
#define TMC_ACCESS_RW          0x03  // 可读写
```

### 4.5 位操作辅助函数

```cpp
// 设置指定位
void tmc4361A_setBits(TMC4361ATypeDef *tmc4361A, uint8_t address, int32_t dat)
{
    int32_t value = tmc4361A_readInt(tmc4361A, address);
    value |= dat;
    tmc4361A_writeInt(tmc4361A, address, value);
}

// 清除指定位
void tmc4361A_rstBits(TMC4361ATypeDef *tmc4361A, uint8_t address, int32_t dat)
{
    int32_t value = tmc4361A_readInt(tmc4361A, address);
    value &= ~dat;
    tmc4361A_writeInt(tmc4361A, address, value);
}

// 读取指定位域
static inline int32_t field_read(int32_t data,
                                  RegisterField field)
{
    return (data & field.mask) >> field.shift;
}

// 写入指定位域
static inline int32_t field_write(int32_t data,
                                   RegisterField field,
                                   int32_t value)
{
    return (data & (~field.mask)) | ((value << field.shift) & field.mask);
}
```

---

## 5. TMC2660 驱动配置

### 5.1 Cover 接口原理

TMC4361A 提供 Cover 接口，将数据转发给 TMC2660：

```
MCU ──SPI──► TMC4361A ──────────────► TMC2660
              │                          │
              │  COVER_LOW_WR 寄存器     │
              │  COVER_HIGH_WR 寄存器    │
              │         │                │
              │         └──── SPI 转发 ──┘
              │                          │
              │  COVER_DRV_LOW_RD 寄存器 │
              │  COVER_DRV_HIGH_RD 寄存器│
              │         │                │
              │         └──── 响应读取 ──┘
```

### 5.2 TMC2660 寄存器配置

**文件**: `TMC4361A_TMC2660_Utils.cpp`

```cpp
void tmc4361A_tmc2660_init(TMC4361ATypeDef *tmc4361A, uint32_t clk_Hz_TMC4361)
{
    // 1. TMC4361A 复位
    tmc4361A_writeInt(tmc4361A, TMC4361A_RESET_REG, 0x52535400);

    // 2. 设置时钟频率
    tmc4361A_writeInt(tmc4361A, TMC4361A_CLK_FREQ, clk_Hz_TMC4361);

    // 3. SPI 输出配置 (用于 TMC2660 通讯)
    // 0x4440108A: 配置 SPI 输出格式、极性、数据长度等
    tmc4361A_writeInt(tmc4361A, TMC4361A_SPIOUT_CONF, 0x4440108A);

    // 4. 通过 Cover 接口配置 TMC2660
    // CHOPCONF: 斩波器配置
    tmc4361A_writeInt(tmc4361A, TMC4361A_COVER_LOW_WR, 0x000900C3);

    // SMARTEN: 智能节能配置
    tmc4361A_writeInt(tmc4361A, TMC4361A_COVER_LOW_WR, 0x000A0000);

    // SGCSCONF: StallGuard 和电流缩放配置
    tmc4361A_writeInt(tmc4361A, TMC4361A_COVER_LOW_WR, 0x000C000A);

    // DRVCONF: 驱动器配置 (SDOFF=1)
    tmc4361A_writeInt(tmc4361A, TMC4361A_COVER_LOW_WR, 0x000E00A1);

    // 5. 电流缩放初始化
    tmc4361A_cScaleInit(tmc4361A);

    // 6. 微步配置
    tmc4361A_writeMicrosteps(tmc4361A);
    tmc4361A_writeSPR(tmc4361A);
}
```

### 5.3 TMC2660 寄存器说明

| 寄存器 | 地址 | 配置值 | 说明 |
|--------|------|--------|------|
| CHOPCONF | 0x09 | 0x00C3 | 斩波器参数：TOFF=3, HSTRT=0, HEND=0 |
| SMARTEN | 0x0A | 0x0000 | 智能节能禁用 |
| SGCSCONF | 0x0C | 0x000A | StallGuard 阈值，电流缩放 |
| DRVCONF | 0x0E | 0x00A1 | SDOFF=1 (SPI 控制步进), RDSEL=01 |

### 5.4 电流配置

```cpp
void tmc4361A_tmc2660_config(TMC4361ATypeDef *tmc4361A,
                              int32_t cscale,           // 电流缩放 (0-31)
                              float holdScale,          // 保持电流比例
                              ...)
{
    // 计算电流缩放值
    // cscale = (I_motor × R_sense) / 0.2298
    // I_motor: 电机电流 (A)
    // R_sense: 采样电阻 (Ω)

    tmc4361A->cscaleMaxCurrent = cscale;
    tmc4361A->cscaleHoldCurrent = (int32_t)(cscale * holdScale);
}
```

**电流计算公式**：

```
I_motor = (cscale × 0.2298) / R_sense

其中:
- cscale: 0-31 的整数值
- 0.2298: TMC2660 内部参考电压
- R_sense: 采样电阻值
```

---

## 6. 初始化流程

### 6.1 系统启动序列

**文件**: `octoaxes.ino`

```cpp
void setup() {
    // 1. 串口初始化
    serialProtocol.begin(115200, 300);

    // 2. 等待上位机发送 ENGINE_START 命令
    serialProtocol.waitEngineStartCommand();

    // 3. 电源管理初始化
    initializePowerManagement();
    // - 检查电源就绪信号
    // - 超时时间: 5 秒

    // 4. 时钟初始化
    initializeClock();
    // - Pin 37: 16 MHz (标准轴)
    // - Pin 28: 16 MHz (扩展轴)

    // 5. SPI 和引脚初始化
    initializeSPIAndPins();
    // - SPI.begin()
    // - 所有 CS 引脚设为 HIGH

    // 6. 创建轴对象
    createAxes();
    // - new StepAxis("X", PIN_CS_X)
    // - new StepAxis("Y", PIN_CS_Y)
    // - ...

    // 7. 注册轴到管理器
    axisManager.addAxis(axisX);
    axisManager.addAxis(axisY);
    // ...

    // 8. 初始化所有轴
    axisManager.beginAll();
}
```

### 6.2 单轴初始化流程

**文件**: `axis.cpp`

```cpp
bool Axis::begin(const AxisConfig &config) {
    _config = config;

    // 1. CS 引脚配置
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);

    // 2. TMC4361A 结构体初始化
    tmc4361A_init(&_tmc4361, _csPin, &_tmc4361Config,
                  tmc4361A_defaultRegisterResetState);

    // 3. 电机参数配置
    tmc4361A_tmc2660_config(&_tmc4361,
        calculateCScale(_config.motorCurrentMA, _config.r_sense),
        _config.holdCurrent,
        1, 1, 1,  // BOW 参数
        _config.screwPitchMM,
        _config.fullStepsPerRev,
        _config.microstepping);

    // 4. TMC4361A + TMC2660 初始化
    tmc4361A_tmc2660_init(&_tmc4361, _config.clockFrequency);

    // 5. 运动参数设置
    setMotionParameters(_config.maxVelocityMM, _config.maxAccelerationMM);

    // 6. 斜坡参数初始化
    initializeRamp();

    // 7. 限位开关配置
    if (_config.enableLeftLimitSwitch) {
        tmc4361A_enableLimitSwitch(&_tmc4361,
            _config.leftSwitchPolarity,
            LEFT_SW,
            _config.leftFlipped,
            _config.leftIsInactive);
    }

    // 8. 归位限位配置
    tmc4361A_enableHomingLimit(&_tmc4361,
        _config.rightSwitchPolarity,
        _config.homingSwitch,
        mmToMicrosteps(_config.homeSafetyMarginMM));

    // 9. 禁用软限位 (归位后启用)
    enableSoftLimits(false);

    // 10. 禁用 PID (开环控制)
    tmc4361A_set_PID(&_tmc4361, PID_DISABLE);

    // 11. 失速检测配置 (可选)
    if (_config.enableStallSensitivity) {
        tmc4361A_config_init_stallGuard(&_tmc4361,
            _config.stallSensitivity,
            true, 1);
    }

    // 12. 使能轴
    enableAxis();

    return true;
}
```

### 6.3 初始化时序图

```
时间 ──────────────────────────────────────────────────────────►

串口初始化
    │
    ├─► 等待 ENGINE_START ─────────────────┐
    │                                       │ (用户触发)
    │                                       ▼
    │                              电源检测 (5s 超时)
    │                                       │
    │                                       ▼
    │                              时钟初始化 (16 MHz)
    │                                       │
    │                                       ▼
    │                              SPI 初始化
    │                                       │
    │                                       ▼
    │                              轴对象创建
    │                                       │
    ├───────────────────────────────────────┤
    │         对每个轴执行:                  │
    │         ├─► CS 引脚配置              │
    │         ├─► TMC4361A 复位            │
    │         ├─► 时钟频率设置             │
    │         ├─► SPI 输出配置             │
    │         ├─► TMC2660 寄存器配置       │
    │         ├─► 电流缩放设置             │
    │         ├─► 微步配置                 │
    │         ├─► 运动参数设置             │
    │         ├─► 限位开关配置             │
    │         └─► 轴使能                   │
    └───────────────────────────────────────┘
                    │
                    ▼
            进入主循环 (loop)
```

---

## 7. 轴类继承体系

### 7.1 类图

```
                    ┌─────────────────────────────┐
                    │         Axis (基类)          │
                    ├─────────────────────────────┤
                    │ - _tmc4361: TMC4361ATypeDef │
                    │ - _currentState: AxisState  │
                    │ - _config: AxisConfig       │
                    │ - _csPin: uint8_t           │
                    ├─────────────────────────────┤
                    │ + begin()                   │
                    │ + update()                  │
                    │ + processCommand()          │
                    │ + moveToPosition()          │
                    │ + moveRelative()            │
                    │ + startHoming()             │
                    │ + stop()                    │
                    │ # mmToMicrosteps()          │
                    │ # microstepsToMM()          │
                    └──────────────┬──────────────┘
                                   │
          ┌────────────────────────┼────────────────────────┐
          │                        │                        │
          ▼                        ▼                        ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│    StepAxis     │    │   FilterWheel   │    │   Objectives    │
├─────────────────┤    ├─────────────────┤    ├─────────────────┤
│ - 反向间隙补偿  │    │ - 滤光片位置    │    │ - 物镜位置      │
│ - 线性运动      │    │ - 旋转控制      │    │ - 快速切换      │
├─────────────────┤    ├─────────────────┤    ├─────────────────┤
│ + X, Y, Z, E3轴 │    │ + W, E4 轴      │    │ + E1 轴         │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### 7.2 Axis 基类职责

**文件**: `axis.h`, `axis.cpp`

| 职责 | 方法 | 说明 |
|------|------|------|
| 状态管理 | `update()` | 状态机驱动 |
| 位置控制 | `moveToPosition()`, `moveRelative()` | 绝对/相对移动 |
| 归位 | `startHoming()` | 归位序列 |
| 单位转换 | `mmToMicrosteps()`, `microstepsToMM()` | 物理单位转换 |
| 限位控制 | `enableSoftLimits()` | 软限位管理 |
| 命令处理 | `processCommand()` | 串口命令解析 |

### 7.3 StepAxis 扩展

**文件**: `stepaxis.h`, `stepaxis.cpp`

```cpp
class StepAxis : public Axis {
private:
    int32_t _backlashCompensation;  // 反向间隙补偿值
    int8_t _lastDirection;           // 上次运动方向

public:
    // 重写移动方法，加入反向间隙补偿
    bool moveToPosition(float positionMM) override;
    bool moveRelative(float distanceMM) override;

    // 反向间隙配置
    void setBacklashCompensation(int32_t microsteps);
};
```

### 7.4 FilterWheel 特化

**文件**: `filterwheel.h`, `filterwheel.cpp`

```cpp
class FilterWheel : public Axis {
private:
    uint8_t _currentPosition;    // 当前滤光片位置 (0-N)
    uint8_t _positionCount;      // 滤光片数量

public:
    // 滤光片位置控制
    bool moveToFilter(uint8_t filterIndex);
    uint8_t getCurrentFilter() const;
};
```

### 7.5 Objectives 特化

**文件**: `objectives.h`, `objectives.cpp`

```cpp
class Objectives : public Axis {
private:
    uint8_t _currentObjective;   // 当前物镜位置
    uint8_t _objectiveCount;     // 物镜数量

public:
    // 物镜切换
    bool selectObjective(uint8_t index);
    uint8_t getCurrentObjective() const;
};
```

---

## 8. 状态机设计

### 8.1 状态定义

**文件**: `axis.h`

```cpp
enum AxisState {
    STATE_IDLE,              // 空闲，可接受新命令
    STATE_MOVING,            // 正在移动
    STATE_HOMING_INIT,       // 归位初始化
    STATE_HOMING_SEARCH,     // 归位搜索限位开关
    STATE_HOMING_SET_ZERO,   // 设置零点
    STATE_LEAVING_HOME,      // 离开归位点
    STATE_ERROR              // 错误状态
};
```

### 8.2 状态转移图

```
                          ┌─────────────────┐
                          │                 │
            ┌─────────────►    STATE_IDLE   ◄─────────────┐
            │             │                 │             │
            │             └────────┬────────┘             │
            │                      │                      │
            │         ┌────────────┼────────────┐         │
            │         │            │            │         │
            │         ▼            ▼            ▼         │
            │    ┌─────────┐ ┌──────────┐ ┌─────────┐    │
            │    │ MOVING  │ │HOMING_   │ │  STOP   │    │
            │    │         │ │  INIT    │ │ Command │    │
            │    └────┬────┘ └────┬─────┘ └────┬────┘    │
            │         │           │            │         │
            │         │           ▼            │         │
            │         │    ┌──────────┐        │         │
            │         │    │HOMING_   │        │         │
            │         │    │ SEARCH   │        │         │
            │         │    └────┬─────┘        │         │
            │         │         │              │         │
            │         │         ▼              │         │
            │         │    ┌──────────┐        │         │
            │         │    │HOMING_   │        │         │
            │         │    │SET_ZERO  │        │         │
            │         │    └────┬─────┘        │         │
            │         │         │              │         │
            │         │         ▼              │         │
            │         │    ┌──────────┐        │         │
            │         │    │LEAVING_  │        │         │
            │         │    │  HOME    │        │         │
            │         │    └────┬─────┘        │         │
            │         │         │              │         │
            └─────────┴─────────┴──────────────┘         │
                                                         │
                      ┌──────────────────────────────────┘
                      │
                      ▼
               ┌─────────────┐
               │ STATE_ERROR │
               └─────────────┘
```

### 8.3 状态处理逻辑

**文件**: `axis.cpp`

```cpp
void Axis::update() {
    switch (_currentState) {
        case STATE_IDLE:
            // 空闲状态，等待命令
            break;

        case STATE_MOVING:
            // 检查运动是否完成
            if (isMovementComplete()) {
                _currentState = STATE_IDLE;
                notifyMovementComplete();
            }
            // 检查限位触发
            if (isLimitTriggered()) {
                stop();
                _currentState = STATE_ERROR;
            }
            break;

        case STATE_HOMING_INIT:
            // 设置归位速度和方向
            startHomingSearch();
            _currentState = STATE_HOMING_SEARCH;
            break;

        case STATE_HOMING_SEARCH:
            // 等待触碰限位开关
            if (isHomeSwitchTriggered()) {
                stopMotion();
                _currentState = STATE_HOMING_SET_ZERO;
            }
            break;

        case STATE_HOMING_SET_ZERO:
            // 设置当前位置为零点
            setCurrentPositionAsHome();
            _currentState = STATE_LEAVING_HOME;
            startLeavingHome();
            break;

        case STATE_LEAVING_HOME:
            // 移动到安全位置
            if (isMovementComplete()) {
                enableSoftLimits(true);
                _currentState = STATE_IDLE;
                notifyHomingComplete();
            }
            break;

        case STATE_ERROR:
            // 错误状态，等待复位
            break;
    }
}
```

### 8.4 运动完成检测

```cpp
bool Axis::isMovementComplete() {
    // 读取状态寄存器
    int32_t status = tmc4361A_readInt(&_tmc4361, TMC4361A_STATUS);

    // 检查位置到达标志 (POS_COMP_REACHED)
    return (status & TMC4361A_POS_COMP_REACHED_MASK) != 0;
}
```

---

## 9. 命令处理流程

### 9.1 命令格式

```
格式: "<轴名>:<命令> [参数类型] [参数值]"

示例:
- "X:MOVE_AXIS HEX 1F40"      // X 轴相对移动 0x1F40 微步
- "Z:MOVETO_AXIS DEC 5000"    // Z 轴移动到位置 5000 微步
- "Y:HOME"                     // Y 轴归位
- "W:STOP"                     // W 轴停止
- "ALL:GET_DATA"               // 获取所有轴状态
```

### 9.2 命令处理链路

```
串口接收
    │
    ▼
SerialProtocol::processSerialCommands()
    │
    ├─► 解析完整命令字符串
    │
    ▼
AxisManager::processCommand(commandString)
    │
    ├─► 提取轴名称 (冒号前部分)
    ├─► 提取命令内容 (冒号后部分)
    │
    ▼
AxisManager::findAxisByName(axisName)
    │
    ├─► 遍历已注册轴
    ├─► 返回匹配的 Axis 指针
    │
    ▼
Axis::processCommand(commandContent)
    │
    ├─► 解析命令类型
    ├─► 解析参数类型 (HEX/DEC)
    ├─► 解析参数值
    │
    ▼
执行具体操作
    │
    ├─► moveRelative() / moveToPosition()
    ├─► startHoming()
    ├─► stop()
    └─► reportStatus()
```

### 9.3 AxisManager 命令路由

**文件**: `axesmrg.cpp`

```cpp
bool AxisManager::processCommand(const String &command) {
    // 1. 查找冒号分隔符
    int colonIndex = command.indexOf(':');
    if (colonIndex < 0) return false;

    // 2. 提取轴名称和命令
    String axisName = command.substring(0, colonIndex);
    String axisCommand = command.substring(colonIndex + 1);

    // 3. 处理广播命令
    if (axisName == "ALL") {
        return processBroadcastCommand(axisCommand);
    }

    // 4. 查找目标轴
    Axis *axis = findAxisByName(axisName);
    if (axis == nullptr) return false;

    // 5. 转发命令到目标轴
    return axis->processCommand(axisCommand);
}

Axis* AxisManager::findAxisByName(const String &name) {
    for (uint8_t i = 0; i < _axisCount; i++) {
        if (_axes[i]->getName() == name) {
            return _axes[i];
        }
    }
    return nullptr;
}
```

### 9.4 Axis 命令解析

**文件**: `axis.cpp`

```cpp
bool Axis::processCommand(const String &command) {
    // 解析命令类型
    if (command.startsWith("MOVE_AXIS")) {
        return processMoveCommand(command, false);  // 相对移动
    }
    else if (command.startsWith("MOVETO_AXIS")) {
        return processMoveCommand(command, true);   // 绝对移动
    }
    else if (command.startsWith("HOME")) {
        return startHoming();
    }
    else if (command.startsWith("STOP")) {
        return stop();
    }
    else if (command.startsWith("GET_DATA")) {
        reportStatus();
        return true;
    }
    // ... 其他命令

    return false;
}

bool Axis::processMoveCommand(const String &command, bool absolute) {
    // 解析参数类型和值
    // "MOVE_AXIS HEX 1F40" -> type="HEX", value=0x1F40

    int32_t microsteps;
    if (parseHexOrDec(command, &microsteps)) {
        if (absolute) {
            return moveToPosition(microstepsToMM(microsteps));
        } else {
            return moveRelative(microstepsToMM(microsteps));
        }
    }
    return false;
}
```

---

## 10. 单位转换系统

### 10.1 转换公式

**微步 ↔ 毫米**:

```
microsteps = mm × (fullStepsPerRev × microstepping) / screwPitchMM

mm = microsteps × screwPitchMM / (fullStepsPerRev × microstepping)
```

**速度转换** (mm/s ↔ microsteps/s):

```
velocity_usteps = velocity_mm × (fullStepsPerRev × microstepping) / screwPitchMM
```

**加速度转换** (mm/s² ↔ microsteps/s²):

```
accel_usteps = accel_mm × (fullStepsPerRev × microstepping) / screwPitchMM
```

### 10.2 转换函数实现

**文件**: `axis.cpp`

```cpp
int32_t Axis::mmToMicrosteps(float mm) {
    float stepsPerMM = (_config.fullStepsPerRev * _config.microstepping)
                       / _config.screwPitchMM;
    return (int32_t)(mm * stepsPerMM);
}

float Axis::microstepsToMM(int32_t microsteps) {
    float stepsPerMM = (_config.fullStepsPerRev * _config.microstepping)
                       / _config.screwPitchMM;
    return (float)microsteps / stepsPerMM;
}

int32_t Axis::velocityMMToMicrosteps(float velocityMM) {
    // TMC4361A 速度单位: microsteps / (2^24 / fCLK)
    // 需要额外的时钟频率换算
    float stepsPerMM = (_config.fullStepsPerRev * _config.microstepping)
                       / _config.screwPitchMM;
    float velocityUsteps = velocityMM * stepsPerMM;

    // 转换为 TMC4361A 内部单位
    return (int32_t)(velocityUsteps * (1 << 24) / _config.clockFrequency);
}
```

### 10.3 参数示例

以 Z 轴为例：

| 参数 | 值 |
|------|-----|
| screwPitchMM | 0.3 mm |
| fullStepsPerRev | 200 |
| microstepping | 256 |

**计算**:

```
stepsPerMM = (200 × 256) / 0.3 = 170,666.67 steps/mm

移动 1mm:
microsteps = 1 × 170,666.67 ≈ 170,667 微步

速度 3 mm/s:
velocity_usteps = 3 × 170,666.67 = 512,000 微步/秒
```

---

## 11. 关键寄存器参考

### 11.1 TMC4361A 常用寄存器

| 寄存器 | 地址 | 类型 | 说明 |
|--------|------|------|------|
| GENERAL_CONF | 0x00 | R/W | 通用配置 |
| REFERENCE_CONF | 0x01 | R/W | 参考/限位配置 |
| START_CONF | 0x02 | R/W | 启动配置 |
| SPIOUT_CONF | 0x04 | R/W | SPI 输出配置 |
| CURRENT_CONF | 0x05 | R/W | 电流配置 |
| SCALE_VALUES | 0x06 | R/W | 缩放值 |
| RAMPMODE | 0x20 | R/W | 斜坡模式 |
| XACTUAL | 0x21 | R/W | 当前位置 |
| VACTUAL | 0x22 | R | 当前速度 |
| AACTUAL | 0x23 | R | 当前加速度 |
| VMAX | 0x24 | R/W | 最大速度 |
| AMAX | 0x28 | R/W | 最大加速度 |
| DMAX | 0x29 | R/W | 最大减速度 |
| BOW1 | 0x2D | R/W | 弯曲参数 1 |
| BOW2 | 0x2E | R/W | 弯曲参数 2 |
| BOW3 | 0x2F | R/W | 弯曲参数 3 |
| BOW4 | 0x30 | R/W | 弯曲参数 4 |
| XTARGET | 0x37 | R/W | 目标位置 |
| X_HOME | 0x39 | R/W | 归位位置 |
| COVER_LOW_WR | 0x6C | W | TMC2660 命令 (低 32 位) |
| COVER_HIGH_WR | 0x6D | W | TMC2660 命令 (高 32 位) |
| COVER_DRV_LOW_RD | 0x6E | R | TMC2660 响应 (低 32 位) |
| STATUS | 0x0F | R | 状态寄存器 |
| EVENTS | 0x0E | R/W | 事件寄存器 |

### 11.2 STATUS 寄存器位定义

| 位 | 名称 | 说明 |
|----|------|------|
| 0 | TARGET_REACHED | 目标位置到达 |
| 1 | POS_COMP_REACHED | 位置比较到达 |
| 2 | VEL_REACHED | 目标速度到达 |
| 3 | VEL_STATE_00 | 速度状态位 0 |
| 4 | VEL_STATE_01 | 速度状态位 1 |
| 5 | RAMP_STATE_00 | 斜坡状态位 0 |
| 6 | RAMP_STATE_01 | 斜坡状态位 1 |
| 7 | STOPL | 左限位触发 |
| 8 | STOPR | 右限位触发 |
| 9 | HOME_ERROR | 归位错误 |
| 10 | STALLGUARD | 失速检测 |

### 11.3 SPIOUT_CONF 配置

```
默认值: 0x4440108A

位域分解:
- [3:0]   = 0xA: SPI 输出数据长度 (10+1=11 位，适配 TMC2660)
- [7:4]   = 0x8: 无操作
- [11:8]  = 0x0: SPI 输出事件选择
- [15:12] = 0x1: SPI 输出时钟分频
- [19:16] = 0x0: 无操作
- [23:20] = 0x4: SPI 数据极性
- [27:24] = 0x4: SPI 时钟极性
- [31:28] = 0x4: 杂项配置
```

---

## 12. 配置参数表

### 12.1 轴参数配置

**文件**: `config.h`

| 轴 | R_sense (Ω) | 丝距 (mm) | 步数/圈 | 微步 | 最大速度 (mm/s) | 最大加速度 (mm/s²) | 电机电流 (mA) | 保持电流比 |
|----|------------|----------|---------|------|-----------------|-------------------|--------------|-----------|
| X | 0.22 | 2.54 | 200 | 256 | 25 | 500 | 1000 | 0.25 |
| Y | 0.22 | 2.54 | 200 | 256 | 25 | 500 | 1000 | 0.25 |
| Z | 0.43 | 0.3 | 200 | 256 | 3 | 20 | 500 | 0.5 |
| W | 0.105 | 100 | 200 | 64 | 319 | 30000 | 1900 | 0.5 |
| E1 | 0.22 | 1 | 200 | 256 | 0.5 | 200 | 1000 | 0.5 |
| E3 | 0.43 | 0.3 | 200 | 256 | 3 | 20 | 500 | 0.5 |
| E4 | 0.105 | 100 | 200 | 64 | 319 | 30000 | 1900 | 0.5 |

### 12.2 限位开关配置

| 轴 | 左限位 | 右限位 | 归位方向 | 归位限位 |
|----|--------|--------|---------|---------|
| X | 启用 | 启用 | 负向 | 左 |
| Y | 启用 | 启用 | 负向 | 左 |
| Z | 启用 | 启用 | 负向 | 左 |

### 12.3 时钟和通讯配置

| 参数 | 值 |
|------|-----|
| TMC4361A 时钟 | 16 MHz |
| SPI 速率 | 500 kHz |
| 串口波特率 | 115200 |

---

## 13. 重构建议

### 13.1 架构优化

1. **抽象 SPI 通讯层**
   - 创建 `SPIDevice` 基类
   - 支持不同 SPI 总线和配置
   - 便于单元测试和模拟

2. **引入依赖注入**
   - `Axis` 类不应直接依赖全局 SPI 对象
   - 通过构造函数注入 `SPIDevice` 接口

3. **配置外部化**
   - 将 `config.h` 中的参数移至 JSON/YAML 文件
   - 支持运行时配置修改

### 13.2 代码质量

1. **减少全局状态**
   - 消除全局变量
   - 使用单例模式替代 (如需要)

2. **错误处理增强**
   - 定义错误码枚举
   - 添加错误回调机制
   - 实现错误恢复策略

3. **日志系统**
   - 实现分级日志 (DEBUG, INFO, WARN, ERROR)
   - 支持日志过滤和输出重定向

### 13.3 功能扩展

1. **运动规划改进**
   - 支持多轴同步运动
   - 实现 S 型加减速曲线
   - 添加运动队列

2. **状态监控**
   - 实时温度监控
   - 电机负载检测
   - 运动轨迹记录

3. **通讯协议升级**
   - 实现二进制协议 (提高效率)
   - 添加校验和/CRC
   - 支持批量命令

### 13.4 测试策略

1. **单元测试**
   - 模拟 TMC4361A 寄存器
   - 测试单位转换函数
   - 测试状态机转移

2. **集成测试**
   - 端到端命令测试
   - 多轴协调测试

3. **硬件在环测试**
   - 实际运动精度验证
   - 限位开关响应测试

---

## 附录 A: 缩写对照表

| 缩写 | 全称 | 说明 |
|------|------|------|
| CS | Chip Select | 片选信号 |
| SPI | Serial Peripheral Interface | 串行外设接口 |
| MCU | Microcontroller Unit | 微控制器 |
| PWM | Pulse Width Modulation | 脉宽调制 |
| PID | Proportional-Integral-Derivative | 比例积分微分控制 |
| MSB | Most Significant Bit | 最高有效位 |
| LSB | Least Significant Bit | 最低有效位 |

---

## 附录 B: 参考文档

1. TMC4361A 数据手册 - Trinamic
2. TMC2660 数据手册 - Trinamic
3. Teensy 4.1 引脚定义 - `documents/斯坦福TeenSy4.1引脚定义.docx`
4. Arduino SPI 库文档
