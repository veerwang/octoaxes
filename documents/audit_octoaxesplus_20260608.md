# octoaxesplus 审计报告（固件 + 上位机）

**日期** 2026-06-08 · **分支** newz
**范围** `firmware/octoaxesplus/`（自有源码，不含 `tmc/` 共享驱动）+ `software/octoaxesplus/` profile + 其实际走到的 `software/common/`
**方法** 两个审查 agent 并行通读 + 对所有 headline 项逐条回读源码核实（已剔除自我撤回的误报）。

---

## 0. 重点：homing 方向 ↔ 编码器极性一致性 ⭐

**结论：当前 `flip=1` 同时满足"编码器朝电机变小"与闭环对齐两个要求 —— 但这一致性是隐式的、跨三处独立控制、且闭环从未实测，是真实风险点。**

### 方向链路实测事实

- Homing：朝**电机端**限位逼近 → 撞限位硬停 → 反向退出 → 该点置零。置零后工作行程 `limits=(-100, 34000)`，即**远离电机方向为正 0→+34000**。
- 开环实测（TODO 2026-06-08）：`flip=0` 时 ENC vs XACTUAL `ratio=-0.997`（反向）；`flip=1` 时 `ratio≈+0.997`（同向）。
- 工程师诉求：滑块**朝电机走，编码器读数变小**。

推导：置零点在电机端 → 远离电机 XACTUAL 增大。`flip=1` 让 ENC 跟随 XACTUAL（同向）→ ENC 在电机端≈0、远离电机增大 → **朝电机变小 ✓**。`flip=0` 时 ENC 反向，会"朝电机变大"——正是工程师观察到的"极性相反"，他翻 `flip=1` 是**正确的**。且 `flip=1`（ENC 与内部计数器同向）也正是闭环 PID 稳定所**必须**的对齐方向，一举两得。

### 三个一致性隐患（按风险排序）

**[ENC-1 · 高度关注] 闭环从未验证，方向错=竖直 Z 正反馈飞车。**
开环 ratio=+0.997 符号正确，理论闭环稳定，但 `ENABLE_STAGE_PID` 闭环至今未开。竖直 Z 一旦编码器方向与运动方向反向，闭环即正反馈→失控。**开闭环前必须在限程内、手扶断电旁实测确认收敛**，不能凭开环符号直接上闭环。

**[ENC-2 · 中] 编码器方向由三处独立控制，无单一数据源。**
1. `config.h:483 invertEncoderDir = true` —— 上电 `begin()` 时 `motor_initABNEncoder`（axis.cpp:147）用它。
2. `constants.py:38 encoder_flip_direction = True` —— GUI 启动下发 `CONFIGURE_STAGE_PID`，固件 `configureStagePID`（axis.cpp:884）用它重新初始化 ABN 编码器、**覆盖第 1 项**。
3. `config.h Z_AXIS.invert_direction`（默认 false）—— `getCurrentPositionMicrosteps/getEncoderPositionMicrosteps`（axis.cpp:708/720）对返回值再取反一次（2026-05-25 滤光轮镜像层）。

三者当前 `true / true / false` → 净效果自洽，但 #1 与 #2 必须人工保持一致：若只改 constants.py 为 `flip=false`、忘改 config.h，则上电瞬间（GUI 配置前）编码器方向与配置后相反。建议两处互相加注释指明"另一处必须同步"，文档写明"runtime flip 覆盖 boot invertEncoderDir"。

**[ENC-3 · 中] `invertEncoderDir` 没纳入 Z 变体宏，破坏"一个开关描述全部新旧 Z 差异"的不变量。**
`homingSwitch / 极性 / Flipped` 都走 `Z_*` 宏切换，唯独 `invertEncoderDir=true` 裸硬编码。旧 Z 编码器禁用时无害，但变体开关承诺被打洞。建议补 `#define Z_INVERT_ENCODER`。

**附带观察**：`getCurrentPositionMicrosteps` 与 `getEncoderPositionMicrosteps` 函数体完全相同（冗余非 bug）。另需确认：homing 置零写 XACTUAL=0，而上报/软限位 clamp 读 ENC_POS，两者间是否存在固定偏移（homing 后应都≈0，导程实测时顺带核对）。

> **决策（用户 2026-06-08）**：ENC-1/2/3 先不动代码，仅记录。闭环实测前不改。

---

## 1. 固件 findings（已核实，按严重度）

| # | 位置 | 级别 | 问题 | 建议 |
|---|---|---|---|---|
| F-1 | `trigger.cpp:154` | **严重** | 频闪 ISR 在"短曝光≤30ms"路径里 `delayMicroseconds(illumination_on_time_us)`，ISR 内最长忙等 30ms，阻塞位置上报/串口/脉冲恢复。长曝光路径已异步两步，短曝光却退化成阻塞。 | 短曝光也改异步两步（开灯记时戳→elapsed 到点关灯），ISR 彻底去阻塞。 |
| F-2 | `config.h` + `illumination.cpp:70` | **严重** | `kDigitalOutputPins[]={6,9,10,15}` 含 pin 9/10，而 9/10 是 `CAMERA_TRIGGER_1`/`ILLUMINATION_D8`；`illumination_init` 对其 `OUTPUT+LOW` 会覆盖 `trigger_init` 电平，初始化期可能误触发相机。HC154 通道号与 GPIO 引脚号在 `Pins` 命名空间数值重叠易混淆。 | `kDigitalOutputPins` 排除 trigger/TTL 已接管引脚；或显式定义初始化顺序与排他。 |
| F-3 | `axis.cpp:1074-1078` | 中 | `isWithinSoftLimits()` 永远 `return true`（注释"暂时"）。第一层软限位防护是空壳，全靠 `clampTargetByDirection` 兜底。 | 实现真实边界判断，或删调用点统一收口到 clamp。 |
| F-4 | `illumination.cpp:199` | 中 | `set_DAC8050x_output(channel,...)` 不校验 channel，越界写到 DAC80508 的 CONFIG/GAIN 等寄存器，可能锁死器件。 | 入口加 `if(channel<0||channel>7) return;` |
| F-5 | `stepaxis.cpp:240` | 中 | 离开 home 用 `maxVelocityMM` 而非 `homingVelocityMM`，短行程轴可能过速冲对端限位；FilterWheel 同段用 homing 速度。 | 改 `homingVelocityMM`，与旧 Squid/FilterWheel 对齐。 |
| F-6 | `filterwheel.cpp:6` / `objectives.cpp:6` | 中 | `new float[count]` 无析构，`AxisManager` 析构 delete 轴时泄漏。 | 加析构 `delete[]` 或改定长数组。 |
| F-7 | `commandprocessor.cpp:347` | 低 | `handleSetLimSwitchPolarity` 用 `findAxisByName("W")` 无 W→W1 兜底，octoaxesplus 下改不了 W1/W2 限位极性。 | 改 `findAxisByNameWithFallback`。 |
| F-8 | `config.h:597` | 低 | `EXPAND3_AXIS.currentRange=0` 与 `Z_AXIS.currentRange=1` 不一致；若把 1.5A 新 Z 接 EXPAND3 会算错电流。 | 统一为 1，或注释标"仅旧 Z/TMC2660"。 |
| F-9 | `axis.cpp:248-273` | 低 | 正常状态切换也调 `handleEmergency()`，命名误导，将来加真急停逻辑会误触发。 | 正常上报与 emergency 路径分离。 |
| F-10 | `illumination.cpp:172` | 低 | `illumination_update()` 首次迭代 `delay` 阻塞主循环 ~14ms（bring-up 遗留）。 | 移到 setup() 末尾或改 millis() 非阻塞。 |
| F-11 | `stepaxis.cpp:78` | 低 | `applyBacklashCompensation` 用 `while(isMoving()) delay(10)` 轮询，单线程下冻结 `updateAll()` 形成死锁（当前未启用）。 | 改状态机驱动。 |

**固件亮点**：HC154 片选成对选通/归零结构清晰；看门狗单次触发；`clampTargetByDirection` BOUNDARY_MARGIN 有完整 git 溯源；`beginAll` 失败不停机利于 bring-up；`illumination_init_matrix_early` 幂等抑制 APA102 上电亮态。

---

## 2. 上位机 findings（已核实，按影响）

| # | 位置 | 级别 | 问题 | 建议 |
|---|---|---|---|---|
| S-1 | `test_panel.py:255` | 中 | `EXPECTED_AXES={"X","Y","Z","W"}` 硬编码，octoaxesplus 固件回 W1/W2 → 集成测试必然误判 missing ['W']。profile-safe 违规。 | `EXPECTED_AXES = set(AXIS_CONFIG.keys())`。 |
| S-2 | `main_window.py:1163/1185/1280` | 中 | `_AXIS_PROTOCOL={X,Y,Z,W}` 缺 W1/W2 → 对 W1/W2 点 Enable/Disable、设速度/加速度静默失效（仅 log 灰字）。 | 加 `"W1":AXIS.W,"W2":AXIS.W2`。 |
| S-3 | `main_window.py:903-909` | 中 | 24 字节响应包硬编码把 `data[14:18]` 解析进 `"W"` 键；octoaxesplus 无 "W" 轴 → W1/W2 位置从命令响应包永远读不到。需确认固件该 slot 在 octoaxesplus 填谁。 | 24 字节路径也按 `AXIS_CONFIG[axis]["index"]` 查表，与 40 字节统一。 |
| S-4 | `constants.py` W1/W2 | 中 | W1/W2 缺 `actuator_motor_current_ma`/`hold_ratio` → `_configure_actuators` 因 `None` 静默 continue，不下发 CONFIGURE_STEPPER_DRIVER，电流凭固件默认且无告警。 | 补全字段，或对 filter_wheel 单独处理 + log。 |
| S-5 | `main_window.py:1480` | 低 | `if axis not in ["E4","W"]` 硬编码轴名（octoaxesplus 恒为真），意图不清。 | 改按 `AXIS_CONFIG[axis]["type"]`。 |
| S-6 | `widgets.py:534…` | 低 | `set_current_axis/get_move_distance` 硬编码 `["Z","E3"]`/`["X","Y"]` 分支；W1/W2 靠 filter 页面绕过，无实际 bug 但结构脆。 | 改 `AXIS_CONFIG[axis]["type"]` 驱动。 |
| S-7 | `main_window.py:1557` | 低 | `tpr` 用 2 字节(uint16)下发，无 `assert tpr<=65535`；现值 10000 安全，未来高分辨率会静默截断。 | 加边界 assert/clamp+log。 |
| S-8 | `serial_thread.py` | 低 | 命令缓冲满时静默丢命令，GUI 只 log "Failed to send"，界面无可见告警。 | 丢弃时 emit `error_occurred`。 |

⚠️ **线程安全（建议核实）** `run_w_test()` 在 daemon 线程里调 `wait_until_idle()`，其内部 `QApplication.processEvents()` 从非 GUI 线程调用是 Qt 未定义行为，有崩溃风险。建议非 GUI 线程改 `time.sleep` 轮询 + 信号同步。

**上位机亮点**：AXIS_CONFIG 驱动的动态轴注册大部分路径到位；`AXIS_MM_PER_STEP` 从 AXIS_CONFIG 派生（单一数据源）；40>24>ASCII 三级混流解析 + CRC 触发消费清晰；`Z_AXIS_VARIANT/_Z_VARIANTS` 一行切换无重复代码。

---

## 3. 跨层一致性

- **协议轴码 5 = "W" 在 octoaxesplus 普遍缺 W→W1 兜底**：S-2/S-3/F-7 是同一根因三处显形。建议集中治理：固件统一 `findAxisByNameWithFallback`，上位机 `_AXIS_PROTOCOL` 统一从 AXIS_CONFIG 生成。
- **24 字节包语义需固件↔上位机对账**：octoaxesplus 命令响应 `bytes[14:18]` 填 W1 还是空，决定 S-3 怎么改。
- **电流 RMS/峰值语义**（记忆 `cmd21-current-rms-peak-mismatch`）：constants.py 注释"峰值"但 TMC2660/TMC2240 两路径语义不同，新 Z 走 TMC2240 峰值路径，注释需对齐固件。

---

## 4. 建议修复优先级

**立即（严重/会误判/正反馈风险）**
1. F-1 ISR 去 `delayMicroseconds`
2. F-2 `kDigitalOutputPins` 排除相机/TTL 引脚
3. ENC-1 开闭环前实测编码器方向收敛（竖直 Z 安全）
4. S-1 `EXPECTED_AXES` 动态化（否则集成测试恒红）

**本迭代（功能完整性）**
5. S-2/S-3/F-7 W→W1/W2 兜底集中治理
6. S-4 W1/W2 电流参数补全或显式 log
7. F-3 `isWithinSoftLimits` 落地，F-4 DAC channel 校验，F-5 离开 home 用 homing 速度

**后续**：ENC-2/ENC-3 编码器方向单一数据源 + 纳入变体宏；F-6 析构泄漏；S-5~S-8、F-8~F-11。
