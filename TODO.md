# TODO.md

任务跟踪文件，用于管理项目待办事项。

> 详细重构方案参见：`documents/refactoring-plan.md`

## 进行中

<!-- 当前正在处理的任务，建议同时只有 1-2 个 -->

- [ ] **优化 W 轴换孔时间** - 基准 144ms，目标 ≤ 60ms，当前 61.3ms (ASTART=180, BOW 截断为硬约束)
- [x] **方向感知闸门完整工程化** (2026-05-09, commits 82dfe2d→e773f21→d92fa2d→df4f1f6→17b8f71, 旧 Squid + octoaxes 双端验证通过) - 包括 reject→clamp 兼容旧 Squid、no-op 短路防 5 秒卡顿、homing VSTOP recovery 完整化、边界 margin 防 chip hard-stop latch 四次迭代
- [x] **上位机限位收紧到物理行程** (2026-05-09, commit febc844) - X (-10, 115000) / Y (-10, 76000) μm，与旧 Squid 配置一致便于复现 VSTOP 场景
- [x] **修复旧 Squid 随机点动 X/Y 卡死** (2026-05-08) - axisName ↔ CS 引脚映射与硬件接线反，X 命令实际驱动物理 Y 电机，导致走到 Y 上限时 fw 把 STOPR_EVENT 归到 X 轴卡死。修复 octoaxes.ino 交换 axisName 字符串
- [x] **协助旧 Squid 定位 5mm 短少 bug** (2026-05-08) - VSTOP 早完成根因，`y_negative=-0.01` 修复（旧 Squid 配置层，方案 A 落地后不再必需但保留无副作用）
- [x] **修复 AXIS_MM_PER_STEP 双源不同步** (2026-05-07, commit 7be758d) - 改 actuator_microstepping 后命令距离与实际位移按比例失配（ms=16 时 5mm→80mm），改为从 AXIS_CONFIG 派生
- [x] **XYZW 全部回退为微步模式** (2026-04-17) - `constants.py` has_encoder = False，响应包保持 24 字节与旧 Squid 兼容

## 待办

### 2026-05-10 今日完成 + 待跟踪
- [x] **AF 激光启动常亮 + 关不掉** (2026-05-10) — `MCU_PINS.AF_LASER = pin 15`（旧 Squid `_def.py:158`），fw 缺旧 Squid `init_io()` 的 `digitial_output_pins[]={6,9,10,15}` 显式 OUTPUT+LOW 初始化 → pin 15 处于 INPUT 高阻 + 激光板上拉 → 常亮，且 cmd 41 `digitalWrite` 在 INPUT 模式下不改实际电平 → 关不掉。修复：① `illumination.cpp::illumination_init()` 增加 4 pin OUTPUT+LOW 初始化 ② `commandprocessor.cpp::handleSetPinLevel` 加 `pinMode(OUTPUT)` 防御。**硬件验证通过**。
- [x] **LED 矩阵启动常亮（旧 Squid 也有）** (2026-05-10) — 两个原因叠加：① FastLED.addLeds 缺第 5 模板参数，SPI 默认 24MHz（旧 Squid 1MHz）clear_matrix 推 0 帧可能被 LED 错收；② `initializePowerManagement` 等 PG 信号 + 后续初始化累计数百 ms~5s，APA102 此期间处于上电默认亮态。修复：① FastLED.addLeds 加 `, 1` 设 1MHz 与旧 Squid 一致 ② 拆出 `illumination_init_matrix_early()`，在 `setup()` 最早期（serialProtocol.begin 后）调，多次 show 锁存全 0 帧。
- [x] **开 D 通道时矩阵也亮（双开）** (2026-05-10) — `set_illumination_led_matrix` 与旧 Squid 行为相反：旧 Squid 仅缓存参数，octoaxes 立即点亮且置 `illumination_is_on=true`。上位机启动调此命令预设明场参数 → 矩阵立即亮 → 后续切 D 通道矩阵不被关。修复：与旧 Squid functions.cpp:359-368 对齐，仅缓存，仅 `if (illumination_is_on)` 才刷新点亮。
- [x] **启动卡死（chip 残留态）** (2026-05-10 发现, 2026-05-11 修复，**硬件验证通过**, commit f3fc03f) — Teensy / TMC chip 不断电（用户只重启上位机软件）→ chip 寄存器（XACTUAL / VMAX / RAMPMODE / EVENTS sticky bit）保留上次断电前状态。原 cmd 0 RESET (`Axis::handleReset`) 和 cmd 1 INITIALIZE 都不清 chip。例如 `XACTUAL=1257654` 微步（按 2.54mm pitch / 256 微步算 ≈ **62.4mm，对应 X 或 Y 轴中段工作位置**；原记录写"Z"是笔误，Z pitch=0.3mm 算下来只有 7.4mm 与典型工作位不符）+ SET_LIM `-1067` 微步（≈ -53μm，是 X/Y 下限"home 后负 1 微步"的典型量级）。chip XACTUAL 远偏离 home 后位置 → cmd 29 HOME 启动 motor_setVelocityInternal 后物理位置严重错位 → limit switch 永远不触发，cmd 29 永远不 ack。**短期解**：USB 拔插重启 Teensy。**修复**（2026-05-11）：参考老 Squid `tmc4361A_tmc2660_init` 第一行写 `RESET_REG=0x52535400` 软复位 chip 的做法，将 `CommandProcessor::handleInitialize` 改为重跑 `axisManager.beginAll()`（内部 `motor_initMotionController` 第一行就是 `SW_RESET=0x52535400`，等价于"断电"）+ 逐轴调 `handleReset()` 清 C++ 软件状态机。`Axis::begin` 幂等性已逐项扫描确认（覆盖写、无重复 new/attachInterrupt/SPI.begin）。
- [x] **Z 1μm 精细调节问题** (2026-05-11 解决) — 根因：烧录了错误的固件版本。烧录正确固件后 joystick Z 调节行为符合预期。原排查中收集的数据（pkt_delta=256 microsteps/detent、量化对齐 16）是错版本固件的现象，不再适用。**调试基础设施保留**（`firmware/octoaxes/joystick.cpp` 的 `[FOCUS]` DEBUG_PRINT、`build_opt.h` 的 `DISABLE_BINARY_POS_UPDATE` 编译开关，默认注释），后续如需可启用。
- [x] **老 Squid 切荧光通道（405/561 等）实际开了明场** (2026-05-11 修复，**硬件验证通过**) — 根因：`commandprocessor.cpp::handleTurnOnIllumination` 多了一行 `illumination_source = data[2]`，老 Squid 固件 `main_controller_teensy41.ino:1529` 此处不动 source。老 Squid 上位机的 `turn_on_illumination()` 命令包 cmd[2]=0 → 固件把 source 覆盖成 0 (LED_ARRAY_FULL=明场) → 矩阵被点亮。`set_illumination(11, ...)` 设的 D1 source 被毁。**修复**：删除该行，与老 Squid 对齐。回归风险：octoaxes GUI 不发 cmd 10；`test_illumination.py` 冗余传 source 但先发 SET_ILLUMINATION，行为不变。
- [x] **荧光通道 D1-D5 TTL 输出永远不亮（联锁失效）** (2026-05-11 修复，**硬件验证通过**) — 上一条 fix 后明场不再误亮，但 D1-D5 也都不亮。根因：`config.h:94` `ILLUMINATION_INTERLOCK = pin 2` + `illumination.cpp:52` `INPUT_PULLUP`，无联锁设备时 pin 2 浮空被上拉为 HIGH → `illumination_interlock_ok()` 返回 false → `turn_on_illumination` 的 D1-D5 case 全部跳过 `digitalWrite(HIGH)`，主循环 `octoaxes.ino:159-165` 还每周期把 D1-D5 强拉 LOW。LED 矩阵（明场）走 APA102 SPI 不经联锁所以不受影响。**修复**：① `platformio.ini` 新增 `[env:teensy41_nointerlock]` 加 `-D DISABLE_LASER_INTERLOCK` ② 新增 `firmware/octoaxes/download.sh` 烧录前交互选 safe/nointerlock，参考 joystick `download.sh` 风格。安全的 teensy41 环境保持默认。烧 nointerlock 版本后实测 405/638 等荧光通道正常点亮。
- [x] **XYZ 运动速度基线测试脚本 + 基线收档** (2026-05-11 commit a3d797c + dbc0598) — 新增 `software/tests/benchmark_xyz_speed.py`（760 行），独立 pyserial 脚本，对齐 GUI 启动序列（configure_actuators + widen_soft_limits + HOME + MOVETO 中心 + 人工确认 pause）后跑 X/Y × 6 档 + Z × 3 档（自动跳过半行程不够的大档）× 每方向 10 trial 交替 +/-。**关键设计**：① movement_sign 转换（Z=-1 与 GUI 一致）；② wait_completed 必须先看 IN_PROGRESS 再看连续 5 帧 status=0 防抖；③ MOVE/MOVETO target 乘 sign 转 firmware 坐标系，读回位置乘 sign 转用户坐标。**调试中暴露并修复的 3 个 bug**：缺 configure_actuators 致 firmware 残留 16 微步、漏 movement_sign 致 Z 撞底、残留 SET_LIM 触发 clampTargetByDirection 短路假完成。**基线**（firmware ef05554，归档至 `documents/baselines/benchmark_xyz_20260511_145604.{csv,md}`）：X/Y 几乎完全对称（差 <1ms），10μm=123ms / 100μm=197ms / 1mm=366ms / 5mm=621ms / 10mm=823ms / 30mm=1593ms；Z 比 X/Y 慢约 2 倍（vmax 3 vs 25 mm/s），10μm=187ms / 100μm=348ms / 1mm=697ms。距离 ×3000 耗时只 ×13，小距离的 ~120ms 基线开销是 ramp 加减速时间主导。
- [x] **旧 Squid X 卡死 chip-level lockup**（2026-05-12，硬件实测验证）— 根因：TMC2240 上 X/Y `enableStallSensitivity=true, stallSensitivity=12` 沿用 TMC2660 SG2 参数，TMC2240 用 SG4 算法过敏感 → 正常电流尖峰误判 → `ACTIVE_STALL_F` (STATUS bit 11) latch → 5s timeout → handleError 写 VMAX=0 → `motor_moveToMicrosteps` recovery 不清此 latch → 必须断电拔 USB 复位。**修复**：`axis.cpp:152` 启用处加 `&& _config.driverType != DRIVER_TMC2240` 守卫，TMC2660 保持启用 SG2，TMC2240 跳过；config.h 参数保留。新增 `software/tests/dump_axis_state.py` 卡死现场诊断工具。
- [ ] **TMC2240 StallGuard4 调优 + chip-level latch 恢复**（2026-05-12 提出，**同日深挖后用户主动 reset --hard 89fbd15 暂搁**，下次重做）

    **2026-05-12 深挖收获（下次接手必读，避免重复研究）**：
    1. **chip-level recovery 架构验证**：硬件实测 `S:RECOVERY_STATS` counter Y stall=3 / Z stall=5，证实 motor_moveToMicrosteps 的 ACTIVE_STALL_F 恢复路径**真实被调用**（与 VSTOP recovery 对称设计可行）。**X stall=0**（X 同硬件配置但 SG4 没触发，说明触发频度跟机械/接线相关）
    2. **DRV_AFTER_STALL=1 是必需补丁**（TMC4361A Programming Guide §19）：单独禁 STOP_ON_STALL 不够，chip 的驱动器仍 latched 在 disabled 状态导致电机不动。**A 方案的 motor_setStopOnStall 必须同时操作 bit 26 和 bit 27**
    3. **SG_RESULT 实测数据（致命发现）**：测 X 轴 6 距离档 × 2 方向，`S:READ_SG` 高频采样。**所有距离的 min/p10 都是 0**（即正常运动期间 ~10% 的样本 SG_RESULT 触底）。p50 ≈ 100-200，p90 300-700，max=1023。这说明 SG2/SG4 算法在 ramp 加速/减速段不可靠，**调 SGT 数值解决不了**（SG_RESULT 已到 0，任何非负阈值都触发；负 SGT 等于关 SG）
    4. **真正修复方向 = TCOOLTHRS**：TMC2240 datasheet §7 明确"工作条件 TCOOLTHRS < TSTEP < THIGH"。当前 TCOOLTHRS=0 → SG 在所有速度激活包括不可靠的低速段。需加 `MotorConfig.tcoolThresh` 字段 + 走 `tmc2240_writeRegister(TMC2240_TCOOLTHRS, val)`（起步经验值 400-1000，需实测扫描确认）
    5. **用户体感问题**：临时移除守卫开 SG（测试）→ Y MOVE 不再 lockup 但每次 stall 误触发 5s timeout + recovery 重试，体感「卡顿 1.5-2s/次」**用户拒绝接受**

    **预留资产**（stash 保留）：
    - `stash@{0}` = "DEFERRED: TMC2240 SG tuning Phase A"
    - 包含 firmware `S:READ_SG` 调试命令 + `motor_readSGResult()` helper + `software/tests/measure_sg_result.py` 高频采样脚本
    - 下次接手第一步：`git stash apply stash@{0}` 取回

    **下次接手 6 步流程**：
    1. `git stash apply stash@{0}` 取回 Phase A 工具
    2. MotorConfig 加 `uint32_t tcoolThresh` 字段；axis.cpp begin 中给 X/Y/Z 设合理初值；motor_initDriver_TMC2240 写 TMC2240_TCOOLTHRS
    3. 重做 DRV_AFTER_STALL=1 补丁（参考被 reset 丢弃的逻辑：motor_setStopOnStall enable=false 时同时设 STOP_ON_STALL=0 + DRV_AFTER_STALL=1）
    4. 用 measure_sg_result.py 扫不同 TCOOLTHRS 下的 SG_RESULT，找到「正常运动 p10 > 100」配置
    5. 上层 stall 语义设计：触发时上报上位机决定（撤回 / 接受 / reset）
    6. 移除 axis.cpp:152 TMC2240 守卫，启用 X/Y 碰撞保护
- [ ] **Y homing 异响 + 慢（硬件变更，调试方向重新规划）** (2026-05-12 更新) — **硬件从 TMC2660 切换到 TMC2240** 后之前所有"对齐老 Squid" chopper 思路不适用（老 Squid 不支持 TMC2240）。新方向（按 TMC2240 芯片特性）：① 启用 StealthChop2 PWM 静音模式（octoaxes 当前 `enableStealthChop=false`）—— 理论上能彻底消除 Y homing 慢速异响；② TMC2240 chopper TOFF/HSTRT 重新调优（字段位置与 TMC2660 不同）；③ CURRENT_RANGE 三档（0/1/2 = 1/2/3A）选择审查。
- [x] **2026-05-12 尝试方案 (a) TMC2660 chopper 对齐被中止** — 解码老 Squid `tmc4361A_tmc2660_init` CHOPCONF=0x000900C3 后定位差异：HSTRT 4→0、HEND -2→0；改完编译通过准备烧录时用户澄清硬件已换 TMC2240，回退 axis.cpp 改动。结论：方案 (a) 不适用 TMC2240 硬件
- [ ] **(已废) Y homing 异响 TMC2660 原始记录** (2026-05-11 暂停) — 老 Squid software + 老 Squid firmware 正常；老 Squid software + octoaxes firmware Y homing 异响 + 速度慢。**已尝试方案**：
    1. **D2 电流 RMS 修正**（commit 3c490ed）：`calculateCurrentScale` 公式改 RMS 解读，X/Y/Z 三 TMC2660 轴 RMS 从 0.685/0.685/0.335 A 升到 0.997/0.997/0.494 A。异响**减弱但未消除**。
    2. **HOME 方向 data[3] 解析**（commit 7533516）：firmware 改为按 data[3] 覆盖 homing_direct，GUI/benchmark 按 movement_sign 派生 data[3]。修复了 X home 方向反置 bug（独立问题）。对 Y 异响无帮助。
    3. **HOMING_MICROSTEPPING 同步**（config.h `HOMING_MICROSTEPPING_X/Y = 32` + axis.cpp `configureDriver` 同步 `homingMicrostepping = microstepping`）：让 homing 不再 32↔256 切换。**异响仍在**，已回退。
    4. **STATE_HOMING_INIT 仅在 VSTOP 激活时做 recovery**：避免 `motor_moveToMicrosteps(current)` 引发 VMAX 25→10 突变 + POSITION→VELOCITY mode 切换。未实测（用户决定先搁置），已回退。
    **后续排查方向**（待优先级）：(a) chopper 参数 TOFF/HSTRT/HEND/TBL 与老 Squid 对齐；(b) 方案 E homing 速度联动 vmax（10mm/s → 24mm/s 测试不同速度是否避开共振）；(c) StealthChop/SpreadCycle 切换阈值；(d) 抓串口 DEBUG_PRINT 日志看 homing 启动序列时序。
    **当前位置**：commit 7533516（含 D2 电流 + HOME 方向 fix），其他修改已回退。

### 框架效率优化（2026-05-12 启动新一轮，协议层）
- [x] **移动完成下降沿立即发包** (2026-05-12, commit a6c5786, **硬件实测**) — `send_position_update` 增加 any_moving 下降沿检测，所有轴 moving→idle 那一帧绕过 10ms 心跳节流立即发 COMPLETED；对旧 Squid + GUI + benchmark 三端同时有效。实测 X/Y 短距离命令省 2-7ms（平均 5ms，10μm 122→116ms 省 5%）；Z 几乎无收益（vmax 慢一个量级，已与心跳 phase 对齐）。每完成一次只触发 1 包，流量影响可忽略
- [ ] **STATUS.TARGET_REACHED 替代 XACTUAL==XTARGET 判完**（低难度）— `Axis::checkMovementComplete` 当前比较两个 32-bit 寄存器读，可换成 STATUS bit 单次读取，节省 1 SPI 周期 + 更可靠
- [ ] **TMC4361A Target Pipeline (§9.2)**（中难度）— 当前 ramp 期间预写下一个 XTARGET，避免减速到 0 再加速。扫描/stitching 路径理论提速 20-50%
- [ ] **多轴并行 home**（中难度）— 现 `home_xyz` 串行 X→Y→Z 约 14s，并行可省 2/3 时间
- [ ] **MOVETO_BATCH 批量命令**（高难度）— 扫描场景一次下发 N 个目标点减少串口往返
- [ ] **Look-ahead corner blending**（高难度）— stitching 路径不停顿过拐角

### 运动效率优化（2026-05-09 记录，2026-05-11 第一轮完成）
- [x] **基线测试脚本** (2026-05-11 commit a3d797c) — `software/tests/benchmark_xyz_speed.py`
- [x] **首次基线数据** (2026-05-11) — `documents/baselines/benchmark_xyz_20260511_145604.{csv,md}`
- [x] **VMAX 提升 commit 405efb7** — X/Y 25→30 mm/s，Z 3→3.8 mm/s。30mm 档减 9% (1593→1450ms)，其他档位 < 1% 变化（ramp 主导）
- [x] **AMAX_Z 100 尝试已撤销** — 实测 Z 加速度从 20 提到 100 mm/s² 反而让 Z 1mm 时间从 697→1569ms (+125%)，疑似 motor_adjustBows BOW 算太大 + Z 电机扭矩不足。AMAX_Z 保留 20。
- [x] **benchmark 启动序列对齐 GUI** (commit a257d22 + 9c00d65) — 加 SET_MAX_VELOCITY_ACCELERATION / SET_LIM_SWITCH_POLARITY / SET_HOME_SAFETY_MERGIN，可在两个 firmware 上公平对比
- [x] **octoaxes vs 老 Squid firmware 对比归档** (2026-05-11 commit 9c00d65) — `documents/baselines/comparison_2026-05-11.md`
   - 同参数下小距离 (10μm-1mm) octoaxes 快 3-14ms (3-7%)
   - 同参数下大距离 (5mm-30mm) 老 Squid 快 20-76ms (3-9%)
   - 总体性能相当，差距 < 10%
- [ ] **（暂搁置）大距离 ramp 优化** — octoaxes 30mm 比老 Squid 慢 74ms (5%)，BOW 已 saturate 到 BOWMAX。可能差距来源：`motor_isRunning` 判定方式（octoaxes 用 VACTUAL vs 老 Squid 用 STATUS TARGET_REACHED/RAMP_STATE）、`axis.update()` 状态机间接调用 vs 主 loop 直接判定。要追这 5% 需要 firmware 调试打点测精确延迟。当前接受现状

### 编码器（暂缓，已全部关闭）
- [x] Z 轴编码器基础设施 (2026-04-13) - config.h 常量 + invertEncoderDir + motor_initABNEncoder
- [x] 编码器透明上报方案 (2026-04-13) - getCurrentPositionMicrosteps 按 enableEncoder 切 XACTUAL/ENC_POS，MSG_LENGTH 保持 24
- [x] **Z 轴编码器硬件验证 + 启用** (2026-05-11) — 用户硬件验证编码器读数与 Steps 一致，`software/utils/constants.py` Z 轴 `has_encoder` False → True，GUI 启动自动下发 CONFIGURE_STAGE_PID(Z, flip=True, tpr=3000)，固件 `getCurrentPositionMicrosteps()` 切到 ENC_POS
- [x] **合并 W 轴编码器修复（maxpro → develop）** (2026-05-11 核实，已无需合并) — 2026-03-27 在 maxpro 上做的 W 轴 ABN 编码器 4 个 commit（f986305 / 4d3f36d / 94e5911 / c09ae84）均位于两分支共同祖先 47570ae 之前，已在 develop 内。maxpro 领先 develop 的 12 个 commit 全部是 `octoaxesplus`（双相机变体）独立工程，与 W 轴编码器无关
- [ ] （暂缓）X/Y 轴编码器验证 + 启用（硬件已布编码器，参数未验证）
- [ ] （暂缓）开启 PID 闭环（当前仅开环 + ENC_POS 上报，未启用 enableStagePID）

### 固件软限位方向感知闸门方案 A — 已落地（2026-05-09，五次迭代）
- [x] **v1/v2/v2 续三次失败** (2026-05-08) — 都试图改 SET_LIM 寄存器时序，触及 TMC4361A 未文档化边界行为
- [x] **方向感知闸门 reject 语义** (2026-05-09 commit 82dfe2d) — `isMoveAllowedByDirection`，越界 target 拒绝命令
- [x] **改为 clamp 语义兼容旧 Squid** (2026-05-09 commit e773f21) — `clampTargetByDirection`，旧 Squid 上位机不可改，固件兜底把越界 target 截到边界
- [x] **clamp 后 target == current 短路** (2026-05-09 commit d92fa2d) — 防 _isMoving 误设致上位机卡 5 秒等 timeout
- [x] **homing VSTOP recovery 完整化** (2026-05-09 commit df4f1f6) — STATE_HOMING_INIT 调 motor_moveToMicrosteps(current) 复用已验证的完整解锁路径，解决固件烧写后 X=0 + SET_LIM 触发 VSTOPL hard-stop 后 X home 卡死
- [x] **边界 margin 防 chip hard-stop latch** (2026-05-09 commit 17b8f71) — target 紧贴 VIRT_STOP 边界 1 微步会让 chip ramp 减速精度跨界触发 VSTOP_ACTIVE，进入永久 latched 状态（必须断电恢复）；clamp 在安全区时强制 target 离开边界至少 100 微步
- [x] **测试脚本** (2026-05-09) — `software/tests/test_homing_with_vstop_latch.py` 复现「X=0 + SET_LIM x_neg + HOME_X」启动卡死场景
- [x] 双端硬件验证通过：octoaxes GUI + 旧 Squid software 启动序列、MOVE/MOVETO 各种越界场景
- [x] **`Axis::moveRelativeMicrosteps` 静默 reject bug** (2026-05-12, commit 475b9fe, **硬件实测验证**) — 复现脚本（test_silent_reject_repro.py）证实 cmd 22 撞 cmd 21 mid-flight 时 silent reject 累计位移 20mm + 假 COMPLETED 误报；按方案 D 仿老 Squid（main_controller_teensy41.ino:845）：STATE_MOVING 时不再 reject 而覆盖 chip XTARGET，chip ramp generator 平滑切换；STATE_HOMING_* 仍 reject 但加 DEBUG_PRINT。修复后 cmd 22 真实驱动电机，最终位移 = mid_position + delta（与老 Squid 行为一致）



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
- [x] 去掉 StepAxis homing debug 打印 (2026-05-12, commit e127a18) - 删除 SEARCH 周期性 dump、Before/After stop、Latched position、safePos 公式展开、homing_direct/speedInternal、Moving to safe position 进度刷新；保留状态转换 + Timeout/PID 关键打印
- [x] 去掉 FilterWheel homing debug 打印 (2026-05-12, commit e127a18) - 删除 HOMING_INIT limit_state dump
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

### 软限位方向感知闸门方案 A 完整落地 (2026-05-09, develop, 五次迭代)
- [x] **设计原则**：越界后只允许电机朝更安全方向移动，禁止朝更深越界方向移动；把限位检查从 chip 硬件层上移到 Axis 协议层
- [x] **核心规则**：`effective_lower = (C ≤ L) ? C : (L+margin); effective_upper = (C ≥ R) ? C : (R-margin); T ∈ [effective_lower, effective_upper]`
- [x] **commit 82dfe2d 初版（reject 语义）**：`Axis::SoftLimitShadow` + `isMoveAllowedByDirection`，越界 target 拒绝命令
- [x] **commit e773f21 (clamp 语义)**：`clampTargetByDirection` 替换 reject，兼容旧 Squid 上位机（不可改）；与旧 Squid `callback_move_x/y/z` 内的 min/max clamp 行为对齐
- [x] **commit d92fa2d (no-op 短路)**：clamp 后 target == current 直接 return true，跳过 motor + startMovement，避免 _isMoving 误设导致上位机硬等 5 秒 timeout
- [x] **commit df4f1f6 (homing VSTOP 解锁)**：StepAxis::performHomingSequence 在 STATE_HOMING_INIT 调 `motor_moveToMicrosteps(_icID, current_xactual)` 复用已验证的完整 VSTOP recovery 路径解锁 chip hard-stop latch（解决固件烧写后 chip XACTUAL=0 + SET_LIM 把限位设在 0 之外触发 VSTOPL_ACTIVE 后 X home 永远不动）
- [x] **commit 17b8f71 (边界 margin 防 chip hard-stop latch)**：target 紧贴 VIRT_STOP 边界 1 微步会触发 chip ramp 内部 latch，**所有后续 MOVE 都启动不了 ramp，必须断电复位**；clamp 在安全区时强制 target 离开边界至少 100 微步（≈80μm，远低于显示精度）
- [x] **避开三次失败的雷区**：完全不动 SET_LIM 寄存器写入策略，也不改 enableSoftLimits 时序
- [x] **双端验证通过**：octoaxes GUI + 旧 Squid software 启动序列、MOVE/MOVETO 各种越界场景、X home 解锁、target 紧贴边界场景
- [x] **TODO「固件 VSTOP 早完成行为隐患」阻塞项消除**
- [x] **测试脚本**：`software/tests/test_homing_with_vstop_latch.py`

### 上位机限位收紧到物理行程 (2026-05-09, develop, commit febc844)
- [x] X 限位 `(-80000, 80000)` → `(-10, 115000)` μm；Y `(-120000, 120000)` → `(-10, 76000)` μm
- [x] 下限设 -10（而非 0）：home 后 XACTUAL=0，下限严格 < 0 才不会让 chip 在 SET_LIM 时立即触发 VSTOPL_ACTIVE_F
- [x] 与旧 Squid 配置思路一致，便于本项目 software 复现 VSTOP 场景

### 旧 Squid 随机点动 X/Y 卡死根因定位与修复 (2026-05-08)
- [x] **症状**：随机点动测试中 X 或 Y 概率性单轴卡死，位置冻结、互不影响、fw 通信正常、必须断电恢复
- [x] **排查路径**：先怀疑 StallGuard → 关闭 SG 仍卡死，排除；加 `S:DUMPREGS [axisName]` 调试命令（serial.cpp）准备抓现场；用户提示核对 firmware 内部 axis 索引差异
- [x] **决定性证据**：旧 Squid `firmware/controller/src/def/def_v1.h:11-21` 注释明确 `Internal: x=1, y=0`，`pin_TMC4361_CS[]={41, 36, ...}` 实际是 `[0]=Y(CS=41), [1]=X(CS=36)`。Octoaxes firmware 把 `Pins::X_AXIS_CS=41/Y_AXIS_CS=36` 与硬件接线**完全反置**
- [x] **bug 链**：用户硬件按旧 Squid 接线（X 接 CS=36），octoaxes fw 把 CS=41 当作 X axis → 旧 Squid 发 MOVE_X 实际驱动物理 Y 电机 → 走到 Y 物理上限 76mm 时 CS=41 chip 收 STOPR_EVENT → fw 归到「X axis」卡死。卡死位置 79.9mm ≈ Y 上限 76mm（数值耦合证实）
- [x] **修复**：`firmware/octoaxes/octoaxes.ino:86-87` 交换 axisName 字符串与 CS 引脚的对应：`xAxis = StepAxis(Pins::Y_AXIS_CS=36, 0, "X")`、`yAxis = StepAxis(Pins::X_AXIS_CS=41, 1, "Y")`。**协议字节零变化**，AxisConfigs::X_AXIS/Y_AXIS 物理参数通过 axisName 匹配自动映射到正确 chip
- [x] 恢复 X/Y `enableStallSensitivity = true`（SG 临时关闭撤销）
- [x] 保留 `serial.cpp` 的 `S:DUMPREGS [axisName]` 调试命令，未来卡死现场取证用
- [x] `config.h` PIN_CS_X/Y 常量加注释说明命名为 PCB 引脚号历史遗留
- [x] 编译通过 (teensy41 / teensy41_debug)

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
- ~~**固件 VSTOP 早完成行为隐患**~~ (2026-05-08 发现, 2026-05-09 修复 commit 82dfe2d) — 方案 A 方向感知闸门已上线，`checkLimitPosition()` 不再因 VSTOP_ACTIVE 提前 completeMovement；上层 isMoveAllowedByDirection 闸门保证 in-progress move 朝安全方向
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
