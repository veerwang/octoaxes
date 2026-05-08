# TODO.md

任务跟踪文件，用于管理项目待办事项。

> 详细重构方案参见：`documents/refactoring-plan.md`

## 进行中

<!-- 当前正在处理的任务，建议同时只有 1-2 个 -->

- [ ] **优化 W 轴换孔时间** - 基准 144ms，目标 ≤ 60ms，当前 61.3ms (ASTART=180, BOW 截断为硬约束)
- [x] **协助旧 Squid 定位 5mm 短少 bug** (2026-05-08) - VSTOP 早完成根因，`y_negative=-0.01` 修复（旧 Squid 配置层）
- [x] **修复 AXIS_MM_PER_STEP 双源不同步** (2026-05-07, commit 7be758d) - 改 actuator_microstepping 后命令距离与实际位移按比例失配（ms=16 时 5mm→80mm），改为从 AXIS_CONFIG 派生
- [x] **XYZW 全部回退为微步模式** (2026-04-17) - `constants.py` has_encoder = False，响应包保持 24 字节与旧 Squid 兼容

## 待办

### 编码器（暂缓，已全部关闭）
- [x] Z 轴编码器基础设施 (2026-04-13) - config.h 常量 + invertEncoderDir + motor_initABNEncoder
- [x] 编码器透明上报方案 (2026-04-13) - getCurrentPositionMicrosteps 按 enableEncoder 切 XACTUAL/ENC_POS，MSG_LENGTH 保持 24
- [ ] （暂缓）Z 轴编码器硬件验证 - Encoder 与 Steps μm 值对比 Δ≈0
- [ ] （暂缓）合并 W 轴编码器修复（maxpro → develop）
- [ ] （暂缓）重新启用编码器并开启 PID 闭环

### 固件软限位「延迟使能」方案 A — 已搁置（2026-05-08）
- [x] **v1 实施失败并 revert** — `setOneSoftLimit` pending 期间「写真值 + EN=0」组合让 TMC4361A 进入未文档化状态，cmd 集体延迟 5 秒
- [x] **v2 实施失败并 reset** — 改为「pending 期持安全值 INT32_MIN/MAX」，覆盖了 SET_LIM 时已越界场景，但没覆盖 homing 重置 XACTUAL 后已使能限位变成内侧的场景
- [x] **v2 续 enableSoftLimits 智能延迟实施失败并 reset** — homing X 完全卡住，原因不明
- [x] **重置回 commit 8571106**，搁置方案 A — 推测无法继续，需要更精确诊断手段
- [ ] （前置条件）实现 `S:DUMP_REGS` 调试命令，dump TMC4361A REFCONF/STATUS/EVENTS/VIRT_STOP_*/VACTUAL/XACTUAL，配合 debug 固件 + 串口监视器逐步触发
- [ ] （前置条件）查 TMC4361A 数据手册「VIRT_STOP_MODE / VSTOP_EVENT / RAMP_STATE 之间的交互」明确每个 corner case
- [ ] **生产可行方案：保留 Plan B 配置层 workaround**（旧 Squid `configuration_Squid+.ini` 维持 `x_negative=0, y_negative=-0.01`，值需严格 < home 后位置）
- [ ] （独立 bug，可单独修）`Axis::moveRelativeMicrosteps` 在 `STATE_LEAVING_HOME` 状态时不要静默返回 false — 应排队等待 IDLE 或上报错误（避免假 COMPLETED；cmd 29 现象）



<!-- 计划要做但尚未开始的任务 -->

### TMC2240 支持
- [x] TMC2240 驱动层代码实现 (2026-03-16) - TMC2240.h/cpp + HW_Abstraction.h
- [x] MotorControl 层多驱动分发 (2026-03-16) - initDriver/setRunCurrent/enableDriver/configStallGuard
- [x] TMC4361A Cover 接口扩展 40-bit (2026-03-16) - COVER_HIGH+COVER_LOW 5字节路径
- [x] AxisConfig 添加 driverType 字段 (2026-03-16) - 所有轴默认 DRIVER_TMC2660
- [x] 编译验证通过 (2026-03-16) - PlatformIO Teensy 4.1 零错误
- [x] 硬件验证 TMC2240 SPI 通信 (2026-03-24) - Cover 40-bit 通信成功，状态字节 0x99/0xB9
- [x] SPI_OUTPUT_FORMAT 修正 (2026-03-24) - 0x09→0x0D (TMC2130 SPI电流传输模式)
- [x] SCALE_VALUES 修复 (2026-03-24) - TMC2240 之前跳过导致零电流，改为统一配置
- [x] GCONF direct_mode 启用 (2026-03-24) - bit 16 使能 SPI 直接线圈电流控制
- [x] 硬件修改 DRV_ENN 接地 (2026-03-25) - 断开 NFREEZE 连接，DRV_ENN 单独接 GND
- [x] DRV_ENN 修改后验证电机运动 (2026-03-25) - W 轴 forward/backward 正常
- [x] TMC2240 Homing CHOPCONF 损坏修复 (2026-03-25) - shadow register 替代不可靠 Cover 读取
- [x] 驱动芯片自动检测 DRIVER_AUTO (2026-03-25) - 初始化时读 IOIN VERSION=0x40 区分 TMC2240/TMC2660
- [x] S:HWINFO 硬件查询命令 (2026-03-25) - 串口命令 + test_hwinfo.py 脚本
- [ ] 清理 TMC2240 调试代码（Cover40 debug 打印等）
- [ ] TMC2240 StealthChop 参数调优

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
- [x] 手控盒模块移植 (2026-03-03) - joystick.h/cpp + motor_moveToMicrosteps VMAX 修复
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

### 旧 Squid 5mm 短少 bug 定位 (2026-05-08, 配置在旧 Squid 仓库)
- [x] 通过 `~/.local/state/squid/log/main_hcs.log` 时序定位：MOVE 命令在 ~18-20ms 内被报告 COMPLETED（远早于真实运动时间 ~300ms）
- [x] 根因：`configuration_Squid+.ini` `x_negative=5, y_negative=4` mm，Squid 启动 `set_limits` 时 XACTUAL 已在下限以下 → TMC4361A `VSTOPL_ACTIVE` 立即触发
- [x] 固件路径：`Axis::checkLimitPosition()` 读到 VSTOP 标志 → 直接 `completeMovement()` → `_isMoving=false` → status 报 COMPLETED → Squid `wait_till_operation_is_completed` 提前唤醒
- [x] 方案 B 修复：`x_negative=0, y_negative=-0.01`（旧 Squid 配置层），y 必须严格 < 0 因 home 后 Y=0 而 X=64（home_safety_margin）
- [x] 验证：5mm X/Y 来回各多次，每条命令 290-300ms 真实完成时间，累计位移与命令一致

### AXIS_MM_PER_STEP 双源不同步修复 (2026-05-07, develop, commit 7be758d)
- [x] 定位根因：`AXIS_MM_PER_STEP` 硬编码 `*256`，与 `_configure_actuators()` 下发的 `actuator_microstepping` 解耦
- [x] 现象：X 轴 ms=16 时 5mm 命令实际走 80mm（系数 ×16）；推断旧 Squid 0.2mm 现象同源（系数 8/256）
- [x] 修复：`AXIS_MM_PER_STEP` 改为字典推导式从 `AXIS_CONFIG` 派生，单一数据源
- [x] 为 W/E1/E3/E4 补齐 `actuator_screw_pitch_mm` 与 `actuator_microstepping` 字段（与 firmware/config.h 默认一致）
- [x] 移除 `main_window.py` 调试 print
- [x] 验证：ms=16 / ms=256 两种配置下 5mm 物理位移均正确

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
- **固件 VSTOP 早完成行为隐患** (2026-05-08 发现): `Axis::checkLimitPosition()` 检测到 VSTOPL/VSTOPR 立即调 `completeMovement()` 并清 `_isMoving`，导致上位机收到提前的 COMPLETED 状态。当 XACTUAL 在 SET_LIM 时已在限位外（如旧 Squid 启动顺序），任何 MOVE 命令都会被 ~18ms 早完成。**当前方案**：通过配置层下放下限规避（Plan B）。固件方案 A 三次实施都失败，已 reset 到 8571106 搁置（详见「固件软限位延迟使能方案 A」节）
- ~~**⚠ TMC2240 DRV_ENN 硬件问题**~~ (2026-03-25 已解决) — DRV_ENN 已从 NFREEZE 断开并接 GND
- **TMC2240 Cover READ 不可靠**: `SPI_OUTPUT_FORMAT=0x0D` 40-bit auto SPI 响应覆盖 COVER_DRV 寄存器，导致 `tmc2240_fieldWrite` read-modify-write 损坏寄存器。已通过 shadow register 规避，但运行时 TMC2240 寄存器回读均不可信
- **待查: TMC4361A format 0x0A vs 0x0D 方向差异根因** — TMC2240 使用 format 0x0D 时电机方向与 TMC2660 (format 0x0A) 相反，已通过 `REVERSE_MOTOR_DIR` 修复。已确认两芯片线圈约定一致(A=sin,B=cos)、PCB 接线一致，根因在 TMC4361A 内部两种格式的 coil A/B 映射差异，需查 TMC4361A 数据手册 SPI Output Stage 章节确认
- **手控盒按钮极性反转**: `control_panel_teensyLC.ino` 发送 `digitalRead(pin_joystick_btn)`（INPUT_PULLUP: 未按=1, 按下=0），但 `joystick.cpp` 按 `buffer[8] != 0` 判定按下，极性相反。需协商修哪一端（建议 joystick 端发送 `!digitalRead()`）
- **手控盒无 CRC 校验**: `packet[9] = 0`，octoaxes 端也不检查。PacketSerial COBS 帧能防噪声误触发，但数据损坏无法检出

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
