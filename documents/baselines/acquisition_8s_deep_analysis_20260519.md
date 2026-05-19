# 采集 90→98s 差距深度分析 (2026-05-19)

> 取代 2026-05-18 版的初步分析（`acquisition_90vs98_analysis_20260518.md`）。
> 第一轮 agent 报告 2 个主凶（main loop 工作量增加 / motor_moveToMicrosteps SPI 翻倍）
> 深读代码后发现 3 个不成立，1 个量级被高估。

## 输入

- 旧 Squid 90s / octoaxes 98s
- 单 well + Laser AF + BF/561/638 三通道 + 20x，~200 FOV，单 FOV 多 40ms
- **2026-05-19 用户确认：同硬件 A/B（同 stage / 同相机 / 同 USB / 同物镜，只换 firmware）实测仍差 ~8s** — firmware 是真主凶

## 核心结论（2026-05-19 二轮 review 修订）

**深读代码后可量化的 octoaxes 真实 overhead ≈ 1.0-1.1s**。**仍缺 ~7s 静态 review 看不见**，需要 firmware 打点测量定位。

之前"8s 不在 firmware 层"的判断被用户实测否定。代码 review 已达静态分析极限，必须实测打点继续。

## 4 个修复点重新评估（基于深读代码）

| 修复点 | 第一轮 agent 估算 | 深读后真实情况 | 状态 |
|---|---|---|---|
| #1.1 axisManager.updateAll 跳空闲轴 | ~6s | **~40ns/iter** | drop（`Axis::update()` STATE_IDLE 已是空 break: axis.cpp:225-227） |
| #1.2 send_position_update 上移到 FastLED 前 | ~ | **~0** | drop（`elapsedMicros` 不被阻塞影响: serial.cpp:198） |
| #2.1 删 line 715 无条件 VMAX 写 | 1.5-2s | **~50ms** | drop（VMAX 写同一值不触发 chip ramp 重启；改 motor_stop 语义有传染风险） |
| #2.2 axis.cpp 重复 STATUS 读合并 | - | **~10-20ms** | **DONE 2026-05-19**（编译通过，未烧录） |

## 全面对比（按调查清单）

### A. 主循环对比 — **方向被翻转**

**旧 Squid `loop()` (main_controller_teensy41.ino:17-66)** 每 iter 调用 26 个函数：
- `INTERLOCK_OK` / `watchdog`
- `joystick_packetSerial.update()` + `process_serial_message()` + `do_camera_trigger()`
- **`prepare_homing_x/y/z/w/w2`（5 个空跑）**
- **`check_homing_x/y/z/w/w2`（5 个空跑）**
- **`finalize_homing_x/y/z/w/w2/xy`（6 个空跑）**
- `check_joystick` + `do_focus_control`
- `send_position_update` + `check_position` + `check_limits`

**octoaxes `loop()` (octoaxes.ino:157-191)** 每 iter 7 个 call：
- interlock 5 × digitalWrite
- `watchdog_check`
- `trigger_update`
- `processSerialCommands`
- `send_position_update`
- `joystick_update`
- `axisManager.updateAll`

**octoaxes main loop 比旧 Squid 明显更轻**。Agent "main loop 工作量增加" 的诊断与代码事实相反。

### B. processSerialCommands 路径

- 旧 Squid: 函数指针表 `cmd_function_pointer_table[cmd_id]()` 一级 dispatch
- octoaxes: 两层 — `processSerialCommands` (peek 2 byte) → `processSerialStandardCommands` (switch + commandprocessor) → handler

差异：octoaxes 多一次 peek + switch，约 ~10ns/cmd。每 FOV 16 cmd × 200 FOV ≈ **32µs total，完全可忽略**。

### C. joystick_update 成本

`joystick.cpp:146-186 do_focus_control`：
- delta=0 时 line 156-157 `if (delta == 0) return;` **已 early-return**
- 仅 1 个 noInterrupts 临界区读取 focusWheelDelta（~10ns）
- DEBUG_PRINT 在 production 编译掉

**joystick_update 空载成本 ≈ noInterrupts + 几个分支 ≈ 50ns**，可忽略。

但 `PacketSerial.update()` 在 `joystick.cpp` 主调用入口会跑（每 iter），它做 Serial5.available 检查 + COBS 解码。无包时也走一遍 available 检查 — 估算 ~200ns。每 iter 0.2µs。

### D. 命令 handler 重头戏

| Cmd | 旧 Squid | octoaxes | 差异 |
|---|---|---|---|
| 41 SET_PIN_LEVEL | `digitalWrite(pin, level)` 1 行 (commands.cpp:110-115) | `pinMode(pin, OUTPUT); digitalWrite(...)` 2 行 (commandprocessor.cpp:482-487) | +1 pinMode ≈ 200ns，4 次/FOV × 200 = **0.16ms 总** |
| 12 set_illumination | `set_illumination(src, intensity)` → DAC 写 | 同 illumination.cpp:415 | 等价 |
| 13 set_illumination_led_matrix | functions.cpp:359-368 仅缓存 | illumination.cpp:441 仅缓存（2026-05-10 对齐） | 等价 |
| 10 turn_on_illumination | functions.cpp:207 + FastLED.show | illumination.cpp:334 + FastLED.show | **行为等价**；FastLED 配置 1 MHz APA102 BGR 字面相同 |
| 11 turn_off_illumination | functions.cpp:270 | illumination.cpp:383 | 等价 |
| 0-2 MOVE_X/Y/Z | `tmc4361A_move` → `tmc4361A_moveTo` 4 SPI op | `Axis::moveRelativeMicrosteps` → `motor_moveToMicrosteps` 6 SPI op | **+2 SPI ≈ 40µs/move**（line 715 VMAX 写 + line 727 STATUS 读，#2.2 已合并 STATUS 读） |
| 6-8 MOVETO_X/Y/Z | 同上 | 同上 | 同上 |

每 FOV 5 次 move × 40µs = 0.2ms；200 FOV = **40ms total**。这是唯一在静态分析里能量化的 firmware 真实开销。

### E. 中断 / 后台开销

- **SPI 时钟**：旧 Squid `SPISettings(500000, MSBFIRST, SPI_MODE0)` (utils.cpp:120) vs octoaxes `TMC_SPI_SPEED=500000` (TMC_SPI.cpp:42) — **完全一致**
- **FastLED**：两端 `addLeds<APA102, ..., BGR, 1>`（1 MHz）— **完全一致**（init.cpp:44 / illumination.cpp:36）
- **SerialUSB**：两端都 Teensy 原生 USB CDC 默认 12 Mbps，无差异
- **ISR**：octoaxes 加 `trigger_update`（trigger.cpp 用 elapsedMicros 计时器，每 iter 检查），但触发恢复仅在 trigger active 时跑工作，无 active 时仅 1 个 elapsedMicros 比较，~10ns 可忽略

### F. 串口看门狗

`watchdog_check()` (illumination.cpp): 每 iter 1 个 millis() 比对 + bool 检查。<10ns 可忽略。

octoaxes 多个 5 × digitalWrite 在 interlock 失败分支（line 165-171），interlock OK 时仅 1 个 `if`，可忽略。

### G. configure_actuators 启动序列

两端在启动序列接收 CONFIGURE_STEPPER_DRIVER / SET_LEAD_SCREW_PITCH / SET_MAX_VELOCITY_ACCELERATION 后只配置一次，**不在每个 trigger / 每次切 channel 重发**。一次性开销，不进入 200 FOV 循环。

### H. 相机触发

`camera.send_trigger` 在 software-trigger 模式下由 Python 直接发 GenICam 命令到相机，**不走 MCU**。firmware 不参与。

### I. 数据上报包

两端 24 字节包，CRC8-CCITT 计算字面一致；`SerialUSB.write(buffer_tx, 24)` 单次写。无差异。

### J. 全局状态机

octoaxes 加 `STATE_HOMING_INIT/SEARCH/SET_ZERO/LEAVING_HOME/MOVING/IDLE/ERROR` 7 个状态。每 iter `Axis::update()` 跑 1 次 switch。STATE_IDLE 分支 1 个 `break` 即出 (axis.cpp:225-227)，~10ns/axis × 4 axis = 40ns total。可忽略。

## 候选主凶（2026-05-19 二轮 review 全清单）

| # | 名称 | file:line | 单次 cost | 频次 | 累计 | 修复 |
|---|---|---|---|---|---|---|
| 1 | `motor_moveToMicrosteps` 多 2 SPI（VMAX 写 + STATUS 读 vs 旧 Squid） | MotorControl.cpp:715+727 | +200µs | 1000 moves | **~200ms** | #2.2 已合并 STATUS 读 |
| 2 | `moveToPositionMicrosteps` no-op check 多 1 SPI `motor_getPositionMicrosteps` | axis.cpp:553 | +100µs | 1000 moves | **~100ms** | 待评估（合并到 motor_moveToMicrosteps 返回 currentPos？） |
| 3 | `clampTargetByDirection` 自己又读 1 次 SPI position | axis.cpp:817 | +100µs | 1000 moves | **~100ms** | 待评估（同上合并） |
| 4 | `Axis::completeMovement` 两个 `[[maybe_unused]]` SPI 读（DEBUG 编译掉但函数仍执行） | axis.cpp:453-454 | +200µs | 1000 moves | **~200ms** | 加 `#ifdef ENABLE_DEBUG` 包裹 |
| 5 | `send_position_update` 4 个 `findAxisByName` × String 构造 + equals | serial.cpp:203-206 | +40µs | 10000 ticks | **~400ms** | 缓存 axis 指针，构造期一次性 findAxisByName |
| 6 | `Axis::update` STATE_MOVING `checkLimitPosition` 每 iter 1 SPI EVENTS | axis.cpp:272 | +100µs/iter × ~10x more iters than old Squid | motion 期间 | **不延长 motion 总时间**（motion 在 chip 上跑） | 加 10ms throttle 对齐旧 Squid |

**累计 ~1.0-1.1s**。**仍缺 ~7s 静态 review 看不见**。

## 仍未定位的 ~7s — 可能藏区（静态 review 看不见，需打点）

1. **运动参数 chip-level 差异** — octoaxes `setMotionParameters` 内部计算 VMAX/AMAX/BOW 是否与旧 Squid 字面一致？benchmark_xyz 只验证了基础时间，acquisition 用的参数路径可能不同
2. **`Axis::configureDriver`** 在 acquisition 启动时是否重写大量 SPI 寄存器（每启动 1 次，但若含 chip reset 可达秒级）
3. **隐藏的非显式 `delay()`** — 某个 cmd handler 内部走的 lib / framework 是否有间接阻塞？
4. **`Axis::update()` STATE_MOVING 期间共用资源抢占** — main loop iter 时间从 50µs 涨到 250µs，USB / Serial5 / SPI bus 抢占行为可能差异
5. **TMC4361A chip 内部行为**：VMAX 同值重写是否真无副作用？没查数据手册，可能错（若每次写 VMAX 触发 chip ramp 重置 1-5ms × 1000 = 1-5s）
6. **某条 ISR 干扰**（trigger 100µs strobeTimer、PWM、SPI ISR）我没仔细对比中断负载差异

## 下一步必要措施：firmware 打点

静态 code review 已达极限，必须打点测量。**4 个候选打点（按优先级）**：

| # | 打点 | 位置 | 输出 | 信号 |
|---|---|---|---|---|
| 1 | **单 cmd 总耗时** | `checkForCommand` 入口 + `send_position_update` 发完 | 每 cmd `micros()` 差 | 找出哪类 cmd 异常慢 — 区分 atomic / move / homing |
| 2 | 单 move 各阶段 | `motor_moveToMicrosteps` 入口/出口 + `completeMovement` 入口 | dispatch / motion / detect 三段 | 区分处理慢 / 物理动慢 / 检测慢 |
| 3 | 主循环 iter 时间 | `loop()` 顶部 + 底部 | 每 100 iter 平均/max | 看是否被某 call 卡顿 |
| 4 | VMAX 写效果 | `motor_moveToMicrosteps` 内 VMAX 写前后读 STATUS | RAMP_STATE bits | 验证是否触发 ramp 重启 |

**建议先打点 #1**：15-20 行代码，写 `S:CMD_TIMING` ASCII 调试输出。Acquisition 后从串口 dump 看 cmd 类型延迟分布。能直接区分主凶在 atomic cmd 还是 move cmd。

## 修复落地

| 代号 | 描述 | 状态 |
|---|---|---|
| #2.2 | `motor_moveToMicrosteps` 返回 vstopWasActive，axis.cpp 移除 2 处重复 STATUS 读 | **DONE 2026-05-19**（编译通过 octoaxes 80412B / octoaxesplus 84188B，未烧录，待用户验收） |
| #1.1 #1.2 #2.1 | 静态分析确认收益不成立 | dropped，归档 |

## 修改文件清单（#2.2）

- `firmware/octoaxes/tmc/motion/MotorControl.h:219` — 签名 `void` → `bool`
- `firmware/octoaxes/tmc/motion/MotorControl.cpp:665` — 实现签名 + 末尾 `return vstopWasActive;`
- `firmware/octoaxes/axis.cpp:560-562` + `:623-625` — 移除 `motor_readStatus` 改用 `motor_moveToMicrosteps` 返回值
- `firmware/octoaxesplus/axis.cpp` — 同上（两端 byte-identical 约束）
- 其他 caller（joystick.cpp / filterwheel.cpp / stepaxis.cpp / MotorControl.cpp 内部）丢弃 bool 返回值，无需改

## 下次会话建议优先级

> 已被 2026-05-19 用户实测推进到第 4 步。

1. ~~跑用户原 A/B 方案~~ — **已完成**，结果：同硬件 ~8s 差距，firmware 是真主凶
2. **写打点 #1**（单 cmd 总耗时）firmware 代码，写完不烧录等用户硬件空闲
3. 用户空闲时烧带打点的 firmware → 跑 1 well 采集 → 串口 dump → 分析
4. 同时验证 #2.2 修复在硬件上不破坏运动行为（顺带带回归测试）
5. 根据打点数据找到主凶后再设计修复

## 已识别可立即落地的清理（独立于 ~7s 主凶）

不强求做但都是真实的浪费：

- **#3 `completeMovement` `[[maybe_unused]]` SPI 包裹**（节省 ~200ms / 200 FOV）— 5 行改动
- **#4 `send_position_update` 缓存 axis 指针**（节省 ~400ms / 200 FOV）— 在 SerialProtocolHandler 构造时一次性 findAxisByName 存指针
- **#5 `Axis::update` STATE_MOVING `checkLimitPosition` 加 10ms throttle**（对齐旧 Squid 节流，减少 SPI 资源竞争）

这三项加上 #2.2 合计 ~800ms 改进，但**主要价值是消除噪声**，不是解决 8s 问题。主凶仍需打点定位。
