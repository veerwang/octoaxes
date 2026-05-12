# SESSION.md

会话记录文件，用于跨会话保持上下文连续性。

---

## 最新会话

**日期**: 2026-05-12
**分支**: develop
**位置**: 文档同步 + Homing 调试打印清理

### 本次完成

#### 1. 提交 Z 轴编码器启用文档（commit 8a7c312）

把上次会话未提交的 SESSION.md / TODO.md 改动提交：Z 编码器硬件验证通过后
`software/utils/constants.py` Z 轴 `has_encoder: False → True`、TODO 对应任务
从「暂缓」改为完成，补充 X/Y 编码器与 PID 闭环两个暂缓子项（实际代码改动
在 cf2eef0 之前已合入）。

#### 2. W 轴 LEFT_SW → RGHT_SW 修正讨论（暂不动）

阅读 `config.h:368-403` W_AXIS 段、`stepaxis.cpp:220-236` safePosition 计算、
`filterwheel.cpp:250` 离开感应区方向逻辑后，向用户解释：当前 `homingSwitch =
LEFT_SW` 与硬件实际是 RIGHT 端开关不匹配；改动会涉及 `enableLeft/Right
LimitSwitch` 翻转 + `rightSwitchPolarity` / `rightFlipped` 配置，需要硬件实测
（一旦极性配错 W home 会撞死或永远找不到限位）。用户决定**暂不修改**，W 轴
60ms 换孔时间已优化稳定，"暂不影响功能" 状态保留。任务条目仍在 TODO「阻塞/
问题」section。

#### 3. 清理 StepAxis/FilterWheel homing 调试打印（commit e127a18 + 16acedf）

按 B 方案（中度清理）执行：

**stepaxis.cpp 删除**（-73 行）：
- SEARCH 周期性 dump（200ms 一次的 `limit_state / STATUS / XACTUAL / VACTUAL`，
  2026-02-25 调 SOFT_STOP_EN bug 时加的）
- Before stop / After stop XACTUAL+STATUS dump
- Latched position 三行打印
- RGHT_SW / LEFT_SW safePos = latched ± margin 公式展开（保留 if/else 数学
  分支，仅删内嵌 print）
- homing_direct / homingVelocityMM / speedInternal 启动 dump
- STATE_HOMING_SET_ZERO 500ms 进度刷新整块（包括外层 else 分支）

**filterwheel.cpp 删除**（-3 行）：
- HOMING_INIT limit_state dump

**保留**（约 10 处）：Starting homing、Already at home、Home limit switch
triggered、Moving to safe position（单行）、Homing completed、各类 Timeout
错误、PID re-enabled、Left home position。

`DEBUG_PRINT` 在生产构建（`pio run -e teensy41`，无 `-D DEBUG`）展开为空，
**生产 FLASH 持平 72060 字节**，本次纯代码可读性清理。两个环境均编译通过。

TODO.md 对应两条 `[ ]` 任务标记 `[x]` 完成。

#### 4. `Axis::moveRelativeMicrosteps` 静默 reject bug 复现 + 修复（commit d103a71 + 475b9fe + 1601cce，硬件实测）

SESSION.md 2026-05-08 的 推测 bug 实证 + 用方案 D（仿老 Squid）修复。

**复现脚本**（`software/tests/test_silent_reject_repro.py`，530 行）：
- 完整测试环境初始化：configure (pitch/microstepping/vmax/polarity/margin) + X home（清 chip VSTOP latch / EVENTS sticky）+ 软限位放宽到 (-50, 150)mm
- 控制组：两条 MOVE 串行 → 累计 25mm（baseline）
- 实验组：cmd 21 MOVE +20mm 发出 200ms 后（mid-flight）发 cmd 22 MOVE +5mm

**实测结果（修复前 firmware ef05554）**：
- 控制组 25mm ✓
- 实验组 **累计 20mm**（cmd 22 被 silent reject），cmd 22 在 884ms 内报 COMPLETED 假信号
  - cmd 22 响应轨迹 84 帧 IN_PROGRESS → COMPLETED 时间精确对应 cmd 21 真实完成时间
  - 电机从未为 cmd 22 移动哪怕 1 微步
  - 上位机视角看完全正常 → 最坑的"完美伪装"

**根因（axis.cpp:573-575）**：
```cpp
if (_currentState != STATE_IDLE) {
    return false;  // 静默 reject
}
```
配合 serial.cpp:102（cmd_id 全局已被刷新）+ serial.cpp:219（status = any_moving ? IN_PROGRESS : COMPLETED）→ 假信号成立。

**修复方案 D 仿老 Squid**（参考 `main_controller_teensy41.ino:845-857` MOVE_X handler 无忙检查，直接 `tmc4361A_moveTo` 覆盖）：

`axis.cpp` 两处条件改为：
```cpp
if (_currentState != STATE_IDLE && _currentState != STATE_MOVING) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Move rejected: Axis is homing, current state: ");
    DEBUG_PRINTLN(_currentState);
    return false;
}
```

- **STATE_IDLE / STATE_MOVING**：走完整 soft-limit + clamp + `motor_moveToMicrosteps` 路径，chip ramp generator 平滑切换 XTARGET，`startMovement()` 重调刷新 `_moveStartMicros`（重置 timeout 窗口）
- **STATE_HOMING_INIT/SEARCH/SET_ZERO/LEAVING_HOME**：仍 reject 但加 DEBUG_PRINT 不再静默。homing 期 chip 在 velocity 模式，覆盖会破坏 homing 序列

**修复后实测**：
- 控制组 25mm ✓（IDLE 路径未受影响）
- 实验组 **累计 6.11mm**（cmd 22 在 mid-flight=61mm 时重瞄准 66mm，chip 平滑切换，最终到达 66mm；cmd 22 在 427ms 报 COMPLETED 即真实减速到新 target 的时间）
- **行为与老 Squid 完全一致**：相对位移基于命令到达时 chip 当前位置，不是叠加上一条命令的目标

生产 FLASH 持平 72060 字节。

#### 5. Y homing 异响 TMC2660 chopper 对齐方案中止 + 硬件已切到 TMC2240

按 SESSION.md 2026-05-11 列的后续方向 (a) 入手：找老 Squid `tmc4361A_tmc2660_init`
CHOPCONF=0x000900C3 解码 (HSTRT=0, HEND=0, TOFF=3, TBL=2)，对比 octoaxes
`motor_initDriver_TMC2660` 的 (HSTRT=4, HEND=-2, TOFF=3, TBL=2)。差异定位
**仅在 HSTRT/HEND 两个 hysteresis 参数**——老 Squid 用零 hysteresis 静音
配置，octoaxes 用高扭矩配置（低速更吵）。

修改 `axis.cpp` 两处硬编码 HSTRT=4→0、HEND=-2→0 编译通过 FLASH 持平 72060。
准备烧录时用户澄清：**当前硬件已全部更换为 TMC2240**（之前 SESSION.md 描述
的 X/Y/Z = TMC2660 是更早一个硬件单元；本次会话之前的 benchmark/速度基线
归档都是那台 TMC2660 机器）。TMC2660 chopper 调优思路不适用于 TMC2240 硬件，
回退 axis.cpp 改动（`git checkout`）。

`S:HWINFO` 验证当前硬件：Y/Z 报 TMC2240（W 报 UNKNOWN 是 TMC2240 Cover READ
不可靠的已知问题，见 TODO 阻塞项；X 行被解析丢失但今天 silent reject 测试
中 X 正常运动，芯片可信）。

#### 6. 首次 TMC2240 速度基线 + TMC2240 vs TMC2660 对比（commit f3019f2）

跑 `software/tests/benchmark_xyz_speed.py --yes`（脚本无需修改，firmware
DRIVER_AUTO 自动适配）：

**TMC2240 baseline XYZ**（vmax 30/30/3.8 mm/s, accel 500/500/20 mm/s²）：

| 距离 | X (ms) | Y (ms) | Z (ms) |
|---|---|---|---|
| 10μm | 122.5 | 122.6 | 187.4 |
| 100μm | 197.0 | 197.3 | 347.0 |
| 1mm | 366.4 | 365.9 | 697.3 |
| 5mm | 620.2 | 619.8 | skip |
| 10mm | 830.0 | 828.6 | skip |
| 30mm | 1453.6 | 1455.5 | skip |

**对比 TMC2660 baseline**（2026-05-11 17:24，同 firmware vmax/accel/microstepping）：
全档位差异 **< 1%**（最大差 +5ms @ 30mm，约 0.4%）。

**原因**：运动 ramp 由 **TMC4361A 内置 generator** 控制，TMC2660/2240 仅接收
step/dir 或 SPI 线圈电流，对 ramp 行为无影响。芯片差异体现在噪声/扭矩/电流
精度，不在速度上。

归档：
- `documents/baselines/benchmark_xyz_tmc2240_20260512_141441.{csv,md}` TMC2240 首次基线
- `documents/baselines/comparison_tmc2240_vs_tmc2660_20260512.md` 详细对比 + 后续调优方向

#### 7. 旧 Squid X 卡死根因定位 + StallGuard TMC2240 跳过（硬件实测）

**用户报告**：旧 Squid 启动后移动几次 X/Y → X 卡死（Y 正常），octoaxes software 也无法
恢复 X，必须断电拔 USB 才能复位。

**新增诊断工具** `software/tests/dump_axis_state.py`：
- 用 `S:DUMPREGS [axis]` 调试命令打 TMC4361A 关键寄存器
- 自动按 `TMC4361A_HW_Abstraction.h` 解码 STATUS / EVENTS bit 位含义
- 自动诊断 latch 类型（VSTOP / STALL / HOME_ERROR / 软件状态异常）

**X 卡死现场抓取**（用户用旧 Squid 触发卡死后跑 dump）：

```
STATUS=0xC1001804:
  bit 11 ACTIVE_STALL_F   latched ⚠⚠  ← 根因
  bit 12 HOME_ERROR_F     latched ⚠
VMAX=0                                  ← handleError → motor_stop 清零
state=6 (ERROR)
XACTUAL=119207 != XTARGET=139364        ← 上次 move 未到目标 1mm
```

（最初解码用了错误的 bit 位置 11/12，正确解码后发现是 `ACTIVE_STALL_F` bit 11
而非 `VSTOPL_ACTIVE_F` bit 9——dump 脚本 STATUS_BITS 已修正）

**根因链**：
1. X 配置 `enableStallSensitivity=true, stallSensitivity=12`，这套参数为 TMC2660
   StallGuard2 调优
2. TMC2240 用 **StallGuard4**（完全不同算法），同样 SGT=12 在 TMC2240 上变得过敏感
3. 正常运动的电流尖峰被 SG4 误判为 stall
4. TMC4361A `REFCONF` 的 `STOP_ON_STALL` (bit 26) 启用 → chip 停车 + ACTIVE_STALL_F latch
5. Axis 等 5s 超时 → `handleError("Movement timeout")` → `smoothStop` → `motor_stop` 写 VMAX=0
6. 软件 state=ERROR；下次 MOVE 触发 handleReset 恢复 VMAX，但
   `motor_moveToMicrosteps` 的 recovery 路径**只检查 VSTOPL/R_ACTIVE_F (bit 9/10)**，
   完全不清 ACTIVE_STALL_F (bit 11) latch
7. chip 拒绝启动新 ramp → 再次 5s timeout → 死循环
8. **必须断电让 chip 重新上电才能清 latch**

**为何 TMC2660 不会卡死**：StallGuard2 算法更保守，SGT=12 是「需较大负载触发」的设置；
TMC2660 + 同参数实测稳定无误触发，所以历史上 TMC2660 平台从未暴露这个 chip-level
latch 恢复缺陷。

**修复方案 — 按驱动芯片分离参数**：

不是简单关闭 StallGuard，而是按 chip 类型分支。`config.h` 中 X/Y 的
`enableStallSensitivity=true, stallSensitivity=12` 参数**保持不变**（TMC2660 baseline），
在 `axis.cpp:152` 启用处加守卫：

```cpp
if (_config.enableStallSensitivity && _config.driverType != DRIVER_TMC2240)
    motor_configStallGuard(...);
```

- **TMC2660**：照常启用 StallGuard2，碰撞保护正常工作
- **TMC2240**：跳过启用（保留参数 / 注释中标注 TMC2240 暂不启用的原因），
  等未来 SG4 调优完成 + chip-level latch 恢复修复后移除守卫

**硬件实测验证**：
- 重新烧录后 dump X/Y 均「完全 idle 无 latch」✓
- 用户用旧 Squid 反复 jog X/Y 不再卡死 ✓
- 生产 FLASH 不变

**取舍**：TMC2240 上 X/Y 当前**没有碰撞保护**（撞到东西不会自动停车）。等
StallGuard4 调优后恢复。Z 配置本就 `enableStallSensitivity=false`，无影响。

### 下次继续

**TMC2240 StallGuard4 调优 + chip-level latch 恢复修复**（中等优先）：
1. 调研 TMC2240 datasheet 中 SG4_THRS / SGT / SEMIN / SEMAX 参数语义，实测找到
   既不误触发又能检测真实 stall 的阈值
2. 修 `motor_moveToMicrosteps` recovery：同时清 STATUS 中 ACTIVE_STALL_F / HOME_ERROR_F
   latch（写 EVENTS 触发 / 暂时禁 STOP_ON_STALL / SW_RESET ramp generator）
3. 上层设计 stall 处理语义：触发时上报上位机由操作员决定（撤回 / 接受 / reset），
   不让 chip 默默死锁

**Y homing 异响（TMC2240 视角重新规划）**：之前 4 次失败的方向 + 今天 TMC2660 chopper
对齐方案都不适用于 TMC2240，需要从 TMC2240 芯片特性入手：
1. **启用 StealthChop2** — TMC2240 独有的 PWM 静音模式，专门为低速静音设计，
   理论上能彻底消除 Y homing 异响。octoaxes 当前 `enableStealthChop=false`。
2. **TMC2240 chopper 参数从默认值调** — TOFF/HSTRT 字段位置与 TMC2660 不同
3. **CURRENT_RANGE 审查** — TMC2240 三档（0/1/2 = 1/2/3 A 全量程）选择

**其他**：

1. **W 轴换孔时间优化（61.3ms → ≤60ms）**（高难度，ASTART 已到 BOW 截断硬约束）
2. **清理 TMC2240 Cover40 debug 打印**（中等优先）
3. **修正 W 轴 config.h LEFT_SW → RGHT_SW + 极性**（需硬件实测，暂搁置）
4. **XYZ 大距离 5% ramp 差距**（需 firmware 调试打点，TMC2240 上待重测确认）

---

## 历史记录

<!-- 保留最近 3-5 次会话记录，太旧的可以删除 -->

### 2026-05-11 - 荧光通道点不亮双重根因修复 + 联锁禁用烧录脚本 + XYZ 速度基线 + Z 编码器启用

### 本次完成

#### 1. Joystick Z 1μm 精细调节"问题"澄清（无代码改动）

- 现象：用户期望 joystick 拨 Z 每 detent 1μm 跳变，实际 10+μm。
- 真相：是用户烧错了固件版本。烧上正确固件后 Z 调节行为符合预期。
- 此前推断的 `pkt_delta=256 microsteps/detent` + 量化对齐 16 是错版本固件的现象，不再适用。
- 调试基础设施（`joystick.cpp` 的 `[FOCUS]` DEBUG_PRINT、`build_opt.h` 的
  `DISABLE_BINARY_POS_UPDATE` 编译开关，默认注释）保留，未来排查直接启用。
- TODO.md 对应条目已标记 (2026-05-11 解决)。

#### 2. 老 Squid 切荧光通道（405/561/638…）实际开了明场（commit 待提交）

**症状**：在老 Squid 上位机选 405/488/561/638/730 任一荧光通道后开灯，发现实际亮的是明场（LED 矩阵），而对应的 D1-D5 TTL 端口不通电。

**根因**：`firmware/octoaxes/commandprocessor.cpp:191-194` 的 `handleTurnOnIllumination` 比老 Squid 固件多了一行 `illumination_source = data[2]`。

对比老 Squid 固件 `main_controller_teensy41.ino:1529-1535`：

```cpp
case TURN_ON_ILLUMINATION:
{
  turn_on_illumination();   // 不动 illumination_source
  break;
}
```

老 Squid 上位机 `microcontroller.py:582` 发 `turn_on_illumination()` 命令包内 `cmd[2]=0`（未设此字段）。octoaxes 固件读 `data[2]=0` 当作 source 写回 → `illumination_source = 0` = `LED_ARRAY_FULL` = 明场。前置 `set_illumination(11, intensity)` 设的 D1 source 被毁，turn_on 走 LED_ARRAY 分支点亮矩阵。

**修复**：删除 `illumination_source = data[2]` 那一行，与老 Squid 固件对齐。cmd 10 只做"打开"动作，source 由前置 `set_illumination(source, intensity)` 维护。

**回归风险评估**：
- octoaxes GUI 不发 cmd 10（grep 确认），不影响。
- `software/tests/test_illumination.py` 冗余地在 cmd 10 包里塞 source，但每个测试都先发 SET_ILLUMINATION 设置 source，行为不变。

#### 3. 荧光通道 D1-D5 TTL 输出永远不亮（联锁失效）（commit 待提交）

**症状**：上一条 fix 落地后，明场不再误亮，但用户报告 D1-D5 荧光通道也都不亮——切 405/638 没有任何输出。

**根因链**：
1. `config.h:94` `ILLUMINATION_INTERLOCK = pin 2`
2. `illumination.cpp:52` `pinMode(ILLUMINATION_INTERLOCK, INPUT_PULLUP)`
3. 这台机器没接激光联锁信号 → pin 2 浮空 → 被内部上拉到 HIGH
4. `illumination.cpp:99-106` `illumination_interlock_ok()` 要求 `digitalRead == LOW` 才返回 true → 现状返回 false
5. `illumination.cpp:343-364` `turn_on_illumination()` 的 D1-D5 case 全部 gated by `illumination_interlock_ok()` → `digitalWrite(HIGH)` 全部跳过
6. 更糟：`octoaxes.ino:158-165` 主循环每个周期都主动把 D1-D5 强拉 LOW
7. LED 矩阵（明场）走 APA102 SPI，**不经联锁检查** → 所以明场能正常控制——这也解释了上一条 bug 为什么"明场总是亮"

**修复**（参考 `firmware/joystick/download.sh` 风格）：

①  `firmware/octoaxes/platformio.ini` 新增环境：
```ini
[env:teensy41_nointerlock]
extends = env:teensy41
build_flags =
    ${env:teensy41.build_flags}
    -D DISABLE_LASER_INTERLOCK
```
默认 `teensy41` 环境保持联锁启用（出厂安全默认）。

②  新增 `firmware/octoaxes/download.sh`（已 chmod +x）：
```bash
./download.sh                # 交互选择
./download.sh safe           # 启用联锁（默认）
./download.sh nointerlock    # 禁用联锁（本工位用）
```

`DISABLE_LASER_INTERLOCK` 是编译期 `#ifdef`，让 `illumination_interlock_ok()` 直接 `return true`，零运行时开销。FLASH 由 72124 → 72060（-64 字节）确认优化生效。

**硬件验证**：烧 nointerlock 版本后，老 Squid 切 405/638 等荧光通道实测正常点亮，对应 TTL 端口拉高，DAC 输出按 intensity 设置 ✓。

#### 4. 启动卡死（chip 残留态）修复（commit f3fc03f + ef05554，硬件验证通过）

**症状**：Teensy/TMC chip 不断电只重启上位机 → chip 寄存器（XACTUAL/VMAX/EVENTS）保留旧值 → cmd 9 SET_LIM 对照旧 XACTUAL 立即触发 VSTOP latch → cmd 29 HOME 物理位置严重错位、limit switch 永不触发，必须拔 USB 才能恢复。

**根因**：原 `handleInitialize` 注释"TMC 轴已在 setup 中初始化，不重复"，只重置 DAC+trigger，完全不动 chip。对比老 Squid `tmc4361A_tmc2660_init` 第一行 `writeInt(RESET_REG, 0x52535400)` 做 chip 软复位，再重写全部配置——这才是"等价于断电再上电"的语义。

octoaxes 同样的 SW_RESET 调用在 `motor_initMotionController` (MotorControl.cpp:281)，但只有 setup() 路径走到，cmd 1 不再触发。

**修复**：`handleInitialize` 改为重跑 `axisManager.beginAll()`（内部 motor_initMotionController 第一行 SW_RESET）+ 逐轴 `handleReset()` 清 C++ 软件状态机。`Axis::begin` 幂等性已扫描确认（覆盖写、无重复 new/SPI.begin/attachInterrupt）。

**TODO 笔误修正**：把记录的"Z XACTUAL=1257654"改为"X/Y 中段位置"——按 2.54mm pitch / 256 微步换算 1257654 μstep ≈ 62.4mm，是 X/Y 工作区中段；Z pitch=0.3mm 算出 7.4mm 与典型工作位不符。

#### 5. XYZ 运动速度基线测试脚本（commit a3d797c，基线收档）

新增 `software/tests/benchmark_xyz_speed.py`（760 行）+ baseline 报告归档到 `documents/baselines/benchmark_xyz_20260511_145604.{csv,md}`。

**执行流程**：
- [0a] configure_actuators —— SET_LEAD_SCREW_PITCH + CONFIGURE_STEPPER_DRIVER 对齐 GUI startup
- [0b] widen_soft_limits —— ±100mm 等效微步消除残留 SET_LIM 干扰
- [1]  home_all_axes —— X/Y/Z 顺序 HOME
- [2]  move_to_center —— 顺序 Y→X→Z（避开上下料装置）
- [2.5] 人工确认 pause（--yes 跳过）
- [3]  benchmark —— for axis × dist × 10 trial × 2 direction 交替 +d/-d
- [4]  写 CSV + Markdown

**关键设计**：
- movement_sign 转换 ↔ GUI: Z sign=-1，MOVE/MOVETO target 乘 sign 转 firmware 坐标系
- wait_completed 防抖：必须先看到 IN_PROGRESS（status=1）才信任后续 status=0，且连续 5 帧 idle 才确认完成
- fits_in_travel 自动跳过超半行程档位（Z 5/10/30mm 自动 skip）
- 测试范围用户指定：X 10-112mm / Y 6-76mm / Z 0.1-6.5mm

**调试中发现的关键 bug**（已解决）：
1. 缺 configure_actuators → firmware 残留 16 微步 → 全轴单位换算错乱 → 走 25% 就停
2. 漏 movement_sign 转换 → Z+3300μm 实际走 firmware 负方向撞底
3. 残留 SET_LIM 触发 clampTargetByDirection 短路 → 假 COMPLETED

**完整基线（10 trials × 6 distances，总耗时 ~3 分钟）**：

| 距离 | X (ms) | Y (ms) | Z (ms) |
|---|---|---|---|
| 10μm | 122.8 | 123.2 | 187.5 |
| 100μm | 197.1 | 197.0 | 348.2 |
| 1mm | 366.1 | 366.0 | 697.1 |
| 5mm | 620.9 | 621.2 | skip |
| 10mm | 823.1 | 822.7 | skip |
| 30mm | 1592.7 | 1592.7 | skip |

- X/Y 几乎完全对称（差 <1ms）
- Z 比 X/Y 慢约 1.5-2 倍（vmax 3 vs 25 mm/s）
- +/- 方向 mean 差 <3ms 高度对称
- 距离从 10μm → 30mm（×3000），耗时只增 ×13—— ramp 加减速时间占小距离的主导

`.gitignore` 加 `software/tests/results/`，原始输出不入库；归档版在 `documents/baselines/`。

#### 6. 老 Squid software 兼容性深挖（commit 3c490ed + 7533516）

用老 Squid software + octoaxes firmware 时 Y homing 异响 + 速度慢，单独问题独立修复：

**6.1 电流 RMS 公式修正（commit 3c490ed）**：老 Squid software `current_rms` 字段意图是 RMS，老 Squid firmware 公式 `(RMS_mA/1000) × R_sense / 0.2298 × 31` 按 RMS 处理；octoaxes firmware `calculateCurrentScale` 当 PEAK 处理 → 实际 RMS 低 30%（X/Y 0.685A 而非 0.997A）。修正公式 `CS = RMS_A × R × 32 × √2 / 0.310 - 1`，影响 X/Y/Z 三 TMC2660 轴（W TMC2240 不受影响）。修后 X/Y/Z RMS 0.997/0.997/0.494 A。**实测异响减弱但未完全消除**。

**6.2 HOME data[3] 解析（commit 7533516）**：老 Squid firmware 按 data[3] 决定 home 方向，octoaxes 之前忽略仅用 config.homing_direct。GUI/benchmark 改为按 `movement_sign` 派生 data[3]（X/Y=1, Z=0），firmware 按 data[3] 覆盖 config.homing_direct。修复 X home 方向反置（独立 bug，对异响无帮助）。

**6.3 Y homing 异响后续 4 次尝试均失败，搁置**：HOMING_MICROSTEPPING 同步、VSTOP recovery 条件化、配置回退测试都无效。详见 TODO.md 对应条目。

#### 7. XYZ 速度优化第一轮（commit 405efb7 + a257d22 + 9c00d65 + 52d9f92）

**7.1 VMAX 优化（commit 405efb7）**：对齐老 Squid HCS v2 配置 `max_velocity_x/y/z_mm = 30/30/3.8`（octoaxes 之前 25/25/3）。30mm 大距离 benchmark 减 9% (1593→1450ms)，其他档位 <1% 变化（ramp 主导）。

**7.2 AMAX_Z 100 撤销**：老 Squid HCS v2 `max_acceleration_z_mm = 100`（octoaxes 20）。实测把 Z 加速度提到 100 反而让 Z 1mm 时间 697→1569ms（+125%），疑似 BOW 自动算太大 + Z 电机扭矩不足。撤销，AMAX_Z 保留 20。

**7.3 benchmark 启动序列对齐 GUI（commit a257d22 + 9c00d65）**：
- 加 SET_MAX_VELOCITY_ACCELERATION（cmd 22）下发 vmax/accel
- 加 SET_LIM_SWITCH_POLARITY（cmd 20）—— **关键**：老 Squid firmware 默认 polarity=0 但 ini 配 X/Y=1，不发就 home 永不触发
- 加 SET_HOME_SAFETY_MERGIN（cmd 28）

完整启动序列对齐老 Squid microcontroller.py:1369 `configure_actuators`。

**7.4 octoaxes vs 老 Squid firmware 对比归档（commit 9c00d65）**：
- `documents/baselines/comparison_2026-05-11.md` 详细对比表
- 同参数小距离 (10μm-1mm) octoaxes 快 3-14ms (3-7%)
- 同参数大距离 (5mm-30mm) 老 Squid 快 20-76ms (3-9%)
- 总体性能相当，差距 < 10%

大距离 5% 差距分析（BOW 已 saturate 到 BOWMAX，差异可能在 motor_isRunning 判定方式 / axis.update 状态机延迟），追这 5% 需 firmware 调试打点。**接受现状，搁置**。

#### 8. Z 轴编码器启用（硬件验证通过）

用户实测确认 Z 编码器读数有效。`software/utils/constants.py:54` Z 轴 `has_encoder: False → True`：

- GUI 启动 `_configure_encoders()` 自动下发 CONFIGURE_STAGE_PID(cmd 25)：axis=Z(2), flip=1, tpr=3000
- 固件 `Axis::configureStagePID()` 运行时设 `_config.enableEncoder=true` + `motor_initABNEncoder(icID, 3000, ..., invertEncoderDir=true)`
- `getCurrentPositionMicrosteps()` 切到读 TMC4361A_ENC_POS（经 ENC_CONST 换算单位仍是微步）
- 响应包 bytes[10-13] 现在是编码器位置；MSG_LENGTH 仍 24 字节，对纯 XYZ 上位机透明
- GUI 位置标签：Z 轴显示 `(encoder)` 后缀

X/Y 编码器硬件已布但参数未验证，保持 `has_encoder: False`。PID 闭环（`enableStagePID`）仍未开，当前是「开环驱动 + 编码器读数上报」模式。

> 2026-05-11 核实：「合并 W 轴编码器修复（maxpro → develop）」已无需合并 —— 2026-03-27 maxpro 上 W 轴 ABN 编码器 4 个 commit（f986305 / 4d3f36d / 94e5911 / c09ae84）位于两分支共同祖先 47570ae 之前，已在 develop。maxpro 领先 develop 的 12 个 commit 全部是 `octoaxesplus`（双相机变体）独立工程。

---

### 2026-05-09 - 方案 A 软限位方向感知闸门完整落地（5 次迭代）

**位置**: 方案 A「软限位方向感知闸门」落地并硬件验证通过

#### 1. 上位机限位收紧（commit febc844）

`software/utils/constants.py` AXIS_CONFIG：
- X: `(-80000, 80000)` → `(-10, 115000)` μm
- Y: `(-120000, 120000)` → `(-10, 76000)` μm

下限设 `-10` μm 而非 0：home 后 XACTUAL=0，下限严格 < 0 才不会让 chip 在 SET_LIM 时立即触发 VSTOPL_ACTIVE_F（与旧 Squid Plan B 配置层 `y_negative=-0.01` 同思路）。上限按物理行程上限。本项目 software 现在和旧 Squid 配置一致，方便复现 VSTOP 场景。

#### 2. 方案 A 方向感知闸门重启（commit 82dfe2d，硬件验证通过）

**设计原则**（与用户共识）：「越界后只允许电机朝更安全方向移动，禁止朝更深越界方向移动」——即把限位检查从 chip 硬件层上移到 Axis 协议层。

**核心规则**：
```
读 chip 当前 VIRT_STOP_LEFT (L), VIRT_STOP_RIGHT (R)、_softLimits.{leftValue,rightValue}
读 XACTUAL = C, target = T

effective_lower = (C ≤ L) ? C : L  // 越下限时下界=C（禁止再下）；安全区时下界=L
effective_upper = (C ≥ R) ? C : R  // 对称

接受 T ∈ [effective_lower, effective_upper]，否则拒绝
```

**实现要点**（axis.h / axis.cpp）：
- `Axis::SoftLimitShadow` 结构追踪 SET_LIM 的上位机意图（leftEnabled/rightEnabled + leftValue/rightValue），与 chip 寄存器解耦——`motor_moveToMicrosteps` recovery 临时清 chip EN 位时，shadow 仍保留语义。
- `setOneSoftLimit(direction, value)` 同步写 shadow 单侧；`setSoftLimits(lo, hi)` 写双侧；`enableSoftLimits(false)` 清空 shadow.enabled 标志。
- 新增 `isMoveAllowedByDirection(target)` 实现上述规则，在 `moveToPositionMicrosteps` / `moveRelativeMicrosteps` 中 `motor_moveToMicrosteps` 之前调用。
- `checkLimitPosition()` 移除「VSTOP_ACTIVE event → completeMovement()」分支：信任上层闸门已确保 in-progress move 朝安全方向，VSTOP_ACTIVE 在此期间是 chip 的 sticky/残留状态；完成判定交给 `checkMovementComplete()`（XACTUAL == XTARGET）。
- chip-level VIRT_STOP_* 仍作为多重防御保留，不破坏现有 motor_moveToMicrosteps 的 recovery 流程。

**前三次失败的差异**（v1/v2/v2 续都试图改 SET_LIM 写寄存器时序）：
- 这次完全不动 SET_LIM 的寄存器写入策略，也不动 enableSoftLimits 的时序——避开了 v1/v2 的 TMC4361A 未文档化边界行为雷区。
- 把决定权放在 Axis 协议层 + checkLimitPosition 的状态机决策，对硬件依赖最少。

**调用链确认**（旧 Squid 与本项目 software 共享 `SET_LIM` (cmd_id=9) 协议）：
- 上位机 → `serial.cpp:439 case Commands::SET_LIM` → `CommandProcessor::handleSetLim` (commandprocessor.cpp:171-189) → `Axis::setOneSoftLimit(direction, value)` → 写 TMC4361A `VIRT_STOP_LEFT/RIGHT` + `REFERENCE_CONF` 中 `VIRTUAL_*_LIMIT_EN` + `VIRT_STOP_MODE=1`（硬停止）
- 触发：chip 实时比较 XACTUAL vs VIRT_STOP_*，越界写 EVENTS 的 VSTOPL/R_ACTIVE_MASK + STATUS 的 VSTOPL/R_ACTIVE_F

#### 3. 旧 Squid 的处理方式核对

旧 Squid 在 `MOVE_X/Y/Z` 命令处理里做了「按方向 clamp 到对侧限位」（main_controller_teensy41.ino:845）：
```c
X_commanded_target_position = (relative_position > 0
    ? min(current_position + relative_position, X_POS_LIMIT)
    : max(current_position + relative_position, X_NEG_LIMIT));
```
但**只 clamp 目标方向那一侧**、**MOVETO 完全不 clamp**、**没有 VSTOP recovery**，所以 SET_LIM 把电机置于禁区时只能「带病能跑」（每次走几百微步），靠配置（Plan B）规避。

我们的方案 A 比旧 Squid 更彻底：MOVE/MOVETO 统一闸门、配合 motor_moveToMicrosteps 的 EN-disable recovery、homing 后 SET_LIM 把 0 置于下限禁区可立即往外爬。

#### 4. 测试用例（reject 版本初次验证）

| # | 准备 | 操作 | 期望 / 实际 |
|---|---|---|---|
| 1 | home X 完成 X≈0 | SET_LIM X 下限=5mm | SET_LIM 收到，无即刻 MOVE ✓ |
| 2 | 同上 | MOVE_X +1mm | 电机朝+走 1mm，不假 COMPLETED ✓ |
| 3 | 同上 | MOVE_X +10mm | 跨 5mm 进入安全区 ✓ |
| 4 | 同上 | MOVE_X -1mm | 拒绝，log `Move rejected (direction)` ✓ |
| 5 | X 在安全区 50mm | MOVE_X -100mm（target 越下限）| 拒绝 ✓ |
| 6 | X 在安全区 | MOVE_X +5mm（内部）| 正常移动 ✓ |

#### 5. reject 改为 clamp 兼容旧 Squid（commit e773f21）

**背景**：旧 Squid 上位机不可改，需要固件兜底处理越界 target。原 reject 语义会让旧 Squid 在「Y=6mm 下限 5mm 发 MOVE_Y -2mm」场景下电机不动。改为 clamp 让电机走到边界停下。

**改动**：
- `axis.h` `bool isMoveAllowedByDirection(target)` → `int32_t clampTargetByDirection(target)`
- `axis.cpp` 在 moveToPositionMicrosteps / moveRelativeMicrosteps 用 clamp 后的 target 替换原 target
- 与旧 Squid `callback_move_x/y/z` 内的 `min/max` clamp 行为一致

**验证**：octoaxes GUI 实测 Y=6mm + 下限 5mm + MOVE_Y -2mm，电机停在 5mm 物理位置，GUI 显示 5mm ✓。

#### 6. clamp 后 target == current 短路（commit d92fa2d）

**问题**（旧 Squid main_hcs.log 10:02 现场）：X 卡在 VIRT_STOP_RIGHT 边界外 1 微步（XACTUAL=6300, R=6299），cmd 191 MOVE_X +3780 朝越界方向 → clamp 截到 6300（=current）→ motor_moveToMicrosteps 写 XTARGET=XACTUAL=6300 但 startMovement() 仍设 _isMoving=true → checkMovementComplete 应当立即看到 XACTUAL==XTARGET 但实际未即时触发（chip transient）→ 5 秒 MOVEMENT_TIMEOUT_MS 触发 handleError → 上位机感受到「卡 5 秒」。

**修复**：clamp 后 target == 当前位置时直接 return true，跳过 motor + startMovement，避免 _isMoving 误设。

#### 7. Homing 路径 VSTOP recovery 修复（commit df4f1f6，撤销中间 commit 2fb5bcc 简化版本）

**现象**（main_hcs.log 10:11 启动卡死）：固件烧写后 chip XACTUAL=0，SET_LIM x_neg=6299 微步立即触发 VSTOPL_ACTIVE_F hard-stop latch，cmd 29 X home 永远不 ack（X 始终为 0）。Y 不卡是因为 cmd 28 MOVE_Y 走的 motor_moveToMicrosteps 完整 VSTOP recovery 解锁了 Y，X home 走的 motor_setVelocityInternal 只清一次 EVENTS 不够。

**先尝试 commit 2fb5bcc**：在 STATE_HOMING_INIT `motor_enableSoftLimits(false, false)` 之后加 `tmc4361A_readRegister(EVENTS)`。**实测仍卡死**。

**最终 commit df4f1f6**：改为调用 `motor_moveToMicrosteps(_icID, motor_getPositionMicrosteps(_icID))`，复用已验证的完整 VSTOP recovery 路径（禁 EN → 清 EVENTS → 写 XTARGET → 再清 EVENTS）。target=XACTUAL 不引起电机移动，仅复位 chip 状态。

**验证**：旧 Squid 启动 cmd 29 X home took 1031ms 正常完成 ✓。

#### 8. 边界缓冲防 chip hard-stop latch（commit 17b8f71）

**现象**（main_hcs.log 10:31:57 后段）：cmd 37 MOVETO_X usteps=6300（target = X_NEG_LIMIT=6299 + 1 微步，紧贴 chip VIRT_STOP_LEFT 边界）→ chip 写 XTARGET=6300 启动 ramp 减速 → ramp generator 减速过程中亚微步精度让 XACTUAL 短暂 ≤ 6299 → 触发 VSTOPL_ACTIVE 进入 hard-stop latch → **此后所有 MOVE_X 朝任何方向都启动不了 ramp**（octoaxes 和旧 Squid 都不能动 X）→ 必须断电复位 chip 才能恢复。

**根因**：firmware 的 motor_moveToMicrosteps VSTOP recovery 仅清 EVENTS sticky bit，**不能解 chip 内部 ramp generator 的 latched 状态**。

**修复**：`clampTargetByDirection` 在安全区时把 target 强制离开 VIRT_STOP 边界至少 100 微步：
```cpp
static constexpr int32_t BOUNDARY_MARGIN_MICROSTEPS = 100;
effective_lower = (C ≤ L) ? C : (L + BOUNDARY_MARGIN_MICROSTEPS);
effective_upper = (C ≥ R) ? C : (R - BOUNDARY_MARGIN_MICROSTEPS);
```
- X/Y 16 microstepping/2.54mm pitch：100 微步 ≈ 79.4μm（远低于显示精度，远高于 chip ramp 精度需求）
- 越界回归路径不受影响（C ≤ L 时下界=C，让电机能从禁区往安全区爬）

**验证**：断电复位 + 烧 commit 17b8f71 后旧 Squid 启动 + 各种 MOVE/MOVETO 操作均正常 ✓。

#### 9. 测试脚本（software/tests/test_homing_with_vstop_latch.py）

复现「X=0 + SET_LIM x_neg=5mm + HOME_X」启动卡死场景的 Python 测试脚本，可用于回归验证。

---

### 2026-05-08 - 旧 Squid 5mm 短少 + 随机点动卡死定位

**位置**: 定位旧 Squid 5mm 移动短少根因 — VSTOP 早完成 bug；方案 A 三次失败搁置（次日 2026-05-09 重启并落地）

#### 1. 定位旧 Squid 5mm 实际位移短少的根因

**现象**：旧 Squid 上位机点 X/Y 5mm 移动，GUI 数值与物理位移都明显短少；同固件用 octoaxes 上位机正常。

**根因（VSTOP 早完成）**：
- 旧 Squid 启动调用 `microscope.py:452` **先 `set_limits` 再 `home_xyz`**
- `configuration_Squid+.ini` 默认 `[SOFTWARE_POS_LIMIT]` 中 `x_negative=5`、`y_negative=4`（mm）
- 启动时 XACTUAL 处于硬件复位后位置（X=1967 微步 ≈ 1.56mm），**已经在下限以下**
- 固件 `Axis::setOneSoftLimit()` 立即使能 TMC4361A `VIRT_STOP_LEFT`，TMC4361A 检测 `XACTUAL ≤ VIRT_STOP_LEFT` → 设置 `VSTOPL_ACTIVE` 标志
- 任何后续 MOVE_X：`moveRelativeMicrosteps` 过 `isWithinSoftLimits(target)` 检查 → `motor_moveToMicrosteps` 写 XTARGET → `startMovement()` 设 `_isMoving=true` → 但电机一启动就触发 VSTOPL → `Axis::update()` 进 `STATE_MOVING` 分支 → `checkLimitPosition()` 读到 `VSTOPL_ACTIVE_MASK` → **直接 `completeMovement()`** (axis.cpp:261) 清 `_isMoving`
- 下次 `send_position_update` 上报 `status=COMPLETED`
- Squid 的 `wait_till_operation_is_completed` 在 ~18-20ms 内被唤醒（远早于电机真完成）→ Squid 以为命令完成，每次 5mm 实际只走几百微步

**为什么 octoaxes 没事**：octoaxes `widgets.py` 默认软限位 `[-6000, 6000] μm`，包含原点 0，VSTOP 不会预先触发。

**关键日志证据**（`~/.local/state/squid/log/main_hcs.log`）：
- bug 现场：cmd 26 MOVE_Y +25197 在 **20.3ms** 报 COMPLETED；cmd 28 MOVE_X +62992 在 **18.2ms** 报 COMPLETED
- 5mm 重复点击：x 从 6 → 25 → 202 → 1395 → 1758 → 2192 → 2940（每次只走 ~400 微步，因 XACTUAL<6299 一直被 VSTOPL 截停）

#### 2. 修复方案 B：旧 Squid 配置层下放下限

修改 `configuration_Squid+.ini`：
```ini
[SOFTWARE_POS_LIMIT]
x_negative = 0       ; 原 5
y_negative = -0.01   ; 原 4，必须严格小于 home 后位置 0
```

**为什么 y_negative 不能是 0**：TMC4361A VSTOPL 触发条件是 `XACTUAL ≤ VIRT_STOP_LEFT`（含等号）。Y home 完成后停在 0，等于下限 0 → 仍触发 VSTOP。X home 完后是 64（home_safety_margin 让 X 离开了原点），所以 X 没问题。改成 `-0.01 mm = -13 微步` 让 0 严格大于下限即可。

#### 3. 验证（commit 待提交，旧 Squid 仓库）

修复后日志（`main_hcs.log` 09:24:54+）：
- cmd 39-41 MOVE_X ±6299：**295.6ms / 298.2ms / 298.0ms** 真实完成时间（25mm/s × 5mm + S-ramp 合理）
- cmd 46-55 MOVE_Y ±6299：**290.8–299.9ms** 同理
- 累计位移精确：x = 25 → 6324 → 12623 → 18922 → 12623 → 6324（每步 ±6299）✓
- y = 0 → 6299 → 12598 → 6299 → 0 → 6299 → 11241 → 18897（每步 ±6299）✓

#### 4. 遗留：cmd 29 MOVE_X +62992 仍 17.2ms 早完成

启动 homing 序列里 `home_xyz` 第二段 `move_x(50)` 命令实际只走 25 微步（X 不动），但该序列 X 已 home，紧接着 HOME Y，**无副作用**，先不修。

**根因推测**：cmd 28 (HOME X) 完成后 X 短暂处于 `STATE_LEAVING_HOME`；cmd 29 到达时 `Axis::moveRelativeMicrosteps` 看到 `_currentState != STATE_IDLE` 直接返回 false，`startMovement()` 不被调用，`_isMoving` 保持 false → 下一次 `send_position_update` 报 COMPLETED。

#### 5. 固件方案 A「软限位延迟使能」三次尝试均失败，已 `reset --hard 8571106`

**目标**：让上位机可以任意时刻下发 `x_negative=5` 等真实安全限位，不必依赖 Plan B 的配置 workaround。

**v1（commit b177144，已 revert）**：
- 设计：SET_LIM 时若 XACTUAL 已越界，**写真值到 VIRT_STOP_*** 同时清 EN=0**，标记 pending；XACTUAL 进入安全区后置 EN=1。
- 现象：cmd 20-26 集体延迟（~5 秒无响应），cmd 28 HEARTBEAT 也 741ms 才 ack。
- 推测根因：TMC4361A 看到「VIRT_STOP_*=已越过值 + EN=0」组合（数据手册无明确文档），疑似 latch VSTOP_EVENT 或进入异常 ramp 状态。
- 已 `git revert`（commit 8571106），用户确认回滚后正常。

**v2（commit 33e85fd，已 reset）**：
- 改进：pending 期间 VIRT_STOP_*** 持有「安全值」**(`INT32_MAX`/`INT32_MIN`)，永不让芯片看到「已被越过」的限位值；原子顺序「写真值→清 EVENTS→置 EN=1」。
- 现象：覆盖了「SET_LIM 时 XACTUAL 已越界」场景，但**没覆盖「homing 重置 XACTUAL 到已使能限位的内侧」**——10:47 测试 homing 完成、cmd 29 MOVE_X +50mm 仍 18.8ms 早完成（X stuck at 272），导致 GUI 报告 X≈0。

**v2 续（commit 08409f9，已 reset）**：
- 给 `Axis::enableSoftLimits(true)` 也加智能延迟逻辑：检查当前 xactual 与 VIRT_STOP_*；冲突时把 VIRT_STOP_* 转成安全值并标记 pending。
- 现象：homing X **完全卡住**（X xactual 维持 0 不动，cmd 27 后所有 cmd 包括 HEARTBEAT 都不 ack），但 fw 主循环还活着（每 10ms 发位置上报）。
- 用户 revert（commit 33c4064），回到 v2 setOneSoftLimit-only 状态。

**最终回退到 8571106**：
- v1/v2 都触及 TMC4361A VSTOP/REFCONF/VIRT_STOP_* 寄存器的边界行为，没有更精确的硬件诊断手段（debug 固件 + 串口监视器、独立 SPI 寄存器 dump）很难定位。
- v2 的「pending 期持安全值」方向**思路正确**，失败原因可能在「使能时序」或「与 stepaxis homing 路径的交互」上的细节，目前推测无法落实。
- 决定：**搁置固件方案 A**，**保留 Plan B 配置 workaround 作为生产可行方案**。

#### 6. 旧 Squid 随机点动 X 卡死根因定位 — axisName ↔ CS 引脚映射反置 (2026-05-08 后期)

**现象**：旧 Squid 跑随机点动测试，X 或 Y 概率性单轴卡死（位置冻结、互不影响、fw 通信正常、必须断电恢复）。点动 cmd 95 MOVETO_X=104957 X 走到 100665 ≈ **79.9mm** 就停（接近 Y 物理上限 76mm 这个数值耦合是关键线索）。

**排查路径**：
1. 先怀疑 StallGuard 误触发 → 临时关闭 X/Y `enableStallSensitivity` 重测 → 仍卡死，**排除 SG**
2. 加 `S:DUMPREGS X` 调试命令准备抓现场（`serial.cpp` 实现）
3. 用户提示核对**新旧 Squid firmware 的 X/Y CS 引脚定义**——找到决定性证据

**决定性证据**（旧 Squid `firmware/controller/src/def/def_v1.h:11-21`）：
```cpp
// IMPORTANT: These are INTERNAL indices, NOT protocol constants!
// Protocol: AXIS_X=0, AXIS_Y=1, AXIS_Z=2, ...
// Internal: x=1, y=0, z=2, w=3, w2=4
// Internal indices match hardware wiring (x/y swapped, ...)
static const uint8_t x = 1;
static const uint8_t y = 0;
```
对应 `constants.h:80` `pin_TMC4361_CS[5] = {41, 36, 35, 34, 16}`：
- `pin_TMC4361_CS[0] = 41` → 内部索引 y=0 → **物理 Y 电机**
- `pin_TMC4361_CS[1] = 36` → 内部索引 x=1 → **物理 X 电机**

而 Octoaxes firmware 之前假设：
- `Pins::X_AXIS_CS = 41` → "X axis" → 操作 CS=41 chip
- `Pins::Y_AXIS_CS = 36` → "Y axis" → 操作 CS=36 chip

**与硬件接线完全相反**！

**bug 链**（用户硬件按旧 Squid 接线 + Octoaxes firmware）：
- 旧 Squid 上位机发 `MOVE_X` (cmd[1]=0) → fw findAxisByName("X") → 操作 CS=41 chip → **实际驱动物理 Y 电机**
- 用户看到 GUI 「X」数字增长 + 物理 X stage 移动 = 同方向，没察觉反置
- 但 Y 物理电机走到 76mm（Y 物理上限）触发 Y 硬件 RIGHT 限位开关 → CS=41 chip 收到 STOPR_EVENT → fw 把这个事件归到「X axis」（因为 CS=41 在 fw 内部叫 X）→ X._isMoving 永远卡 true
- 后续所有 MOVETO_X 被 `_currentState != STATE_IDLE` 拒绝（reject 但 cmd_id 仍更新）
- 必须断电硬复位 TMC4361A 才能清 EVENTS

**解释了所有症状**：
- 单轴卡死、互不影响（限位 latch 在单个 chip）
- fw 通信正常（主循环正常）
- 必须断电（TMC4361A 内部 EVENTS sticky 状态需硬复位）
- 卡死位置 79.9mm ≈ Y 上限 76mm（数值耦合）

**修复方案**：交换 axisName 字符串与 CS 引脚的对应关系，**不改 PIN_CS_X/Y 常量名（保持 PCB 引脚号历史命名）**：

`firmware/octoaxes/octoaxes.ino:86-87`:
```cpp
// 旧 (与硬件接线反)
Axis *yAxis = new StepAxis(Pins::Y_AXIS_CS=36, 0, "Y");
Axis *xAxis = new StepAxis(Pins::X_AXIS_CS=41, 1, "X");

// 新 (axisName 与硬件接线对齐)
Axis *xAxis = new StepAxis(Pins::Y_AXIS_CS=36, 0, "X");  // CS=36 = 物理 X 电机
Axis *yAxis = new StepAxis(Pins::X_AXIS_CS=41, 1, "Y");  // CS=41 = 物理 Y 电机
```

**协议字节零变化**——上位机完全不需要改。`AxisConfigs::X_AXIS / Y_AXIS` 物理参数通过 `beginAll()` 按 axisName 匹配，正确映射到对应物理 chip。

**附加改动**：
- 恢复 X/Y `enableStallSensitivity = true`（SG 不是元凶，临时关闭撤销）
- 保留 `serial.cpp` 中的 `S:DUMPREGS [axisName]` 调试命令（dump TMC4361A 关键寄存器，未来卡死现场取证用）
- `config.h` PIN_CS_X/Y 常量加注释说明命名是 PCB 引脚号历史，不代表物理轴对应

> 当时记录的「下次继续」中的方案 A 已于 2026-05-09 落地（commit 82dfe2d），见最新会话。

---

### 2026-05-07 - 修复 AXIS_MM_PER_STEP 双源不同步 bug（commit 7be758d）

**现象**：用户把 `constants.py` 中 X 轴 `actuator_microstepping` 从 256 改为 16 后，下发 5mm 实际位移变 80mm（系数 ×16）。

**根因**：
- `_configure_actuators()` 启动时读 `actuator_microstepping=16` 下发 `CONFIGURE_STEPPER_DRIVER`，固件 TMC4361A 切到 `MSTEP_PER_FS=4`（16 細分）
- `_move_step_axis_relative_position()` 走的 `AXIS_MM_PER_STEP` 是**硬编码** `2.54/(200*256)`，仍按 256 細分算
- 结果：5mm → 100787 microsteps 下发，固件 16 細分模式下走 100787/3200×2.54 = 80mm

**修复**：`software/utils/constants.py` 中 `AXIS_MM_PER_STEP` 改为字典推导式从 `AXIS_CONFIG` 派生，单一数据源；为 W/E1/E3/E4 补齐 `actuator_screw_pitch_mm` 和 `actuator_microstepping` 字段。

**固件已确认正确（无需改动）**：`handleConfigureStepperDriver` 立即生效；`Axis::configureDriver()` → `motor_setMicrosteps()` 同步刷新 `STEP_CONF`、`stepsPerMM`、TMC2240 `CHOPCONF.MRES`。

---

### 2026-04-17 - XYZW 全部回退为微步模式 + 协议对齐

#### 1. XYZW 全部回退为微步模式

- `software/utils/constants.py` — XYZW 四轴 `has_encoder` 全部改为 `False`
- GUI 连接后不再下发 `CONFIGURE_STAGE_PID`，固件 `_config.enableEncoder` 保持 `false`
- 位置来源回到 XACTUAL（微步），GUI 显示从 `Encoder | Steps | Δ` 回到 `Position (steps)`
- 编码器基础设施（`encoder_transitions_per_rev`、`encoder_flip_direction`、固件 `motor_initABNEncoder`）保留，日后恢复只需切回 `has_encoder: True`

#### 2. 协议兼容性分析（与旧 Squid 对比）

- **MOVE_X/Y/Z 相对移动命令完全兼容** — 两端均为 `cmd[2..5] = int32 BE 微步`（两位补码处理负数），CRC-8-CCITT on `cmd[0..6]`
- **响应包实际为 24 字节**（早先 SESSION/CLAUDE 里"32 字节"描述是临时扩展方案，实际未落地）
- 唯一差异：bytes[14-17] — 旧 Squid 不写 W 位置（残留数据），Octoaxes 填 W 滤光轮位置；对纯 XYZ 上位机兼容

#### 3. 文档修正

- `CLAUDE.md` — 修正"32 字节协议"误述，同步当前 24 字节格式与编码器关闭状态
- `SESSION.md` — 重写最新会话记录本次变更

---

### 2026-04-13 - Z 轴编码器使能 + 协议扩展尝试（已回退）

#### 1. 手控盒焦点轮修复

- `control_panel_teensyLC.ino` — encoder_step_size 加 volatile 消除 ISR 竞态，pow() 改位移运算
- 编码器量化对齐值保持 16（f92ef36 误改为 256 导致低档位无反应）

#### 2. Z 轴编码器使能

- `config.h` — 新增 `ENCODER_RESOLUTION_UM_X/Y/Z` 常量（μm/pulse: 0.05/0.05/0.1）
- X/Y/Z 轴 `encoderLinesPerRev` 改为 `screwPitchMM * 1000 / ENCODER_RESOLUTION_UM` 公式
- Z 轴 `enableEncoder = true`（3000 lines/rev），X/Y 预填参数未使能
- 新增 `invertEncoderDir` 配置项，Z 轴设为 `true`（编码器方向与电机相反）

#### 3. 位置上报协议扩展方案（提出但未落地）

- 早期方案：响应包 28→32 字节，bytes[23-26] 放 Z 编码器位置，byte[30] 固件版本，byte[31] CRC
- 实际实现采用透明方案：`MSG_LENGTH` 保持 24，`getCurrentPositionMicrosteps()` 按 `enableEncoder` 返回 XACTUAL 或 ENC_POS，bytes[10-13] 复用
- GUI 曾实现 `Encoder | Steps | Δ` 三值显示（main_window.py 有编码器分支）

---

### 2026-03-27 - Engine Start 删除 + W 轴编码器调试 (maxpro)

#### 1. 删除 Engine Start 机制

固件不再阻塞等待上位机发送 "S:Engine Start"，上电后直接初始化电机系统。

**固件变更**:
- `octoaxes.ino` — 删除 `waitEngineStartCommand()` 调用，setup() 直接初始化
- `serial.h` — 删除 `waitEngineStartCommand()`、`isEngineStarted()`、`engineStarted` 成员
- `serial.cpp` — 删除 `waitEngineStartCommand()` 函数体和 `engineStarted` 全局变量；"S:Engine Start" 命令保留为空操作（兼容测试脚本）

**GUI 变更**:
- `main_window.py` — 删除 Engine Start 按钮及 `send_engine_start()` 方法；连接成功后自动触发 `startup_launch`（设限位）；`setup_timers()` 移到 `find_and_connect_teensy()` 之前避免属性未创建错误

#### 2. W 轴 ABN 编码器支持（调试中）

**硬件**: 4000 线 ABN 编码器，接在 W 轴 (TMC4361A icID=3)

**新增配置**:
- `axis.h` — AxisConfig 新增 `enableEncoder`、`encoderLinesPerRev` 字段
- `config.h` — 7 个轴全部添加编码器配置，W 轴 `enableEncoder=true, encoderLinesPerRev=4000`
- `axis.cpp` — `begin()` 中如果 `enableEncoder=true`，调用 `motor_initABNEncoder()` 初始化

**调试功能**:
- `serial.cpp` — 新增 `S:ENCPOS` 命令，打印各轴编码器位置 + W 轴寄存器诊断（GENERAL_CONF、ENC_IN_CONF、STEP_CONF、ENC_CONST）
- `octoaxes.ino` — loop() 中每 2 秒打印 W 轴 `enc`/`xactual`/`dev`
- `main_window.py` — Log 页面新增 Debug Command 输入框，可手动发送 `S:ENCPOS` 等命令

**诊断结果** (S:ENCPOS 输出):
```
GENERAL_CONF=0x10000001 diff_dis=0 ser_mode=0
ENC_IN_CONF=0x00000400 STEP_CONF=0x00000C85 ENC_IN_RES(readback=ENC_CONST)=1000
```

**发现的问题**:
1. `diff_dis=0` — 差分编码器模式未禁用，单端编码器需要禁用 → **已修复**（`motor_initABNEncoder` 中设 GENERAL_CONF bit12）
2. `ENC_IN_RES` 地址 0x54 写入/读回不同：写入设置 ENC_IN_RES，读回得到 ENC_CONST（TMC4361A 特性）
3. W 轴走 1 圈：`xactual` 变化 1600（正确），`enc` 变化 400 → 原始编码器计数 4000/rev（应为 16000 即 4x 正交），怀疑差分模式导致只有 1x 计数 → **待验证**

**W 轴电机参数（TMC4361A 视角）**:
- STEP_CONF: MSTEP_PER_FS=8, FS_PER_REV=200 → 1600 微步/转
- TMC2240 硬件: MRES=256 + interpolation（对 TMC4361A 透明）
- ENC_IN_RES=16000 (4000线 × 4 正交)
- 理论 ENC_CONST = 1600/16000 = 0.1

#### 3. 修正编码器 transitions 计算 (2026-03-31)

**问题**: `axis.cpp` 中 `transitions = encoderLinesPerRev * 4`（假设需要 4x 正交倍频），实际编码器线数就是 transitions，不需要乘 4。

**修复**:
- `axis.cpp:128` — 去掉 `* 4`，`transitions` 直接等于 `encoderLinesPerRev`
- `axis.h:64` — 更新注释

**修正后 W 轴编码器参数**:
- ENC_IN_RES = 4000（之前错误写入 16000）
- ENC_CONST = 1600/4000 = 0.4（TMC4361A 自动计算）
- 走 1 圈 ENC_POS 应 = XACTUAL = 1600

#### 4. Homing 后同步 ENC_POS (2026-03-31)

**问题**: `motor_setPosition()` 清零 XACTUAL/XTARGET 但未同步 ENC_POS，导致 homing 完成后编码器读数与微步位置不一致。

**修复**:
- `MotorControl.cpp:976` — `motor_setPosition()` 中新增 `ENC_POS = position`，与 XACTUAL 同步
- 对未启用编码器的轴写 ENC_POS 无影响

#### 5. 编码器轴位置上报改用 ENC_POS (2026-03-31)

**需求**: 启用编码器的轴，上报位置应来自编码器而非开环 XACTUAL，GUI 需提示数据来源。

**固件变更**:
- `axis.cpp` — `getCurrentPositionMicrosteps()` 根据 `enableEncoder` 返回 ENC_POS 或 XACTUAL
- ENC_POS 经 ENC_CONST 换算后单位与微步一致，上位机无需额外转换

**上位机变更**:
- `constants.py` — W 轴添加 `"has_encoder": True`
- `main_window.py` — 位置显示标签：有编码器显示 `(encoder)`，否则显示 `(steps)`

### 下次继续

1. **验证编码器修复** — 烧写后走 1 圈，ENC_POS 应 ≈1600 匹配 XACTUAL
2. 如果方向反，设 `invert_dir=true`
3. 验证正确后考虑开启 PID 闭环
4. TMC2240 StealthChop 参数调优
5. 清理 TMC2240 调试代码
6. 修正 W 轴 config.h 配置（LEFT_SW → RGHT_SW + 极性修正）

---

### 2026-03-25 - TMC2240 驱动自动检测 + Homing 修复 (maxpro)
- DRV_ENN 硬件修改：TMC2240 DRV_ENN 接 GND，电机可运动
- TMC2240 Homing CHOPCONF 损坏修复：shadow register 替代不可靠 Cover 读取
- 驱动芯片自动检测 DRIVER_AUTO：初始化时读 IOIN VERSION=0x40 区分 TMC2240/TMC2660
- S:HWINFO 硬件查询命令 + test_hwinfo.py 脚本

---

### 2026-03-24 - TMC2240 W 轴硬件调试 (maxpro)
- W 轴 TMC2240 SPI 通信成功（Cover 40-bit，状态字节 0x99/0xB9）
- 修复 SPI_OUTPUT_FORMAT、SCALE_VALUES、GCONF direct_mode
- 发现 DRV_ENN 硬件问题（连接 NFREEZE 内部上拉→HIGH→驱动禁用）
- 新增 test_tmc2240_debug.py 调试脚本

### 2026-03-03 - 手控盒模块移植 (develop)
- joystick.h/cpp 新增：XY 摇杆速度控制 + Z 焦点轮跟随
- 修复 motor_moveToMicrosteps() VMAX 不恢复（Serial5 浮空噪声触发 motor_stop）
- 修复 Z 焦点轮无动作（do_focus_control 应在 flag_read_joystick 外面）

### 2026-02-27（续 2）- 删除旧 TMC-API 兼容层 (develop)
- 删除 Fields.h/Register.h/Constants.h，统一到 HW_Abstraction.h，248 警告→0

---

### 2026-02-27（续）- W 轴回归测试 + 上位机 Bug 修复 + 协议对齐 (develop)
- W 轴换孔时间回归测试：~60ms 平均，与 61.3ms 一致，无性能退化
- 上位机 axis_manager.py 日志吞掉轴前缀 DEBUG 行 Bug 修复
- MOVE/MOVETO 协议单位改为微步，与 Squid 原版一致

### 2026-02-27 - VSTOP 恢复机制修复 (develop)
- VSTOP recovery：STATUS 寄存器延迟恢复策略，参照 TMC4361A §10.4
- motor_moveToMicrosteps() VSTOP 恢复：禁用限位→清事件→写 XTARGET
- checkLimitPosition() 虚拟限位去掉方向检查
- homing 期间软限位标志与硬件操作分离

### 2026-02-26（续 4）- P5 PID/编码器命令实现 (develop)
- 最后一组桩函数全部完成：CONFIGURE_STAGE_PID(25)/ENABLE(26)/DISABLE(27)/SET_PID_ARGUMENTS(29)
- MotorControl 层新增 ABN 编码器初始化 + PID 参数写入 + PID 开关
- Axis 层 PIDState 结构体 + homing 后自动恢复 PID

### 2026-02-26 - 照明系统完整移植 + 上位机照明面板 (develop)
- 新建 illumination.h/cpp：DAC80508 驱动、APA102 LED 矩阵、5 端口控制、新旧双 API
- 实现 11 个照明 handler（命令 10-17 旧 API + 命令 34-39 新多端口 API）
- 上位机 IlluminationPanel：5 路 TTL 端口 + LED 矩阵 + 全局因子

### 2026-02-25 - Z 轴 homing SOFT_STOP_EN Bug 修复 (develop)
- 修复 Z 轴 homing 停车失败：移除 REFERENCE_CONF 中的 SOFT_STOP_EN 位
- 硬件验证通过，提交 5652bc3

---

## 使用说明

### 开始新会话时

1. 阅读「最新会话」了解上次进度
2. 查看「下次继续」确定本次任务

### 结束会话前

1. 将当前「最新会话」移到「历史记录」
2. 更新「最新会话」记录本次工作
3. 明确写出「下次继续」的任务

### 提示 Claude 更新

在会话结束前说：
> "请更新 SESSION.md 记录本次会话"
