# TODO.md

任务跟踪文件，用于管理项目待办事项。

> 详细重构方案参见：`documents/refactoring-plan.md`

## 进行中

<!-- 当前正在处理的任务，建议同时只有 1-2 个 -->

- [ ] **优化 W 轴换孔时间** - 基准 144ms，目标 ≤ 60ms，当前 61.3ms (ASTART=180, BOW 截断为硬约束)

## 待办

<!-- 计划要做但尚未开始的任务 -->

### 硬件测试
- [ ] 硬件测试触发系统（示波器验证 pin 29-32 脉冲波形）
- [ ] 硬件测试照明系统（TTL 端口 + DAC 输出 + LED 矩阵图案）
- [ ] 上位机兼容性测试（Python 发送触发 + 照明命令验证协议）
- [ ] 测试 GUI 黑黄主题效果

### 功能验证
- [x] 编译测试 (2026-01-23)
- [x] API 参数对比检查 (2026-01-23) - 修复 REFERENCE_CONF 位偏移错误
- [x] 新旧 API 初始化一致性修复 (2026-01-23) - 添加复位 + CHOPCONF 参数对齐
- [x] 创建硬件测试脚本 (2026-01-23) - `software/tests/`
- [x] 修复 REFERENCE_CONF 多处位偏移错误 (2026-01-23)
- [x] 系统性新旧 API 行为比对 (2026-01-23) - 修复 4 个关键函数
- [x] 修复 Cover 接口超时问题 (2026-01-23) - 使用延时替代 COVER_DONE 轮询
- [x] 烧写固件并验证 Z 轴限位开关 (2026-01-23) - 限位状态 0x0 正确
- [x] 验证 Z 轴基本移动功能 (2026-01-27) - MOVETO 命令正常工作
- [x] 调试 Z 轴 homing 流程 (2026-02-25) - 修复 SOFT_STOP_EN 导致停车失败
- [ ] 去掉 StepAxis homing debug 打印（确认稳定后）
- [ ] 去掉 FilterWheel homing debug 打印
- [ ] 修正 W 轴 config.h 配置（LEFT_SW → RGHT_SW + 极性修正）

### 命令移植
- [x] 照明系统移植 (2026-02-26) - illumination.h/cpp + 11 个 handler
- [x] 相机触发系统移植 (2026-02-26) - trigger.h/cpp + 6 个 handler
- [x] 优先级 3 运动配置命令移植 (2026-02-26) - SET_LIM/POLARITY/DRIVER/PITCH/MARGIN 5 个 handler
- [x] 响应机制实现 (2026-02-26) - send_position_update() 10ms 周期上报，W 轴位置 + 固件版本
- [x] INITFILTERWHEEL(253) / INITFILTERWHEEL_W2(252) 实现 (2026-02-26)
- [x] motion 命令移植完成 (2026-02-26) - HomeOrZero 轴映射修复，enable/disable 二进制协议
- [x] P5 PID/编码器命令实现 (2026-02-26) - CONFIGURE_STAGE_PID(25)/ENABLE(26)/DISABLE(27)/SET_PID_ARGUMENTS(29)
- [x] 修复固件版本号不显示 (2026-02-27) - sendDebugInfo 改为 SerialUSB.println
- [x] 上位机 SET_LIMITS 改为二进制协议 (2026-02-27) - LIMIT_CODE + μm→微步转换
- [x] 修复虚拟限位 (VSTOP) 恢复机制 (2026-02-27) - STATUS 寄存器延迟恢复策略，参照 §10.4
- [x] W 轴换孔时间回归测试 (2026-02-27) - 日志分析确认 ~60ms，与 61.3ms 一致，无性能退化
- [x] 排查 `completeMovement()` 计时输出缺失 (2026-02-27) - 上位机 parse_axis_data() 吞掉轴前缀行，已修复
- [ ] 硬件验证 VSTOP 恢复（反复测试到达限位→反向→再到达）
- [x] MOVE/MOVETO 协议单位改为微步 (2026-02-27) - 固件接受微步，上位机 μm→微步转换
- [x] 旧上位机兼容性验证 (2026-02-27) - Squid Python configure_actuators → Octoaxes 固件

### 代码清理（可选）
- [x] 修正 calculateCurrentScale 注释和变量名（峰值 vs RMS 区分）(2026-02-12)
- [x] MotorControl.cpp debug 打印统一使用 DEBUG 宏 (2026-02-14)
- [x] motor_moveToMicrosteps 移除未使用的 rampModeBefore 变量 (2026-02-14)
- [x] CommandProcessor 27 个空桩函数添加 NOT_IMPLEMENTED 日志 (2026-02-14)
- [x] 移除兼容层中不再需要的代码 (2026-02-27) - 删除 Fields.h/Register.h/Constants.h，统一到 HW_Abstraction.h，248 警告→0
- [ ] 清理注释和文档

---

## 已完成

<!-- 已完成的任务，保留最近的记录作为参考 -->

### 上位机 UI 标签化重构 (2026-02-26, develop)
- [x] main_window.py 改为 QTabWidget 三标签页（Motion / Illumination / Log）
- [x] 纯布局调整，零逻辑变更

### 相机触发系统移植 (2026-02-26, develop)
- [x] 新建 trigger.h/cpp：4 路触发、双模式脉冲、100μs 频闪 ISR
- [x] 实现 6 个 handler：SEND_HARDWARE_TRIGGER(30)、SET_STROBE_DELAY(31)、SET_TRIGGER_MODE(33)、ANALOG_WRITE_ONBOARD_DAC(15)、SET_PIN_LEVEL(41)、ACK_JOYSTICK_BUTTON_PRESSED(14)
- [x] octoaxes.ino 集成 trigger_init() + trigger_update()
- [x] 编译通过

### 照明系统完整移植 (2026-02-26, develop)
- [x] 新建 illumination.h/cpp：DAC80508 驱动、APA102 LED 矩阵、5 端口控制
- [x] 实现 11 个照明 handler（旧 API 命令 10-17 + 新多端口 API 命令 34-39）
- [x] config.h 添加照明引脚、命令码、IlluminationConfig 命名空间
- [x] 编译通过

### Z 轴 homing 停车失败修复 (2026-02-25, develop)
- [x] 定位根因: motor_configLimitSwitches() 多设 SOFT_STOP_EN (bit 5)，master 无此位
- [x] SOFT_STOP_EN 导致 TMC4361A 内部软停车状态机锁定寄存器写入
- [x] 修复: 移除 SOFT_STOP_EN，与 master 保持一致（硬停车）
- [x] 新增 motor_setHardwareStopEnable() API（备用）
- [x] 添加 StepAxis homing 搜索阶段周期性 debug 打印
- [x] 硬件验证: homing 正常完成，停车、latch、安全位置移动均正确

### W 轴 ASTART + Homing 竞态修复 (2026-02-10, develop)
- [x] 实现 ASTART/DFINAL 起始加速度功能 (AxisConfig, MotorControl, config.h)
- [x] 修复 sRampInit 无条件清除 USE_ASTART_AND_VSTART
- [x] 修复 FilterWheel homing 完成竞态条件 (VMAX 写入导致漂移)
- [x] 恢复 AMAX 400 rev/s² (梯形测试遗留)
- [x] 测试验证: motor 时间 70ms→62ms, 24 次移动 err=0

### Homing 细分切换功能 (2026-02-09, develop)
- [x] AxisConfig 新增 homingMicrostepping 字段（默认 256）
- [x] MotorControl 新增 motor_setMicrosteps() 运行时切换
- [x] StepAxis/FilterWheel/Objectives homing 中切换和恢复
- [x] W 轴 6 次连续 homing 测试通过

### W 轴 FilterWheel homing 两阶段精确定位 (2026-02-09, from master)
- [x] 排查 W 轴 homing 传感器信号和 limit_state 返回值
- [x] 重写 FilterWheel homing 为两阶段：快速搜索+慢速逼近
- [x] 添加 _slowApproach 标志控制两阶段切换

### 新旧 API 一致性修复 + 运动调试 (2026-01-27)
- [x] velocity_mode 状态追踪 - 在 MotorParams 添加字段
- [x] sRampInit 完整实现 - 从速度模式切换时重写所有斜坡参数
- [x] motor_adjustBows() - BOW 参数自动计算 (BOW = AMAX² / VMAX)
- [x] RAMPMODE 位操作修复 - 使用 setBits/rstBits 逻辑
- [x] 创建调试脚本 test_09/10/11
- [x] 验证 Z 轴基本移动正常 - MOVETO 命令成功

### Cover 接口修复 (2026-01-23)
- [x] 定位初始化慢的根因：COVER_DONE 轮询超时（每次约 3 秒）
- [x] 修复 `tmc4361A_readWriteCover` - 使用简单延时替代 COVER_DONE 轮询
- [x] 验证初始化速度恢复正常（4 轴 < 0.1 秒）
- [x] 验证 GET_DATA 命令正常
- [x] 验证限位开关状态正确 (0x0)

### 系统性 API 行为比对 (2026-01-23)
- [x] 完整比对 `motor_configLimitSwitches` vs `tmc4361A_enableLimitSwitch`
- [x] 完整比对 `motor_enableSoftLimits` vs `tmc4361A_enableVirtualLimitSwitch`
- [x] 完整比对 `motor_moveToMicrosteps` vs `tmc4361A_moveTo`
- [x] 完整比对 `motor_configStallGuard` vs `tmc4361A_config_init_stallGuard`
- [x] 修复 `motor_configLimitSwitches` - 添加 LATCH_X_ON_ACTIVE + 读-修改-写
- [x] 修复 `motor_enableSoftLimits` - 添加 VIRT_STOP_MODE 硬停止
- [x] 修复 `motor_moveToMicrosteps` - 添加 EVENTS 清除 + XACTUAL 刷新
- [x] 修复 `motor_configStallGuard` - 添加 VSTALL_LIMIT 设置

### 硬件测试准备 (2026-01-23)
- [x] 创建测试脚本 `software/tests/`
- [x] 运行测试 01-03 (串口、版本、Engine Start)
- [x] 运行测试 04 (TMC 状态) - 发现 Z 轴限位异常
- [x] 修复 `motor_enableHomingLimit` - 重写整个函数逻辑
- [x] 修复 `motor_enableSoftLimits` 位偏移
- [x] 修复 `motor_configStallGuard` 寄存器错误

### TMC4361A 编程文档 (2026-01-22)
- [x] 编程指南 v1.0 - 页 1-40（概述、引脚、SPI 通信、斜坡配置）
- [x] 编程指南 v1.1 - 页 41-80（高级斜坡、外部步进、参考开关）
- [x] 编程指南 v1.2 - 页 81-120（目标管线、无主同步、SPI 输出）
- [x] 编程指南 v1.3 - 页 121-160（紧急停止、PWM、dcStep、编码器）
- [x] 编程指南 v1.4 - 页 161-200（闭环操作详解）
- [x] 编程指南 v1.5 - 页 201-224（完整寄存器、电气特性、封装）

### 固件重构 (2026-01-21 ~ 2026-01-22)
- [x] 阶段 1: 实现 SPI 硬件抽象层 (HAL) - `965acdb`
- [x] 阶段 2: TMC4361A 驱动重构 - `92b0da4`
- [x] 阶段 3: TMC2660 驱动分离 - `000a7c7`
- [x] 阶段 4: 运动控制层 - `4c9dbc6`
- [x] 阶段 5: Axis 类适配新架构 - `8b6184a`
- [x] 阶段 6: 测试和清理 - `2ae9549`
- [x] 阶段 7: API替换和风险修复 - `bebac80`

### 项目管理
- [x] 2026-01-21: 创建详细重构任务列表
- [x] 2026-01-21: 创建重构计划文档 (documents/refactoring-plan.md)
- [x] 2026-01-21: 创建固件架构技术文档 (documents/firmware-architecture.md)
- [x] 2026-01-21: 项目初始化，创建 Claude Code 项目管理文件

## 阻塞/问题

<!-- 遇到的问题或阻塞项，需要解决后才能继续 -->

- W 轴 config.h 中 homingSwitch=LEFT_SW 与实际硬件（RIGHT switch）不匹配，暂不影响功能但 latch 位置不准确

---

## 使用说明

1. 新任务添加到「待办」
2. 开始处理时移到「进行中」
3. 完成后移到「已完成」并标注日期
4. 遇到问题记录在「阻塞/问题」

## 参考资料

- 重构计划：`documents/refactoring-plan.md`
- 架构文档：`documents/firmware-architecture.md`
- 测试脚本：`software/tests/`
- 官方 API 文档：`/home/hds/github.com/TMC-API/docs/TMC4361A_TMC2660_API_Reference.md`
- TMC4361A 示例：`/home/hds/github.com/TMC-API/tmc/ic/TMC4361A/Examples/`
- TMC2660 示例：`/home/hds/github.com/TMC-API/tmc/ic/TMC2660/Examples/`
