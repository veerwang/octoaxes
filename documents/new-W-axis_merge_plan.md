# new-W-axis → develop 融合计划

> 记录 `veerwang/OctoaxesMega` 仓库 `new-W-axis` 分支相对本仓库 `develop` 的差异，
> 以及逐单元融合的清单与进度。
>
> - 本仓库（baseline）：`/home/hds/gitee.com/octoaxes` 分支 `develop`
> - 目标（under review）：`/home/hds/github.com/veerwang/mega/New-W-Objectives/t2000_OctoaxesMega` 分支 `new-W-axis`
> - **共同祖先（分叉点）**：`488a1ec` `tune(firmware): W PID-P 扫描 + 最终验证配置 ms=8 + P=8192 (1 slot 72ms)`
> - 分叉后：develop 30 提交（newz/Z 变体软件化/Z 老化）；new-W-axis 88 提交（W 物镜化/TMC2240/homing 重复性）
> - **双向分叉，非父子关系**：合并须双向 cherry-pick，`MotorControl.cpp`/`commandprocessor.cpp`/`serial.cpp` 会冲突，不可 fast-forward。

## 关键架构差异

| | 本仓库 develop | Mega new-W-axis |
|---|---|---|
| W 轴 | 滤光轮 `FilterWheel` (icID=3) | **物镜转换器 `Objectives`** (icID=3) |
| 物镜轴 | 独立 `Turret`/E1 (`Objectives`, icID=5) | 无独立轴，物镜就是 W |
| 驱动板 | 混合（TMC2660 + TMC2240 新 Z）| **全 TMC2240** |
| profile | 双 profile（octoaxes + octoaxesplus）| **单 profile**（仅 octoaxes，已删 plus）|
| Z | 新旧 Z 变体软件化（cmd 20 极性/homing 速度下发）| 单一旧 Z |

**决定性洞察**：`objectives.cpp/h` 是同一个 `Objectives` 类文件。Mega 对物镜类的改进
**可直接融进本项目 `objectives.cpp`，让本项目的 Turret 受益，完全不碰 W 滤光轮**。
config.h/octoaxes.ino 里「W 改物镜」的接线改动一律**不要**。

---

## 融合清单（按单元分组）

### ✅ 组 A：建议融合（Mega 领先、本项目缺、风险低）

#### A1. 固件·物镜类改进（落到 objectives.cpp/h，Turret 受益，不碰 W）
- `52419b0` 到位去使能/弹片自定位（`_autoDisableAtRest`+`wakeForMotion`+`syncXActualToEncoder`）
- `a3cde03` 两段式 homing（全速粗找 + 1/10 慢逼近）
- `aad7b42` homing INIT 补 hard-stop latch 解锁
- `bd3f47f` homing 开始关芯片 PID、完成恢复
- `9e72ddd` homing search 方向改用 `_config.homing_direct`
- `dddeff3` leaving-home 方向改 `-homing_direct`
- `b97e814` 右硬停只在 homing 期间开
- `8d01838` 修区内反复 homing +漂移绕圈
- `4844a5a` PID tolerance 20→10
- ⚠️ `9e72ddd`/`dddeff3` 方向逻辑可能解掉本项目暂停中的 Turret homing 根因（homing_direct 被 movement_sign 覆盖）

#### A2. 固件·TMC2240 enable 修复（通用，X/Y/Z/W 全受益）
- `8136bff` enable/disable 改 shadow register，规避不可靠 Cover read（修「只有 W 卡住」根因）
- `98976ab` `S:DUMP_TOFF` 诊断命令（可选）

#### A3. 固件·超时/速度健壮性（通用）
- `f4c3c35` 动态 move 超时（按距离/速度算，修 5s 砍停半路）
- `cf93900` 速度写回 `_config`（修「设速度后第二次移动回退默认」）
- `f254a63` StepAxis 重搜索改用 `homingVelocity` ⚠️ 只取 re-search 速度那段，别覆盖本项目「方向感知退回」
- `df7a38a` X homing 10→20mm/s（修 meijiasquid 主机超时）
- `9d7993a` Y homing 速度修正

#### A4. 固件·独立健壮性
- `29c2fec` USB 冷启动超时（不再死等主机，修手控盒冷上电失效）
- `ae812bd` joystick 编码器中断保护 + pow 改整数位移
- `2548292` cmd 252 明确为完全空操作

#### A5. 上位机·GUI 物镜标定 + 显示（落到本项目 Turret/objective 类型轴）
- `82e9f0e` 工位角度标定 + 绝对定位（持久化 `~/.octoaxes/objective_calib.json`）
- `152d50e` 位置显示改「工位 + 角度」（齿轮比 2.75）
- `763fe5a` 标定区每行「移动到」按钮
- `adb5365` homing 拆「纯 homing」+「移到工位0」
- `cef69b5` homing 后自动移工位0
- `518a5c6` W PID 闭环集成 GUI 启动流程（`SET_PID_ARGUMENTS→CONFIGURE→ENABLE` 顺序 load-bearing）
- `d129a32` `wait_until_idle` 重写
- `81d0e68` 物镜界面英文化 + 度数 0.1°

#### A6. 上位机·照明（独立、零风险）
- `be20270` 明场 LED 矩阵 0-100% 亮度滑条

### 🤔 组 B：需决策后再融（架构/参数相关）
- W 编码器启用 `cb97a04`/`ff4396b`/`1f75927` — 依赖「物镜轴装编码器」，看本项目物镜放哪个轴
- W PID 整定值 `8d85b67`/`d6db9cf`/`14324a0` — 硬件相关，别照抄，用 tune_w_pid.py 重测
- `d830608`/`c7190ba` Z_SAFEPOSITION 0.7→1.40→1.00 — ⚠️ 本项目 Z 是新 Z 变体，会改焦面绝对读数，单独评估
- `eda164b` 物镜速度/加速度 V0.3/A30 — 保守起点，按硬件调
- `0f53db7` R_sense_xy 拆分 R_sense_x/R_sense_y — 纯重构，可选

### ❌ 组 C：不要融合
- Revert 对（净零）：`087056b`/`0d5007f`/`38fe9aa`/`d1fcf8c` 撤销 `4ee13a8`/`32b5fea`/`483c393`/`05ebca5`
- 80160 X 电机/电流：`92b96dd`/`77d2f1e`/`9abaf1b`/`ded4ca3`/`de2db76`/`8c6a67d`/`c56edd3`/`d364db7`/`a4f4de5`/`34eea05`/`c44cc11`/`bb9dbc6`（Mega 自述未应用，净 tree X 未变）
- 收口删轴：`bad0793`/`701d6f4`/`d5a9dce`（删 octoaxesplus/W2/E1/E3/E4）
- 审计 F-4/F-8：`e5150c3`/`298b00b`/`f2a7496`/`d26f094` — **本项目 develop 已独立实现**（illumination.cpp:144 / config.h:526）
- 各 docs(session) 提交

---

## 建议融合顺序
A2/A3/A4（通用固件健壮性）→ A6（照明）→ A1（物镜类）→ A5（GUI 物镜标定）→ 组 B 逐项评估。

## 融合进度
- [x] **A2 TMC2240 enable shadow-register**（2026-07-08 完成）
  - `MotorControl.cpp` `motor_enableDriver` TMC2240 分支：`tmc2240_readRegister(CHOPCONF)` → **shadow register read-modify-write TOFF**（规避不可靠 Cover 读，修「只有 W disable 后 enable 失效」根因）。
  - `MotorControl.cpp/.h` 新增 `struct ChopconfDump` + `motor_dumpChopconf()` 诊断。
  - `serial.cpp`（octoaxes + octoaxesplus 各一份）新增 `S:DUMP_TOFF [axis]` 命令。
  - ⚠️ **`motor_configLimitSwitches` 未取 Mega 版**——本项目的「极性/锁存解耦」版领先，Mega 是旧耦合版，取了会回退限位极性 bug。
  - `motor_syncXActualToEncoder`（弹片自定位用）**延到 A1** 再带。
  - MotorControl 经 `octoaxesplus/tmc` 符号链接自动惠及双 firmware；两 firmware 编译 SUCCESS。
  - **未烧录**，待硬件实测（TMC2240 轴 disable→enable 可逆性；`S:DUMP_TOFF` 看 coverTOFF match=N）。
- [x] **A3 超时/速度健壮性**（2026-07-08 完成，仅逻辑；config.h 速度数值用户拍板不改）
  - `axis.h` 加 `_moveTimeoutMs` 成员（octoaxes + octoaxesplus）。
  - `axis.cpp` `setMotionParameters` 速度写回 `_config`（修「设速度后第二次移动回退默认」，cf93900）。
  - `axis.cpp` `startMovement` 动态 move 超时（按距离/速度算，60s 上限）+ moving 态用 `_moveTimeoutMs`（修 5s 砍停半路，f4c3c35）。
  - `stepaxis.cpp` LEAVING_HOME 重搜索速度 `maxVelocityMM`→`homingVelocityMM`（latch 重复性，f254a63）。**只改 240 行重搜索，退回方向保留本项目「方向感知」版**。
  - 4 处编辑 octoaxes + octoaxesplus 各一份（axis 三件套非符号链接、各自独立）；两 firmware 编译 SUCCESS。
  - ⚠️ 避开：Mega 删的 `polarityAffectsChip`/`reapplyLimitSwitches`/ENC-2 tripwire/value-init/W tol=10/wakeForMotion 全保留本项目版。
  - **config.h 速度数值（用户拍板不改）**：`HOMING_VELOCITY_X_MM` 保持 10（不取 Mega 的 20/df7a38a）、`HOMING_VELOCITY_Y_MM` 保持 30（不取 Mega 的 20/9d7993a）。理由：机器相关，本项目 Y=30 有「256 微步+30mm/s 最安静」实测，Mega 是 t2000 不同机器且 Y 注释陈旧。
- [x] **A4 独立健壮性**（2026-07-08 完成，2 提交；第 3 项 cmd252 跳过）
  - **`0a5c915`** serial.cpp USB 冷启动不再死等主机（`while(!SerialUSB)` → 最多等 timeout ms，29c2fec）——修「无上位机冷上电 setup() 卡死 → 手控盒/焦点轮失效」。octoaxes + octoaxesplus 各一份，两 firmware SUCCESS。
  - **`901e8b1`** joystick 编码器读取加 noInterrupts 快照 + pow 浮点改整数位移（ae812bd）。octoaxes + octoaxesplus 两 joystick 变体，teensyLC/teensyLC_overseas 四 env SUCCESS。
  - ❌ **cmd 252 注释（2548292）跳过**：本项目已是 no-op，Mega 只改注释且其注释是 mega 形态专属（「只有 XYZ+W 物镜、无 W2」）对本项目错误，融过来反误导。
- [x] **A6 LED 亮度滑条**（2026-07-09 完成，`eb3c42a`，be20270）— IlluminationPanel 加 0-100% 亮度滑条（默认 100%），`_scaled_rgb()` 整体缩放 R/G/B；实时调节走新信号 `led_matrix_update_cmd` → `_send_illu_led_matrix_update` 仅发 cmd13（固件已点亮自动重刷，未点亮只缓存不误点亮）。纯上位机 common/ profile-safe，固件无需改。py_compile + 双 profile 加载 + headless 实例化冒烟测试全过。
- [x] **A1 物镜类改进**（A1a/A1b/A1c firmware 侧全完成，2026-07-09；GUI 侧归 A5）
  - [x] **A1a Turret homing 两段式 + 方向修复**（2026-07-09，`8791e1e`，纯固件）— objectives.cpp/h（octoaxes + octoaxesplus 两 profile，受益 Turret）：两段式 homing（a3cde03）+ homing_direct 方向（9e72ddd/dddeff3）+ latch 解锁（aad7b42）+ 右硬停只在 homing 期间开（b97e814）+ 漂移修复（8d01838）。有意排除 begin() autoDisable（A1b）。两 firmware SUCCESS。**software 不涉及**（现有 GUI Homing 即可测）。⚠️**未烧录，必须上机实测**（本项目 Turret homing 本就暂停/未通过）；Turret 盘几何/home 区极性/慢逼近比例可能需按硬件微调。
  - [x] **A1b 弹片自定位基础设施**（2026-07-09，`862abab`，纯固件）— MotorControl 加 `motor_syncXActualToEncoder`；axis.h 加 `_autoDisableAtRest`+`wakeForMotion()` 声明；axis.cpp disableAxis 断流不碰 PID / enableAxis 通电前 sync / wakeForMotion 实现 / move·moveRelative·startHoming 调 wakeForMotion / startHoming 关 PID 走开环；objectives begin `_autoDisableAtRest=true`。octoaxes + octoaxesplus 两 profile，两 firmware SUCCESS。⚠️ 去使能**时序由 GUI 主导（属 A5）**，GUI 未接管前**惰性**（无 cmd32 disable 就不去使能，wakeForMotion 只当安全网），其它轴 =false 行为不变。未烧录（随 A1a 上机验证）。
  - [x] **A1c Turret PID tolerance=10**（2026-07-09，`c9de0b5`）— axis.cpp configureStagePID 加 Turret 专属分支 tolerance=10（物镜盘 ±0.10°），W/W2 维持 20。Mega 是改 W(物镜)，本项目 W 是滤光轮故单开 Turret 分支。两 profile，两 firmware SUCCESS。未烧录。
  - **A1 firmware 侧全部完成**（A1a/A1b/A1c）；剩物镜 GUI（弹片自定位时序 cmd32 + 工位标定/绝对定位/PID 下发）归入 A5。
- [ ] A5 GUI 物镜标定
- [ ] 组 B 逐项评估

---

## 当前状态（2026-07-09）

**firmware 侧融合全部完成并已同步 github/main**：A2 / A3(×3) / A4(×2) / A6 / A1a / A1b / A1c。
develop 与 github/main 一致。

**⏸️ 暂停在此，等硬件实测后再进 A5。**

### ⚠️ 未上机验证的项（阻塞 A5）
A1a / A1b / A1c 三项 firmware **全部编译通过但未烧录**，且相互关联：
- A1a 改的是本项目**本就暂停/未通过的 Turret homing** 路径
- A1b 弹片自定位建立在 A1a homing 之上
- A5（物镜 GUI）又完全建立在 A1a/b/c 之上

→ **必须先烧录实测 Turret，通过后再做 A5**，否则 A5 大量 GUI 工作可能白做。

### 烧录实测清单（Turret，建议用 octoaxesplus 借板）
1. **homing 方向**：点 Homing，Turret 是否朝 home 传感器方向搜索（不是反向绕远）
2. **两段式**：是否「全速粗找 → 退出 → 慢速精逼近 → 锁定清零」，不再过冲绕圈
3. **停位重复性**：反复 homing 多次，物理停位是否基本一致（A1a 核心目标）
4. **转过 home 区**：homing 后能否正常换位到各工位（含跨 home 区的工位，验证 SET_ZERO 关右硬停）
5. **不漂移**：连续 homing 5+ 次，落点不逐次漂出 home 区（验证 8d01838 漂移修复）
6. （若装编码器且 GUI 发 disable）弹片自定位：到位去使能后弹片归中、再动不跳枪

### 实测可能需要的微调（决策 2，硬件相关）
- Turret 盘几何 / home 区极性与 Mega 不同 → SET_ZERO 关右硬停、homing_direct 方向可能要调
- `SLOW_APPROACH_RATIO=0.1` 慢逼近比例可能按盘调
- 若方向反：优先查 constants.py Turret `movement_sign`（推导 homing_direct），别直接改固件

### 实测通过后的下一步
- **A5**（物镜 GUI）：弹片自定位 cmd32 时序（main_window 移动前使能 + 到位延迟去使能）+
  工位角度标定/绝对定位（持久化 JSON）+ 单工位分步移动 + PID 启动下发 + constants 加
  pid/velocity/auto_disable 字段。参考 Mega commit：82e9f0e / 152d50e / 763fe5a /
  adb5365 / cef69b5 / 518a5c6 / d129a32 / 81d0e68。
- **组 B**：W 编码器启用 / PID 整定值（硬件重测）/ Z_SAFEPOSITION（谨慎）/ 物镜速度加速度。
