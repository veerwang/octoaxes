# TODO.md

任务跟踪文件，用于管理项目待办事项。

> 详细重构方案参见：`documents/refactoring-plan.md`

## 进行中

<!-- 当前正在处理的任务，建议同时只有 1-2 个 -->

- [ ] **🔵 新 Z 轴调试（newz 分支，当前优先）** (2026-06-06) — 暂停 Turret，优先调试新 Z 轴（MOONS' LE143S-W0601，导程 1mm/1.8°/1.5A，TMC2240 驱动板 currentRange=1，见记忆 newz-axis-le143s-spec）。待用户说明具体调试目标/现象。

- [ ] **⏸️ octoaxesplus Turret homing（已暂存，根因已定位，待恢复）** (2026-06-06) — **真根因**：改 config.h `homing_direct` 无效，因为**上位机每次 HOME 都用 data[3] 覆盖** `_config.homing_direct`，而 data[3] 由 `constants.py` 的 `movement_sign` 推导（main_window send_homing:715 `home_dir=1 if sign==1 else 0` → commandprocessor:151 `new_direct`）。Turret movement_sign=-1 → 永远 homing_direct=+1（逆时针），与 config.h 无关。用户实验 `-1*speedInternal` 能翻向证明 chip 能反向。Previous/Next 不受影响（move_objective 硬编码符号、不读 movement_sign）。**硬件**：J4/J5 未焊，home 信号经主板→J1→STOPL(pin12)，HOME_REF(pin13) 悬空；06-04 dump 证 chip 静态看得到 STOPL。**新脚本** `software/common/tests/turret_homing_only.py`（纯 homing + 高频 DUMPREGS 时间序列 + 判读停没停/STOPL 是否 active，`--dir 0/1`）。**WIP 已提交**：enableLeftLimitSwitch=true / VERSION=107 / Turret movement_sign 改 1（测反向）。
  - [ ] **恢复时第一件事**：脚本各跑 `--dir 0`/`--dir 1`，看哪个方向 VACTUAL 归 0 + 运动中 STOPL 是否 active。能停 → 解耦 homing 方向与 movement_sign（firmware objective 轴 HOME 忽略 data[3] 用 config.h，或 GUI 单独给 objective homing 方向）；两方向都不停 → 软件停车（enableLeftLimitSwitch=false + objectives.cpp 命中限位补 setVelocityInternal(0)+delay+setCurrentPos(0)+moveTo(0)）。
  - [ ] （独立）Turret 电流 1800mA 峰值可能偏低（已验证物镜 3100mA/TMC2660），如换位丢步再提，与"不停"无关。

- [x] **octoaxesplus Turret homing 到零点后来回震荡、停不下来（根因已定位，修复待实施）** (2026-06-05) — 用户实测：Previous/Next 正常（位置模式有斜坡+target），但 homing 到零点后**电机来回震荡无法停**、software 读数持续往负方向涨（-123,-234...）。**根因**：config.h EXPAND1_AXIS 把 chip 两个限位硬停都禁用了（`enableLeftLimitSwitch=false`+`enableRightLimitSwitch=false`，改用「readLimitSwitches 读原始引脚 + 软件 poll + 软件停车」架构），但 objectives.cpp 的**软件停车被回退**（本次按用户要求只留 homing_direct 方向跟随）→ 速度模式 homing 撞限位后没有任何东西让它停。对比 StepAxis/FilterWheel：两者命中限位都有 `motor_setVelocityInternal(0); delay(100)` + 切回位置模式（`motor_moveToMicrosteps(0)`），唯独 Objectives 直接 `setCurrentPositionMicrosteps(0)` 从不停速度/从不切模式。**推荐修复**（对齐 FilterWheel 已验证范式）：`STATE_HOMING_SEARCH` 命中限位时补 `motor_setVelocityInternal(0); delay(100); motor_setCurrentPositionMicrosteps(0); motor_moveToMicrosteps(0);`（== 判定保留，无需位与 &）。**教训**：禁用 chip 硬停（config）+ 软件停车（状态机）是配套架构，不能只动一侧。
  - [ ] 实施修复 → 编译 → 用户烧录实测 homing 在零点正确停车 + 4 物镜切换不受影响

- [x] **octoaxesplus Turret homing 限位开关方向修复（实测定位）** (2026-06-04) — 用户报告 octoaxesplus Turret homing 到限位点不停车、冲过去。诊断：用 `dump_axis_state.py Turret` 读 TMC4361A STATUS 寄存器，**手工 A/B 实测**：压限位时 `STOPL_ACTIVE_F (bit7) = 1` / `STOPR_ACTIVE_F (bit8) = 0`，离开限位 `STOPL=0` —— home 传感器实际接 **TMC4361A LEFT 输入**。但 2026-06-02 配置照搬 octoaxes E1 的"home 接 RIGHT"假设写成 `homingSwitch = RGHT_SW`，导致 `objectives.cpp:85 if(limit_state==homingSwitch)` 比较 `0b01==0b10` 永远 false → 到限位不认 → 不停。**交叉验证**：objectives 分支 octoaxes W 轴（物镜实测可用配置）= `LEFT_SW / leftEn=true / rightEn=false`，与实测一致，反证 RGHT_SW 是未验证假设。**修复**（仅 `firmware/octoaxesplus/config.h` EXPAND1_AXIS 三行）：`homingSwitch` RGHT_SW→**LEFT_SW**、`enableLeftLimitSwitch` false→**true**、`enableRightLimitSwitch` true→**false**。objectives.cpp leaving-home 离开方向自动走 else 分支（负速度离左限位），无需另改。编译 SUCCESS（FLASH 85212 无增量）。**未烧录**待用户重烧后测 homing 是否正确停车。
  - [ ] **（待用户实测确认）octoaxes EXPAND1_AXIS 同款 RGHT_SW 假设是否也要改 LEFT_SW** — 当前 develop `firmware/octoaxes/config.h` EXPAND1_AXIS 同样是 `RGHT_SW / leftEn=false / rightEn=true`（2026-06-02 同批写入），但 octoaxes 板 Turret homing **一直未烧录实测过**。objectives 分支 W 轴用 LEFT_SW 是强参考，**很可能** octoaxes Turret 也需 LEFT_SW，但它是另一个连接器（icID=5 / pin19 CS，非 objectives 的 W/icID=3 连接器），缺该板实测证据。**本次按用户要求只改 octoaxesplus，octoaxes 暂不动，仅文档记录**。修法（确认后）：octoaxes config.h EXPAND1_AXIS 三行同 octoaxesplus 改法。验证手段：在 octoaxes 板手工把 Turret 推到限位，`dump_axis_state.py Turret` 看 STOPL/STOPR 哪个 active。

- [x] **物镜轴 E1 → Turret 全局重命名（两固件 + 共享 software）** (2026-06-02) — "E1" 是历史扩展槽位名，不直观；物镜转换器改用行业标准名 **Turret**。逻辑轴身份全套改名（**协议值 44/45/轴码 7 不变，串口兼容**），保留硬件槽位名 EXPAND1_AXIS/EXPAND1_AXIS_CS/PIN_CS_E1/R_AXIS_CS/IC_E1，不动 E3/E4。改名映射：轴名字符串 "E1"→"Turret"、命令 MOVE_E1/MOVETO_E1→MOVE_TURRET/MOVETO_TURRET、C++ 函数 handleMoveE1/handleMoveToE1→handleMoveTurret/handleMoveToTurret、AXIS.E1→AXIS.TURRET、protocolAxisToName case 7、beginAll equals("Turret")、两 .ino new Objectives(...,"Turret",4) + 局部变量 turretAxis、两 profile constants.py key "E1"→"Turret"、main_window homing/_ensure_objective_configured 协议映射、AXIS_MOVE/MOVETO_CMD_MAP、verify_profiles/test_04 期望集。两固件编译 SUCCESS；两 profile 加载（octoaxes ['E3','E4','Turret','W','X','Y','Z'] / octoaxesplus ['Turret','W1','W2','X','Y','Z']）+ py_compile 通过。**未烧录**

- [x] **octoaxesplus beginAll 死轴容错（与 octoaxes 对齐）** (2026-06-02) — 2026-05-26 octoaxes 加了 begin 失败时 `delete axes[i]; axes[i]=nullptr;`（让 findAxisByName 返回 nullptr → handler if(axis) silent no-op → 不卡 5s timeout），但**当时漏同步到 octoaxesplus**。本次补齐：octoaxesplus `axesmrg.cpp::beginAll` 失败分支加同款 delete+nullptr。修复前隐患：缺驱动板（如 E1）时该轴指针仍非空，一旦发 move/home → 打死 chip → 卡 "moving" → any_moving 永真 → 上位机 wait 5s 超时拖累整机。修复后：缺任何板都不影响其他轴，缺板轴命令 silent no-op + 即时 COMPLETED。编译 SUCCESS。**未烧录**

- [x] **移植 objectives 分支 move_objective 增强到 develop common** (2026-06-02) — develop 的物镜换位是基础版（仅 mm→μm 截断 + 单发相对运动）。从 objectives 分支移植增强版到共享 common 层（octoaxes/octoaxesplus 两 profile 共享）：① `define.py` 新增 OBJECTIVE_NEXT_SIGN=-1 / OBJECTIVE_BACKLASH_FACTOR_MM=0.2 / OBJECTIVE_MOTOR_CURRENT_RMS_MA=1000 / I_HOLD=0.5 / MAX_VELOCITY=0.5 / MAX_ACCELERATION=10.0；② `main_window.py` import 扩展 + `__init__` 加 `_objective_move_direction`/`_objective_configured`；③ homing handler 从硬编码 `if axis not in ("W","E4")` 改为 **type-based**（filter_wheel→offset / objective→重置方向基线 / else→set_limits，profile-safe，顺带修正 octoaxesplus W1/W2 漏 offset + E1 误走 set_limits 两个旧 bug）；④ 新增 `_move_step_axis_relative_usteps`（直接微步不经 μm 截断，省 ~7 微步/位累积）；⑤ 新增 `_ensure_objective_configured`（首次换位懒加载下发柔和电机参数：螺距/微步/电流RMS/速度/加速度，**协议映射加 "E1": AXIS.E1** 让 develop 的 E1 物镜轴生效，objectives 分支当年物镜叫 W 才跳过 E1）；⑥ `move_objective` 重写：齿轮回程间隙补偿（换向先吃齿隙+等完成防 mid-flight 覆盖）+ 运动期间使能 W 轴+同步等待到位防掉电丢步。两 profile constants 加载 + define 常量 + py_compile 全通过。无固件改动。**未实测**

- [x] **octoaxesplus AXIS_R 启用为物镜转换器（复用 E1 协议）** (2026-06-02) — 把物镜转换器代码适配到 octoaxesplus 的物理 R 轴（HC154 ch3）。决策：**复用 octoaxes E1 协议**（axisName="E1" + MOVE_E1=44/MOVETO_E1=45 + 协议轴码 7），不新建 R 协议；**电机参数沿用 octoaxes E1 调参**。改 9 处：① `config.h` Commands 加 MOVE_E1=44/MOVETO_E1=45；② `config.h` OBJECTIVES_MOTOR_PEAK_CURRENT 1000→1800 + MAX_ACCELERATION_OBJECTIVES 200→80；③ `config.h` EXPAND1_AXIS（beginAll "E1" 用此模板）homingSwitch LEFT_SW→RGHT_SW + enableLeft/Right 对调 + currentRange 0→1；④ `tmc/hal/TMC_SPI.cpp` HC154 分支 icID=5 槽位 HC154_AXIS_F2→**HC154_AXIS_R**（仅影响 octoaxesplus，改动在 `#ifdef USE_HC154_CS` 内）；⑤ `octoaxesplus.ino` `new Objectives(R_AXIS_CS,5,"E1",4)`+addAxis（第 6 轴）；⑥ `commandprocessor.h` 声明；⑦ `commandprocessor.cpp` handleMoveE1/handleMoveToE1 + protocolAxisToName case7→"E1"；⑧ `serial.cpp` 分发；⑨ `software/octoaxesplus/constants.py` 加 E1（type=objective/index=5/limits(0,3)/sign=-1）。**复用现成**：beginAll 已有 "E1"→EXPAND1_AXIS 分支；common define.py/main_window/widgets 已 E1-ready（octoaxes 那次铺好）；Objectives 类两固件一致。两固件编译 SUCCESS（octoaxesplus FLASH 85212，octoaxes 无回归），两 profile constants 加载通过（octoaxesplus 轴列表 +E1）。**已知限制同 E1**：不在 24 字节响应包 → GUI 位置不回读。**未烧录**待实测（homing/换 4 物镜方向/1800mA 防丢步）

- [x] **物镜转换器代码适配到 E1 轴（不动 W）** (2026-05-29) — objectives 分支（领先 develop 7 commit）为测试把 W(icID=3) 改成 Objectives，会破坏 develop 的 W 滤光轮成果（72ms 优化/编码器/W2）。本次把**物镜通用代码适配到 E1 轴**，W 完全不动。硬件确认：6 轴并存 X/Y/Z/W/W2/**E1(物镜,4 物镜)**，E1 CS=pin19/CLK=pin28/**icID=5**(新增)。改 12 文件：① firmware `TMC_SPI.h` IC_COUNT 5→6；② `TMC_SPI.cpp` PIN_CS_E1=19 + 第 6 IC 槽(CLOCK_EXPAND)；③ `octoaxes.ino` `new Objectives(EXPAND1_AXIS_CS,5,"E1",4)`+addAxis；④ `config.h` EXPAND1_AXIS(RGHT_SW+enable对调+currentRange=1) + 加速度 200→80 + 电流 1000→1800 + Commands `MOVE_E1=44`/`MOVETO_E1=45`；⑤ `objectives.cpp` homing 根因修复 OBSW_SW→`_config.homingSwitch`(3 处)；⑥ `commandprocessor.{h,cpp}` handleMoveE1/handleMoveToE1 + protocolAxisToName case7→"E1"；⑦ `serial.cpp` 分发；⑧ `define.py` AXIS.E1=7 + MOVE/MOVETO_E1 + CMD_MAP 改专属命令；⑨ `constants.py` E1 limits(0,3)/movement_sign=-1/index=5；⑩ `main_window.py` homing _AXIS_PROTOCOL 加 E1；⑪ `widgets.py` objective 隐藏 Test/Rounds。**为何加专属命令**：MOVE_W 硬编码到"W"无轴索引，objectives 分支靠改 W 绕开路由；放 E1 必须仿 W2 加 MOVE_E1/MOVETO_E1（旧 Squid 不发，不破坏 drop-in）。两固件 + 两 profile 编译/加载通过（octoaxes FLASH 81628）。**已知限制**：E1 不在 24 字节响应包 → GUI 位置不回读（运动/homing 正常），需 40 字节扩展包。**未烧录**待实测

- [x] **filterwheel.cpp homing 方向 bug 修复** (2026-05-21, commit 2b5dce4)（**后于 5-25 撤销 W 部分**：偏离旧 Squid W 段特定行为）
- [x] **W 量纲对齐 1/64** (2026-05-22)
- [x] **W ASTART = 0** (2026-05-22) — 与旧 Squid sRampInit::rstBits(USE_ASTART_AND_VSTART) 一致，消除短距离过冲
- [x] **W has_encoder = True** (2026-05-22) — GUI 通过 ENC_POS 读 chip 真实位置
- [x] **SQUID_FILTERWHEEL_OFFSET = -0.011** (2026-05-22)（**后于 5-25 恢复 +0.008**：硬件反相移到 firmware 层）
- [x] **GUI 端到端验证 W homing+offset** (2026-05-22 用户实测确认) — 转盘准确停在 1 号孔位
- [x] **撤销 commit 2b5dce4 W 部分** (2026-05-25) — 恢复 filterwheel.cpp 硬编码 + 方向 search，与旧 Squid W 段一致
- [x] **加 firmware 层 invert_direction 反相** (2026-05-25) — axis.h AxisConfig 加字段；axis.cpp 3 个入口反相；filterwheel.cpp homing search 反速度。W_AXIS/EXPAND4_AXIS=true（octoaxes 本硬件镜像装配）
- [x] **SQUID_FILTERWHEEL_OFFSET 恢复 +0.008** (2026-05-25) — software 协议层与旧 Squid 完全一致
- [x] **octoaxes firmware 完整替代旧 Squid firmware** (2026-05-25 用户实测确认) — "旧 Squid software + octoaxes firmware" 与 "旧 Squid software + 旧 Squid firmware" 行为完全一致 ✓
- [x] **W round-trip 测试脚本** (2026-05-25, commit 7e7de9a) — test_w_round_trip.py 与 GUI W Test 按钮完全对齐 (sleep 0.5, send_homing 完整流程)，增强 ENC_POS 准确性验证。10 轮 × 7 槽 = 140/140 通过，累计漂移 +7 µstep
- [x] **W 视觉位置最终验证** (2026-05-25 用户确认) — chip W=+103 µstep (home + offset 完成位置) **实际在 1 号孔位中心 ✓**。之前手动测量的 1 号孔位 chip raw 数据不可靠（8 孔均匀转盘手动定位精度差，转过头一格差 45°）。**chip ENC_POS 是 ground truth，不要用人眼"测量"绝对位置**
- [x] **W 累积漂移根因定位 — motor↔wheel 机械打滑** (2026-05-25 续晚二) — 用户报告"视觉差异越来越大"。降速测试 (4.2→1.0 / 400→80) 仍漂移 → 排除动态扭矩。日志统计 108 条 W-POS：chip_w 全程漂移 ≤ 9 µstep ≈ 0.25°，远小于视觉 22.5°。编码器装在 motor 后端同轴，用户硬件直接观察确认 motor↔wheel 之间打滑。**结论：静态机械预紧不足（紧定螺钉/联轴器/皮带），软件无法修复**。临时降速改动已撤回，工作树干净
- [x] **W2 (Filter Wheel 2) 适配旧 Squid firmware** (2026-05-26) — octoaxes 主线长期未实例化 W2，旧 Squid software 发 MOVE_W2/INITFILTERWHEEL_W2 silent no-op。本次补齐 6 处：① `TMC_SPI.h` TMC4361A_IC_COUNT 7→5（移除 E1/E3 未实例化槽位）；② `TMC_SPI.cpp` PIN_CS_W2=16 + tmc_ic_configs[] icID=4 槽位 (CLOCK_EXPAND)；③ `config.h` `Pins::W2_AXIS_CS = EXPAND4_AXIS_CS` 别名；④ `octoaxes.ino` `new FilterWheel(Pins::W2_AXIS_CS, 4, "W2")` + addAxis；⑤ `axesmrg.cpp::beginAll` 加 "W2"→EXPAND4_AXIS 分支；⑥ `commandprocessor.cpp::handleMoveW2` 完整实现（仿 octoaxesplus）。硬件层与旧 Squid `pin_TMC4361_CS[4]=16 / pin_TMC4361_CLK_W2=28` 字节级一致。3 环境编译通过，FLASH +128B
- [x] **W2 板未插兼容性代码** (2026-05-26) — `axesmrg::beginAll` 检测到 begin 失败时 `delete axes[i]; axes[i] = nullptr;`。findAxisByName 已 nullptr-safe，所有 handler 的 `if (axis)` 保护让命令 silent no-op，响应包 any_moving=false 立即报 COMPLETED。通用化处理（不只 W2 专用），所有 5 轴享受这层保护
- [x] **INITFILTERWHEEL 触发 homing 的字节级偏差修复** (2026-05-26 续) — 用户报告：旧 Squid software + octoaxes firmware 启动时在长 homing 场景下报 `TimeoutError: Current mcu operation timed out after 5 [s]`（旧 Squid firmware 无此问题）。根因对照旧 Squid `callback_initfilterwheel` (commands.cpp:188-192)：仅 `enable_filterwheel=true + init_filterwheel_axis(w)`（chip 寄存器原子配置，**不**触发 homing，**不**设 mcu_cmd_execution_in_progress）。但 octoaxes `handleInitFilterWheel` 错误调用 `axis->startHoming()` → 旧 Squid software `cephla.py::_configure_wheel` 调 `init_filter_wheel + sleep(0.5) + configure_squidfilter` 时，W 仍 homing → any_moving=true → status=IN_PROGRESS → `set_leadscrew_pitch` 后 `wait_till_operation_is_completed` 5s 超时。修复：octoaxes/octoaxesplus 两 firmware 的 `handleInitFilterWheel` / `handleInitFilterWheelW2` 改为 no-op + 日志（W/W2 启动已配 filter wheel 模式 + 后续 configure_squidfilter 会重写关键寄存器，no-op 安全）。教训：drop-in replacement 任何 handler 必须以对方协议规范为准，不能用名字推断行为
- [x] **W invert_direction 回归 false（字节级 drop-in 修复）** (2026-05-26) — 用户报告：旧 Squid software + 旧 Squid fw vs + octoaxes fw，filter wheel next/previous 物理方向相反。根因：2026-05-25 加的 `W_AXIS.invert_direction=true` 让 axis.cpp 入口反相 MOVE_W/MOVETO_W、filterwheel.cpp 反相 motor_setVelocityInternal → 所有 W 运动物理方向与旧 Squid 反。**回滚**：`W_AXIS.invert_direction` true→**false**；`EXPAND4_AXIS.invert_direction` true→**false**（W2 同步）。代码层反相逻辑保留（其他轴未来如需启用直接打开 flag）。**牺牲**：W home+offset 物理停在 +2.87°（你硬件镜像装配引起，与旧 Squid 在本硬件上完全相同的"固有错位"），slot 1 精准对齐需硬件层重装配。**反面教材**：2026-05-25 决策违反 CLAUDE.md 字节级 drop-in 目标，当时回归测试只验证单点位置 + chip frame round-trip（双方向闭环），没测物理方向比对 → 教训：drop-in 验证必须包含多方向对比
- [ ] **硬件紧固 motor↔wheel 机械连接**（硬件待办，软件无法替代）— 检查联轴器/皮带轮紧定螺钉、皮带张力、联轴器弹性元件等。紧固后回测确认漂移消失
- [ ] **（可选）周期 auto-home 软件缓解** — 硬件修复前每 N 次切槽位自动 home 一次，强制 motor↔wheel 重新对齐。硬件修好后此项可不做
- [ ] **（长期可选）编码器移到 wheel 端 + 启用 PID 闭环** — 让 chip_w 反映 wheel 真实位置，chip 自动纠偏机械打滑。需要硬件改装
- [x] **W 轴速度优化二轮 (2026-05-26 续二)** — 1 slot 181ms → **72ms (-60%)**。优化路径：脚本 idle frames 5→1 (B1) + firmware W/W2 target_tolerance 2→20 (B2) + W_AXIS.astartMM 22.5 rev/s² (C v2) + MICROSTEPPING_FILTERWHEEL 64→16→8（路径 D）。全档位 std < 0.4ms 极稳定。失败实验：ASTART=180 @ ms=64 灾难性退化（HOME 17.6s），根因跨微步 chip 寄存器值线性缩放被忽略。完整报告 `documents/baselines/W-speed-optimization-20260526.md`。**字节级牺牲**：MICROSTEPPING=8 (旧 Squid software 覆盖回 64，仅 benchmark 脚本生效)、ASTART=22.5 (旧 Squid 透明)。剩余空间：ASTART 22.5→180 @ ms=8 可能再到 ~65ms 历史水平
- [ ] **（可选）W 速度优化第三轮** — ASTART=180 @ ms=8（匹配历史 chip 寄存器 288K µstep/s²，2026-02-10 已实证 motor 61.3ms / PC ~69ms）
- [ ] ~~优化 W 轴换孔时间~~ ~~基准 144ms，目标 ≤ 60ms~~（2026-05-26 二轮优化 1 slot 达到 72ms，超目标）
- [x] **方向感知闸门完整工程化** (2026-05-09, commits 82dfe2d→e773f21→d92fa2d→df4f1f6→17b8f71, 旧 Squid + octoaxes 双端验证通过) - 包括 reject→clamp 兼容旧 Squid、no-op 短路防 5 秒卡顿、homing VSTOP recovery 完整化、边界 margin 防 chip hard-stop latch 四次迭代
- [x] **上位机限位收紧到物理行程** (2026-05-09, commit febc844) - X (-10, 115000) / Y (-10, 76000) μm，与旧 Squid 配置一致便于复现 VSTOP 场景
- [x] **修复旧 Squid 随机点动 X/Y 卡死** (2026-05-08) - axisName ↔ CS 引脚映射与硬件接线反，X 命令实际驱动物理 Y 电机，导致走到 Y 上限时 fw 把 STOPR_EVENT 归到 X 轴卡死。修复 octoaxes.ino 交换 axisName 字符串
- [x] **协助旧 Squid 定位 5mm 短少 bug** (2026-05-08) - VSTOP 早完成根因，`y_negative=-0.01` 修复（旧 Squid 配置层，方案 A 落地后不再必需但保留无副作用）
- [x] **修复 AXIS_MM_PER_STEP 双源不同步** (2026-05-07, commit 7be758d) - 改 actuator_microstepping 后命令距离与实际位移按比例失配（ms=16 时 5mm→80mm），改为从 AXIS_CONFIG 派生
- [x] **XYZW 全部回退为微步模式** (2026-04-17) - `constants.py` has_encoder = False，响应包保持 24 字节与旧 Squid 兼容

## 待办

### 2026-06-02 记录（暂不修）：octoaxes constants.py X/Y `index` 与固件 icID 不符

- [ ] **（潜在坑，暂不修）octoaxes `software/octoaxes/constants.py` 的 X/Y `index` 反了** —
  现状 X `index=1` / Y `index=0`，但 octoaxes 固件实际 icID 是 **X=icID0 / Y=icID1**
  （`octoaxes.ino`：`new StepAxis(Y_AXIS_CS, 0, "X")` + `new StepAxis(X_AXIS_CS, 1, "Y")`，
  轴名与 CS 引脚名交叉是 2026-05-08 修过的 PCB 接线补偿）。
  **当前无害**：octoaxes 只走 24 字节位置包（X/Y/Z/W 按硬编码 slot 解析，不查 `index`），
  `index` 字段在 octoaxes 上是死配置。**移动命令也不受影响**：MOVE_X/MOVE_Y 按轴名
  `findAxisByName("X")` 路由，与 icID 无关，GUI "X" 永远驱动物理 X 电机。
  **何时会爆**：若 octoaxes 将来启用 40 字节扩展位置包（`main_window.py:872` 用
  `AXIS_CONFIG["index"]` 反查 icID→轴名），X/Y 位置会读反。
  **对照**：octoaxesplus 没这个问题 —— 它 software index(X=1/Y=0) 与固件 icID(X=1/Y=0)
  一致（`octoaxesplus.ino`：`new StepAxis(Y_AXIS_CS,0,"Y")` + `new StepAxis(X_AXIS_CS,1,"X")`）。
  即 **X/Y 的 icID 序号在 octoaxes 与 octoaxesplus 之间是反的**，是两套硬件接线不同的
  各自补偿，运动正确，仅 octoaxes 的 software index 这一处遗留不一致。
  **修法（启用 40 字节包前必须做）**：octoaxes constants.py X `index` 1→0、Y `index` 0→1。

### 2026-05-18 joystick ↔ firmware 10 字节协议加 CRC-8-CCITT 校验

- [x] **joystick 固件目录分离**：`firmware/joystick/{octoaxes,octoaxesplus}/`
  对称主 firmware 目录；`TM1650.h` 用相对符号链接共享（commit fa625d1）
- [x] **CRC-8-CCITT 校验实施**：byte[9] 兼容性闸门（== 0 → legacy 直通，
  ≠ 0 → CRC 校验失败丢包），CRC=0x00 强制映射为 0x01 避免 sentinel 冲突。
  复用 firmware ↔ 上位机协议同款 CRC 算法（poly 0x07 / init 0x00）。
  四工程编译 SUCCESS（commit fa625d1）
- [x] **`S:JOYSTICK_STATS` 调试命令**：firmware 侧输出 `legacy=N crc_ok=N crc_fail=N`
  三个计数器，现场诊断 5 种指纹场景（commit fa625d1）
- [x] **协议落地文档**：`documents/joystick_protocol.md`（218 行 / 9 章节），
  含物理层 / 字段表 / byte[9] 双语义 / CRC 算法 / 兼容性矩阵 / 诊断速查 /
  升级路径约束（commit 8824204）
- [x] **兼容性矩阵补脚注**：核实旧 Squid `functions.cpp:509-546`
  `onJoystickPacketReceived` 函数体不读 `buffer[9]`，"新 joy → 老 fw"
  从"95% 推测"升级为"100% 源码已核实"（commit c5e3867）
- [x] **修复 joystick_print_stats DEBUG_PRINTLN 空宏 bug**（2026-05-18，commit a716980）：
  fa625d1 误用 sendDebugInfo（受 -D DEBUG 控制，生产 env 空宏），改用
  SerialUSB.println 直发对齐 S:HWINFO；新增 `software/common/tests/check_joystick_stats.py`
  查询脚本
- [x] **硬件烧录验证（部分实测，2026-05-18）**：
  - [ ] 回归：新 fw + 老 joystick → 未测（未换回老 joystick）
  - [x] **正向：新 fw + 新 joystick** → 实测 `crc_ok=110 → 5820 / 3.5s`，
    `crc_fail=0` 全程 ✅（commit a716980 烧入后）
  - [x] **反向：新 joystick + 老 fw（含 fa625d1 前 octoaxes）** → 摇杆/焦点/按钮
    全部正常 ✅（与旧 Squid `functions.cpp:509-546` 不读 byte[9] 源码事实一致）
  - [x] **干扰：crc_fail≈0** → 3.5s 内 5820 包零失败，趋势良好（长时间样本待累积）

### 2026-05-15 octoaxesplus W2 端到端打通 + 协议 v2 + GUI 修复全套

- [x] **W2 端到端运动验证通过**（2026-05-15 用户实测确认）
- [x] **协议 v2 (24→40 字节)**：firmware sendExtendedResponse + GUI 双长度解析（commit 2c59071）
- [x] **W1/W2 firmware MOVE/MOVETO handler 实施**（commit 408a8e0）
- [x] **W1/W2 滤光转盘 GUI 菜单**：用 AXIS_CONFIG["type"] 动态判断（commit ba77b20）
- [x] **W1/W2 GUI 按钮（Homing/Previous/Next）响应修复**（commit 3f3de06）
- [x] **profile 隔离工程化**：verify_profiles.py 自动验证 + CLAUDE.md common/ 修改原则（21ac3ca / c905168）
- [x] **AxisManager 不再硬编码 7 轴**（commit 7517f7a）
- [x] **constants.py 去油**：octoaxesplus profile 只 5 轴纯净（commit 120972f）
- [x] **POWER_GOOD bypass 撤销**：电源线飞线修好后 firmware 恢复原 LTC2903 轮询逻辑
- [x] **W1 PCB CLK 走线缺失** (2026-05-18 用户确认) — 飞线已修，CLK 通后 W1 自动可用
- [x] **Z 轴 PyQt 运动单独验证** (2026-05-18 用户确认) — Z 运动通过
- [ ] **bring-up 工具归宿决策**：clk_test/hc154_test/pg_test/pin13_blink 4 个工程 → `.gitignore` 还是归档到 `firmware/tests/`
- [ ] **C 维度 HOME 复杂场景**：handleHomeOrZero AXES_XY 联合归位等扩展

### 2026-05-13 octoaxesplus 真机调试 — 偏离 octoaxes 基线修改待审查

> **前提**：firmware/octoaxes 长期硬件验证可行（XYZW + Y homing 256/30 +
> TMC2660/TMC2240 互换均通过），是 squid++ bring-up 的"已知良好基线"。下列
> 修改是 octoaxesplus 真机调试期间为绕过新硬件问题做的临时改动，部分改了
> octoaxes 同段已验证代码，需逐条审查哪些是 squid++ 必需 / 哪些可能是误判
> 异常的绕过。完整记录见 `SESSION.md` "2026-05-13 后续" 段。

- [x] **IC4 虚焊根因定位 + SPI 通信恢复 + XY 轴运动验证**（2026-05-14）— bring-up
  期间所有 TMC4361A SPI 读返回 0x00，经历 3 个错误假设（PWM 4-bit / LTC2903 RST# /
  Teensy pin 13 短路）逐一否定，用户手动检查 PCB 发现 IC4 引脚虚焊。补焊后
  `S:HWINFO` 识别三轴 TMC4361A+TMC2240，`S:SPITEST` VERSION_NO=0x00000002 稳定
  返回，上位机 PyQt 测试 XY 轴运动通过。详细诊断历程见 SESSION.md "2026-05-14"。
  - [x] B1 PWM 4-bit 撤销（恢复 octoaxes 主线 8-bit + duty 128 基线）
  - [x] S:SPITEST 寄存器笔误修复（0x09 ENC_OUT_DATA → 0x7F VERSION_NO）
  - [ ] Z 轴运动验证（下次会话）
  - [ ] POWER_GOOD bypass 待 PCB 飞线修原理图 +24V_XY net 错误后撤销
  - [ ] bring-up 工具归宿（`firmware/clk_test/` `hc154_test/` `pg_test/`
    `pin13_blink/`）：`.gitignore` 还是归档到 `firmware/tests/`

- [x] **Axis::begin 两个隐患修复**（2026-05-13，本会话 SPI bring-up 时新发现）
  - **隐患 A — csPin 双义性误操作 Teensy 物理 pin**：`axis.cpp:52-53` 的
    `pinMode/digitalWrite(_csPin)` 在 octoaxesplus (USE_HC154_CS) 模式下把
    HC154 通道号 8/9/10 当 GPIO pin 号用，结果误操作 Teensy 物理 pin 8/9/10
    （squid++ 上分别是 `CAMERA_TRIGGER_2` / `CAMERA_TRIGGER_1` /
    `ILLUMINATION_D8`）。初始化时会误触发两路相机和一路激光 TTL。
    修复：用 `#ifndef USE_HC154_CS` 保护这两行，HC154 模式下完全跳过。
  - **隐患 B — motor_initMotionController 返回值未检查**：`axis.cpp:75` 调
    `motor_initMotionController` 但不检查 bool 返回值。SPI 通信失败时（写
    SW_RESET 后读 VERSION_NO 返回 0/0xFFFFFFFF）chip 实际未初始化但 begin
    继续写 SPI_OUT_CONF/VMAX/限位等全部寄存器，最终 return true，上层完全
    不知道 chip 掉线。修复：检查返回值，失败时 DEBUG_PRINT + return false。
  - 同时同步到 firmware/octoaxes 和 firmware/octoaxesplus 两个 axis.cpp
    （保持 A 类 byte-identical 同步约束）
  - 两工程编译均 SUCCESS（octoaxes 80540 / octoaxesplus 81500 字节）

- [ ] **B1 PWM 分辨率改动是否必需**（最可疑）— `octoaxesplus.ino::initializeClock`
  改 `analogWriteResolution(4) + analogWrite(8)`，octoaxes 同样 8-bit + 16MHz
  + duty=128 长期工作 → 理论矛盾。**验证步骤**：① 用 `firmware/clk_test/`
  单独跑 octoaxes 基线写法测 squid++ 板 pin 37 实测波形/电压 ② 若可工作则
  回滚 octoaxesplus 这次改动
- [ ] **B2 POWER_GOOD 轮询 bypass 改 PCB 飞线** — `initializePowerManagement`
  改 `delay(500)` 直接放行。原理图 +24V_XY 标签 bug 不是固件能根治，需要
  PCB 改版/飞线让 IC6 LTC2903 真实监控电源。短期保留 bypass 也要把 500ms
  可配置 + 加 WARN 打印
- [ ] **A1 5 轴注释掉的根因** — commit `1ce942a` 注释 f1/z2/f2/r/t + addAxis。
  应让 `axis.begin` 对未接 TMC4361A 优雅退出（SPI 读到 0xFF 时跳过），而非
  注释源码
- [ ] **A1 axisName "Z1" 为何 findAxisByName 命中失败** — `axesmrg.cpp::beginAll`
  声称支持 "Z"/"Z1" 双名映射，重新阅 commandprocessor + axesmrg 路径确认
  实际行为；如果双名 OK 应回退 zAxis 命名
- [ ] **B3 serial.cpp 3 个调试命令归档** — `S:CLKMODE` / `S:CSHOLD` /
  `S:SPITEST` bring-up 阶段保留；完成后剥到独立 debug 模块或 `#ifdef`
  开关（FLASH +XXXB 不是大问题但要明确属于调试基础设施）
- [ ] **未跟踪测试工程归宿** — `firmware/clk_test/` `hc154_test/` `pg_test/`
  bring-up 工具：决定 `.gitignore` 还是归档到 `firmware/tests/` 复用

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
- [x] **Y homing 异响 + 慢（2026-05-12 解决：256 微步 + 30 mm/s）** — 硬件切回 TMC2660 后用 `diag_y_homing_noise.py` 扫 (微步 × 速度) 矩阵，**实测最优：微步 256 + 速度 30 mm/s 最安静**。配置改动：① `config.h HOMING_VELOCITY_Y_MM = 30`（原 10/15）；② `axis.cpp configureDriver` 撤回 `_config.homingMicrostepping = microstepping` 同步，让 Y homing 永远走 config.h 默认 `HOMING_MICROSTEPPING_Y = 256`；③ chopper HSTRT=0/HEND=0 保留（对齐老 Squid 全局默认）。新增 `S:SET_HOMING_VEL <axis> <vel>` 调试命令（serial.cpp +40 行，FLASH +8K String 库），运行时设 homingVelocityMM 不重烧。新增 `check_homing_vel_cmd.py` 验证 firmware 支持 + `diag_y_homing_noise.py` 交互扫描脚本。**理论**：满速 + 高微步避开 SpreadCycle 低速噪声段，未触及根因（chopper 模式/电流/接地），但生产可接受。**新性能**：76mm / 30 mm/s ≈ 3-4s（原 ~8s，缩半）。**调试基础设施关键发现**：firmware ASCII 调试协议需 `0x55 0xAA` 二进制前缀（DEBUG_PROTOCOL_HEADER_1/2），不是直接 S: ASCII
- [x] **2026-05-12 chopper 对齐 TMC2660 全局默认**（前置改动，axis.cpp `Axis::begin` + `configureDriver` 两处 HSTRT 4→0、HEND -2→0）— 对齐老 Squid `CHOPCONF=0x000900C3` 零滞回静音配置，三 TMC2660 轴同改。单独看未消除异响，但与最终方案叠加无副作用，保留

### 采集流程效率优化（2026-05-18 启动，2026-05-19 二轮 review 后用户同硬件 A/B 实测推翻初步结论）

**最新报告**：`documents/baselines/acquisition_8s_deep_analysis_20260519.md`（已包含 2026-05-19 二轮 review 更新）
**初版报告**：`documents/baselines/acquisition_90vs98_analysis_20260518.md`（已 superseded）

**场景**：Wellplate Multipoint single-well + Laser AF + BF/561/638 三通道 + 20x，~200 FOV，旧 Squid 90s / octoaxes 98s 差距 8s

**实测确认（2026-05-19 用户反馈）**：**同硬件 A/B（同 stage / 相机 / USB / 物镜，只换 firmware）实测仍差 ~8s** — firmware 是真主凶。

**二轮 review 累计可量化 overhead ≈ 1.0-1.1s**（见报告候选主凶表），**仍缺 ~7s 静态 review 看不见**。

- [x] **#2.2 motor_moveToMicrosteps 重复 STATUS 读合并** (2026-05-19, 编译通过未烧录) — `MotorControl.h:219` 签名 void→bool 返回 vstopWasActive，axis.cpp 两处 (line 560-562 + 623-625) 移除 `motor_readStatus`，octoaxesplus 同步。省 ~10-20µs/move
- [x] **同硬件 A/B 实测** (2026-05-19 用户已完成) — 差 ~8s，firmware 是真主凶
- [ ] **写打点 #1（单 cmd 总耗时）firmware 代码** — 优先级最高
  - 位置：`checkForCommand` 入口 + `send_position_update` 发完
  - 输出：`S:CMD_TIMING <cmd_id> <us>` ASCII 调试输出
  - 用途：区分主凶在 atomic cmd / move cmd / homing cmd
  - 估代码量：15-20 行
- [ ] **打点 firmware 烧录测试** — 用户硬件空闲时
- [ ] **#2.2 烧录验证** — 跑现有 X/Y/Z/W 运动测试 + acquisition 确认无回归

**可选清理（独立于 8s 主凶，但消除噪声 ~800ms）**：
- [x] **#3 `completeMovement` 加 `#ifdef ENABLE_DEBUG` 包裹** (2026-05-19，编译通过未烧录) — axis.cpp 两端同步，DEBUG 编译掉后 SPI 不再执行，省 ~200ms / 200 FOV
- [x] **#4 `send_position_update` 缓存 axis 指针** (2026-05-19，编译通过未烧录) — octoaxes/serial.cpp 加 static + 一次性 findAxisByName + cached flag，省 ~400ms / 200 FOV。octoaxesplus 已用 index lookup 无需改
- [x] **#5 `Axis::update` STATE_MOVING `checkLimitPosition` 加 10ms throttle** (2026-05-19，编译通过未烧录) — axis.h 加 `_limitCheckThrottle` elapsedMicros 成员，axis.cpp STATE_MOVING 分支加 `if (_limitCheckThrottle >= 10000)` 节流，对齐旧 Squid `check_limits` 节流，减少 SPI bus 抢占

**仍未排除的可能藏区（~7s 主凶候选，需打点）**：
1. `setMotionParameters` chip-level VMAX/AMAX/BOW 计算差异
2. `Axis::configureDriver` 启动时 SPI 寄存器写入差异
3. 某 cmd handler lib/framework 间接阻塞
4. STATE_MOVING SPI bus 抢占行为差异
5. **VMAX 同值重写是否触发 chip ramp 重启**（没查 TMC4361A 数据手册，1-5s 隐藏开销可能）
6. ISR 干扰（trigger 100µs strobeTimer / PWM / SPI）

**dropped（深读代码挑战不成立）**：
- ~~#1.1 axisManager.updateAll 跳空闲轴~~ — `Axis::update()` STATE_IDLE 已是空 break (axis.cpp:225-227)
- ~~#1.2 send_pos_update 上移到 FastLED 前~~ — `elapsedMicros` 不被 FastLED 阻塞影响 (serial.cpp:198)
- ~~Python 侧 elapsed_ms 日志验证主凶占比~~ — 用户改选直接 A/B firmware

### 框架效率优化（2026-05-12 启动新一轮，协议层）
- [x] **移动完成下降沿立即发包** (2026-05-12, commit a6c5786, **硬件实测**) — `send_position_update` 增加 any_moving 下降沿检测，所有轴 moving→idle 那一帧绕过 10ms 心跳节流立即发 COMPLETED；对旧 Squid + GUI + benchmark 三端同时有效。实测 X/Y 短距离命令省 2-7ms（平均 5ms，10μm 122→116ms 省 5%）；Z 几乎无收益（vmax 慢一个量级，已与心跳 phase 对齐）。每完成一次只触发 1 包，流量影响可忽略
- [x] **B.6 STATUS.TARGET_REACHED 替代 XACTUAL==XTARGET 判完** (2026-05-12, commit 5e487aa, **硬件实测**) — `Axis::checkMovementComplete` 改用 `motor_isTargetReached`，1 SPI 读替代 2 SPI 读（XACTUAL+XTARGET）。顺手清 `_lastPosition` 死字段。实测 X/Y 全档省 0.4-1.0ms，Z 无变化，480 trial 无误判。FLASH +192 字节（linker 拉入未用函数体）。主要价值：判完用 chip 权威信号 + 给 B.5 铺路
- [x] **B.6.1 三 bit 严格判完** (2026-05-12, commit a0e03d5, **硬件实测**) — `motor_isTargetReached` 改为 `TARGET_REACHED AND !VEL_STATE_F AND !RAMP_STATE_F` 三 bit 检查，对齐旧 Squid `tmc4361A_isRunning` 取反语义。单次 STATUS 读多 bit 提取零额外 SPI 成本。实测与 B.6 时间一致（差异 ±2ms 噪声内），480 trial 全过。现有参数下 chip ramp 末尾边缘 case 不存在，B.6.1 是预防性升级，为 Target Pipeline 高速切换铺路。FLASH 持平 72188 字节
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

### octoaxesplus (squid++ 双相机)
- [x] 创建 octoaxesplus 空工程 (2026-04-14) - platformio.ini 复用 octoaxes 配置，tmc 符号链接共享，编译通过
- [x] squid++（双相机）硬件配置 Markdown (2026-04-14) - 由 xlsx 转换，含引脚定义 / 74HC154 片选 / MCP23S17_1 扩展 IO 三表
- [x] 74HC154 片选映射 (2026-04-14) - config.h Pins 命名空间新增 A0-A3 引脚、HC154_Channel 枚举（16 通道）、hc154_init()/hc154_select() 选通函数
- [x] CAMERA_TRIGGER 1..8 引脚映射 (2026-04-14) - config.h 替换 4→8 路，trigger.h NUM_TRIGGER_CHANNELS=8
- [x] Pins 命名空间冲突落地 A1 (2026-04-14) - DAC8050x_CS / X/Y/Z/W_AXIS_CS / ILLUMINATION_D3/D5 共 7 处冲突旧值改为 DEPRECATED_PIN=255（符号保留，下游零修改）
- [x] 8 轴 config.h 主体重构（第一阶段：4 轴 + CS 抽象）(2026-04-16) - 删除 DEPRECATED_PIN + 4 轴 CS 改 HC154 通道号 + TMC_SPI HAL 加 USE_HC154_CS 分支 + illumination TTL 扩 8 端口 + DAC CS 走 HC154；两工程编译均通过
- [x] MCP23S17_1 扩展 IO 驱动 (2026-04-17) - mcp23s17.h/cpp 新建；CS 走 HC154 通道 0；IOCON=0x00 + IODIR=0xFF 全输入 + GPPU=0xFF 上拉容错 + 关硬件中断（轮询模式）；API：readReg/writeReg/readPortA/B/readGPIO
- [x] Pin 28 冲突修复 (2026-04-17) - 删除 TMC4361_EXPAND_CLK（与 TTL5 共用 pin 28 导致 2 MHz PWM 干扰 TTL 输出）；squid++ 单套时钟已够
- [x] 补充 Pins 占位 (2026-04-17) - IIC_WP/SDA/SCL (pin 14/18/19)、RX2/TX2 (pin 16/17)
- [x] **8 轴 AxisConfig 扩展（Z2/F1/F2/R/T）** (2026-05-13, commit 64fa643) - TMC4361A_IC_COUNT 用 ifdef USE_HC154_CS 区分（squid++=8, octoaxes=7）；TMC_SPI.cpp HC154 分支扩 8 条目；config.h 加 Z2/F2/R/T HC154 通道号 + AxisConfig（const struct 拷贝同类轴）；octoaxesplus.ino 实例化 8 轴（Y/X/Z1/F1/Z2/F2/R/T，R/T=Objectives）；axesmrg.cpp beginAll 加 6 个新 axisName 分支映射；两工程编译 SUCCESS
- [x] **merge develop → maxpro** (2026-05-13, commit c03e7c4) - 合并 62 commits 主线进展（Y homing 256/30、Z 编码器、XYZ 速度基线、静默 reject 修复、AF 激光修复、B.6/B.6.1 判完优化等）；4 个冲突文件解决（CLAUDE.md/SESSION.md 手工，TODO.md/constants.py 自动）；两工程编译 SUCCESS
- [x] **同步 octoaxes 主线全部进展到 octoaxesplus** (2026-05-13, commit 266e589) - A 类 10 文件 byte-identical cp、B 类 4 文件 3-way 合并（保留 squid++ HC154/MCP23S17/8 端口适配）、C 类 1 新增 download.sh；最终验证 octoaxes 与 octoaxesplus 仅 7 文件差异，全部 squid++ 硬件资源调整
- [x] **上位机 constants.py Phase 3.1 - 8 轴扩展 + enabled_for** (2026-05-13, commit e9fd888) - AXIS_CONFIG 7→13 条目；X/Y 共享，Z/W/E1/E3/E4 标 octoaxes，新增 Z1/F1/Z2/F2/R/T 标 octoaxesplus；新增 axes_for_model() helper + FIRMWARE_MODELS 常量（默认 octoaxes 向后兼容）；现有 GUI import 全部保留
- [ ] **Phase 3.2 GUI 启动用 S:HWINFO 识别固件型号 + 按 profile 过滤** (Phase 3.1 已完成 metadata 基础设施，需硬件验证)
- [ ] **Phase 3.3 固件响应包扩展 24→40 字节** - 新增 Z2/F2/R/T 4×int32 = 16 字节；octoaxes 仍 24 字节兼容；GUI 根据 S:HWINFO 决定 RESPONSE_LENGTH（需硬件验证）
- [ ] **Phase 3.4 GUI 8 轴位置控件渲染** - 配合 3.2 + 3.3 把 octoaxesplus 全 8 轴显示完整位置反馈
- [ ] **Z2/F2/R/T 真实硬件参数实测调优** - 当前用 const struct 拷贝默认（Z 同 Z1、W 同 F1、EXPAND1 同 R/T），实测后改完整初始化器
- [ ] MCP23S17 接入 Axis 层 - TARGET_REACHED 用于运动完成判定（可选，目前走 XACTUAL 轮询）
- [x] **TRIGGER_IN/OUT1-2 (pin 1-4) 定义** (2026-05-13, commit 4ca1626) - 外部触发联动基础设施：Pins::TRIGGER_OUT1/IN1/OUT2/IN2 + trigger.h 加 NUM_EXT_TRIGGERS=2 + 数组 + 3 个 API (set_out/pulse_out/read_in)；trigger.cpp init OUT/LOW + IN INPUT_PULLUP；未接入命令字 handler 待协议层决策
- [x] **CAM_TRI_READY1/2 (pin 7/6) 定义** (2026-05-13, commit TBD) - 双相机 READY 反馈输入；config.h 加 Pins::CAM_TRI_READY1=7/CAM_TRI_READY2=6；trigger.cpp init INPUT_PULLUP + cam_tri_read_ready(channel) helper。**双相机握手集成**（trigger 发射前等 READY 拉高）属于后续协议层任务
- [ ] **双相机握手集成** - trigger 模块发射前等 CAM_TRI_READY 拉高的逻辑设计（中难度，需要超时/超时路径）
- [x] **硬件资源使用率审计 + EXPAND CS 别名清理** (2026-05-13) - 扫描 44 个 Pin 常量：33 已用 / 5 占位（IIC/Serial2）/ 4 删除 EXPAND1-4_AXIS_CS / 2 补齐（CAM_TRI_READY）；EXPAND1_AXIS AxisConfig 保留作 R/T 模板
- [ ] LT3932 SYNC 核实 - squid++ 是否取消独立 SYNC 引脚（目前占 pin 255 无效），还是挪走
- [ ] 核实 GPB2 INTR_T/F2轴、GPB6 INTR_Z2/F1轴 标签是否原表笔误
- [ ] **核实 squid++ 配置 §1 pin 5/6/7 描述与名称不一致** - 文档标 pin 6 CAM_TRI_READY2 描述"相机1_触发"、pin 7 CAM_TRI_READY1 描述"相机1_等待触发"、pin 5 RESERVED 描述"相机2_等待触发"，三者疑笔误，需对照原 xlsx
- [ ] IIC_WP/SDA/SCL (pin 14/18/19) Wire1 外设方案落地（当前占位）
- [ ] RX2/TX2 (pin 16/17) Serial2 用途定义（当前占位）
- [x] **`tags` 加入 `.gitignore`** (2026-05-09, commit 40bef79b 已在 develop) - tags/TAGS/cscope.* 全部加入

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
