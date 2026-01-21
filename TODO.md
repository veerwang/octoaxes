# TODO.md

任务跟踪文件，用于管理项目待办事项。

> 详细重构方案参见：`documents/refactoring-plan.md`

## 进行中

<!-- 当前正在处理的任务，建议同时只有 1-2 个 -->

（暂无）

## 待办

<!-- 计划要做但尚未开始的任务 -->

---

### 阶段 1：基础设施 (HAL 层)

**目标**: 创建硬件抽象层，实现 SPI 回调机制

#### 1.1 创建目录结构
- [ ] 创建 `firmware/octoaxes/tmc/` 目录
- [ ] 创建 `firmware/octoaxes/tmc/hal/` 子目录
- [ ] 创建 `firmware/octoaxes/tmc/ic/` 子目录
- [ ] 创建 `firmware/octoaxes/tmc/motion/` 子目录

#### 1.2 定义 IC 配置
- [ ] 在 `config.h` 中添加 IC 数量宏定义
  ```cpp
  #define TMC4361A_IC_COUNT  7  // X, Y, Z, W, E1, E3, E4
  #define TMC2660_IC_COUNT   7
  ```
- [ ] 定义 `TMC_IC_Config` 结构体（CS 引脚、时钟源）
- [ ] 创建 IC 配置数组 `tmc4361a_configs[]`，映射 icID 到 CS 引脚
- [ ] 定义 IC 名称枚举（便于代码可读性）
  ```cpp
  enum TMC_IC_ID { IC_X=0, IC_Y, IC_Z, IC_W, IC_E1, IC_E3, IC_E4 };
  ```

#### 1.3 创建 TMC_SPI.h
- [ ] 定义 `TMC_IC_Config` 结构体
- [ ] 声明 `tmc_spi_init()` 初始化函数
- [ ] 声明 `tmc4361A_readWriteSPI()` 回调函数
- [ ] 声明 `tmc4361A_setStatus()` 状态回调函数
- [ ] 声明 `tmc2660_readWriteSPI()` 回调函数（预留）

#### 1.4 实现 TMC_SPI.cpp
- [ ] 实现 `tmc_spi_init()`: 初始化所有 CS 引脚为 HIGH，调用 `SPI.begin()`
- [ ] 实现 `tmc4361A_readWriteSPI()`:
  - 根据 icID 查找 CS 引脚
  - SPI 事务：500kHz, MSBFIRST, SPI_MODE0
  - CS 拉低 → 延迟 100us → 传输数据 → CS 拉高
- [ ] 实现 `tmc4361A_setStatus()`: 存储状态字节（可选诊断用）
- [ ] 添加 SPI 错误检测和日志（可选）

#### 1.5 编译验证
- [ ] 确保 TMC_SPI 模块可独立编译
- [ ] 在 `octoaxes.ino` 中包含头文件测试

---

### 阶段 2：TMC4361A 驱动重构

**目标**: 将 TMC4361A 驱动改为使用 icID 机制，添加全局缓存

#### 2.1 创建新目录结构
- [ ] 创建 `tmc/ic/TMC4361A/` 目录
- [ ] 复制现有文件作为起点：
  - `TMC4361A_Register.h` → `TMC4361A_HW_Abstraction.h`
  - `TMC4361A_Fields.h` → 保留
  - `TMC4361A_Constants.h` → 保留

#### 2.2 定义 RegisterField 结构体
- [ ] 在 `TMC4361A.h` 中定义：
  ```cpp
  typedef struct {
      uint32_t mask;
      uint8_t  shift;
      uint8_t  address;
      bool     isSigned;
  } TMC4361A_RegisterField;
  ```
- [ ] 更新 `TMC4361A_Fields.h`，为常用字段创建 `_FIELD` 常量
  - `TMC4361A_TARGET_REACHED_FIELD`
  - `TMC4361A_RAMPMODE_FIELD`
  - `TMC4361A_XACTUAL_FIELD` 等

#### 2.3 实现全局缓存数组
- [ ] 定义缓存数据结构：
  ```cpp
  static uint32_t tmc4361A_cache[TMC4361A_IC_COUNT][TMC4361A_REGISTER_COUNT];
  static uint8_t  tmc4361A_dirty[TMC4361A_IC_COUNT][(TMC4361A_REGISTER_COUNT+7)/8];
  ```
- [ ] 实现 `tmc4361A_initCache()`: 填充默认值
- [ ] 实现 `tmc4361A_setDirtyBit()` / `tmc4361A_getDirtyBit()`
- [ ] 实现 `tmc4361A_cache()` 缓存操作函数

#### 2.4 重构寄存器读写 API
- [ ] 新增 `tmc4361A_readRegister(uint16_t icID, uint8_t address)`
  - 调用 `tmc4361A_readWriteSPI()` 回调
  - 两次 SPI 传输获取数据
  - 更新缓存
- [ ] 新增 `tmc4361A_writeRegister(uint16_t icID, uint8_t address, int32_t value)`
  - 构建 5 字节数据帧（地址|0x80 + 4字节数据）
  - 调用 SPI 回调
  - 更新缓存和脏位

#### 2.5 实现字段级操作
- [ ] 实现 `tmc4361A_fieldExtract(data, field)`: 从数据提取字段
- [ ] 实现 `tmc4361A_fieldRead(icID, field)`: 读取并提取字段
- [ ] 实现 `tmc4361A_fieldWrite(icID, field, value)`: 读-改-写操作

#### 2.6 创建兼容层（过渡用）
- [ ] 保留旧 API 签名，内部转发到新 API
  ```cpp
  // 兼容层
  inline void tmc4361A_writeInt(TMC4361ATypeDef *tmc, uint8_t addr, int32_t val) {
      tmc4361A_writeRegister(tmc->icID, addr, val);
  }
  ```
- [ ] 在 `TMC4361ATypeDef` 中添加 `uint8_t icID` 字段
- [ ] 标记旧 API 为 `[[deprecated]]`

#### 2.7 编译和基础测试
- [ ] 确保编译通过
- [ ] 测试读取 VERSION_NO 寄存器
- [ ] 测试写入/读取 XTARGET 寄存器

---

### 阶段 3：TMC2660 驱动分离

**目标**: 创建独立的 TMC2660 驱动层

#### 3.1 创建 TMC2660 目录和文件
- [ ] 创建 `tmc/ic/TMC2660/` 目录
- [ ] 创建 `TMC2660.h` 头文件
- [ ] 创建 `TMC2660.cpp` 实现文件
- [ ] 创建 `TMC2660_HW_Abstraction.h` 寄存器定义

#### 3.2 定义 TMC2660 寄存器和常量
- [ ] 定义寄存器地址常量
  ```cpp
  #define TMC2660_DRVCTRL   0x00
  #define TMC2660_CHOPCONF  0x04
  #define TMC2660_SMARTEN   0x05
  #define TMC2660_SGCSCONF  0x06
  #define TMC2660_DRVCONF   0x07
  ```
- [ ] 定义字段设置宏
  ```cpp
  #define TMC2660_SET_MRES(x)    ((x) << 0)
  #define TMC2660_SET_INTPOL(x)  ((x) << 9)
  #define TMC2660_SET_CS(x)      ((x) << 0)
  // ...
  ```
- [ ] 定义状态位提取宏

#### 3.3 实现 TMC2660 缓存
- [ ] 定义缓存数组（5 个只写寄存器）
- [ ] 实现 `tmc2660_initCache()`

#### 3.4 实现通过 Cover 接口的通信
- [ ] 定义通信模式枚举 `TMC2660_CommMode`
- [ ] 实现 `tmc2660_setCommMode()`: 设置通信方式和关联的 TMC4361A icID
- [ ] 实现 `tmc2660_writeRegister()`:
  - Cover 模式：通过 `tmc4361A_writeRegister(icID, COVER_LOW_WR, data)`
  - 直接 SPI 模式：预留
- [ ] 实现 `tmc2660_readRegister()`:
  - 从缓存读取（只写寄存器）
  - 或通过 Cover 响应读取

#### 3.5 实现配置 API
- [ ] `tmc2660_initDriver(icID)`: 初始化驱动器
- [ ] `tmc2660_setRunCurrent(icID, current)`: 设置运行电流
- [ ] `tmc2660_setMicrostepResolution(icID, mres)`: 设置微步
- [ ] `tmc2660_setInterpolation(icID, enable)`: 设置插值
- [ ] `tmc2660_setChopperConfig(icID, toff, hstrt, hend, tbl)`: 斩波配置
- [ ] `tmc2660_enableDriver(icID, enable)`: 使能/禁用驱动

#### 3.6 实现状态检测 API
- [ ] `tmc2660_getStatusBits(icID)`: 获取状态字节
- [ ] `tmc2660_isStalled(icID)`: 检测失速
- [ ] `tmc2660_isOvertemperature(icID)`: 检测过温
- [ ] `tmc2660_isOvertemperatureWarning(icID)`: 过温预警
- [ ] `tmc2660_isShortToGroundA/B(icID)`: 短路检测
- [ ] `tmc2660_isOpenLoadA/B(icID)`: 开路检测
- [ ] `tmc2660_isStandstill(icID)`: 静止检测

#### 3.7 实现诊断 API
- [ ] `tmc2660_getStallGuardValue(icID)`: 获取 SG 值
- [ ] `tmc2660_getMicrostepPosition(icID)`: 获取微步位置

#### 3.8 测试 TMC2660 驱动
- [ ] 测试初始化
- [ ] 测试电流设置
- [ ] 测试状态读取

---

### 阶段 4：运动控制层

**目标**: 创建高层运动控制封装

#### 4.1 创建运动控制文件
- [ ] 创建 `tmc/motion/TMC4361A_Motion.h`
- [ ] 创建 `tmc/motion/TMC4361A_Motion.cpp`

#### 4.2 定义配置结构体
- [ ] `TMC4361A_MotionConfig`: 运动参数（时钟、螺距、微步、速度、加速度）
- [ ] `TMC2660_MotorConfig`: 电机参数（电流、微步、插值）

#### 4.3 实现初始化
- [ ] `motor_init(icID, motionCfg, motorCfg)`:
  - 调用 `tmc4361A_initCache()`
  - 读取 VERSION_NO 验证通信
  - 配置 GENERAL_CONF
  - 设置 RAMPMODE
  - 配置速度/加速度参数
  - 调用 `tmc2660_initDriver()`

#### 4.4 实现运动控制
- [ ] `motor_moveToPosition(icID, position)`: 绝对位置移动
- [ ] `motor_moveByDistance(icID, distance)`: 相对位置移动
- [ ] `motor_rotateVelocity(icID, velocity)`: 速度模式旋转
- [ ] `motor_stop(icID)`: 停止电机

#### 4.5 实现状态查询
- [ ] `motor_isPositionReached(icID)`: 检查位置到达
- [ ] `motor_isRunning(icID)`: 检查是否运动中
- [ ] `motor_getActualPosition(icID)`: 获取当前位置
- [ ] `motor_getActualVelocity(icID)`: 获取当前速度

#### 4.6 实现参数设置
- [ ] `motor_setVmax(icID, vmax)`: 设置最大速度
- [ ] `motor_setAmax(icID, amax)`: 设置最大加速度
- [ ] `motor_setDmax(icID, dmax)`: 设置最大减速度
- [ ] `motor_setSShapedRamp(icID, bow1-4)`: 配置 S 型斜坡

#### 4.7 实现单位转换
- [ ] 定义每个 icID 的运动参数缓存（螺距、微步等）
- [ ] `motor_mmToMicrosteps(icID, mm)`: 位置转换
- [ ] `motor_microstepsToMM(icID, microsteps)`: 反向转换
- [ ] `motor_velocityMMToInternal(icID, mmPerSec)`: 速度转换
- [ ] `motor_velocityInternalToMM(icID, internal)`: 反向转换

#### 4.8 实现限位开关
- [ ] `motor_enableLimitSwitch(icID, which, polarity)`
- [ ] `motor_disableLimitSwitch(icID, which)`
- [ ] `motor_readLimitSwitches(icID)`

#### 4.9 实现归位
- [ ] `motor_startHoming(icID, direction, velocity)`
- [ ] `motor_setHomePosition(icID, position)`

---

### 阶段 5：Axis 类适配

**目标**: 更新 Axis 类使用新 API

#### 5.1 更新 Axis 基类
- [ ] 添加 `uint8_t _icID` 成员变量
- [ ] 修改构造函数接受 `icID` 参数
- [ ] 更新 `begin()` 方法：
  - 构建 `TMC4361A_MotionConfig` 和 `TMC2660_MotorConfig`
  - 调用 `motor_init()`
- [ ] 更新 `moveToPosition()`: 调用 `motor_moveToPosition()`
- [ ] 更新 `moveRelative()`: 调用 `motor_moveByDistance()`
- [ ] 更新 `stop()`: 调用 `motor_stop()`
- [ ] 更新 `isPositionReached()`: 调用 `motor_isPositionReached()`
- [ ] 更新 `getCurrentPositionMM()`: 调用 `motor_getActualPosition()` + 转换

#### 5.2 更新 StepAxis
- [ ] 继承更新后的 Axis
- [ ] 确保反向间隙补偿逻辑正常工作

#### 5.3 更新 FilterWheel
- [ ] 继承更新后的 Axis
- [ ] 确保滤光轮位置控制正常

#### 5.4 更新 Objectives
- [ ] 继承更新后的 Axis
- [ ] 确保物镜切换正常

#### 5.5 更新 AxisManager
- [ ] 更新轴创建逻辑，传入 icID
- [ ] 更新 `beginAll()` 调用顺序
- [ ] 确保多轴协调正常

#### 5.6 更新 octoaxes.ino
- [ ] 在 `setup()` 中调用 `tmc_spi_init()`
- [ ] 在 `setup()` 中调用 `tmc4361A_initCache()`
- [ ] 更新轴对象创建代码

---

### 阶段 6：测试和清理

**目标**: 验证功能，移除废弃代码

#### 6.1 单元测试
- [ ] 测试 SPI 读写（回环测试）
- [ ] 测试 TMC4361A 寄存器读写
- [ ] 测试 TMC2660 配置
- [ ] 测试单位转换函数
- [ ] 测试字段级操作

#### 6.2 集成测试
- [ ] 测试单轴初始化
- [ ] 测试绝对位置移动
- [ ] 测试相对位置移动
- [ ] 测试速度模式
- [ ] 测试停止功能
- [ ] 测试限位开关检测
- [ ] 测试归位序列

#### 6.3 多轴测试
- [ ] 测试多轴同时运动
- [ ] 测试轴切换
- [ ] 测试所有 7 个轴

#### 6.4 上位机兼容性测试
- [ ] 测试串口命令响应
- [ ] 测试状态查询
- [ ] 测试 PyQt5 界面交互

#### 6.5 代码清理
- [ ] 移除兼容层代码
- [ ] 移除旧的 `TMC4361A_TMC2660_Utils.cpp` 中已迁移的函数
- [ ] 移除未使用的类型定义
- [ ] 清理注释和文档

#### 6.6 文档更新
- [ ] 更新 `firmware-architecture.md`
- [ ] 更新 `CLAUDE.md` 中的开发指南
- [ ] 添加 API 使用示例

---

## 已完成

<!-- 已完成的任务，保留最近的记录作为参考 -->

- [x] 2026-01-21: 创建详细重构任务列表
- [x] 2026-01-21: 创建重构计划文档 (documents/refactoring-plan.md)
- [x] 2026-01-21: 创建固件架构技术文档 (documents/firmware-architecture.md)
- [x] 2026-01-21: 项目初始化，创建 Claude Code 项目管理文件

## 阻塞/问题

<!-- 遇到的问题或阻塞项，需要解决后才能继续 -->

（暂无）

---

## 使用说明

1. 新任务添加到「待办」
2. 开始处理时移到「进行中」
3. 完成后移到「已完成」并标注日期
4. 遇到问题记录在「阻塞/问题」

## 参考资料

- 重构计划：`documents/refactoring-plan.md`
- 架构文档：`documents/firmware-architecture.md`
- 官方 API 文档：`/home/hds/github.com/TMC-API/docs/TMC4361A_TMC2660_API_Reference.md`
- TMC4361A 示例：`/home/hds/github.com/TMC-API/tmc/ic/TMC4361A/Examples/`
- TMC2660 示例：`/home/hds/github.com/TMC-API/tmc/ic/TMC2660/Examples/`
