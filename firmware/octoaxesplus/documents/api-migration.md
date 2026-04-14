# API 迁移文档

本文档记录从旧 API (TMC4361A_TMC2660_Utils) 到新 API (MotorControl) 的迁移差异。

## 概述

| 项目 | 旧 API (master) | 新 API (develop) |
|------|-----------------|------------------|
| 头文件 | `TMC4361A_TMC2660_Utils.h` | `tmc/motion/MotorControl.h` |
| 对象引用 | `TMC4361ATypeDef *tmc4361A` | `uint8_t icID` |
| 架构 | 指针传递结构体 | IC 标识符索引 |

## 函数对照表

### 初始化函数

| 功能 | 旧 API | 新 API |
|------|--------|--------|
| 运动控制器初始化 | `tmc4361A_init()` | `motor_initMotionController()` |
| 电机参数配置 | `tmc4361A_tmc2660_config()` | (集成到 motor_initDriver) |
| 硬件初始化 | `tmc4361A_tmc2660_init()` | (集成到 motor_initMotionController) |
| 驱动器初始化 | - | `motor_initDriver()` |
| 限位开关配置 | `tmc4361A_enableLimitSwitch()` | `motor_configLimitSwitches()` |
| 归位限位配置 | `tmc4361A_enableHomingLimit()` | `motor_enableHomingLimit()` |
| PID 禁用 | `tmc4361A_set_PID(tmc, PID_DISABLE)` | `motor_disablePID(icID)` |
| StallGuard 配置 | `tmc4361A_config_init_stallGuard()` | `motor_configStallGuard()` |
| 斜坡初始化 | `tmc4361A_sRampInit()` | (集成到 motor_initMotionController) |

### 运动参数设置

| 功能 | 旧 API | 新 API |
|------|--------|--------|
| 设置最大速度 | `tmc4361A_setMaxSpeed(tmc, v)` | `motor_setMaxVelocity(icID, v)` |
| 设置最大加速度 | `tmc4361A_setMaxAcceleration(tmc, a)` | `motor_setMaxAcceleration(icID, a)` |
| 设置最大减速度 | - | `motor_setMaxDeceleration(icID, d)` |

### 位置控制

| 功能 | 旧 API | 新 API |
|------|--------|--------|
| 获取当前位置(微步) | `tmc4361A_currentPosition(tmc)` | `motor_getPositionMicrosteps(icID)` |
| 获取当前位置(mm) | `tmc4361A_xmicrostepsTomm(tmc, pos)` | `motor_getPositionMM(icID)` |
| 获取目标位置 | `tmc4361A_targetPosition(tmc)` | `motor_getTargetMicrosteps(icID)` |
| 移动到位置(微步) | `tmc4361A_moveTo(tmc, pos)` | `motor_moveToMicrosteps(icID, pos)` |
| 移动到位置(mm) | - | `motor_moveToPosition(icID, pos)` |
| 相对移动 | - | `motor_moveByDistance(icID, dist)` |
| 设置当前位置(微步) | `tmc4361A_setCurrentPosition(tmc, pos)` | `motor_setCurrentPositionMicrosteps(icID, pos)` |
| 设置当前位置(mm) | - | `motor_setCurrentPosition(icID, pos)` |

### 速度控制

| 功能 | 旧 API | 新 API |
|------|--------|--------|
| 设置速度(内部单位) | `tmc4361A_setSpeed(tmc, v)` | `motor_setVelocityInternal(icID, v)` |
| 设置速度(mm/s) | - | `motor_rotateVelocity(icID, v)` |
| 停止(减速) | `tmc4361A_setSpeed(tmc, 0)` | `motor_stop(icID)` |
| 紧急停止 | - | `motor_emergencyStop(icID)` |

### 驱动器控制

| 功能 | 旧 API | 新 API |
|------|--------|--------|
| 禁用驱动器 | `tmc4361A_tmc2660_disable_driver(tmc)` | `motor_enableDriver(icID, false)` |
| 使能驱动器 | `tmc4361A_tmc2660_enable_driver(tmc)` | `motor_enableDriver(icID, true)` |
| 设置电流 | `tmc4361A_setCurrentScale(tmc, cs)` | `motor_setRunCurrent(icID, mA)` |

### 软限位

| 功能 | 旧 API | 新 API |
|------|--------|--------|
| 设置下限位 | `tmc4361A_setVirtualLimit(tmc, -1, pos)` | `motor_setSoftLimits(icID, lower, upper)` |
| 设置上限位 | `tmc4361A_setVirtualLimit(tmc, 1, pos)` | (同上) |
| 启用下限位 | `tmc4361A_enableVirtualLimitSwitch(tmc, -1)` | `motor_enableSoftLimits(icID, true, true)` |
| 启用上限位 | `tmc4361A_enableVirtualLimitSwitch(tmc, 1)` | (同上) |
| 禁用下限位 | `tmc4361A_disableVirtualLimitSwitch(tmc, -1)` | `motor_enableSoftLimits(icID, false, false)` |
| 禁用上限位 | `tmc4361A_disableVirtualLimitSwitch(tmc, 1)` | (同上) |

### 状态读取

| 功能 | 旧 API | 新 API |
|------|--------|--------|
| 读取限位开关状态 | `tmc4361A_readLimitSwitches(tmc)` | `motor_readLimitSwitches(icID)` |
| 读取开关事件 | `tmc4361A_readSwitchEvent(tmc)` | `motor_readSwitchEvent(icID)` |
| 读取事件寄存器 | `tmc4361A_readInt(tmc, TMC4361A_EVENTS)` | `motor_readEvents(icID)` |
| 读取状态寄存器 | `tmc4361A_readInt(tmc, TMC4361A_STATUS)` | `motor_readStatus(icID)` |
| 读取锁存位置 | `tmc4361A_readInt(tmc, TMC4361A_X_LATCH_RD)` | `motor_readLatchPosition(icID)` |
| 检查目标到达 | - | `motor_isTargetReached(icID)` |
| 检查是否运行中 | - | `motor_isRunning(icID)` |

### 单位转换

| 功能 | 旧 API | 新 API |
|------|--------|--------|
| mm → 微步 | `tmc4361A_xmmToMicrosteps(tmc, mm)` | `motor_mmToMicrosteps(icID, mm)` |
| 微步 → mm | `tmc4361A_xmicrostepsTomm(tmc, steps)` | `motor_microstepsToMM(icID, steps)` |
| 速度 mm/s → 内部 | `tmc4361A_vmmToMicrosteps(tmc, v)` | `motor_velocityMMToInternal(icID, v)` |
| 速度 内部 → mm/s | - | `motor_velocityInternalToMM(icID, v)` |
| 加速度 mm/s² → 内部 | `tmc4361A_ammToMicrosteps(tmc, a)` | `motor_accelMMToInternal(icID, a)` |

## 关键实现差异

### 1. motor_readLimitSwitches()

**旧 API:**
```c
uint8_t tmc4361A_readLimitSwitches(TMC4361ATypeDef *tmc4361A) {
  uint32_t i_datagram = tmc4361A_readInt(tmc4361A, TMC4361A_STATUS);
  i_datagram &= (TMC4361A_STOPL_ACTIVE_F_MASK | TMC4361A_STOPR_ACTIVE_F_MASK);
  i_datagram >>= TMC4361A_STOPL_ACTIVE_F_SHIFT;  // bit 7
  return i_datagram & 0xff;
}
```

**新 API:**
```c
uint8_t motor_readLimitSwitches(uint8_t icID) {
  if (icID >= MOTOR_IC_COUNT) return 0;
  uint32_t status = tmc4361A_readRegister(icID, TMC4361A_STATUS);
  status &= (TMC4361A_STOPL_ACTIVE_F_MASK | TMC4361A_STOPR_ACTIVE_F_MASK);
  status >>= TMC4361A_STOPL_ACTIVE_F_SHIFT;
  return (uint8_t)(status & 0x03);
}
```

**差异:** 新 API 添加了边界检查，使用 icID 索引替代指针。

### 2. motor_readSwitchEvent()

**旧 API:**
```c
uint8_t tmc4361A_readSwitchEvent(TMC4361ATypeDef *tmc4361A) {
  uint32_t i_datagram = tmc4361A_readInt(tmc4361A, TMC4361A_EVENTS);
  i_datagram &= (TMC4361A_STOPL_EVENT_MASK | TMC4361A_STOPR_EVENT_MASK);
  i_datagram >>= TMC4361A_STOPL_EVENT_SHIFT;  // bit 11
  return i_datagram & 0xff;
}
```

**新 API:**
```c
uint8_t motor_readSwitchEvent(uint8_t icID) {
  if (icID >= MOTOR_IC_COUNT) return 0;
  uint32_t events = tmc4361A_readRegister(icID, TMC4361A_EVENTS);
  events &= (TMC4361A_STOPL_EVENT_MASK | TMC4361A_STOPR_EVENT_MASK);
  events >>= TMC4361A_STOPL_EVENT_SHIFT;
  return (uint8_t)(events & 0x03);
}
```

**差异:** 新 API 添加了边界检查，使用 icID 索引替代指针。

### 3. motor_velocityMMToInternal()

**旧 API:**
```c
int32_t tmc4361A_vmmToMicrosteps(TMC4361ATypeDef *tmc4361A, float mm) {
  int32_t microsteps = (1 << 8) * mm *
    ((float)(tmc4361A->microsteps * tmc4361A->stepsPerRev)) /
    (tmc4361A->threadPitch);
  return microsteps;
}
```

**新 API:**
```c
int32_t motor_velocityMMToInternal(uint8_t icID, float velocityMM) {
  if (icID >= MOTOR_IC_COUNT || !motorParams[icID].initialized) return 0;
  int32_t velocity = (int32_t)((1 << 8) * velocityMM * motorParams[icID].stepsPerMM);
  return velocity;
}
```

**差异:**
- 新 API 使用预计算的 `stepsPerMM` 代替每次计算
- 添加了初始化状态检查
- 公式等效: `(1 << 8) * mm * stepsPerMM`

### 4. motor_setVelocityInternal()

**旧 API:**
```c
void tmc4361A_setSpeed(TMC4361ATypeDef *tmc4361A, int32_t velocity) {
  tmc4361A->velocity_mode = true;
  tmc4361A_readInt(tmc4361A, TMC4361A_EVENTS);  // 清除事件寄存器
  tmc4361A_rstBits(tmc4361A, TMC4361A_RAMPMODE, TMC4361A_RAMP_POSITION | TMC4361A_RAMP_HOLD);
  tmc4361A_writeInt(tmc4361A, TMC4361A_VMAX, velocity);
}
```

**新 API:**
```c
void motor_setVelocityInternal(uint8_t icID, int32_t velocityInternal) {
  if (icID >= MOTOR_IC_COUNT) return;
  tmc4361A_readRegister(icID, TMC4361A_EVENTS);  // 清除事件寄存器
  uint32_t rampMode = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);
  rampMode &= ~(TMC4361A_RAMP_POSITION | TMC4361A_RAMP_HOLD);
  tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);
  tmc4361A_writeRegister(icID, TMC4361A_VMAX, velocityInternal);
}
```

**差异:** 功能等效，新 API 添加了边界检查。

## 位定义验证

| 寄存器 | 位 | 掩码 | 用途 |
|--------|-----|------|------|
| STATUS | bit 7 | 0x0080 | STOPL_ACTIVE_F (左限位激活) |
| STATUS | bit 8 | 0x0100 | STOPR_ACTIVE_F (右限位激活) |
| EVENTS | bit 11 | 0x0800 | STOPL_EVENT (左限位事件) |
| EVENTS | bit 12 | 0x1000 | STOPR_EVENT (右限位事件) |

## 迁移注意事项

1. **参数传递方式变化**: 从指针 `TMC4361ATypeDef*` 改为索引 `uint8_t icID`
2. **初始化顺序**: 新 API 需要先调用 `motor_initMotionController()` 再调用 `motor_initDriver()`
3. **软限位**: 新 API 合并了上下限位的设置和启用/禁用操作
4. **单位转换**: 新 API 的参数缓存需要在初始化时设置
5. **边界检查**: 新 API 所有函数都添加了 `icID >= MOTOR_IC_COUNT` 检查

## 删除的文件

- `TMC4361A.cpp` / `TMC4361A.h`
- `TMC4361A_TMC2660_Utils.cpp` / `TMC4361A_TMC2660_Utils.h`

## 新增的文件

- `tmc/hal/TMC_SPI.cpp` / `TMC_SPI.h`
- `tmc/ic/TMC4361A/TMC4361A.cpp` / `TMC4361A.h`
- `tmc/ic/TMC2660/TMC2660.cpp` / `TMC2660.h`
- `tmc/motion/MotorControl.cpp` / `MotorControl.h`
- `tmc/helpers/*.h` (官方 API 辅助文件)
