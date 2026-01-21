# Octoaxes 固件重构计划

> 基于 TMC-API 官方设计模式的重构方案
>
> 创建日期：2026-01-21

---

## 目录

1. [重构目标](#1-重构目标)
2. [当前架构 vs 官方 API 对比](#2-当前架构-vs-官方-api-对比)
3. [重构方案](#3-重构方案)
4. [实施步骤](#4-实施步骤)
5. [新架构设计](#5-新架构设计)
6. [代码模板](#6-代码模板)
7. [迁移指南](#7-迁移指南)
8. [测试计划](#8-测试计划)

---

## 1. 重构目标

### 1.1 主要目标

1. **对齐官方 API 设计模式** - 便于后续升级和维护
2. **分离 TMC2660 驱动层** - 独立封装，不依赖 Cover 接口
3. **简化实例管理** - 使用 icID 索引替代指针传递
4. **增强可测试性** - 回调函数分离，便于模拟测试
5. **提升代码可读性** - 统一命名规范，完善注释

### 1.2 非目标

- 不改变上层 Axis 类的公共接口
- 不改变串口通信协议
- 不改变硬件配置参数

---

## 2. 当前架构 vs 官方 API 对比

### 2.1 实例标识方式

| 方面 | 当前实现 | 官方 API | 差异分析 |
|------|---------|---------|---------|
| 标识符 | `TMC4361ATypeDef*` 指针 | `uint16_t icID` 索引 | 官方更简洁，便于数组管理 |
| 多实例 | 每个轴独立分配结构体 | 全局缓存数组 + icID | 官方内存更紧凑 |
| SPI 通道 | 结构体内 `config->channel` | 回调函数内根据 icID 选择 | 官方解耦更好 |

**当前代码示例：**
```cpp
// 当前：传递指针
void tmc4361A_writeInt(TMC4361ATypeDef *tmc4361A, uint8_t address, int32_t value);
tmc4361A_writeInt(&_tmc4361, TMC4361A_XTARGET, position);
```

**官方代码示例：**
```cpp
// 官方：使用 icID
void tmc4361A_writeRegister(uint16_t icID, uint8_t address, int32_t value);
tmc4361A_writeRegister(0, TMC4361A_XTARGET, position);
```

### 2.2 SPI 回调机制

| 方面 | 当前实现 | 官方 API |
|------|---------|---------|
| 函数签名 | `tmc4361A_readWriteArray(channel, data, len)` | `tmc4361A_readWriteSPI(icID, data, len)` |
| CS 控制 | 在回调函数内部处理 | 在回调函数内部处理 |
| 状态解析 | 无专用回调 | `tmc4361A_setStatus(icID, data)` |

**建议：** 添加状态解析回调，便于监控 SPI 通信状态。

### 2.3 缓存机制

| 方面 | 当前实现 | 官方 API |
|------|---------|---------|
| 缓存位置 | `ConfigurationTypeDef->shadowRegister[]` | 全局 `tmc4361A_cache[]` 数组 |
| 脏位管理 | `registerAccess[] |= DIRTY` | `tmc4361A_setDirtyBit(icID, index)` |
| 初始化 | `tmc4361A_fillShadowRegisters()` | `tmc4361A_initCache()` |
| IC 数量 | 动态分配 | 编译时宏定义 `TMC4361A_IC_CACHE_COUNT` |

**建议：** 采用官方的静态缓存数组，编译时确定 IC 数量。

### 2.4 字段级操作

| 方面 | 当前实现 | 官方 API |
|------|---------|---------|
| 类型 | 宏定义 `TMC4361A_FIELD_READ/WRITE` | 函数 `tmc4361A_fieldRead/Write` |
| 参数 | `(tdef, address, mask, shift)` | `(icID, RegisterField)` |
| RegisterField | 无 | `{mask, shift, address, isSigned}` |

**当前代码示例：**
```cpp
TMC4361A_FIELD_READ(&_tmc4361, TMC4361A_STATUS, TMC4361A_TARGET_REACHED_MASK, TMC4361A_TARGET_REACHED_SHIFT)
```

**官方代码示例：**
```cpp
tmc4361A_fieldRead(0, TMC4361A_TARGET_REACHED_FIELD)
```

**建议：** 定义 `RegisterField` 结构体，简化字段操作。

### 2.5 TMC2660 驱动

| 方面 | 当前实现 | 官方 API |
|------|---------|---------|
| 通信方式 | 仅通过 TMC4361A Cover 接口 | 独立 SPI 或 Cover 接口 |
| API 封装 | 无独立封装 | 完整的 `tmc2660_*` API |
| 状态读取 | 手动解析 Cover 响应 | `tmc2660_getStatusBits()` |
| 字段操作 | 无 | `tmc2660_fieldRead/Write()` |

**建议：** 添加独立的 TMC2660 驱动层，即使通过 Cover 接口通信。

### 2.6 高层封装

| 方面 | 当前实现 | 官方示例 |
|------|---------|---------|
| 初始化 | `tmc4361A_tmc2660_init()` | `tmc4361A_initMotor()` + `tmc2660_initDriver()` |
| 位置移动 | `tmc4361A_moveTo()` | `tmc4361A_moveToPosition()` |
| 状态检查 | `tmc4361A_isRunning()` | `tmc4361A_isPositionReached()` |
| S 型斜坡 | `tmc4361A_sRampInit()` | `tmc4361A_setSShapedRamp()` |

---

## 3. 重构方案

### 3.1 架构层次

```
新架构层次：

┌─────────────────────────────────────────────────────────────┐
│                    应用层 (Application)                      │
│  Axis, StepAxis, FilterWheel, Objectives, AxisManager       │
├─────────────────────────────────────────────────────────────┤
│                    运动控制层 (Motion Control)               │
│  TMC4361A_Motion.h/.cpp  (高层运动封装)                      │
├─────────────────────────────────────────────────────────────┤
│                    驱动层 (Driver)                           │
│  TMC4361A.h/.cpp         TMC2660.h/.cpp                     │
│  (寄存器级操作)           (寄存器级操作)                      │
├─────────────────────────────────────────────────────────────┤
│                    硬件抽象层 (HAL)                          │
│  TMC_SPI.h/.cpp (SPI 回调实现)                              │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 文件结构

```
firmware/octoaxes/
├── tmc/                              # TMC 驱动目录
│   ├── hal/                          # 硬件抽象层
│   │   ├── TMC_SPI.h                 # SPI 接口定义
│   │   └── TMC_SPI.cpp               # Teensy SPI 实现
│   │
│   ├── ic/                           # IC 驱动
│   │   ├── TMC4361A/
│   │   │   ├── TMC4361A.h            # API 头文件
│   │   │   ├── TMC4361A.cpp          # 寄存器级操作
│   │   │   ├── TMC4361A_HW_Abstraction.h  # 寄存器定义
│   │   │   └── TMC4361A_Fields.h     # 字段定义
│   │   │
│   │   └── TMC2660/
│   │       ├── TMC2660.h             # API 头文件
│   │       ├── TMC2660.cpp           # 寄存器级操作
│   │       └── TMC2660_HW_Abstraction.h  # 寄存器定义
│   │
│   └── motion/                       # 运动控制层
│       ├── TMC4361A_Motion.h         # 高层运动 API
│       └── TMC4361A_Motion.cpp
│
├── axis/                             # 轴控制
│   ├── Axis.h
│   ├── Axis.cpp
│   ├── StepAxis.h
│   ├── StepAxis.cpp
│   ├── FilterWheel.h
│   ├── FilterWheel.cpp
│   ├── Objectives.h
│   └── Objectives.cpp
│
├── config.h                          # 系统配置
└── octoaxes.ino                      # 主程序
```

### 3.3 核心改动

#### 3.3.1 引入 icID 机制

```cpp
// 新增：IC 配置
#define TMC4361A_IC_COUNT  7  // X, Y, Z, W, E1, E3, E4
#define TMC2660_IC_COUNT   7

// 新增：IC 到 CS 引脚映射
typedef struct {
    uint8_t csPin;
    uint8_t clockSource;  // 0 = 标准, 1 = 扩展
} TMC_IC_Config;

extern const TMC_IC_Config tmc4361a_config[TMC4361A_IC_COUNT];
extern const TMC_IC_Config tmc2660_config[TMC2660_IC_COUNT];
```

#### 3.3.2 SPI 回调实现

```cpp
// TMC_SPI.cpp
void tmc4361A_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength)
{
    uint8_t csPin = tmc4361a_config[icID].csPin;

    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
    digitalWrite(csPin, LOW);
    delayMicroseconds(100);

    for (size_t i = 0; i < dataLength; i++) {
        data[i] = SPI.transfer(data[i]);
    }

    digitalWrite(csPin, HIGH);
    SPI.endTransaction();
}

void tmc4361A_setStatus(uint16_t icID, uint8_t *data)
{
    // 可选：解析状态字节用于监控
    uint8_t status = data[0];
    // 存储或处理状态...
}
```

#### 3.3.3 独立 TMC2660 驱动

```cpp
// TMC2660.h
void tmc2660_initDriver(uint8_t icID);
void tmc2660_setRunCurrent(uint8_t icID, uint8_t current);
void tmc2660_setMicrostepResolution(uint8_t icID, uint8_t mres);
uint8_t tmc2660_isStalled(uint8_t icID);
uint8_t tmc2660_isOvertemperature(uint8_t icID);

// TMC2660.cpp - 通过 TMC4361A Cover 接口实现
void tmc2660_writeRegister(uint8_t icID, uint8_t address, uint32_t value)
{
    // 使用 TMC4361A 的 Cover 寄存器转发
    tmc4361A_writeRegister(icID, TMC4361A_COVER_LOW_WR, value);
}
```

---

## 4. 实施步骤

### 阶段 1：基础设施 (预计 2-3 天)

- [ ] **1.1** 创建 `tmc/hal/` 目录，实现 `TMC_SPI.h/.cpp`
- [ ] **1.2** 定义 IC 配置数组 (`tmc4361a_config[]`, `tmc2660_config[]`)
- [ ] **1.3** 实现 `tmc4361A_readWriteSPI()` 和 `tmc4361A_setStatus()` 回调

### 阶段 2：TMC4361A 驱动重构 (预计 3-4 天)

- [ ] **2.1** 将 `TMC4361A.h/.cpp` 迁移到 `tmc/ic/TMC4361A/`
- [ ] **2.2** 重构 API 使用 `icID` 替代指针
- [ ] **2.3** 实现全局缓存数组 `tmc4361A_cache[]`
- [ ] **2.4** 定义 `RegisterField` 结构体和字段操作函数
- [ ] **2.5** 保留旧 API 作为兼容层 (标记为 deprecated)

### 阶段 3：TMC2660 驱动分离 (预计 2-3 天)

- [ ] **3.1** 创建 `tmc/ic/TMC2660/` 目录
- [ ] **3.2** 从 `TMC4361A_TMC2660_Utils.cpp` 提取 TMC2660 相关代码
- [ ] **3.3** 实现独立的 `tmc2660_*` API
- [ ] **3.4** 保持通过 Cover 接口通信的实现

### 阶段 4：运动控制层 (预计 2 天)

- [ ] **4.1** 创建 `tmc/motion/TMC4361A_Motion.h/.cpp`
- [ ] **4.2** 实现高层封装函数 (`initMotor`, `moveToPosition` 等)
- [ ] **4.3** 迁移单位转换函数

### 阶段 5：Axis 类适配 (预计 3-4 天)

- [ ] **5.1** 更新 `Axis` 基类使用新 API
- [ ] **5.2** 添加 `icID` 成员变量替代 `_tmc4361` 指针
- [ ] **5.3** 更新子类 (`StepAxis`, `FilterWheel`, `Objectives`)
- [ ] **5.4** 更新 `AxisManager` 初始化流程

### 阶段 6：测试和清理 (预计 2-3 天)

- [ ] **6.1** 编写单元测试
- [ ] **6.2** 集成测试
- [ ] **6.3** 移除废弃代码
- [ ] **6.4** 更新文档

---

## 5. 新架构设计

### 5.1 TMC4361A API 设计

```cpp
// tmc/ic/TMC4361A/TMC4361A.h

#ifndef TMC4361A_H_
#define TMC4361A_H_

#include <stdint.h>
#include <stdbool.h>

// 配置宏
#ifndef TMC4361A_IC_CACHE_COUNT
#define TMC4361A_IC_CACHE_COUNT 7
#endif

// 寄存器字段定义
typedef struct {
    uint32_t mask;
    uint8_t  shift;
    uint8_t  address;
    bool     isSigned;
} TMC4361A_RegisterField;

// 缓存操作类型
typedef enum {
    TMC4361A_CACHE_READ,
    TMC4361A_CACHE_WRITE,
    TMC4361A_CACHE_FILL_DEFAULT
} TMC4361A_CacheOp;

// ============ 需要用户实现的回调 ============
extern void tmc4361A_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength);
extern void tmc4361A_setStatus(uint16_t icID, uint8_t *data);

// ============ 核心 API ============

// 初始化
void tmc4361A_initCache(void);

// 寄存器级操作
int32_t tmc4361A_readRegister(uint16_t icID, uint8_t address);
void tmc4361A_writeRegister(uint16_t icID, uint8_t address, int32_t value);

// 字段级操作
uint32_t tmc4361A_fieldExtract(uint32_t data, TMC4361A_RegisterField field);
uint32_t tmc4361A_fieldRead(uint16_t icID, TMC4361A_RegisterField field);
void tmc4361A_fieldWrite(uint16_t icID, TMC4361A_RegisterField field, uint32_t value);

// 缓存管理
bool tmc4361A_cache(uint16_t icID, TMC4361A_CacheOp operation,
                     uint8_t address, uint32_t *value);
void tmc4361A_setDirtyBit(uint16_t icID, uint8_t index, bool value);
bool tmc4361A_getDirtyBit(uint16_t icID, uint8_t index);

#endif /* TMC4361A_H_ */
```

### 5.2 TMC2660 API 设计

```cpp
// tmc/ic/TMC2660/TMC2660.h

#ifndef TMC2660_H_
#define TMC2660_H_

#include <stdint.h>
#include <stdbool.h>

// 配置宏
#ifndef TMC2660_IC_CACHE_COUNT
#define TMC2660_IC_CACHE_COUNT 7
#endif

// 通信模式
typedef enum {
    TMC2660_COMM_COVER,      // 通过 TMC4361A Cover 接口
    TMC2660_COMM_DIRECT_SPI  // 直接 SPI (预留)
} TMC2660_CommMode;

// ============ 需要用户实现的回调 (直接 SPI 模式) ============
extern void tmc2660_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength);

// ============ 核心 API ============

// 初始化
void tmc2660_initDriver(uint8_t icID);
void tmc2660_setCommMode(uint8_t icID, TMC2660_CommMode mode, uint8_t tmc4361a_icID);

// 寄存器操作
uint32_t tmc2660_readRegister(uint8_t icID, uint8_t address);
void tmc2660_writeRegister(uint8_t icID, uint8_t address, uint32_t value);

// 配置函数
void tmc2660_setRunCurrent(uint8_t icID, uint8_t current);
void tmc2660_setMicrostepResolution(uint8_t icID, uint8_t mres);
void tmc2660_setInterpolation(uint8_t icID, uint8_t enable);
void tmc2660_setChopperConfig(uint8_t icID, uint8_t toff, uint8_t hstrt,
                               int8_t hend, uint8_t tbl);
void tmc2660_enableDriver(uint8_t icID, uint8_t enable);

// 状态检查
uint8_t tmc2660_getStatusBits(uint8_t icID);
uint8_t tmc2660_isStalled(uint8_t icID);
uint8_t tmc2660_isOvertemperature(uint8_t icID);
uint8_t tmc2660_isOvertemperatureWarning(uint8_t icID);
uint8_t tmc2660_isShortToGroundA(uint8_t icID);
uint8_t tmc2660_isShortToGroundB(uint8_t icID);

// 诊断
uint16_t tmc2660_getStallGuardValue(uint8_t icID);
uint16_t tmc2660_getMicrostepPosition(uint8_t icID);

#endif /* TMC2660_H_ */
```

### 5.3 运动控制 API 设计

```cpp
// tmc/motion/TMC4361A_Motion.h

#ifndef TMC4361A_MOTION_H_
#define TMC4361A_MOTION_H_

#include "TMC4361A.h"
#include "TMC2660.h"

// 运动参数配置
typedef struct {
    uint32_t clockFrequency;    // 外部时钟频率 (Hz)
    float    screwPitchMM;      // 丝杆螺距 (mm)
    uint16_t fullStepsPerRev;   // 每转步数
    uint16_t microsteps;        // 微步数
    uint32_t vmax;              // 最大速度
    uint32_t amax;              // 最大加速度
    uint32_t dmax;              // 最大减速度
} TMC4361A_MotionConfig;

// 电机配置
typedef struct {
    uint8_t  currentScale;      // 电流缩放 (0-31)
    float    holdCurrentRatio;  // 保持电流比例 (0-1)
    uint8_t  mres;              // 微步分辨率
    uint8_t  interpolation;     // 插值使能
} TMC2660_MotorConfig;

// ============ 初始化 ============
void motor_init(uint8_t icID, const TMC4361A_MotionConfig *motionCfg,
                const TMC2660_MotorConfig *motorCfg);

// ============ 运动控制 ============
void motor_moveToPosition(uint8_t icID, int32_t position);
void motor_moveByDistance(uint8_t icID, int32_t distance);
void motor_rotateVelocity(uint8_t icID, int32_t velocity);
void motor_stop(uint8_t icID);

// ============ 状态查询 ============
uint8_t motor_isPositionReached(uint8_t icID);
uint8_t motor_isRunning(uint8_t icID);
int32_t motor_getActualPosition(uint8_t icID);
int32_t motor_getActualVelocity(uint8_t icID);

// ============ 参数设置 ============
void motor_setVmax(uint8_t icID, uint32_t vmax);
void motor_setAmax(uint8_t icID, uint32_t amax);
void motor_setDmax(uint8_t icID, uint32_t dmax);
void motor_setSShapedRamp(uint8_t icID, uint32_t bow1, uint32_t bow2,
                           uint32_t bow3, uint32_t bow4);

// ============ 单位转换 ============
int32_t motor_mmToMicrosteps(uint8_t icID, float mm);
float   motor_microstepsToMM(uint8_t icID, int32_t microsteps);
int32_t motor_velocityMMToInternal(uint8_t icID, float mmPerSec);
float   motor_velocityInternalToMM(uint8_t icID, int32_t internal);

// ============ 限位开关 ============
void motor_enableLimitSwitch(uint8_t icID, uint8_t which, uint8_t polarity);
void motor_disableLimitSwitch(uint8_t icID, uint8_t which);
uint8_t motor_readLimitSwitches(uint8_t icID);

// ============ 归位 ============
void motor_startHoming(uint8_t icID, int8_t direction, int32_t velocity);
void motor_setHomePosition(uint8_t icID, int32_t position);

#endif /* TMC4361A_MOTION_H_ */
```

### 5.4 Axis 类适配设计

```cpp
// axis/Axis.h (重构后)

class Axis {
protected:
    // 替换 TMC4361ATypeDef _tmc4361 为：
    uint8_t _icID;              // IC 标识符
    String _name;
    AxisState _currentState;
    AxisConfig _config;

    // 运动配置缓存
    TMC4361A_MotionConfig _motionConfig;
    TMC2660_MotorConfig _motorConfig;

public:
    Axis(const String& name, uint8_t icID);

    bool begin(const AxisConfig& config);
    void update();
    bool processCommand(const String& command);

    // 运动控制 - 内部调用 motor_* API
    bool moveToPosition(float positionMM);
    bool moveRelative(float distanceMM);
    bool startHoming();
    void stop();

    // 状态查询
    bool isMoving() const;
    bool isPositionReached() const;
    float getCurrentPositionMM() const;

    // 单位转换 - 委托给 motor_* API
    int32_t mmToMicrosteps(float mm) const;
    float microstepsToMM(int32_t microsteps) const;
};
```

---

## 6. 代码模板

### 6.1 SPI 回调实现模板

```cpp
// tmc/hal/TMC_SPI.cpp

#include "TMC_SPI.h"
#include <SPI.h>

// IC 配置表
static const TMC_IC_Config tmc4361a_configs[] = {
    {PIN_CS_X,  0},  // icID 0: X 轴
    {PIN_CS_Y,  0},  // icID 1: Y 轴
    {PIN_CS_Z,  0},  // icID 2: Z 轴
    {PIN_CS_W,  0},  // icID 3: W 轴 (滤光轮1)
    {PIN_CS_E1, 1},  // icID 4: E1 轴 (物镜)
    {PIN_CS_E3, 1},  // icID 5: E3 轴 (扩展Z)
    {PIN_CS_E4, 1},  // icID 6: E4 轴 (滤光轮2)
};

// TMC4361A SPI 回调
void tmc4361A_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength)
{
    if (icID >= TMC4361A_IC_CACHE_COUNT) return;

    uint8_t csPin = tmc4361a_configs[icID].csPin;

    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
    digitalWrite(csPin, LOW);
    delayMicroseconds(100);

    for (size_t i = 0; i < dataLength; i++) {
        data[i] = SPI.transfer(data[i]);
    }

    digitalWrite(csPin, HIGH);
    SPI.endTransaction();
}

// TMC4361A 状态回调
void tmc4361A_setStatus(uint16_t icID, uint8_t *data)
{
    // 可选：存储状态用于诊断
    // tmc4361a_lastStatus[icID] = data[0];
}

// 初始化所有 CS 引脚
void tmc_spi_init(void)
{
    for (int i = 0; i < TMC4361A_IC_CACHE_COUNT; i++) {
        pinMode(tmc4361a_configs[i].csPin, OUTPUT);
        digitalWrite(tmc4361a_configs[i].csPin, HIGH);
    }
    SPI.begin();
}
```

### 6.2 TMC4361A 寄存器操作模板

```cpp
// tmc/ic/TMC4361A/TMC4361A.cpp

#include "TMC4361A.h"

// 缓存数组
static uint32_t tmc4361A_cache_data[TMC4361A_IC_CACHE_COUNT][TMC4361A_REGISTER_COUNT];
static uint8_t  tmc4361A_cache_dirty[TMC4361A_IC_CACHE_COUNT][(TMC4361A_REGISTER_COUNT + 7) / 8];

void tmc4361A_initCache(void)
{
    for (uint16_t ic = 0; ic < TMC4361A_IC_CACHE_COUNT; ic++) {
        for (uint8_t reg = 0; reg < TMC4361A_REGISTER_COUNT; reg++) {
            tmc4361A_cache_data[ic][reg] = tmc4361A_defaultRegisterResetState[reg];
        }
        memset(tmc4361A_cache_dirty[ic], 0, sizeof(tmc4361A_cache_dirty[ic]));
    }
}

int32_t tmc4361A_readRegister(uint16_t icID, uint8_t address)
{
    uint8_t data[5] = {address, 0, 0, 0, 0};

    // 第一次传输：发送地址
    tmc4361A_readWriteSPI(icID, data, 5);
    tmc4361A_setStatus(icID, data);

    // 第二次传输：获取数据
    data[0] = address;
    tmc4361A_readWriteSPI(icID, data, 5);
    tmc4361A_setStatus(icID, data);

    int32_t value = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
                    ((uint32_t)data[3] << 8) | data[4];

    // 更新缓存
    tmc4361A_cache_data[icID][address] = value;

    return value;
}

void tmc4361A_writeRegister(uint16_t icID, uint8_t address, int32_t value)
{
    uint8_t data[5] = {
        (uint8_t)(address | 0x80),  // 写标志
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)(value)
    };

    tmc4361A_readWriteSPI(icID, data, 5);
    tmc4361A_setStatus(icID, data);

    // 更新缓存
    tmc4361A_cache_data[icID][address] = value;
    tmc4361A_setDirtyBit(icID, address, true);
}

uint32_t tmc4361A_fieldRead(uint16_t icID, TMC4361A_RegisterField field)
{
    int32_t value = tmc4361A_readRegister(icID, field.address);
    return (value & field.mask) >> field.shift;
}

void tmc4361A_fieldWrite(uint16_t icID, TMC4361A_RegisterField field, uint32_t value)
{
    int32_t regValue = tmc4361A_readRegister(icID, field.address);
    regValue = (regValue & ~field.mask) | ((value << field.shift) & field.mask);
    tmc4361A_writeRegister(icID, field.address, regValue);
}
```

### 6.3 Axis 类适配模板

```cpp
// axis/Axis.cpp (重构后)

#include "Axis.h"
#include "tmc/motion/TMC4361A_Motion.h"

Axis::Axis(const String& name, uint8_t icID)
    : _name(name), _icID(icID), _currentState(STATE_IDLE)
{
}

bool Axis::begin(const AxisConfig& config)
{
    _config = config;

    // 构建运动配置
    _motionConfig.clockFrequency = config.clockFrequency;
    _motionConfig.screwPitchMM = config.screwPitchMM;
    _motionConfig.fullStepsPerRev = config.fullStepsPerRev;
    _motionConfig.microsteps = config.microstepping;
    _motionConfig.vmax = motor_velocityMMToInternal(_icID, config.maxVelocityMM);
    _motionConfig.amax = config.maxAccelerationMM;  // 需要转换
    _motionConfig.dmax = config.maxAccelerationMM;

    // 构建电机配置
    _motorConfig.currentScale = calculateCScale(config.motorCurrentMA, config.r_sense);
    _motorConfig.holdCurrentRatio = config.holdCurrent;
    _motorConfig.mres = microstepToMRES(config.microstepping);
    _motorConfig.interpolation = 1;

    // 调用运动层初始化
    motor_init(_icID, &_motionConfig, &_motorConfig);

    // 配置限位开关
    if (config.enableLeftLimitSwitch) {
        motor_enableLimitSwitch(_icID, LEFT_SW, config.leftSwitchPolarity);
    }
    if (config.enableRightLimitSwitch) {
        motor_enableLimitSwitch(_icID, RGHT_SW, config.rightSwitchPolarity);
    }

    return true;
}

bool Axis::moveToPosition(float positionMM)
{
    if (_currentState != STATE_IDLE) return false;

    int32_t microsteps = motor_mmToMicrosteps(_icID, positionMM);
    motor_moveToPosition(_icID, microsteps);

    _currentState = STATE_MOVING;
    return true;
}

bool Axis::moveRelative(float distanceMM)
{
    if (_currentState != STATE_IDLE) return false;

    int32_t microsteps = motor_mmToMicrosteps(_icID, distanceMM);
    motor_moveByDistance(_icID, microsteps);

    _currentState = STATE_MOVING;
    return true;
}

void Axis::stop()
{
    motor_stop(_icID);
    _currentState = STATE_IDLE;
}

bool Axis::isPositionReached() const
{
    return motor_isPositionReached(_icID);
}

float Axis::getCurrentPositionMM() const
{
    int32_t microsteps = motor_getActualPosition(_icID);
    return motor_microstepsToMM(_icID, microsteps);
}
```

---

## 7. 迁移指南

### 7.1 API 映射表

| 旧 API | 新 API | 说明 |
|--------|--------|------|
| `tmc4361A_writeInt(&tmc, addr, val)` | `tmc4361A_writeRegister(icID, addr, val)` | 使用 icID |
| `tmc4361A_readInt(&tmc, addr)` | `tmc4361A_readRegister(icID, addr)` | 使用 icID |
| `TMC4361A_FIELD_READ(&tmc, addr, mask, shift)` | `tmc4361A_fieldRead(icID, field)` | 使用 RegisterField |
| `tmc4361A_tmc2660_init(&tmc, clk)` | `motor_init(icID, motionCfg, motorCfg)` | 分离配置 |
| `tmc4361A_moveTo(&tmc, pos)` | `motor_moveToPosition(icID, pos)` | 更清晰命名 |
| `tmc4361A_isRunning(&tmc, pid)` | `motor_isRunning(icID)` | 简化参数 |
| `tmc4361A_xmmToMicrosteps(&tmc, mm)` | `motor_mmToMicrosteps(icID, mm)` | 统一前缀 |

### 7.2 兼容层 (过渡期)

```cpp
// 兼容层 - 将旧 API 映射到新 API
// 在过渡期使用，最终应移除

#define tmc4361A_writeInt_COMPAT(tmc, addr, val) \
    tmc4361A_writeRegister((tmc)->icID, addr, val)

#define tmc4361A_readInt_COMPAT(tmc, addr) \
    tmc4361A_readRegister((tmc)->icID, addr)

// 扩展 TMC4361ATypeDef 添加 icID
typedef struct {
    uint8_t icID;  // 新增
    // ... 其他字段保持不变
} TMC4361ATypeDef_Compat;
```

### 7.3 分步迁移策略

1. **第一步：添加新 API，保留旧 API**
   - 新代码使用新 API
   - 旧代码继续使用旧 API

2. **第二步：逐步迁移旧代码**
   - 从底层向上迁移
   - 每次迁移后运行测试

3. **第三步：移除旧 API**
   - 确认所有代码已迁移
   - 删除兼容层和旧 API

---

## 8. 测试计划

### 8.1 单元测试

| 测试项 | 测试内容 | 预期结果 |
|--------|---------|---------|
| SPI 读写 | 发送/接收测试数据 | 数据一致 |
| 寄存器读 | 读取 VERSION_NO | 返回正确版本号 |
| 寄存器写 | 写入 XTARGET | 缓存更新 |
| 字段读写 | 读写 RAMPMODE 字段 | 只修改目标位 |
| 缓存机制 | 写入后检查缓存 | 缓存与硬件一致 |
| 单位转换 | mm ↔ microsteps | 双向转换正确 |

### 8.2 集成测试

| 测试项 | 测试步骤 | 预期结果 |
|--------|---------|---------|
| 初始化 | 调用 motor_init() | 无错误，寄存器配置正确 |
| 绝对移动 | motor_moveToPosition(10000) | 电机移动到目标位置 |
| 相对移动 | motor_moveByDistance(1000) | 电机移动指定距离 |
| 停止 | 运动中调用 motor_stop() | 电机减速停止 |
| 限位检测 | 触发限位开关 | 电机停止，状态正确 |
| 归位 | 执行归位序列 | 电机归位，位置清零 |

### 8.3 回归测试

- [ ] 所有现有功能在重构后仍正常工作
- [ ] 串口命令响应正确
- [ ] 多轴协调运动正常
- [ ] 上位机兼容性测试

---

## 附录：参考资源

1. **官方 TMC-API**
   - 仓库：https://github.com/analogdevicesinc/TMC-API
   - 文档：`/home/hds/github.com/TMC-API/docs/TMC4361A_TMC2660_API_Reference.md`

2. **示例代码**
   - TMC4361A：`/home/hds/github.com/TMC-API/tmc/ic/TMC4361A/Examples/`
   - TMC2660：`/home/hds/github.com/TMC-API/tmc/ic/TMC2660/Examples/`

3. **现有架构文档**
   - `/home/hds/gitee.com/octoaxes/documents/firmware-architecture.md`

---

*文档版本：1.0*
*创建日期：2026-01-21*
