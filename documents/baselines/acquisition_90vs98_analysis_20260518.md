# Wellplate Multipoint Acquisition 90 → 98s 差距调查 (2026-05-18)

## 场景

- GUI：Wellplate Multipoint，单 well（D6），Scan Shape=Circle，Scan Size=8.62mm，FOV Overlap=10%
- 物镜：20x；Sample Format：96 well plate
- 选中通道：BF LED matrix full + Fluorescence 561 nm Ex + Fluorescence 638 nm Ex（3 通道）
- 勾选：Laser AF；未勾选：Contrast AF / Z stack / Use Focus Map / Skip Saving
- 上位机：旧 Squid software `/home/hds/github.com/veerwang/lihongquan/Squid/software`（两机共用）
- 旧机 firmware：`/home/hds/github.com/veerwang/lihongquan/Squid/firmware/controller`
- 新机 firmware：`/home/hds/gitee.com/octoaxes/firmware/octoaxes`
- 实测：旧 90s / 新 98s，差距 8s

## FOV 时序图（每 FOV 命令序列）

来源：`multi_point_worker.py:1200-1340`（旧 Squid software）

1. **`move_to_coordinate`** (`mpw.py:738`)：
   - `move_x_to_usteps` + wait（1 round-trip）
   - `_sleep(160ms)` SCAN_STABILIZATION_TIME_MS_X
   - `move_y_to_usteps` + wait（1 RT）
   - `_sleep(160ms)` SCAN_STABILIZATION_TIME_MS_Y
2. **`perform_autofocus`** — Laser AF（`laser_autofocus_controller.py:339`）：
   - `set_pin_level(AF_LASER, 1)` + wait → 相机抓图（无 MCU）→ `set_pin_level(AF_LASER, 0)` + wait（2 RT）
   - `_move_z(um)` → `stage.move_z(um/1000)`：方向<0 时 backlash compensation = 2 RT；方向>0 = 1 RT
   - `_verify_spot_alignment` 再 turn_on/off + 抓图（2 RT）
3. **3 通道循环**，每通道（`mpw.py:1267-1283, 1451-1538`）：
   - `set_microscope_mode` → `update_illumination`：
     - BF：`set_illumination_led_matrix(src,r,g,b)` cmd 13 仅缓存（1 RT）
     - 荧光：`set_intensity` → `set_illumination(src, dac)`（1 RT）
   - `turn_on_illumination()` + wait（1 RT；BF 路径真触发 `FastLED.show()`）
   - `camera.send_trigger` + 等帧回调
   - `turn_off_illumination()`（1 RT，无 wait）

**单 FOV MCU 命令计数**：~2(XY) + ~5(Laser AF) + 3×3(每通道 3 cmd) ≈ **16 命令 + 320ms 显式 sleep + 物理运动 + 相机曝光**

**FOV 总数估算**：20x objective + 5MP 2/3" sensor → FOV ≈ 0.55mm；8.62mm circle + 10% overlap step≈0.5mm → 17×17 grid ⨯ 圆形掩膜 ≈ **180-220 FOV**

**单 FOV 预算**：旧 90s/200 ≈ 450ms；新 98s/200 ≈ 490ms → **单 FOV 多 ~40ms**

## 主凶 #1（约 6-7s / 8s，~75%）— ACK 心跳抖动 + BF FastLED.show 阻塞

**现象**：每个 SW-trigger 通道的 `turn_on_illumination` 比旧 Squid 多 ~3-5ms，每 FOV 3 通道 + Laser AF 4 次 AF_LASER pin → ~10 个 atomic 命令额外 30-50ms

**旧 Squid 处理**：
- `callback_turn_on_illumination()` (`light_commands.cpp:3-9`) atomic 操作不动 `mcu_cmd_execution_in_progress`
- `send_position_update` interval 10ms（`constants.h:110`）
- BF LED matrix 路径在 `turn_on_illumination` 内分支 LED_ARRAY_FULL 走 `FastLED.show()`（functions.cpp 同名实现）

**octoaxes 处理**：
- `serial.cpp:225` 状态 byte = `any_moving ? IN_PROGRESS : COMPLETED`，与 `mcu_cmd_execution_in_progress` 无关 → atomic 命令处理完下一帧 status=COMPLETED（与旧 Squid 语义等价）
- **但** `turn_on_illumination` BF 路径调用 `turn_on_LED_matrix_pattern → FastLED.show()`（`illumination.cpp:354, 290`），128-LED APA102 串行写约 1.5-2ms，期间 `noInterrupts` 阻塞主循环，下一次 `send_position_update` 推迟到 (FastLED 时间 + 10ms 心跳)
- 主循环每 iter 工作量增加：`axisManager.updateAll()` 4 个 axis state 分发 + `joystick_update()` (PacketSerial + do_focus_control) + `commandprocessor` 二级 dispatch，单 loop ~50-150µs vs 旧 ~30-80µs

**根因**：
1. octoaxes main loop iter 时间增加 → `send_position_update` 心跳实际抖动到 ~10-13ms；每 FOV 16 atomic 命令 × 平均 ~2ms 多抖 ≈ **30ms/FOV**
2. BF FastLED.show 2ms vs 旧 1ms，~3ms/FOV

**占用估算**：(30 + 3) × 200 FOV ≈ **6-7s**

**修复方向（待硬件空闲实施）**：
1. `axisManager.updateAll()` 加 `if (!isMoving) continue;` 跳过空闲轴的 state 比较
2. `send_position_update` 节流上移到 `FastLED.show` 之前；或 LED 写丢到独立标志位延迟执行
3. 评估 `INTERVAL_SEND_POS_US` 10000 → 5000（上位机协议无影响）

## 主凶 #2（约 1.5-2s / 8s，~25%）— `motor_moveToMicrosteps` SPI 操作翻倍

**硬事实**：
- 旧 Squid `tmc4361A_moveTo`（`TMC4361A_TMC2660_Utils.cpp:1264-1283`）= **4 个 SPI** ops（EVENTS×2 + XTARGET + XACTUAL）
- octoaxes `motor_moveToMicrosteps`（`MotorControl.cpp:665-754`）= **9-12 个 SPI** ops，含 line 715 **无条件 VMAX 写**，velocity_mode 切换路径还多 BOW/AMAX/DMAX/RAMPMODE/GENERAL_CONF 7 个寄存器读写
- `axis.cpp:561` 还有一次 pre-status `motor_readStatus()`，与 move 内部 STATUS 读重复
- `motor_moveToMicrosteps` 还含 `clampTargetByDirection` + `isWithinSoftLimits` + currentPos read，1-2 次额外 SPI

**根因**：每次 move 多 ~250µs SPI；Z VMAX 写可能引发 chip 内部 ramp 重启延迟 1-5ms

**Laser AF 影响放大**：
- `_move_z` → `cephla.py:69-97 move_z`：rel_mm<0 触发 backlash = 2 段 `move_z_usteps` + 2 wait
- 每 FOV Laser AF 估 2-3 次 Z move + 2 次 XY move = 5 次 move

**占用估算**：5 × 250µs SPI × 200 FOV ≈ 0.25s 直接 SPI；加 VMAX 写引发 chip 内部刷 ramp ~1-5ms × 200 FOV ≈ **1.5-2s 总**

**修复方向（待硬件空闲实施）**：
1. `motor_moveToMicrosteps` 去掉 line 715 无条件 VMAX 写（仅 velocity_mode 切回时写一次）
2. `axis.cpp:561` pre-status 改为仅在 `_needReenableLimits` 时按需读
3. 对齐旧 Squid 紧凑序列：EVENTS×2 + XTARGET + XACTUAL

## 次要嫌疑（留给下轮）

1. `joystick_update → do_focus_control` 即使 delta=0 也跑 1 次 VIRT_STOP_* read → 加 delta==0 early-return
2. `processSerialStandardCommands` 50+ case switch + commandprocessor 二级转发 vs 旧 Squid 函数指针表 → 微差 ~5µs/cmd（量级不显著）
3. USB serial 包 1ms jitter 与 PC 状态相关，非代码层

## 数据信心度

**硬事实**（代码直读 file:line 可复核）：
- 旧 Squid `interval_send_pos_update = 10000`（`constants.h:110`）；octoaxes `INTERVAL_SEND_POS_US = 10000`（`serial.cpp:21`）+ 下降沿即时发（`serial.cpp:195-199`）
- 旧 Squid `tmc4361A_moveTo` 4 SPI vs octoaxes `motor_moveToMicrosteps` 9-12 SPI
- `status` 返回语义两端等价
- 上位机两端均下发 MAX_VELOCITY_X/Y/Z = 25/25/2 mm/s（`_def.py:652-654`），物理运动时间一致 → **非主凶**
- LED matrix 都走 `FastLED.show`（旧 functions.cpp / 新 illumination.cpp:290）

**推断**（需打点实测验证）：
- octoaxes 主循环工作量增加导致 ACK jitter +3ms — 实测 send_position_update 间隔分布
- BF FastLED.show 阻塞导致 ACK 多等一拍 — `turn_on_illumination` 前后打 micros() 时间戳
- motor_moveToMicrosteps 多 SPI 累积 — 单次 move handler 进出打点

## 不烧录就能做的实测验证

Python 侧已有的工具足以隔离主凶 #1：

1. **启用 `wait_till_operation_is_completed` 现有 elapsed_ms 日志**（`microcontroller.py:1567` 附近），跑 multipoint 10 FOV
2. 按 cmd 类型聚合统计 mean/p50/p90/max 延迟，对比旧机 vs 新机
3. 关键比较项：
   - atomic 命令（set_pin_level, set_illumination_*, turn_on/off_illumination）平均延迟
   - move 命令（move_x_to / move_z）平均延迟
   - 短距 Z micro-move（Laser AF）专门统计

若 atomic 命令平均延迟差 ~2-3ms → 验证主凶 #1
若 short move 平均延迟差 ~5-10ms → 验证主凶 #2

## 后续路线

1. **先实测验证**（不动 firmware，仅 Python 侧打开日志）→ 确认主凶占比
2. 实测数据反过来 calibrate 估算
3. 硬件空闲时实施 #1（loop 优化 + LED 节流）评估收益
4. 评估是否需要 #2（move SPI 紧凑化）
