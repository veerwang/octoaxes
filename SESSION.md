# SESSION.md

会话记录文件，用于跨会话保持上下文连续性。

---

## 最新会话

**日期**: 2026-06-09
**分支**: develop
**位置**: **Z 变体切换软件化** —— 限位极性走上位机 cmd 20 下发，消除「切 Z 需同步改 config.h + constants.py 两处」的易错点（commit `afb4dc5`，**新 Z + 旧 Z 均上机实测通过**，仓库默认切回旧 Z）

### 一句话

用户痛点：切换新旧 Z 要改两个地方（固件 `config.h` `#define Z_VARIANT_NEW` + 软件 `constants.py` `Z_AXIS_VARIANT`），容易漏改。本次让固件那一处也由软件下发，**切 Z 只改 constants.py 一行**。

### 关键洞察：06-09 传感器对调后，固件差异已塌缩成「一个字段」

对比 config.h 新旧 Z 变体宏块，现在只剩 `Z_SW_POLARITY`（new=1/old=0）不同——`Z_HOMING_SWITCH`(都 RGHT_SW)/`Z_ENABLE_LIMITS`(都 true)/`Z_FLIPPED`(都 false)/`Z_INVERT_ENCODER`(都 true) 全部相同。所以「软件化」只需让这一个极性位走下发。

而固件早有现成命令 `SET_LIM_SWITCH_POLARITY = 20`（旧 Squid 协议），白捡。

### 改动（11 文件，commit `afb4dc5`）

**固件（octoaxes + octoaxesplus 各一份）**
- `axis.h/cpp` 新增 `reapplyLimitSwitches()`：从 `_config` 重建 LimitConfig 调 `motor_configLimitSwitches` + `motor_enableHomingLimit`，把极性真正写进芯片 REFERENCE_CONF。**修了 cmd 20 旧实现的坑**——它只改内存结构体、不写芯片（begin() 开机只配一次，之后改结构体不生效）。
- `commandprocessor.cpp::handleSetLimSwitchPolarity` 末尾调 `axis->reapplyLimitSwitches()` 使下发真生效。
- `config.h` `#define Z_VARIANT_NEW` 退化为「开机窗口安全默认」，注释标明切换无需动此处。

**软件（两 profile + 共享 GUI）**
- `constants.py` `_Z_VARIANTS` 加 `switch_polarity`（old=0/new=1）。
- `main_window.py::_configure_actuators()` 对带 `switch_polarity` 的轴下发 cmd 20（数据驱动、profile-safe，X/Y/W 无此键自动跳过）。
- **S:ZVARIANT tripwire 降级为信息日志、不再拦截 Z 操作**：极性已是软件单一权威源，固件 `#define` 与软件不一致是合法的（软件下发覆盖）。否则软件切 old、固件留 new 会被误拦 → 正好废掉本次统一。`_z_op_blocked` 现恒返回 False（方法保留无害）。

### 验证

- octoaxes + octoaxesplus 固件均编译 SUCCESS。
- 两 profile 加载：Z `switch_polarity` new=1 / old=0；py_compile OK。

### ✅ 上机实测通过（验证缺口已闭合）

用户烧录两固件后实测：**新 Z + 旧 Z 双变体均正常运行**。
- 新 Z（软件 "new"/极性 1）：正常。
- **旧 Z（软件切 "old"/极性 0，物理换装旧 Z 硬件）：正常** —— 固件开机默认极性 1(new)、软件下发 0(old) 仍工作正确，**证明 `reapplyLimitSwitches()` 芯片重写路径生效**。这是本方案唯一未上机的点，现已闭合。
- 切换全程**只改 constants.py 一行 + 重启 GUI，未重烧固件** —— 软件化目标达成。
- 收尾：仓库 octoaxesplus 默认切回旧 Z（`Z_AXIS_VARIANT="old"`，贴合当前在装硬件）。

### 决策记录

- **范围=最小（只下发极性）**（用户拍板，备选「通用化下发整组限位配置」未采用）。理由：当前唯一差异就是极性，YAGNI。若将来 octoaxes 板上机实测发现 flipped/homingSwitch 也需不同（SESSION 反复警告 octoaxes 板限位未实测），再扩成整组下发。
- config.h `#define` 保留作开机默认（不删），最小改动、不破坏结构。

### 后续清理：彻底删除 Z_VARIANT_NEW 编译开关 + 死掉的 S:ZVARIANT tripwire（2026-06-09 续）

软件化落地并实测通过后，固件 `#define Z_VARIANT_NEW` 已无操作意义（06-09 传感器对调后新旧 Z 唯一差异=极性，已走软件下发；其余宏新旧取值全相同），其联动的 `S:ZVARIANT` 上报 + GUI tripwire 也已是死代码 → 用户拍板**一并删除**。改 5 文件、净删 ~117 行：
- **2× config.h**：删 `#define Z_VARIANT_NEW` + `#ifdef/#else/#endif` + 5 个 Z_* 中间宏，Z_AXIS 字段改回直接字面值（homingSwitch=RGHT_SW / 极性=1（开机默认 new，运行时软件下发覆盖）/ flipped=false / enable=true / invertEncoder=true），注释标明「已删编译开关、变体由软件决定」。
- **2× serial.cpp**：删 `S:ZVARIANT` 命令分支（config.h include 保留，仍被 Commands::/Pins:: 大量使用，仅修注释）。
- **1× main_window.py**：删整条 tripwire 链 —— `Z_AXIS_VARIANT` import、`_z_variant_mismatch`、`_z_op_blocked` 方法 + 3 处调用、S:ZVARIANT 响应处理、启动查询发送。
- 验证：两固件编译 SUCCESS；两 profile 加载（octoaxes new→极性1/导程1.0，octoaxesplus old→极性0/导程0.3）；py_compile OK；全仓库无残留代码引用（只剩 2 行「已删除」历史注释）。

### Z homing 速度软件化（2026-06-09 续二）

排查「旧 Z + octoaxes 固件 + 旧 Squid」场景时发现：`HOMING_VELOCITY_Z_MM` 在 06-08 为新 Z 从 1→2mm/s，但**旧 Squid 和 octoaxes GUI 启动都没有下发 homing 速度的通道** → 旧 Z 在旧 Squid 下被迫以 2mm/s 回零（破坏 drop-in 等价、过冲 ~4×）。矛盾：新 Z 行程 ~34.5mm 需 2mm/s 防超时，旧 Z 行程 ~6mm 要 1mm/s 防过冲，单值难两全。

采用推荐三步方案（用户拍板）：
1. **固件开机默认 `HOMING_VELOCITY_Z_MM` 2→1**（两 config.h）—— 旧 Z + 旧 Squid 恢复历史 1mm/s（drop-in 等价；旧 Squid 无下发通道，只能用此默认）。
2. **Z `homing_timeout_ms` 40000→60000**（两 config.h）—— 给「新 Z + 旧 Squid」(只能用默认 1mm/s、行程 34.5mm、最坏 34.5s) 留足余量，加大无副作用。
3. **octoaxes/octoaxesplus GUI 启动按变体下发 `S:SET_HOMING_VEL`**（复用固件现成诊断命令）—— `_Z_VARIANTS` 加 `homing_velocity_mm`(old=1/new=2)，`_configure_actuators` 对带此键的轴发 ASCII 命令（profile-safe）。新 Z 经 octoaxes GUI 仍享 2mm/s。
- 结果 4 组合全稳：旧 Squid+旧 Z=1✓ / 旧 Squid+新 Z=1+60s✓ / GUI+旧 Z=下发1✓ / GUI+新 Z=下发2✓。
- 验证：两固件 SUCCESS；两 profile 加载 Z homing_velocity_mm（old=1/new=2）；py_compile OK。
- 配套产出：`documents/oldsquid_newz_adaptation.md`（旧 Squid 适配新 Z 清单）+ `Squid/software/configuration_HCS_v2_newZ.ini`（旧 Squid 示例配置，仅改 5 个 Z 值）。

### Z 限位极性回归排查 + 三层根因（2026-06-09 续三，上机实测全通过）

旧 Squid 实测：先旧 Z 动不了、修好后换新 Z 又动不了。AI 直连 `/dev/ttyACM0` dump TMC4361A REFCONF 逐层定位（三个独立 bug）：

1. **回归 A：cmd 20 reapply 误伤 X/Y**（FIX B 修）—— `2ba5343` 的 reapplyLimitSwitches 让 cmd 20 对**所有轴**写芯片；旧 Squid 启动发 `x_home=1/y_home=1`（其硬件 active-high），落到 octoaxes 硬件(active-low) → X/Y 极性翻反 → 限位假触发 → XYZ 卡死。旧 Squid 固件 cmd 20 本就只设软件变量、从不写芯片（实读 `stage_commands.cpp` 证实）。**FIX B**：`AxisConfig.polarityAffectsChip`（仅 Z=true），cmd 20 写芯片只对 Z 生效，X/Y 恢复"只改结构体不碰芯片"= drop-in 等价。
2. **回归 B：Z 开机默认极性错**（boot default 修）—— newz 合并后 Z 开机默认极性变成新 Z 的 1，但主线实装旧 Z(需 0)。芯片实读 REFCONF POL=1 + state=6 ERROR 佐证。改两固件 Z 开机默认极性 **1→0**（默认支持旧 Z，满足用户要求）；新 Z 由 cmd 20 下发 1 覆盖。**FIX B + boot 0 已 commit `9bd3600`。**
3. **根因 C：旧 Squid `_def.py` import 顺序 bug**（改旧 Squid 修）—— 新 Z 仍动不了。dump 发现旧 Squid 发的 Z 极性是 **2 (DISABLED)**、不是 .ini 的 1。根因：`_def.py:1203` `Z_HOME_SWITCH_POLARITY = LIMIT_SWITCH_POLARITY.Z_HOME` 在 .ini 加载(行 1292)**之前**求值，捕获类默认 `Z_HOME=2(DISABLED)`；.ini 只更新类属性、不更新已定死的模块变量 → 旧 Squid 给 Z 发 DISABLED → 固件忽略 → 极性停在开机默认。**改 .ini 的 z_home 对旧 Squid 完全无效**。修复：`_def.py` 在 .ini 加载后重新求值 `X/Y/Z_HOME_SWITCH_POLARITY`（约行 1383 后）。

**关键验证手段**：AI 手动发二进制 cmd 20 Z=1（含 CRC-8-CCITT）→ dump 确认芯片 REFCONF POL 0→1 → 证明**固件 reapply 完全正常**，把锅精确甩给旧 Squid `_def.py`。

**结果（上机实测）**：旧 Squid + 旧 Z ✓、旧 Squid + 新 Z ✓、octoaxes GUI + 新/旧 Z ✓ —— 四组合全通。改动落点：octoaxes 仓库（固件 `9bd3600` 已提交）+ 旧 Squid 仓库（`_def.py` 修复 + `configuration_HCS_v2.ini`=新Z配置，未提交，在 Squid 仓库）。

**经验教训**：①旧 Z 测试通过 ≠ reapply 路径验证通过（旧 Z boot 默认与下发值都是 0，掩盖了 reapply 是否生效）；②限位"动不了"先 dump 芯片 REFCONF POL 位，别空想；③drop-in 等价要对照对方固件的**实际行为**（旧 Squid cmd 20 不写芯片），不能想当然。

### 下次

1. （可选）push develop → github/main
2. octoaxes 主线板（非 octoaxesplus 借板）新 Z 限位极性/翻转/导程/编码器上机实测 —— config.h 已同款软件化，但 octoaxes 板是另一连接器/接线，限位行为未在该板实测
3. 新 Z 闭环 PID 验证（ENC-1，竖直 Z 防下坠/对焦精度时再开）

---

## 上次会话

**日期**: 2026-06-08 续
**分支**: develop
**位置**: newz 分支 Z 工作合并进 develop（已 push github/main）+ **octoaxes 主线固件新 Z 适配**

### 本次完成

#### 1. newz → develop 合并（a4fdf27..46b30a5，19 提交）

- **策略**：newz 的 Z 工作建立在 objectives 路线（W=物镜），develop 走 Turret 路线，共享文件两边都改 → **cherry-pick 而非 merge**。把 19 提交**压成单个净差异提交**，每文件只解决一次冲突（newz 分支保留完整粒度历史）。
- **冲突解决**（3 文件，其余 13 自动合并）：`serial.cpp` VERSION 取较高 119；`TODO.md` 丢弃被取代的占位 stub、并入 newz 详细 Z 记录、Turret 任务全保留；`SESSION.md` **并集**——newz 三个 Z 会话（06-08/06-06/06-03）置顶 + develop Turret 条目及全部历史完整保留，剔除 newz 重复的 objectives 旧条目。原则：代码合并 / markdown 全留。
- **验证**：两固件编译 SUCCESS（octoaxesplus 85276 / octoaxes 81628）；两 profile 加载（octoaxes Z=new / octoaxesplus Z=old）；py_compile 通过。提交 `714fb32`。
- **push**：`git push github develop:main` fast-forward 488a1ec..714fb32（github/main 同步，本地 github-main 同步）。gitee origin 未动（develop 无对应远程分支）。

#### 2. octoaxes 主线固件新 Z 适配

- **缺口定位**：合并后 octoaxes software 侧已支持新 Z（constants.py `Z_AXIS_VARIANT="new"`），固件消费侧也早就位（MotorControl.cpp 符号链接共享 INVERT_STOP_DIRECTION、axis.cpp 已接 leftFlipped/leftSwitchPolarity、stepaxis.cpp 与 octoaxesplus 完全相同）。**唯一缺口 = config.h 的 Z_AXIS 没有变体宏块**。
- **改动 2 文件**：
  - `firmware/octoaxes/config.h`：新增 `Z_VARIANT_NEW` 变体宏块（`Z_HOMING_SWITCH`/`Z_SW_POLARITY`/`Z_ENABLE_LIMITS`/`Z_FLIPPED`/`Z_INVERT_ENCODER`），Z_AXIS 7 字段改用宏；**默认启用新 Z**（用户拍板，与 software 一致）；`homing_timeout` 20000→40000；`HOMING_VELOCITY_Z` 1→2 mm/s。
  - `firmware/octoaxes/axis.cpp`：移植 ENC-2 编码器方向 tripwire（boot vs runtime flip 不一致告警），与 octoaxesplus 对齐。
- **验证**：octoaxes 新 Z 变体 + 旧 Z 变体（注释开关）+ octoaxesplus 三者编译均 SUCCESS；软硬件一致（都 new）。
- **⚠️ 待上机实测**：限位极性/翻转值（`Z_FLIPPED=true`/极性=1/`RGHT_SW`）移植自 octoaxesplus squid++ 板 06-08 实测，octoaxes 是另一连接器/接线，**新 Z 限位行为尚未在 octoaxes 板实测**——须烧录后用 `z_limit_monitor.py` 验证 STOPL/STOPR 极性与 INVERT 方向（参考 Turret LEFT/RIGHT 反面教材）。不符则调 `#ifdef Z_VARIANT_NEW` 块内 4 个宏。

### 切旧 Z 步骤（octoaxes，两处手动一致）

① config.h 注释 `#define Z_VARIANT_NEW` ② constants.py 设 `Z_AXIS_VARIANT="old"` ③ 重烧固件 + 重启 GUI。

### 下次

1. 烧录 octoaxes 新 Z 固件，用 z_limit_monitor.py 实测限位极性/INVERT 方向，确认 homing 上下限位都正确硬停+退回
2. 验证导程 1mm（命令 1mm 量实际位移）+ 编码器 flip 方向收敛（开闭环 PID 前必做，竖直 Z 方向错=飞车）
3. （可选）octoaxes 也补 ENC-1 开闭环 PID 实测；Turret homing 恢复（已暂存，根因已定位）

---

## 上次会话

**日期**: 2026-06-08
**分支**: newz
**位置**: 新 Z homing 终极方案 **INVERT_STOP_DIRECTION**（取代 06-06 的 LEFT_SW+软件停车）+ 编码器验证 + 上限位实测 + **新旧 Z 双变体通吃 + 一致性 tripwire** ✅

### 一句话

06-06 用「LEFT_SW + enableLimit=false + 软件停车」勉强让新 Z homing 通过，但根因（坐标翻转）没解决、限位被关、易回归。本次找到**纯固件零改线**根因解法 `INVERT_STOP_DIRECTION`，恢复硬限位，并把 octoaxesplus 做成新旧 Z **一个板通吃**（软件运行时开关 + 固件编译开关 + 启动一致性自检）。

### 根因：坐标翻转（最终定论）

- 本台 **firmware 正方向 = 物理左 = 朝 home**。来源：**丝杠旋向 / 电机安装**，**不是线圈接线**（用户实测 A/B 两相、相内两接头均未调换）。
- TMC4361A 限位方向是**固定约定**：`STOP_LEFT` 拦 firmware 负方向、`STOP_RIGHT` 拦 firmware 正方向。坐标翻转的轴上，正方向撞 home 开关 → 落到 STOP_RIGHT 域，且退回(负)会被另一侧 chip 锁死 → 06-06 才被迫关硬限位走软件停车。

### 终极解法：INVERT_STOP_DIRECTION（REFCONF bit4）

- `leftFlipped || rightFlipped` → 置 REFERENCE_CONF bit4，**同时**反转「哪个方向被哪个 stop 拦」**和**「哪个开关输入映射到哪个 STATUS 位」。一举把翻转轴的限位语义掰正。
- 配套：`homingSwitch` 必须用 **RGHT_SW**（INVERT 后 home 开关电平反映在 STOPR 位，软件查 STOPR 才检测得到命中），`Z_SW_POLARITY=1`，`Z_ENABLE_LIMITS=true`（**硬限位恢复开启**）。
- 实测全链路：撞 home 硬停 → 软件检测到 → 退回 → 离开感应区 → 置零；上限位硬停 → 退回。**上下电子限位都真生效**，且**不用动电机线**。

### 新旧 Z 双变体通吃（octoaxesplus 一个板）

octoaxesplus 这块借调板既可能装新 Z 也可能装旧 Z，做成双开关：

| 层 | 开关 | 作用 |
|---|---|---|
| 软件 | `software/octoaxesplus/constants.py` `Z_AXIS_VARIANT="old"/"new"`（运行时，GUI 启动下发 pitch/电流/微步/编码器） | limits/导程/电流/hold/编码器/tpr 整组切换 |
| 固件 | `firmware/octoaxesplus/config.h` `#define Z_VARIANT_NEW`（编译期，控 homingSwitch/极性/INVERT/enableLimit，**需重烧**） | 限位语义随变体切换 |

两处必须一致；漏改任一处 → 错电流/错限位方向。

### 一致性 tripwire（commit `3ba8938`，方案 1）

- 固件 `serial.cpp` 加 `S:ZVARIANT` → 按 `#ifdef Z_VARIANT_NEW` 回 `new`/`old`（实测回 old ✓，VERSION=119）。
- GUI `main_window.py` 启动查 `S:ZVARIANT` ↔ 软件 `Z_AXIS_VARIANT` 比对：不一致 → 红字告警 + **拦死 Z 的 home/move/moveto**（`_z_op_blocked`）；一致 → 记「一致性 OK」。
- 安全降级：profile 无 `Z_AXIS_VARIANT`（→None）或旧固件无响应 → 不误拦。
- 限制：仅**启动时**比对一次（换硬件须重启 GUI，符合「换变体→重烧→重启」流程）。错配拦截路径为代码逻辑验证（未在 PyQt 跑真错配）。

### 编码器验证（commit 已含）

- 新 Z 编码器 `flip=1` / `tpr=10000`（1.0mm 导程 / 0.1μm 分辨率），ENC_POS↔XACTUAL **ratio≈1.0、偏差有界** → 实测通过，`has_encoder` 默认 True。
- 编码器方向唯一权威源 = constants.py `encoder_flip_direction`（运行时 CONFIGURE_STAGE_PID 下发覆盖 config.h boot 默认）；config.h boot 默认当前被 `enableEncoder=false` gate 掉。详见 `documents/audit_octoaxesplus_20260608.md`（ENC-2）。

### 上限位实测 + limits

- 实测新 Z 行程上限 ≈ **34.5mm**（STOPR 开关），留 0.5mm 余量 → limits **(-100, 34000)** μm。

### 本次提交链

| commit | 内容 |
|---|---|
| `80f4577` | 限位极性/锁存独立于 enable 写入（修软件停车 homing，共享 MotorControl.cpp） |
| `a60c8a8` | docs: 新 Z homing 全链路通过记录 |
| `674499c` | 新 Z 编码器默认启用（has_encoder 入变体，flip=1） |
| `d81b6b9` | 测定新 Z 上限位 34.5mm + limits(-10,34000) + 右限位同左关 chip 硬停 |
| `a28574b` | **新旧 Z 通吃：软件 + 固件双变体开关** |
| `45fc96f` | 编码器方向单一权威源 + 脱钩 tripwire（审计 ENC-2） |
| `e096adb` | docs: audit_octoaxesplus_20260608.md |
| `3ba8938` | **Z 变体一致性 tripwire（固件上报 + GUI 启动比对拦截）** |
| `7cc00c9` | **仓库默认变体切回旧 Z**（贴合当前在装硬件） |

### 仓库默认 = 旧 Z（收尾）

当前 octoaxesplus 板实装旧 Z，仓库默认 `config.h` 注释 `Z_VARIANT_NEW` + `constants.py "old"`，两处一致、开箱即用。换新 Z：取消注释 + 改 `"new"` + 重烧，tripwire 兜底漏改。

### 调试工具（AI 直连 /dev/ttyACM0 实测，用户授权）

- `z_limit_monitor.py` 限位实时监视 / `z_homing_safedist.py` 测退回安全距离 / `z_find_upper_limit.py` 找上限位行程 / `turret_homing_only.py` 纯 Turret homing。

### 反面教材

- 06-06 的「关硬限位走软件停车」是**症状级绕过**：根因是坐标翻转下 TMC4361A 固定限位约定与轴方向冲突。先怀疑接线（用户实测排除），再回到 REFCONF 才找到 `INVERT_STOP_DIRECTION` 这个寄存器级正解。教训：限位「方向不对/退不动」先查 REFERENCE_CONF 的方向位，别急着关限位。
- INVERT 会**同时**换 STATUS 位映射 → 软件检测开关必须跟着从 STOPL 改查 STOPR，否则「硬停了但软件没察觉」怼穿（本次踩过一次）。

### 待办 / 下次

- （可选）方案 2 根治：把 Z 限位配置（homingSwitch/INVERT/enable）做成**可下发**，消除「重烧 + 两处同步」。
- （可选）`switch_z_variant.py` 一键切两处。
- 新 Z 闭环 PID 验证（可选）/ 导程 1mm 实测位移核对。
- claude-code CLI 升级残目录清理后重试（ENOTEMPTY，被打断未完成）。
- 回主线待办：W motor↔wheel 硬件紧固、采集 8s 打点。

---

## 上次会话

**日期**: 2026-06-06
**分支**: newz
**位置**: 新 Z 借 squid++（octoaxesplus）板 bring-up —— **正反转 + 限位 + homing 全链路通过** ✅
**⚠️ 注**: 本次的 homing 方案（LEFT_SW + enableLimit=false + 软件停车）已被 06-08 的 **INVERT_STOP_DIRECTION** 方案取代，下文保留作攻坚过程记录。

### 背景

手上无 octoaxes 主线板，**借 octoaxesplus（squid++ 双相机）板调试新 Z 电机**（MOONS' LE143S-W0601）。
把 newz 的新 Z 变体适配从 octoaxes profile 移植到 octoaxesplus profile。

### 本次完成（octoaxesplus profile，多 commit，全部已提交）

| commit | 内容 |
|---|---|
| `7ad6b7b` | 新 Z 适配移植到 octoaxesplus：config.h Z_AXIS `currentRange 0→1`（TMC2240 I_FS=2A 撑 1.5A）+ 限位极性 `0→1`；constants.py 加 `Z_AXIS_VARIANT="new"` + `_Z_VARIANTS`（导程1.0/1500mA/hold0.75）；VERSION 110 |
| `4b0b412` | `z_limit_monitor.py` 限位实时监视脚本 |
| `15894c0` | Z `homingSwitch RGHT_SW→LEFT_SW`（home 在左限位）+ 新-Z-专用块注释；VERSION 111 |
| `e23e949` | homing 速度 1→2mm/s（×2）+ 超时 20000→40000（×2）；VERSION 112 |
| `3cfcf16` | StepAxis 退回方向改 `-homing_direct*margin`；VERSION 113 |
| `346fe50` | Z `enableLeftLimitSwitch false`（关 chip 硬停改软件停车）+ `z_homing_safedist.py`；VERSION 114 |
| `80f4577` | **限位极性/锁存独立于 enable 写入**（共享 MotorControl.cpp）；VERSION 115 ← homing 通过 |

### 实测进展

1. ✅ **正反转点动**通过
2. ✅ **限位极性**：翻转后手动触发左/右限位显示符合预期
3. ✅ **homing 全链路通过**（VERSION=115，AI 直连串口实测）：撞左限位→软件停→退回 0.7mm→离开感应区(STOPL off)→置零(XACTUAL=0)

### homing 攻坚（关键，多轮定位）

现象：homing 撞左限位后"离不开回零感应区"。逐层定位（commit 顺序）：
- `3cfcf16` StepAxis 退回方向改 `-homing_direct*margin`（旧逻辑按 homingSwitch ±margin，本台坐标翻转时朝限位更深处退）
- `346fe50` **真根因①**：enable=true 时 TMC4361A STOP_LEFT 固定拦 firmware 负方向；本台坐标翻转(firmware 正=物理左)、正方向撞左开关 → 退回(负)被 chip **锁死**(XACTUAL 冻结、连 MOVETO 带恢复都退不动)。→ 改 `enableLeftLimitSwitch=false` 走软件停车
- `80f4577` **真根因②**：enable=false 后又发现 `motor_configLimitSwitches` 把极性/锁存关在 `if(enable)` 里 → 关硬停时极性丢失，STOPL_ACTIVE_F 读未取反原始电平(新 Z active-low → 在限位读成 off)，软件检测不到 → homing 怼穿。→ **极性(POL)+锁存(LATCH)解耦，独立于 enable 写入**（共享文件，对 enable=true 轴无回归）
- 实测迟滞仅 0.15mm，0.7mm 退回足够，**Z_SAFEPOSITION 不用改**

最终结论链：`enable=true 锁死退回` → `enable=false 又因极性绑定 enable 读反` → `解耦极性/锁存` → 软件停车成立。

### 调试工具（AI 直连 /dev/ttyACM0 实测，用户授权）

- `z_limit_monitor.py`：限位实时监视
- `z_homing_safedist.py`：自动测退回安全距离（逼近找触发点 A + 退离找释放点 B；read 用 reset+正则+重试免疫二进制帧）

### 已知小问题（非阻塞）

homing 2mm/s 逼近时减速过冲约 1.25mm 越过触发点（仍在感应区内、未撞机械端，退回正常）。若限位预行程紧可降 homing 速度。

### ⚠️ 烧录小坑

`pio run -t upload` **首发常失败**（"Unable to soft reboot"），**重试一次即成功**。Z 竖直，重烧瞬间断电会被重力带离限位（属正常）。

### 关键技术点

### 关键技术点

- **符号链推导**（避免 homing 方向反）：GUI "Forward" 点动 = +dist × movement_sign(-1) = 负微步 → firmware 负 → 用户实测=物理右 ⇒ **firmware 正 = 物理左**。homing 搜索 = homing_direct(+1)×vel = firmware 正 = 物理左 = 朝左限位 ✓。故只需把 home 参考改 LEFT_SW，搜索方向不用动。
- **homing_direct 实际值由 GUI 覆盖**：constants.py Z movement_sign=-1 → send_homing home_dir=0 → firmware homing_direct=+1（config.h 的 homing_direct 只是默认，会被覆盖；与 Turret 同一套机制）。
- Z 是 StepAxis，homing 命中限位有**软件停车**（stepaxis.cpp setVelocityInternal(0)+delay），不依赖 chip 硬停方向。
- z_limit_monitor.py 解析坑：固件持续推二进制位置帧会粘在 ASCII 行前 → 必须容错解码(latin1)+正则抠 STATUS，否则只能读第一帧。

### ⚠️ 旧 Z 兼容性影响（用户确认接受 A 方案，后期再做兼容）

本次改动**只在 octoaxesplus**（firmware/octoaxesplus + software/octoaxesplus），**octoaxes 主线旧 Z 机器完全不受影响**。但在 octoaxesplus 上：
- `currentRange 0→1`：对旧 Z 安全（旧 Z 接此板也走 TMC2240，500mA 在 I_FS=2A 下 IRUN≈7 正确）
- **`homingSwitch=LEFT_SW` + 极性翻转 1/1**：⚠️ 新 Z 专属、firmware 独有无下发协议、**不随 Z_AXIS_VARIANT 软件开关切换** → 软件切回 "old" 变体（接旧 Z）时这两项对旧 Z 错（旧 Z home 在右限位、极性 0/0）
- **决策（用户）**：当前接受（本板借调新 Z），config.h 已加块注释标明"新 Z 专用"。**后期需通吃**（squid++ 板也可能接旧 Z）→ 见 TODO「Z firmware 变体开关」。

### 下次

1. 烧录 VERSION=111，安全测 homing（Z 置中 + 开 z_limit_monitor 盯 + 手放电源旁；预期朝物理左 → 左限位 STOPL 停住置零；若朝右立即断电反馈）
2. 验证导程：命令 1mm 量实际位移 ≈1mm
3. （后期）Z firmware 变体开关，让 octoaxesplus 通吃新旧 Z

---

## 上次会话

**日期**: 2026-06-03
**分支**: newz
**位置**: 新 Z 轴适配 MOONS' LE143S-W0601 + 新旧 Z「变体开关」（一个固件通吃）

### 背景

`newz` 分支把 octoaxes 主线 Z 轴换成 **MOONS' LE143S-W0601-100-AR1-S-150** 外部驱动式直线步进电机
（丝杠即电机轴），驱动板换 **TMC2240 ICS**（与物镜分支 EXPAND1 同板型）。
资料在 `~/Documents/newz/`：型号图 `LE143S-W0601-100-AR1-S-150.png` + `鸣志直线产品综合选型手册.pdf`（LE14 在 p21-25）。

### 型号解码（LE143S-W0601-100-AR1-S-150）

| 段 | 代码 | 含义 |
|---|---|---|
| 结构 | LE | 外部驱动式直线步进（中空转子，丝杠贯穿即电机轴） |
| 机座 | 14 | 35mm（≈NEMA14） |
| 机身 | 3S | 35mm，**1.8°**（200 整步/转） |
| 丝杠 | W0601 | 外径 6mm，**导程 1mm/转**，整步行程 5µm |
| 丝杠长 | 100 | Lx=100mm |
| 螺母 | AR1 | 圆形标准螺母，Ø25.4 法兰 |
| 端部 | S | 标准端部加工 |
| 电流 | 150 | **1.50A/相** |

电气：2 相，1.8°±5%，24V，4 线（BLACK/GREEN/RED/BLUE，JST ZHR-11）。

### 对旧 Z 的关键变化

| 参数 | 旧 Z | 新 Z LE143S-W0601 |
|---|---|---|
| 丝杠导程 | 0.3mm | **1.0mm**（×3.33） |
| 整步行程 | 1.5µm | **5µm** |
| 256 微步分辨率 | 5.86nm/µstep | 19.5nm/µstep（对焦够用） |
| 额定电流 | 500mA峰值(R=0.43,实际0.47A) | **1.5A**（TMC2240 ICS） |
| 驱动板 | TMC2660 R=0.43Ω | TMC2240 ICS currentRange=1 |

### 关键架构发现：Z 运行时参数由 software 下发

GUI 启动 `main_window.py::_configure_actuators()` 给 X/Y/Z 下发 `SET_LEAD_SCREW_PITCH`(cmd23) +
`CONFIGURE_STEPPER_DRIVER`(cmd21)，pitch/微步/电流/hold **取自 AXIS_CONFIG**，覆盖固件 config.h 默认。
→ **切换 Z 只需改 software，不用重烧固件**。
电流语义：固件 `calculateCurrentScale_TMC2240` 把下发的 currentMA 当**峰值**算
（`commandprocessor.cpp:335` 注释写"RMS"是历史误标），1500mA → IRUN=round(1500/2000×32)-1=23 → 精确 1.5A 峰值。

### 本次完成（变体开关方案，全部编译通过，未烧录/未硬件实测）

**用户拍板「能否新旧 Z 都支持」→ 采用变体开关，一个固件通吃。**

**① software/octoaxes/constants.py — Z_AXIS_VARIANT 开关**
- 顶部加 `Z_AXIS_VARIANT = "old"/"new"`（默认 `"new"`）+ `_Z_VARIANTS` 两套参数集
  （`old`: 0.3/500/0.5/3000；`new`: 1.0/1500/0.75/10000）
- Z 条目改为 `**_Z_VARIANTS[Z_AXIS_VARIANT]` 合入；微步 256 / limits ±6mm / 类型为新旧共用
- 切换电机：改这一行 + 重启 GUI 生效，无需重烧

**② firmware/octoaxes/config.h — 保守默认 + currentRange=1 通吃**
- Z 默认**回退保守旧值**：`SCREW_PITCH_Z_MM=0.3` / `Z_MOTOR_PEAK_CURRENT_mA=500` / `Z_MOTOR_I_HOLD=0.5`
- **保留 `Z_AXIS.currentRange=1`**：无下发协议/固件独有，但对两板都安全
  （旧 Z=TMC2660 忽略此字段走 R_sense；新 Z=TMC2240 用它 I_FS=2A）
- `DRIVER_AUTO` 上电自动识别在位板 → 一个固件服务新旧 Z
- 开机瞬间(GUI 配置前)新电机仅 500mA=弱但安全，避免旧电机被过流

### 决策记录

1. **只改 octoaxes**：octoaxesplus 是 squid++ 独立 Z 硬件，不动（已验证未受影响）
2. **变体开关放 constants.py**（octoaxes 专属、profile-safe），不放 common/define.py（避免污染共享层）
3. **firmware 默认取保守旧值**：欠流安全、过流伤电机，开机窗口取低值最安全
4. **currentRange 烧死=1**：对两板安全，换取「一个固件通吃」
5. 微步沿用 256 / 编码器保持关闭 / limits 暂不改 / hold ratio 0.75（竖直 Z 防下坠）

### 编译 + 验证

- firmware/octoaxes teensy41: **SUCCESS**
- `"new"` 变体 → 1.0/1500/0.75/10000；`"old"` → 0.3/500/0.5/3000
- octoaxes 默认加载 `new`，Z 全字段正确；octoaxesplus 未受影响（仍 5 轴，Z pitch 0.3）

### ⚠️ 已知限制

变体开关仅 **octoaxes GUI** 有效。**旧 Squid software** 会下发它写死的旧 Z 参数（0.3mm pitch），
配新 Z 硬件会有 **3.33× 位置错位**（旧 Squid 不可改）。若新 Z 只配 octoaxes GUI 用则无影响。

### 待验证（等硬件空闲）

1. 烧 firmware/octoaxes 实测：homing 正常、1.5A 下不丢步平稳、hold 0.75 防 Z 下坠
2. 导程 1mm 后命令 X µm = 实测 X µm，无比例失配
3. 视实测决定是否调速度/加速度（导程变大电机转速降，有提速空间）
4. 行程 limits 实测机械装配后确定（现 ±6mm，丝杠 100mm 可更大）

### 下次

1. 烧录 + 硬件实测上述 4 项
2. 实测通过后提交（firmware config.h + constants.py + 文档）
3. 回主线/物镜分支其他待办

---

## 上次会话

**日期**: 2026-06-06
**分支**: develop（Turret homing 深挖，**找到 homing_direct 不生效的真根因**；本次 WIP 已提交以临时保存，**暂停 Turret 转去调新 Z 轴**）
**位置**: octoaxesplus Turret homing —— 定位"改 config.h homing_direct 无效"的根因 = 上位机每次 HOME 都覆盖；建诊断脚本；现暂存，优先新 Z 轴

### 🔑 决定性根因：homing_direct 被上位机每次 HOME 覆盖

用户实验发现：改 config.h `homing_direct`（1↔-1）homing 方向**不变**，但在 objectives.cpp:80 手改 `-1 * speedInternal` 方向**就变了**（证明 chip 完全能反向）。顺线追出完整因果链：

```
constants.py: Turret movement_sign = -1
   │
   ▼  main_window.py send_homing(715): home_dir = 1 if sign==1 else 0  → sign=-1 → home_dir=0
   │
   ▼  发 HOME_OR_ZERO data[3]=0
commandprocessor.cpp:151: new_direct = (data[3]==HOME_NEGATIVE)? -1 : +1  → data[3]=0 → +1
   _config.homing_direct = +1     ← 【每次 HOME 都覆盖 config.h 的值!】
   │
   ▼  objectives.cpp:79: speedInternal = (+1) * vel  → 永远 +1 方向（逆时针），与 config.h 无关
```

**结论**：config.h 的 homing_direct 只是上电默认，GUI 一发 HOME 就用 data[3] 冲掉。Turret 实际 homing 方向由 **constants.py 的 movement_sign** 决定（sign=-1→dir=0→firmware homing_direct=+1）。
- Previous/Next 不受影响：走 move_objective()，符号硬编码、不读 movement_sign（所以一直对）。
- 这就是"改什么 config 都没反应"的真相。

### 之前几轮（均被本轮根因推翻/解释）

- chip 硬停路线（`enableLeftLimitSwitch=true`）：homing_direct=1/-1 **两个方向都不停、都逆时针**——因为 GUI 把方向锁死成 +1，根本没真正换过方向；不是 chip 不能停，是方向没变过。
- 验证版 W（objectives 分支 octoaxes，物镜放 W 轴）能停的机理：objectives.cpp 命中限位调 `motor_setCurrentPositionMicrosteps(0)`（内部 VMAX=0）→ 靠**软件读到限位 + VMAX=0** 停，chip 硬停只是保险。

### 硬件确认（看驱动板原理图 ~/TMC4361+TMC2240 SCH.pdf）

- TMC4361A 三个独立开关输入：**pin12 STOPL / pin13 HOME_REF / pin14 STOPR**；STOPL 有 R3 10k 下拉（空闲 LOW、active HIGH）。
- **J4/J5（本地 2-pin 开关座）未焊接** → home/限位信号经 **J1（10-pin 上主板）** 走：J1 pin1=STOPL、pin2=STOPR。HOME_REF(pin13) 只到 J4/J5 → **现在悬空**（固件 readLimitSwitches 也只读 STOPL/STOPR，不读 HOME_REF）。
- 06-04 dump 实测 STOPL_ACTIVE_F 跟随 → home 传感器经主板→J1→STOPL→pin12，链路通、chip 静态看得到。

### 电流对比（见记忆 cmd21-current-rms-peak-mismatch）

- 当前 Turret：1800mA 峰值（TMC2240, R=0.22, currentRange=1）；GUI 换位时还会覆盖成 1000mA RMS。homing 时用 1800mA 峰值。
- 已验证物镜（objectives 分支 W 轴）：3100mA（TMC2660, R=0.1, currentRange=2）。**驱动语义不同（2660=RMS/2240=峰值）不能直接比**，但已验证电流明显更高，Turret 1800mA 峰值可能偏低（独立的扭矩/丢步话题，与"不停"无关）。

### 本次产出

- **新诊断脚本** `software/common/tests/turret_homing_only.py`：纯 homing（只发 INITIALIZE 恢复 config.h 默认 + HOME，不发任何 GUI configure 覆盖），高频轮询 S:DUMPREGS 打 XACTUAL/VACTUAL/STATUS/限位 时间序列 + 自动判读"停没停 / 运动中 STOPL 是否 active"。`--dir 0/1` 控方向。
- **WIP 暂存（本次提交）**：config.h `enableLeftLimitSwitch=true`、serial.cpp `VERSION=107`、constants.py Turret `movement_sign -1→1`（=用正规通道把 GUI homing 方向翻到 homing_direct=-1 测反向）。

### ⚠️ Turret 未决问题（恢复时第一件事）

**反方向到底停不停？** 还没答。用脚本各跑一次 `--dir 0` 和 `--dir 1`（或对应 movement_sign=−1/1），看：
1. 哪个方向 VACTUAL 能归 0（停住）
2. 运动中是否曾读到 STOPL active
- 若某方向能停 → 根因纯是"方向被 movement_sign 锁错"，正解=**解耦 homing 方向与 movement_sign**（firmware 让 objective 轴 HOME 忽略 data[3] 用 config.h；或 GUI 给 objective 单独 homing 方向配置），不要靠改 movement_sign（会连带翻显示）。
- 若两方向都不停 → 回软件停车方案（enableLeftLimitSwitch=false + objectives.cpp 命中限位补 setVelocityInternal(0)+delay+setCurrentPos(0)+moveTo(0)）。

### 下一步（本次切换）

**暂停 Turret，优先调新 Z 轴**（newz 分支，MOONS' LE143S-W0601，见记忆 newz-axis-le143s-spec）。Turret WIP 已提交保存，随时可回。

---

## 上次会话

**日期**: 2026-06-05
**分支**: develop（本次提交了 config.h LEFT_SW + 双限位禁用 + objectives.cpp homing_direct 重构；**Turret homing 仍故障，根因已定位，修复待下次**）
**位置**: octoaxesplus Turret 现状 —— Previous/Next 正常，**homing 到零点后来回震荡、停不下来、读数持续往负方向涨**；根因定位 + objectives.cpp 精简

### 用户实测现象（当前代码）

- ✅ **Previous Position / Next Position 正常**：能正反转，符合预期（位置模式，有斜坡 + target，正常停）
- ❌ **homing 故障**：点 homing 后 Turret 能转回零点，但电机在零点**来回震荡无法停下**，software 读数**持续往负方向变大**（-123, -234, ...）

### 本次代码改动（已提交）

1. **config.h EXPAND1_AXIS（承接 06-04 + 本次演进）**：
   - `homingSwitch` RGHT_SW → **LEFT_SW**（06-04 实测：本板 home 传感器接 TMC4361A LEFT 输入）
   - `enableLeftLimitSwitch=false` + `enableRightLimitSwitch=false` —— **两个 chip 限位硬停都禁用**，改用「readLimitSwitches 读原始引脚电平 + objectives.cpp 软件 poll + 软件停车」架构（注释已写明：使能后该位被芯片压住、运动中读不到）
2. **objectives.cpp 精简（按用户要求）**：
   - 保留：`performHomingSequence` / `performLeavingHome` 搜索 & 离开方向跟随 `_config.homing_direct`（与 stepaxis 一致；else 分支由「按 RGHT_SW 二选一」改为统一 `-1 * homing_direct`）
   - **回退**：位与容错（`limit_state & homingSwitch` 退回精确 `==`）+ 软件主动停车（删 `motor_setVelocityInternal(0); delay(100)`）

### ⚠️ homing 故障根因（已定位，修复待下次）

**矛盾配置**：config.h 把 chip 两个限位硬停**都禁用了**（软件 poll 架构），但 objectives.cpp 的**软件停车又被回退了** → 速度模式 homing 撞到限位后**没有任何东西让它停**。

三个同类轴 STATE_HOMING_SEARCH 命中限位后的对比：

| 轴 | 命中限位后处理 | 状态 |
|---|---|---|
| StepAxis (X/Y/Z) | `motor_setVelocityInternal(0); delay(100)` → moveTo 安全位 → 设零 | 正常 |
| FilterWheel (W) | `motor_setVelocityInternal(0); delay(100)` → 设零 → `motor_moveToMicrosteps(0)` 切回位置模式 | 正常 |
| **Objectives (Turret)** | ❌ 直接 `motor_setCurrentPositionMicrosteps(0)`，**从不停速度、从不切回位置模式** | **故障** |

机理：到限位时速度指令一直挂着 + chip 硬停已禁用 → 电机越过限位继续跑（读数往负涨）→ 限位开关压上/弹开反复触发 + 残留速度指令重驱 → 来回震荡。

**推荐修复（对齐 FilterWheel 已验证范式，下次实施）**：STATE_HOMING_SEARCH 命中限位时补
```cpp
motor_setVelocityInternal(_icID, 0);  // 停车
delay(100);                            // 等待完全停止
motor_setCurrentPositionMicrosteps(_icID, 0);  // 设零
motor_moveToMicrosteps(_icID, 0);              // 切回位置模式，保持当前位置
```
（== 判定保留，不必加位与 &）。

### 反面教材

「软件 poll + 软件停车」是一套**配套架构**：禁用 chip 硬停（config.h）必须同时保留软件停车（objectives.cpp），缺一不可。本次单独回退软件停车、却留着 chip 硬停禁用，等于两头落空。教训：改 homing 的限位策略要**把 config 与状态机当一个整体**看，不能只动一侧。

### 下次

1. 实施上述 FilterWheel 范式修复，编译 + 用户实测 homing 在零点正确停车
2. （待定）若改回「chip 硬停」路线则需重新使能 enableLimitSwitch + 验证本通道硬停是否真的不工作

---

## 上次会话

**日期**: 2026-06-04
**分支**: develop（未提交，工作树有 1 处固件改动 + 文档）
**位置**: octoaxesplus Turret homing 限位开关方向修复（实测定位 LEFT vs RIGHT）

### 问题报告

用户烧录 2026-06-02 的 octoaxesplus 固件后测 Turret 物镜，homing 到限位点**不停车、冲过去**。

### 诊断（硬件实测 A/B）

用现成脚本 `software/common/tests/dump_axis_state.py Turret --port /dev/ttyACM0` 读 TMC4361A STATUS 寄存器：

| 状态 | STATUS | STOPL_ACTIVE_F (bit7, LEFT) | STOPR_ACTIVE_F (bit8, RIGHT) | readLimitSwitches() |
|---|---|---|---|---|
| 用户手工压到限位 | 0x80000083 | **1** | 0 | `0b01 = LEFT_SW` |
| 用户手工离开限位 | 0x80000003 | **0** | 0 | `0b00 = 无` |

两次读数各自稳定（STOPL 跟随传感器实时电平，非 sticky latch）→ **home 传感器物理接在 TMC4361A LEFT 输入**。

但 `firmware/octoaxesplus/config.h` EXPAND1_AXIS 是 2026-06-02 照搬 octoaxes E1 的"home 接 RIGHT"假设写的 `homingSwitch = RGHT_SW`，导致 `objectives.cpp:85 if(limit_state == _config.homingSwitch)` 比较 `0b01 == 0b10` 永远 false → 到限位不认 → homing 不停。

### 交叉验证（objectives 分支）

用户提示对照 objectives 分支。`git show objectives:firmware/octoaxes/config.h` 的 **W_AXIS**（当年把物镜放 W 轴实测可用的配置）= `homingSwitch=LEFT_SW / enableLeftLimitSwitch=true / enableRightLimitSwitch=false`，与本次硬件实测**完全一致** → 反证 2026-06-02 的 RGHT_SW 是未验证假设。

### 本次完成

**修复（仅 octoaxesplus，按用户要求 octoaxes 暂不动）**：`firmware/octoaxesplus/config.h` EXPAND1_AXIS 三行：
- `.homingSwitch` RGHT_SW → **LEFT_SW**
- `.enableLeftLimitSwitch` false → **true**
- `.enableRightLimitSwitch` true → **false**

objectives.cpp leaving-home 离开方向自动走 else 分支（负速度离左限位），无需另改。编译 **SUCCESS**（FLASH 85212，纯常量无增量）。**未烧录**。

### ⚠️ 待用户实测 + 待办

1. 用户重烧 octoaxesplus 固件，测 Turret homing 是否在限位点正确停车 + 切 4 物镜方向/位置
2. **octoaxes EXPAND1_AXIS 同款 RGHT_SW 假设暂未改**（用户要求只改 octoaxesplus）——octoaxes 板 Turret 一直未实测；很可能也需 LEFT_SW，但缺该板实测证据（另一连接器 icID=5/pin19）。详见 TODO.md 顶部子项

### 反面教材

2026-06-02 给 Turret 写限位配置时凭"对齐 octoaxes E1：home 接 RIGHT"的注释假设直接写 RGHT_SW，**既没在 octoaxesplus 板实测、也没对照 objectives 分支已验证的 LEFT_SW**。教训：限位开关方向（LEFT/RIGHT）是硬件接线决定的，必须以"该板 dump STATUS 实测"或"已验证分支配置"为准，不能跨板/跨连接器套用假设。

---

## 上次会话

**日期**: 2026-06-02
**分支**: develop（已 push 到 origin/develop = 2607902）
**位置**: octoaxesplus 物镜转换器端到端打通（AXIS_R 启用 + 增强换位 + 容错）+ 物镜轴 E1→Turret 全局重命名

### 本次完成（5 个 commit，已全部 push）

| commit | 内容 |
|---|---|
| `4b0e9f0` | docs: 记录 octoaxes constants.py X/Y index 与固件 icID 不符（潜在坑，暂不修）|
| `a048c42` | feat: octoaxesplus 启用 AXIS_R 为物镜转换器（复用 E1 协议）|
| `a8e0599` | feat: 移植 objectives 分支 move_objective 增强到 develop common |
| `a024996` | fix: octoaxesplus beginAll 死轴容错与 octoaxes 对齐 |
| `2607902` | refactor: 物镜轴 E1 → Turret 全局重命名（两固件 + 共享 software）|

#### 1. X/Y 序号调查（仅记录，未改代码）

用户连续追问 develop↔objectives、octoaxes↔octoaxesplus 的 X/Y 是否反。结论：
- **develop↔objectives 的 X/Y 完全相同**（byte-identical），分支唯一差异是 W(objectives 把 W 改物镜) vs E1(develop 物镜另起 E1)。
- **octoaxes↔octoaxesplus 的 X/Y icID 确实是反的**（octoaxes "X"=icID0/"Y"=icID1；octoaxesplus "X"=icID1/"Y"=icID0），是两套 PCB 接线不同的各自补偿，运动命令按轴名路由不受影响。
- **遗留隐患**：octoaxes constants.py X index=1/Y index=0，但固件 icID 是 X=0/Y=1 —— **对不上**。当前走 24 字节包不查 index 故无害；**启用 40 字节扩展包前必须改 X→0/Y→1**。详见 TODO.md 2026-06-02 段 + 记忆 octoaxes-xy-icid-index-mismatch。

#### 2. octoaxesplus AXIS_R 启用为物镜转换器（复用 E1 协议）

决策（用户拍板）：**复用 octoaxes E1 协议**（不新建 R 协议）+ **沿用 octoaxes E1 调参**。改 9 处（config.h Commands MOVE_E1/MOVETO_E1 + OBJECTIVES 电流 1000→1800/加速度 200→80 + EXPAND1_AXIS homingSwitch RGHT_SW/enable 对调/currentRange=1；TMC_SPI.cpp HC154 icID=5 槽位 F2→R；.ino new Objectives(R_AXIS_CS,5,...)；commandprocessor handler + protocolAxisToName case7；serial 分发；constants.py 加物镜轴）。beginAll 已有 "E1"/"R" 分支、common 层已 E1-ready、Objectives 类两固件一致 —— 大量白捡。

#### 3. 移植 move_objective 增强到 common（develop 原为基础版）

从 objectives 分支移植到共享 common（两 profile 受益）：齿轮回程间隙补偿、直接微步不经 μm 截断、首次换位懒加载下发柔和电机参数、运动期间使能+同步等待防掉电丢步。define.py 加 6 个 OBJECTIVE_* 常量。homing handler 改 type-based（顺带修正 octoaxesplus W1/W2 漏 offset + E1 误走 set_limits 两个旧 bug）。**比原版多一处**：_ensure_objective_configured 协议映射加 "Turret": AXIS.TURRET，让 develop 物镜轴真正收到柔和参数（objectives 分支物镜叫 W 才跳过）。

#### 4. octoaxesplus beginAll 死轴容错（补 2026-05-26 octoaxes 修复的漏同步）

octoaxesplus beginAll 失败分支补 `delete axes[i]; axes[i]=nullptr;`。修复前：缺驱动板（如 Turret）时指针仍非空，发 move/home → 打死 chip → 卡 moving → any_moving 永真 → 上位机 wait 5s 超时拖累整机。修复后：缺任何板都不影响其他轴，缺板轴命令 silent no-op + 即时 COMPLETED。

#### 5. 物镜轴 E1 → Turret 全局重命名

"E1" 是历史扩展槽位名不直观，物镜转换器改用行业标准名 Turret。**协议值 44/45/轴码 7 不变（串口兼容）**。改名："E1"→"Turret" / MOVE_E1→MOVE_TURRET / handleMoveE1→handleMoveTurret / AXIS.E1→AXIS.TURRET / 局部变量 e1Axis→turretAxis。**保留**硬件槽位名 EXPAND1_AXIS/EXPAND1_AXIS_CS/PIN_CS_E1/R_AXIS_CS/IC_E1，**不动 E3/E4**。覆盖 20 文件（两固件 + 共享 software）。

### 验证状态

- 两固件编译 SUCCESS（octoaxesplus FLASH ~85K，octoaxes 无回归）
- 两 profile 加载：octoaxes `['E3','E4','Turret','W','X','Y','Z']` / octoaxesplus `['Turret','W1','W2','X','Y','Z']`
- py_compile 通过；协议 AXIS.TURRET=7 / MOVE_TURRET=44 / MOVETO_TURRET=45
- 用户已自行烧录 + software 测试（测试结果待用户反馈，未回报具体现象）

### ⚠️ 待用户实测验证（未回报）

1. Turret 物镜：homing（home sensor 接 TMC4361A RIGHT 引脚→RGHT_SW）、previous/next 切 4 物镜方向与位置显示、1800mA + 柔和参数下不丢步
2. 其他轴（X/Y/Z/W1/W2）不受 Turret 影响
3. 缺板容错：对缺板轴发命令整机仍流畅、不卡 5s
4. **注意**：GUI 配置 key 从 "E1" 变 "Turret"，需重烧固件 + 重启 software 后物镜页才以 "Turret" 出现

### ⚠️ 固件/GUI 两层参数关系（Turret）

固件 EXPAND1_AXIS = 1800mA 峰值/accel80（begin 默认 + homing 用）；GUI 首次换位下发"柔和运行参数" 1000mA(RMS≈1414峰值)/accel10/vel0.5 覆盖之 —— objectives 分支设计的两层意图（homing 高扭矩，正常换位柔和）。烧录后需观察是否丢步。

### 下次

1. 收用户 Turret 实测反馈（上述 4 项），按现象调参或修复
2. 回主线：硬件紧固 W motor↔wheel + 采集 8s 打点

---

## 上次会话

**日期**: 2026-05-29
**分支**: develop
**位置**: 把 objectives 分支的物镜转换器代码适配到 develop 的 E1 轴（W 滤光轮完全不动）

### 背景

objectives 分支（领先 develop 7 commit，线性）为测试物镜把 **W 轴 (icID=3)** 从 FilterWheel 改成 Objectives。但这会破坏 develop 上 W 滤光轮的全部成果（72ms 速度优化、ABN 编码器、W2 适配）。用户要求：把 objectives 分支里**真正属于物镜转换器的通用代码**适配到 develop 的 **E1 轴**，不动 W。

develop 上 E1 在软件里本就是 `type=objective` + 用 `EXPAND1_AXIS` 配置模板，但 firmware **从未实例化 E1**（octoaxes.ino 只 new 了 X/Y/Z/W/W2），且协议路由断裂（`MOVE_W` 硬编码到 "W"，E1 复用 MOVE_W 会误驱动 W）。所以物镜功能在 develop 上一直是死代码。

### 硬件确认（用户 2026-05-29）

- 轴构成：**6 轴并存** X/Y/Z/W(滤光轮)/W2(滤光轮2)/**E1(物镜转换器, 4 物镜)**
- E1 CS = **pin 19** (`EXPAND1_AXIS_CS`)，时钟 CLK = pin 28 (CLOCK_EXPAND，与 W2 共用扩展时钟线)
- E1 占 **icID=5**（新增），`TMC4361A_IC_COUNT` 5→6

### 本次完成（12 文件，两固件 + 两 profile 全部编译/加载通过）

#### Firmware（8 文件）

| 文件 | 改动 |
|---|---|
| `tmc/hal/TMC_SPI.h` | `TMC4361A/TMC2660_IC_COUNT` 5→6；IC 枚举更正（icID4=W2, icID5=E1，加"实际由 addAxis 顺序决定"注释） |
| `tmc/hal/TMC_SPI.cpp` | 加 `#define PIN_CS_E1 19` + tmc_ic_configs 第 6 槽 `{PIN_CS_E1, CLOCK_EXPAND}` (icID=5) |
| `octoaxes.ino` | `new Objectives(Pins::EXPAND1_AXIS_CS, 5, "E1", 4)` + addAxis（顺序 X/Y/Z/W/W2/E1） |
| `config.h` | EXPAND1_AXIS：`homingSwitch LEFT_SW→RGHT_SW` + enableLeft/Right 对调 + `currentRange 0→1`；常量 `MAX_ACCELERATION_OBJECTIVES` 200→80、`OBJECTIVES_MOTOR_PEAK_CURRENT` 1000→1800；Commands 加 `MOVE_E1=44`/`MOVETO_E1=45` |
| `objectives.cpp` | homing 根因修复 `OBSW_SW`→`_config.homingSwitch`（performHomingSequence × 2 + performLeavingHome × 1） |
| `commandprocessor.h/cpp` | 新增 `handleMoveE1`/`handleMoveToE1`（findAxisByName("E1")，仿 handleMoveW2）；`protocolAxisToName` 加 case 7→"E1" |
| `serial.cpp` | 分发 `case MOVE_E1`/`MOVETO_E1` |

#### 上位机（4 文件）

| 文件 | 改动 |
|---|---|
| `common/define.py` | `AXIS.E1=7`、`CMD_SET.MOVE_E1=44`/`MOVETO_E1=45`、`AXIS_MOVE_CMD_MAP["E1"]→MOVE_E1`、`AXIS_MOVETO_CMD_MAP["E1"]→MOVETO_E1`（不再复用 MOVE_W） |
| `octoaxes/constants.py` E1 | limits (0,4)→**(0,3)**；movement_sign 1→**-1**（翻转显示 + 让 homing home_dir=0→+1 与 EXPAND1_AXIS.homing_direct=1 一致）；**index 4→5**（=firmware icID） |
| `common/gui/main_window.py` | homing `_AXIS_PROTOCOL` 加 `"E1": AXIS.E1` |
| `common/gui/widgets.py` | `rounds_label` 改 `self.rounds_label`；objective 类型隐藏 Test/Rounds 控件（仅 filter_wheel 显示） |

### 为什么必须加专属协议命令（不能纯移植）

objectives 分支干脆改 W 来绕开协议路由问题：MOVE_W → handleMoveW → findAxisByName("W")，而 W 此时是 Objectives 实例，"自动 work"。要把物镜放 E1 而不动 W，必须解决 objectives 分支回避的协议路由——`MOVE_W`/`MOVETO_W` 硬编码到 "W" 且不带轴索引，无法兼用。故仿 W2 给 E1 加专属 `MOVE_E1=44`/`MOVETO_E1=45` + `protocolAxisToName` case 7（旧 Squid 不发这些命令，不破坏字节级 drop-in）。

### 编译/加载验证

- `firmware/octoaxes` teensy41: **SUCCESS** (FLASH 81628)
- `firmware/octoaxesplus` teensy41: **SUCCESS**（共享 tmc/ 符号链接改动都在 `#ifndef USE_HC154_CS` 内，HC154 路径不受影响）
- 两 profile constants 加载正常：E1 type=objective / index=5 / limits=(0,3) / sign=-1 / mm_per_step=7.8125e-05；协议映射 MOVE_E1=44 / MOVETO_E1=45 / AXIS.E1=7 全部正确

### ⚠️ 已知限制（选 E1 而非 W 的固有代价）

E1 **不在 24 字节响应包**（仅含 X/Y/Z/W）。objectives 分支把物镜放 W 所以位置能从响应包回读；E1 物镜运动/homing 都正常，但 **GUI 位置显示不会从固件刷新**。要 E1 位置回读需走 40 字节扩展包（Phase 3.3，需硬件验证）。

### 待用户验证（未烧录）

1. E1 homing（home sensor 接 TMC4361A RIGHT 引脚 → RGHT_SW）
2. previous/next 切 4 物镜的方向与位置显示（movement_sign=-1，Next 显正值）
3. 1800mA + 加速度 80 mm/s² 下齿轮减速物镜不丢步
4. E1 板未插场景：beginAll delete+nullptr → MOVE_E1/HOME 命令 silent no-op，不影响 X/Y/Z/W/W2

### 下次

1. 烧录 + 实测验证上述 4 项
2. （可选）E1 物镜测试脚本
3. （可选）若需 E1 位置回读 → 推进 40 字节扩展包
4. 回到主题：硬件紧固 W motor↔wheel + 采集 8s 打点

---

## 上次会话

**日期**: 2026-05-26 续二
**分支**: develop
**位置**: W 轴速度优化 — 1 slot 181ms → 72ms (-60%)

### 总览

完整优化路径见 `documents/baselines/W-speed-optimization-20260526.md`。

5 个里程碑：
- **B1**: 脚本 `wait_completed` idle frames 5→1（去除 50ms 人为防抖）
- **B2**: firmware `axis.cpp` W/W2 `target_tolerance` 2→20 encoder counts
- **C v2**: `astartMM = 22.5f * SCREW_PITCH_FILTERWHEEL_MM`（与历史 chip 寄存器值匹配）
- **路径 D**: `MICROSTEPPING_FILTERWHEEL` 64 → 16（BOW 截断 29→7×）
- **最终**: `MICROSTEPPING_FILTERWHEEL` 16 → 8（BOW 截断 7→3.6×，匹配 2026-02 历史最优）

### 1 slot 时间演进

```
181ms (原)  →  141ms (B1)  →  129ms (C v2)  →  87ms (ms=16)  →  72ms (ms=8)
            -22%             -29%            -52%             -60%
```

### 全档位最终数据 (ms=8, ASTART=22.5, PID on, P=4096, idle=1, tol=20)

| 角度 | 时间 (ms) | std (ms) |
|---|---|---|
| 1.4° (6 µstep) | 23.3 | 0.2 |
| 5.6° (25) | 37.0 | 0.3 |
| 22.5° (100) | 57.5 | 0.3 |
| **45° (200) = 1 slot** | **72.2** | 0.3 |
| 90° (400) | 97.5 | 0.3 |
| 135° (600) | 125.9 | 0.3 |
| 180° (800) | 154.8 | 0.3 |

重复性极佳：std < 0.4ms 全档位。

### 失败实验记录（路径 C v1）

ASTART=180 rev/s² @ microstep=64 直接复用历史值灾难性失败：
- 短距 50 µstep 退化 +187ms（62 → 234ms）
- HOME 时间从 1.4s 暴涨到 17.6s
- Offset 末位置严重过冲（W=628 vs 期望 102）

**根因**：chip 寄存器层 ASTART = 180 × 12800 = 2.3M µstep/s²（历史 ms=8 时 288K，本次 ms=64 翻 8 倍）。短距 chip ramp 起步过猛 → encoder 检测过冲 → PID 反拉振荡。

**教训**：跨微步参数迁移要按"chip 寄存器值等价"换算，不能直接复用 rev/s² 物理值。

### 字节级对齐牺牲

本次优化偏离 CLAUDE.md "字节级 drop-in 旧 Squid firmware" 原则：
1. **MICROSTEPPING_FILTERWHEEL = 8**（旧 Squid 64）— 旧 Squid software 会通过 `configure_motor_driver` 协议覆盖回 64，**本次优化仅对 benchmark 脚本有效**，对旧 Squid GUI 实际无效
2. **ASTART = 22.5 rev/s²**（旧 Squid `sRampInit` 强制 0）— 旧 Squid software 无法感知 chip 寄存器，**对旧 Squid GUI 完全透明**

**对旧 Squid GUI 实际有效的优化**：仅 B2 (tolerance=20) + ASTART=22.5（chip 层），约节省 ~50ms。要享受全部收益需要修改旧 Squid software 让它发 `configure_motor_driver(W, 8, ...)`。

### 配置最终值

`firmware/octoaxes/config.h`:
- `MICROSTEPPING_FILTERWHEEL = 8`
- `HOMING_MICROSTEPPING_FILTERWHEEL = 256`（不变）
- `W_AXIS.astartMM = 22.5f * SCREW_PITCH_FILTERWHEEL_MM`
- `EXPAND4_AXIS.astartMM = 22.5f * SCREW_PITCH_FILTERWHEEL_MM`（W2 同步）

`firmware/octoaxes/axis.cpp` + `firmware/octoaxesplus/axis.cpp`:
- W/W2 `target_tolerance = 20`, `pid_tolerance = 20`（替代之前 2）

`software/common/tests/benchmark_w_speed.py`:
- 新增 `--pid`, `--pid-p/-i/-d`, `--idle-frames`, `--label` CLI
- W_MICROSTEPPING = 8
- 距离档位改为基于角度派生 (跨微步物理可比)
- 新增 `send_init_filter_wheel` + `configure_pid` 函数

### 剩余优化空间（未实施）

| 路径 | 估算 | 风险 |
|---|---|---|
| ASTART 22.5 → 180 @ ms=8 (匹配历史 chip 寄存器 288K µstep/s²) | 1 slot 72 → ~65ms | 低，历史已证 |
| 调大 VMAX | 协议层硬约束（旧 Squid software 覆盖） | 不可行 |
| Target pipeline | 仅连续 move 受益 | 中难度，单 slot 无效 |

**理论物理底线**（jerk-limited @ ms=8 + BOW 不截断）≈ 40ms motor + 5ms overhead = 45ms。

### 下次

1. 决策：试 ASTART=180 @ ms=8（打满到历史 ~65ms）还是定稿 ms=8 + ASTART=22.5 (72ms)
2. 提交（firmware + 脚本 + 9 份基线 + 优化文档）
3. 决定是否同步改动到 octoaxesplus
4. 回到主题：硬件紧固 W motor↔wheel + 采集 8s 打点

---

## 上次会话

**日期**: 2026-05-26 续
**分支**: develop
**位置**: INITFILTERWHEEL 触发 homing 的字节级偏差修复（长 homing 时 5s 超时根因）

### 问题报告

用户场景：旧 Squid software + octoaxes firmware，在 homing 时间较长时 GUI 启动报 `TimeoutError: Current mcu operation timed out after 5 [s]`。Traceback 显示 `configure_squidfilter` → `set_leadscrew_pitch` → `wait_till_operation_is_completed` 超时。旧 Squid software + 旧 Squid firmware 无此问题。

### 根因诊断（旧 Squid 源码对照）

**旧 Squid `callback_initfilterwheel` (commands.cpp:188-192)**：
```cpp
void callback_initfilterwheel() {
    enable_filterwheel = true;
    init_filterwheel_axis(w);  // 仅 chip 寄存器配置（atomic）
}
// 不设 mcu_cmd_execution_in_progress，不触发 homing
```

**octoaxes `handleInitFilterWheel`（错版）**：
```cpp
Axis *axis = axisManager.findAxisByName("W");
if (axis) {
  axis->startHoming();   // ← 错：触发实际 homing
}
```

**触发链**（旧 Squid `cephla.py::_configure_wheel`）：
```python
self.microcontroller.init_filter_wheel(axis)         # cmd 253
time.sleep(0.5)
self.microcontroller.configure_squidfilter(axis)     # set_leadscrew_pitch + wait(5s) + ...
```

- 旧 Squid fw: chip config 原子，wait 立即返回 ✓
- octoaxes fw: W 仍在 homing → any_moving=true → status=IN_PROGRESS → set_leadscrew_pitch 后 **wait 5s 超时** ❌

实际 W homing 由后续 `home_w()` (HOME_OR_ZERO + AXIS_W) 单独触发，符合旧 Squid 协议。

### 本次完成

**修复**：`handleInitFilterWheel` / `handleInitFilterWheelW2` 改为 no-op + 日志（两 firmware 同步）。

- `firmware/octoaxes/commandprocessor.cpp`: 删除 startHoming，加完整注释解释字节级偏差
- `firmware/octoaxesplus/commandprocessor.cpp`: 同步修复（保持两 firmware 一致）

**为何 no-op 安全**：
1. W/W1/W2 轴在 `axesmrg::beginAll` 启动时已配置 filter wheel 模式（W_AXIS / EXPAND4_AXIS / W2_AXIS 模板），chip 已初始化
2. 后续 `configure_squidfilter` 的 set_leadscrew_pitch / configure_motor_driver / set_max_velocity_acceleration 会重写 chip 关键寄存器（microstep/current/VMAX/AMAX）
3. octoaxes GUI 自身不调用 INITFILTERWHEEL（software/common/define.py:105 仅定义命令值，无调用代码），零回归

### 反面教材

把"INITFILTERWHEEL"望文生义解释成"初始化滤光轮（包括 homing）"，但旧 Squid 协议里它只是"chip 配置 + enable flag"。homing 走单独的 HOME_OR_ZERO 命令。

**教训**：实现协议命令时必须以**对方协议规范**为准，不能用名字推断行为。drop-in replacement 任何 handler 都要看旧 Squid 源码 callback 验证语义。

### 编译验证

- `firmware/octoaxes` teensy41: SUCCESS（FLASH 几乎不变）
- `firmware/octoaxesplus` teensy41: SUCCESS

未烧录，等用户硬件空闲。

### 待用户验证

1. 旧 Squid software + octoaxes firmware：启动 `_configure_wheel` 在长 homing 场景下不再 5s 超时
2. W/W2 homing 通过单独的 `home_w()` / `home_w2()` 命令正常触发
3. configure_squidfilter 全程通过：set_leadscrew_pitch + configure_motor_driver + set_max_velocity_acceleration 三步均在原子时间内 COMPLETED

---

## 上次会话

**日期**: 2026-05-26
**分支**: develop
**位置**: W2 (Filter Wheel 2) 适配 + W invert_direction 回归 false（字节级 drop-in 一致性修复）

### 背景

CLAUDE.md 写的核心目标：**octoaxes firmware 字节级替代旧 Squid firmware**，让"旧 Squid software + octoaxes firmware"与"旧 Squid software + 旧 Squid firmware"行为完全一致。

旧 Squid firmware 有 W2 (AXIS_W2=6) 第二滤光转盘，octoaxes 主线长期未实例化 → 旧 Squid software 发 MOVE_W2/INITFILTERWHEEL_W2 等命令时 silent no-op。本次补齐。

同时 2026-05-25 加的 W_AXIS.invert_direction=true 被用户测出导致 next/previous 方向与旧 Squid 反 → 回滚到 false。

### 本次完成

#### 1. W2 (Filter Wheel 2) 适配（6 文件，3 环境编译通过）

旧 Squid software 发的 W2 命令清单与 octoaxes 现状：

| 旧 Squid software 命令 | cmd | octoaxes 改前 | octoaxes 改后 |
|---|---|---|---|
| `move_w2_usteps()` | MOVE_W2=19 | stub: NOT_IMPLEMENTED | 完整实现 (handleMoveW2) ✓ |
| `init_filter_wheel(W2)` | INITFILTERWHEEL_W2=252 | handler 已存在，W2 未实例化 → no-op | W2 已实例化 ✓ |
| `home_w2()` / `zero_w2()` | HOME_OR_ZERO=5 + axis=6 | handler 已支持 W2 路由，axis 未实例化 | ✓ |
| `configure_squidfilter(W2)` 内含 leadscrew/driver/velocity | 通用 axis cmd + axis=6 | `protocolAxisToName(6)→"W2"` 已就绪 | ✓ |
| `configure_stage_pid(W2)` etc | 通用 axis cmd | 同上 | ✓ |
| 24 字节响应包 W2 位置 | — | 旧 Squid 也不上报 W2，上位机本地累加 | 零改动 ✓ |

**关键硬件对齐**：W2 复用原 EXPAND4 硬件，CS=pin 16, CLK=pin 28，与旧 Squid `pin_TMC4361_CS[4]=16` / `pin_TMC4361_CLK_W2=28` 字节级一致。

**改动**：
- `tmc/hal/TMC_SPI.h`: TMC4361A_IC_COUNT 7→5（移除 E1/E3 未实例化槽位）
- `tmc/hal/TMC_SPI.cpp`: 删 PIN_CS_E1/E3，新增 PIN_CS_W2=16，tmc_ic_configs[] 5 槽位 (Y/X/Z/W/W2)
- `config.h`: 加 `Pins::W2_AXIS_CS = EXPAND4_AXIS_CS` 别名；EXPAND4_AXIS 仍作 W2 config 模板
- `octoaxes.ino`: 实例化 `new FilterWheel(Pins::W2_AXIS_CS, 4, "W2")`，addAxis 顺序 X/Y/Z/W/W2
- `axesmrg.cpp::beginAll`: 加 "W2" → EXPAND4_AXIS 分支
- `commandprocessor.cpp`: handleMoveW2 完整实现（仿 octoaxesplus）

#### 2. W2 板未插兼容性代码（通用化处理）

`axesmrg::beginAll` 检测到任意轴 begin 失败时（SPI 无响应：写 SW_RESET 后读 VERSION_NO 返回 0/-1）：

```cpp
if (!success) {
  DEBUG_PRINT("Failed to initialize axis: ");
  DEBUG_PRINTLN(axisName);
  delete axes[i];     // ← 新增：删除 Axis 实例
  axes[i] = nullptr;  // ← 新增：槽位置 nullptr
  allSuccess = false;
}
```

**效果链**：
- `findAxisByName("W2")` 已 nullptr-safe（line 95 检查 `axes[i] != nullptr`） → 返回 nullptr
- 所有 W2 handler 的 `if (axis) axis->...` 保护自动让命令 silent no-op
- `send_position_update` 检测 any_moving=false 立即报 STATUS_COMPLETED
- 旧 Squid `wait_till_operation_is_completed` 立刻唤醒，不卡 5 秒 timeout
- 不会浪费 SPI 总线对死 chip 反复轮询

**通用化**：所有 5 轴都享受这层保护（不只 W2 专用）。比当前"保留 dead axis"行为严格更好，无退化风险。

#### 3. W_AXIS.invert_direction 从 true 回归 false（字节级 drop-in 修复）

**问题报告**：用户在旧 Squid software 下，分别用旧 Squid firmware 与 octoaxes firmware 做下位机，点 filter wheel next/previous **物理方向相反**。

**根因**：2026-05-25 加的 `W_AXIS.invert_direction=true`（"修正镜像装配让 home+offset 落 1 号孔位"）让 axis.cpp 入口反相 MOVE_W/MOVETO_W，filterwheel.cpp 反相 motor_setVelocityInternal 速度 → 所有 W 运动物理方向与旧 Squid 反。

**决策**：用户选择"纯字节级替代"，回滚 invert_direction：
- `W_AXIS.invert_direction` true → **false**
- `EXPAND4_AXIS.invert_direction` true → **false**（W2 同步）
- 代码层（axis.cpp / filterwheel.cpp）反相逻辑保留（其他轴未来如需启用直接打开 flag），仅配置位翻转

**牺牲**：W home+offset 物理停在 +2.87°（你硬件镜像装配引起），不在 1 号孔位中心 — 与旧 Squid firmware 在你硬件上完全相同的"固有错位"。slot 1 精准对齐需要硬件层重新装配 wheel，不是 firmware 能修复的。

### 反面教材

2026-05-25 的"firmware 加镜像适配层"决策违反了 CLAUDE.md 的"字节级 drop-in replacement"目标。当时只验证 home+offset 视觉落位 + chip frame round-trip 测试 140/140 通过，**没测物理方向是否与旧 Squid 一致**（round-trip 测试是 next×7 + previous×7 闭环，方向反也能通过；视觉确认是单点位置，看不出运动方向）。

**教训**：drop-in replacement 的回归测试必须包含**多方向比对**，不只单点位置。物理硬件相关的"修正"应推到硬件层（重装配）或上位机 config 层（调 offset），不要把 firmware 改成"比旧 firmware 行为更好"，会破坏字节级一致性。

### 编译验证

| 环境 | FLASH | 增量 |
|---|---|---|
| firmware/octoaxes teensy41 | 80796 | +128B (W2 添加 + handleMoveW2) |
| firmware/octoaxes teensy41_debug | 89412 | — |
| firmware/octoaxes teensy41_nointerlock | (success) | — |
| firmware/octoaxesplus teensy41 | 84380 | 0（tmc/ 符号链接走 HC154 路径不受影响） |

**未烧录**，等用户硬件空闲。

### 待用户验证

1. 旧 Squid software → octoaxes firmware：W next/previous 方向应与旧 Squid firmware 一致
2. 旧 Squid software → octoaxes firmware：W2 端到端通过（init/home/move_w2/configure_squidfilter）
3. W2 板未插场景：拔掉 W2 后跑同套测试，确认 X/Y/Z/W 仍工作，W2 命令 silent no-op
4. W home+offset 落点回到 +2.87°（不在 1 号孔位中心是预期行为，与旧 Squid 一致）

### 下次

1. 烧录 + 用户实测验证 4 项
2. 等用户硬件紧固 motor↔wheel（继续 W 累积漂移课题）
3. 采集 8s 打点 firmware（继续 acquisition 优化）

---

## 上次会话

**日期**: 2026-05-25 续（晚二）
**分支**: develop
**位置**: W 累积漂移根因定位 — motor↔wheel 机械打滑（chip_w 不变但 wheel 转角累积偏移）

### 背景

5-25 下午确认 chip_w deterministic + round-trip 140/140 通过后，用户视觉报告"差异越来越大"。
本段会话定位真因：**降速测试 + 编码器位置确认 + 用户硬件直接观察打滑**。

### 关键证据链（按时间顺序）

1. **临时降速实验**：W 速度 4.2→1.0 mm/s，加速度 400→80 mm/s²（编译通过 80668 字节）。用户实测**漂移依然出现**，且视觉判断偏移已达"半个孔位"（≈800 µstep ≈ 22.5°）
2. **日志分析（W-POS 108 条）**：

   | slot | 次数 | chip_w 分布 |
   |---|---|---|
   | 1 (home+offset) | 2 | 103, 112（差 9 µstep ≈ 0.25°） |
   | 3 | 36 | 35 次 = 3300，首次 3296（差 4 µstep） |
   | 4 | 36 | 35 次 = 4896，末次 4900（差 4 µstep） |
   | 5 | 36 | 早期 6493 / 后期 6496（差 3 µstep） |

   **chip_w 全程漂移 ≤ 9 µstep ≈ 0.25°，比视觉偏移 22.5° 小 90 倍**
3. **编码器位置**：用户确认装在 **motor 后端同轴**（不是 wheel 端）
4. **视觉参考**：用户用"转盘孔位中心 vs 固定参考点（镜筒/指示线）"判断偏移
5. **用户实测打滑**：用户直接观察到 **motor 端与 wheel 端之间发生打滑**

### 结论

```
chip 命令 motor 转 1600 µstep
     ↓
motor 转子转 22.5°          ← encoder 装这里 (chip_w 读到这里) ✓ 准确
     ↓ ⚠️ 累积打滑发生在这一段（用户已视觉确认）
联轴器 / 皮带 / 紧定螺钉
     ↓
filter wheel 实际转 < 22.5° ← 用户看到的位置慢慢漂移
```

- 这是**静态机械问题**，降速降加速度无效（已实测验证）
- chip 端控制和编码器读取都完美，无任何 firmware/software 缺陷
- 必须硬件层面修复（紧固/张紧/更换松动元件）

### 本次完成

1. **临时降速测试 + 撤回**：临时改 config.h 速度/加速度，实测后恢复 4.2/400 原值，工作树干净
2. **日志统计验证 chip_w deterministic**：108 条 W-POS 全部归类，最大漂移 9 µstep
3. **编码器物理位置确认**：motor 后端同轴
4. **根因定位**：motor↔wheel 机械打滑（用户硬件直接观察 + 日志数据双重证据）

### 反面教材 — 降速无效

初步假设是"降低加速度 → 减小瞬时扭矩 → 减少打滑"，**用户实测推翻**：
- 降速到 4.2→1.0 / 加速度 400→80，仍漂移
- 说明**不是动态扭矩问题**，是**静态预紧不足**（紧定螺钉松动 / 联轴器虚位 / 皮带松弛）
- 静态机械松动与运动参数完全无关，软件层任何参数调整都无效

教训：见到"运动相关问题"先别下意识改运动参数，先用数据定位是 chip 端、传动端还是 load 端。

### 修复方向（不在软件侧）

| 元件 | 检查方法 | 修复手段 |
|---|---|---|
| 联轴器紧定螺钉（最常见） | 内六角验证螺钉是否松动 | 拧紧 + 加螺纹胶 |
| 皮带轮紧定螺钉 / 平键 | 同上 | 同上 |
| 皮带张力 | 手指压皮带中段下陷量 | 张紧机构调节 |
| 联轴器弹性元件 | 拆开看磨损/碎屑 | 更换 |
| set screw 平面咬合 | 看 motor 轴是否有铣平面 | 平面铣槽 |

### 可选软件层缓解（待用户决定）

**周期 auto-home**：每 N 次切槽位自动 home 一次，强制 motor↔wheel 重新对齐
- 优点：硬件未修前可日常使用
- 缺点：每次 home 用户看到转盘大转，体验差；home 期间不能正常切通道

**长期改进**：编码器移到 wheel 端 + 启用 chip PID 闭环
- 让 chip_w 反映 wheel 真实位置，chip 自动纠偏打滑
- 需要硬件改装

### 下次

1. 等用户紧固松动元件后回测，确认漂移消失
2. 决定是否实施"周期 auto-home"软件缓解
3. 如果硬件改不动，考虑把编码器移到 wheel 端 + 开 PID 闭环

---

## 上次会话

**日期**: 2026-05-25
**分支**: develop
**位置**: octoaxes firmware 完整替代旧 Squid firmware 能力 — filterwheel.cpp 行为对齐 + 硬件方向反相层 + round-trip 测试 + 视觉验证

### 5-25 晚追加：round-trip 测试 + 视觉确认配置正确

下午 commit cae430f 完成后，用户跑 GUI 视觉发现"测试完成转盘不在孔位上"，进入深度调试。

**新建 `software/common/tests/test_w_round_trip.py`** (commit 7e7de9a)：完整复刻 GUI `run_w_test` 流程 (启用编码器 → send_homing → N 轮 (next ×7 + previous ×7)，每 move 后 sleep(0.5))，增强：每步比对 ENC_POS 与期望位置。

实测 10 轮 × 7 槽 = 140 次 move 全部通过，每步误差 ≤7 µstep ≈ 0.2°，累计漂移 +7 µstep，chip 行为完全 deterministic。

#### 视觉位置怀疑 → 多次手动定位测量

用户多次 disable axis 手动转盘到"1 号孔位"读编码器：
- 5-22 三次: -157, +83, -141 (不一致)
- 5-25: -839 (与脚本完成位置 +103 差 -736 µstep ≈ -20.7°)
- 测 2-8 号孔位 chip raw: +700, +2323, +3913, +5504, +7120, +8713, +10316
- 8 孔间距平均 1593.6 µstep ≈ 44.8° (与设计 45° 极接近)

数据看似显示：脚本完成位置（W=+103）不在任何孔位中心，与"用户手动 1 号孔位" -839 差 ≈ 0.46 孔位。曾怀疑硬件 home 标志位置偏离 1 号孔位 20°。

#### 关键转折：用户视觉确认 chip 真的在 1 号孔位中心

让用户直接看 chip 当前停的位置（W=+103，没动），**用户视觉确认转盘对准 1 号孔位中心 ✓**。

**之前手动测量数据全部不可靠** — 视觉判断"哪个是 1 号孔位"在 8 孔均匀转盘上不可靠（转过头一格差 1600 µstep）。chip ENC_POS 才是 ground truth。

### 最终配置确认（不变）

| 配置 | 值 | 来源 |
|---|---|---|
| W chip 量纲 | 1mm pitch / 64 微步 | `firmware/{octoaxes,octoaxesplus}/config.h` |
| ASTART | 0 | 同上 W_AXIS/EXPAND4_AXIS |
| has_encoder | True | `software/octoaxes/constants.py` W |
| **W invert_direction** | **true** | `firmware/octoaxes/config.h` W_AXIS/EXPAND4_AXIS |
| **SQUID_FILTERWHEEL_OFFSET** | **+0.008** (与旧 Squid 完全一致) | `software/common/define.py` |
| filterwheel.cpp | 与旧 Squid W 段一致（硬编码 + 方向 search） | commit cae430f 撤销了 2b5dce4 W 部分 |

### 反面教材

**手动定位的视觉精度**：8 孔均匀转盘上视觉判断"哪个孔是 1 号"非常容易出错。转过头一格差 45°（1600 µstep），眼睛容差大。**chip ENC_POS 是 ground truth，不要用人眼"测量"绝对位置**。

未来类似调试，应该：
1. 先让 chip 跑到设计位置（脚本 home + offset 后不动）
2. **视觉确认**当前停的位置（"在 / 不在 / 偏多少"）
3. 而不是反过来"手动转到 1 号孔位 + 测 chip raw"

### 当前提交

- 7e7de9a test: W round-trip 测试脚本（已 push）
- cae430f feat: octoaxes 替代旧 Squid firmware 能力（已 push）
- 5-25 晚的"视觉确认 + 文档"将单独 commit

### 目标

让 **"旧 Squid software + octoaxes firmware"** 与 **"旧 Squid software + 旧 Squid firmware"** 行为完全一致。旧 Squid software 不可改，octoaxes firmware 必须 mimic 旧 Squid firmware 的字节级行为。

### 深入对比旧 Squid firmware W 段，发现两个偏离

#### 偏离 1：commit 2b5dce4 的"方向 bug 修复"实际偏离旧 Squid W 段

旧 Squid `stage_commands.cpp:621-636` W HOME_NEGATIVE 段：
```cpp
if (homing_direction_W == HOME_NEGATIVE) {
  if (limit_state == 0x00) {
    setSpeed(LEFT_DIR * vel);   // 在感应区，朝 LEFT 移出
  } else {
    setSpeed(RGHT_DIR * vel);   // ★ 不在感应区，朝 RGHT (+ 方向) search
  }
}
```

**W 段 HOME_NEGATIVE search 方向是 RGHT_DIR (+1)**，与 X/Y/Z 用 LEFT_DIR (-1) search 相反。这是 W 段特定行为（filter wheel 光电感应器逻辑反向）。

commit 2b5dce4 把 filterwheel.cpp 的硬编码 + 方向改成"跟 `_config.homing_direct`"（与 stepaxis.cpp X/Y/Z 对齐），**结果反而偏离旧 Squid W 段特定行为**。

#### 偏离 2：硬件镜像装配但 firmware 没适配层

本硬件 1 号孔位在 home 标志的**顺时针**方向（chip -141 µstep = -3.97°），但旧 Squid 设计假设 1 号孔位在 home 标志的**逆时针**方向（chip +102 µstep = +2.87°）。

旧 Squid firmware **没有硬件方向反相功能**，所以"旧 Squid software + 旧 Squid firmware + 你硬件"实际也错（chip 停在 +2.87° 不到 1 号孔位）。如果想用"旧 Squid software + octoaxes firmware"在你硬件上正确，必须 firmware 加镜像适配层。

### 本次完成

#### 1. 撤销 commit 2b5dce4 W 部分

`firmware/{octoaxes,octoaxesplus}/filterwheel.cpp`：4 处方向计算从 "跟 `_config.homing_direct`" 改回 "硬编码 + 方向"（与旧 Squid W 段一致）。`continue leaving` 分支恢复原始 "按 `homingSwitch == RGHT_SW` 二选一" 逻辑。

#### 2. 加 firmware 层硬件方向反相 `invert_direction`

`firmware/{octoaxes,octoaxesplus}/axis.h`：AxisConfig 加字段：
```cpp
bool invert_direction;   // 硬件方向反相：true 时所有 MOVE/HOMING 命令在 firmware 层反 payload
```

`firmware/{octoaxes,octoaxesplus}/axis.cpp` 入口反相：
- `moveToPositionMicrosteps(target)`: target *= -1 if invert
- `moveRelativeMicrosteps(delta)`: delta *= -1 if invert
- `getCurrentPositionMicrosteps()` / `getEncoderPositionMicrosteps()`: chip XACTUAL/ENC_POS 返回值反相

`firmware/{octoaxes,octoaxesplus}/filterwheel.cpp`：所有 `motor_setVelocityInternal(speed)` 前判断 invert 反 speed。

#### 3. 配置生效

`firmware/octoaxes/config.h`:
- **W_AXIS .invert_direction = true** （本硬件镜像装配）
- **EXPAND4_AXIS .invert_direction = true** （E4 同 W 同类型 filter wheel）
- X/Y/Z/E1/E3 = false（默认）

`firmware/octoaxesplus/config.h`:
- 所有 axis = false（squid++ 新硬件待实测，如需反相用户自改）

#### 4. 恢复 software 与旧 Squid 完全一致

`software/common/define.py`: `SQUID_FILTERWHEEL_OFFSET = -0.011 → +0.008`（**与旧 Squid 完全一致**）
`software/octoaxes/constants.py` W: `movement_sign = 1`（与旧 Squid 一致）

### 实测验证（用户确认两组行为一致 ✓）

| 用例 | 行为 |
|---|---|
| 旧 Squid software + 旧 Squid firmware（参考组） | 用户原有视觉行为 |
| 旧 Squid software + **octoaxes firmware（修复后）** | ✓ **完全一致** |
| octoaxes software + octoaxes firmware（修复后） | ✓ 也一致（同样的 firmware，同样的协议命令） |

**达成"octoaxes firmware 完整替代旧 Squid firmware"的目标**：
- 协议层：W 行为字节级一致（同 cmd 同 payload 同响应）
- 物理层：chip 反相后镜像硬件也能到正确位置

### 关键决策记录

1. **撤销 commit 2b5dce4 而不是保留方向修复 + 补丁**：commit 2b5dce4 假设 filterwheel 应与 stepaxis 一致，但这与旧 Squid W 段特定行为冲突。要替代旧 Squid 必须复刻 W 段反向特性
2. **硬件反相在 firmware 层，不在 software**：让 software 协议层与旧 Squid 完全一致，所有"硬件特异"集中在 firmware AxisConfig
3. **invert_direction 默认 false**：与旧 Squid 行为兼容，硬件镜像装配机器单独打开
4. **octoaxesplus 默认 false**：squid++ 新硬件未实测，保守

### 设计权衡

| | 旧方案（SQUID_FILTERWHEEL_OFFSET=-0.011） | 新方案（firmware invert_direction） |
|---|---|---|
| 改动层 | software/common/define.py | firmware/{octoaxes,octoaxesplus}/{axis,filterwheel,config}.h/cpp |
| 替代旧 Squid firmware | ✗（旧 Squid software 用 +0.008 仍会到错位置） | ✓ |
| 硬件配置切换 | 改 software 常量（重启 GUI 生效）| 改 firmware config.h + 重烧 |
| 通用性 | W 专用 | 任意轴可标记反相 |

新方案让 firmware 成为"硬件抽象层"，更符合架构设计。

### 下次

1. 同步改动到 octoaxesplus 实际硬件验证（W1/W2 实测，看是否也需要 invert）
2. X/Y/Z 回归测试（理论 invert=false 不影响，axis.cpp 入口加判断后确认无回归）
3. 如果其他硬件需要 X/Y/Z 方向反相，invert_direction 字段已就位可直接用

---

## 上次会话

**日期**: 2026-05-21 续（晚间）
**分支**: develop
**位置**: 量纲对齐 1/64 cherry-pick 到 develop + 复现旧 Squid sRampInit 行为 + 禁用 jerk-start (ASTART=0)

### 接续早上的工作

早上 cherry-pick 了 filterwheel.cpp 方向 bug 修复 (commit 2b5dce4)。下午用户实测 GUI W homing 不符合预期：offset 移动转盘没看到明显动作。

### 本次完成

#### 1. W 量纲对齐 1/64 完整 cherry-pick

把 filtewheel 分支的量纲改动拿到 develop：

| 文件 | 改动 |
|---|---|
| `firmware/{octoaxes,octoaxesplus}/config.h` | `SCREW_PITCH_FILTERWHEEL_MM` 100→1, `MICROSTEPPING_FILTERWHEEL` 8→64 |
| `software/octoaxes/constants.py` W | actuator pitch 100.0→1.0, microstepping 8→64 |
| `software/octoaxesplus/constants.py` W1/W2 | 同上 |
| `software/common/define.py` | `SQUID_FILTERWHEEL_OFFSET` `0.008*100` → `0.008` |
| `software/{octoaxes,octoaxesplus}/constants.py` | `FILTERWHEEL_DISTANCE` `0.125*100` → `0.125` |

GUI 算 offset 微步：12 → **102 微步**（与旧 Squid 完全一致）。

#### 2. 深度对比旧 Squid firmware sRampInit，找到 ASTART 是真正根因

实测 W offset 102 微步后 chip XACTUAL 过冲到 **1884 微步 = 50.12°**（接近一整个孔位），最终回到 ~102。视觉看到大幅震荡。

审查 `/home/hds/github.com/veerwang/lihongquan/Squid/firmware/controller/`:
- `TMC4361A_TMC2660_Utils.cpp:985` `tmc4361A_sRampInit`：**`rstBits(TMC4361A_GENERAL_CONF, USE_ASTART_AND_VSTART_MASK)`** — 永远禁用 jerk-start
- `init.cpp:167-168`: `rampParam[ASTART_IDX] = 0; rampParam[DFINAL_IDX] = 0;` — 不用 ASTART
- 配置 `MAX_VELOCITY_W_mm = 3.19 mm/s, MAX_ACCELERATION_W_mm = 300 mm/s²`

对比 octoaxes W_AXIS:
- `.astartMM = 180 * SCREW_PITCH_FILTERWHEEL_MM = 180 mm/s³`（**启用 jerk-start**）
- `MAX_VELOCITY_FILTERWHEEL_mm = 4.2, MAX_ACCELERATION_FILTERWHEEL_mm = 400`

**ASTART 是过冲根因**。`motor_moveToMicrosteps` 看到 `astart > 0` 启用 `USE_ASTART_AND_VSTART_MASK`，chip 用 ASTART 作为 ramp 起始加速度，jerk-bounded ramp 在短距离过冲。

#### 3. 禁用 W 的 ASTART（与旧 Squid 完全对齐）

把 W/W1/W2/E4 的 `.astartMM = 180 * SCREW_PITCH_FILTERWHEEL_MM` 改为 `.astartMM = 0`：
- `firmware/octoaxes/config.h` (W_AXIS line 397, EXPAND4_AXIS line 511)
- `firmware/octoaxesplus/config.h` (W_AXIS line 490, EXPAND4_AXIS/W1/W2 line 623 + W_AXIS=W1_AXIS 别名)

`motor_moveToMicrosteps` 看到 `motorParams.astart == 0` 自动 `rstBits(USE_ASTART_AND_VSTART_MASK)`，chip 进入与旧 Squid sRampInit 完全一致的状态。

两 firmware 编译 SUCCESS。**未烧验证**，等用户烧后实测。

### 性能影响讨论（用户关注 filter wheel 效率）

**禁用 ASTART 的物理后果**：
- chip ramp 从 0 加速度开始 jerk-bounded ramp（之前从 ASTART=180 起步）
- **短距离 (offset 102 微步)**: 不再过冲 ✓
- **长距离 (切槽位 1600 微步)**: ramp 启动更平缓，整体加速时间可能略增（待实测确认）

**如果切槽位变慢明显，方案**：
- 距离自适应 ASTART：`motor_moveToMicrosteps` 入口判断 delta，短距离禁用 ASTART（不过冲），长距离启用 ASTART（保留 jerk-start 加速优势）
- ~10 行 firmware 改动，只对 motorParams.astart > 0 的轴生效
- **暂不实施**，先实测 astart=0 切槽位耗时再决定

### 决策记录

1. **暂用与旧 Squid 完全一致的配置（astart=0）**：风险最低，行为可预测
2. **不实施距离自适应 ASTART**：等实测确认切槽位真的太慢再加复杂度
3. **filter wheel 效率是关键关注点**：本次记录是为下次评估准备 — 如果切槽位耗时不可接受，参考此处方案 B 实施

### 提交记录

- `2b5dce4` fix(firmware): filterwheel.cpp homing 方向跟随 _config.homing_direct（早上）
- `2aa3e94` docs: 记录 2026-05-21 cherry-pick 决策（早上）
- 本次改动（量纲对齐 1/64 + ASTART=0）尚**未 commit**，等实测后提交

### 下次

1. **烧 develop firmware** (W=1mm pitch/64 微步/ASTART=0) 验证 GUI W homing
2. **实测切槽位耗时**（test_w_homing_sequence.py，offset 改成 1600 微步）
3. 若切槽位 > 300ms 可接受，commit 并收尾
4. 若切槽位太慢，加距离自适应 ASTART (方案 B)

### 续：编码器辅助定位 + 1 号孔位精确匹配

烧 develop firmware（含量纲 + ASTART=0）后，GUI 测试 W homing+offset 位置目测不对。深入调试：

#### 1. 开启 W 编码器

`software/octoaxes/constants.py` W: `has_encoder = True`。GUI 启动时 `_configure_encoders` 下发 CONFIGURE_STAGE_PID(W) 启用 chip ABN 编码器，位置上报走 ENC_POS（chip 真实位置）。

#### 2. 反复手动定位实测 1 号孔位

用 disable axis + 手动转动 + read encoder 流程多次测量：

| 次数 | W ENC_POS | 物理 | 状态 |
|---|---|---|---|
| 1 | -157 µstep | -4.42° | "1 号孔位"（用户首次） |
| 2 | +83 µstep | +2.33° | 用户误转到相邻孔位 |
| 3 | **-141 µstep** | **-3.97°** | 用户确认这次是 1 号孔位 |

第 2 次的 +2.33° 与旧 Squid 设计 (+2.87°) 同向，差距小，曾误判旧 Squid 设计正确；后续手动反复测量确认 1 号孔位实际在 **home 标志的 - 方向 3.97°**（与旧 Squid 设计反向）。

#### 3. 最终 offset 配置

`software/common/define.py`:
```python
SQUID_FILTERWHEEL_OFFSET = -0.011  # 实测匹配硬件 1 号孔位 (chip -141 µstep)
```

注意：
- **数值与旧 Squid 反号**（-0.011 vs +0.008）— 你的硬件 home 标志位置与旧 Squid 设计假设镜像
- **movement_sign 保留 1**（与旧 Squid 一致）— movement_sign 只影响 home search 方向，不影响 offset 物理方向；chip 归零后无论怎么 search 都停在 home 标志位
- GUI 算: `int(-0.011 × 1000) = -11 → -11/0.0625 ≈ -141 µstep` ✓

#### 4. 实测验证

| 项 | 期望 | 实际 |
|---|---|---|
| Homing 完成 | W=0 | **W=0** ✓ |
| Homing 耗时 | <1s | **768 ms** ✓ |
| Offset 完成位置 | -141 µstep | **-138 µstep** (误差 3 µstep = 0.08°) ✓ |
| Offset peak | 接近 target | **-144** (微小过冲 4 µstep) ✓ |
| Offset 耗时 | <200 ms | **111 ms** ✓ |

视觉上 chip 停在 1 号孔位中心，与手动定位完美吻合。

### 完整配置清单（develop 当前生效）

| 配置 | 值 | 来源 |
|---|---|---|
| W chip 量纲 | 1mm pitch / 64 微步 | `firmware/{octoaxes,octoaxesplus}/config.h` |
| W 上位机量纲 | actuator_pitch=1.0 / microstepping=64 | `software/octoaxes/constants.py` W + `software/octoaxesplus/constants.py` W1/W2 |
| W ASTART | 0 | `firmware/{octoaxes,octoaxesplus}/config.h` W_AXIS/EXPAND4_AXIS |
| W movement_sign | 1 | `software/octoaxes/constants.py` |
| W has_encoder | True | `software/octoaxes/constants.py` |
| SQUID_FILTERWHEEL_OFFSET | **-0.011** mm | `software/common/define.py`（**硬件特有**） |
| FILTERWHEEL_DISTANCE | 0.125 mm | `software/{octoaxes,octoaxesplus}/constants.py` |
| filterwheel.cpp 方向 bug 修复 | 已生效 | commit 2b5dce4 |

### 关键决策记录

1. **量纲对齐旧 Squid (1/64)**：让 GUI 算的微步数与旧 Squid 一致（102 微步/2.87°），chip 4361A 内部 ramp 行为与旧 Squid 等价
2. **ASTART=0**：与旧 Squid `sRampInit::rstBits(USE_ASTART_AND_VSTART)` 一致，禁用 jerk-start，消除短距离过冲（之前 50° 过冲降到 < 4°）
3. **SQUID_FILTERWHEEL_OFFSET = -0.011 是硬件实测特有值**：与旧 Squid 设计反号，因为本硬件 home 标志位与标准 Squid 镜像装配。**换硬件需要重新实测**
4. **不实施距离自适应 ASTART**：ASTART=0 已经完美，切槽位耗时也 OK（不需要 jerk-start 加速）
5. **filterwheel.cpp 方向 bug 修复 (2b5dce4) 是基础**：让 chip 朝上位机请求的方向 search，所有后续配置才能生效

### 反面教材

1. **第二次手动定位误转到 +2.33°**：与旧 Squid 设计 +2.87° 接近，曾误判"旧 Squid 配置就是对的"。撤销了 movement_sign=-1 和 offset 反号改动，结果还是不对。**滤光转盘有 8 个孔，手动定位时要确保转到正确的孔**（用户重新转后是 -3.97°，与第一次 -4.42° 接近）
2. **disable axis 后忘记 re-enable 跑测试**：第一次跑 GUI homing 测试时 W 没动，chip 耗时 5s 还 W=83 不变。原因是脚本前调过 disable 让用户手动转，没 re-enable 就跑 homing，chip 收命令但 motor 线圈无电流，电机不动。修复：跑 homing 前必须 enable axis
3. **手动定位精度**：±10-20 µstep（0.3-0.6°）是合理误差范围，单次实测 -141 与 -157 的差 16 µstep = 0.45° 视觉看不出，但取多次平均更稳

### 下次（如换硬件需重做）

每台新硬件需重新做"手动定位 + 实测 SQUID_FILTERWHEEL_OFFSET"流程，因为 home 标志位与孔位的物理偏移取决于装配。可以考虑：
- 把 `SQUID_FILTERWHEEL_OFFSET` 移到 profile-specific 配置（每机一份）
- 或在 GUI 加"校准"按钮，让用户运行时调整后写入配置文件

---

## 上次会话

**日期**: 2026-05-21
**分支**: develop（filtewheel 分支保留完整 W 修复链）
**位置**: 从 filtewheel 分支精准 cherry-pick "filterwheel.cpp homing 方向 bug 修复" 到 develop

### 背景

5-20/5-21 在 `filtewheel` 分支做了一长串 W 轴修复（量纲对齐 1/64 + 方向修复 + chip 时序根因 + 距离自适应 VMAX + 编码器启用 + 调试打印 + 测试脚本，共 2 个 commit `ba71a46` 和 `fd8d032`，全部已 GUI 实测通过）。

今天评估后决定：**只把"方向 bug 修复"单独 cherry-pick 到 develop**，其余修复保留在 filtewheel 分支（量纲改动影响范围大、有不可回退风险；距离自适应是 motor 层 API 改动，需要更多回归才能进主线）。

### 本次完成

#### Commit: `2b5dce4 fix(firmware): filterwheel.cpp homing 方向跟随 _config.homing_direct`

`firmware/{octoaxes,octoaxesplus}/filterwheel.cpp`：4 处速度计算从硬编码 `+vel` 改为 `_config.homing_direct * vel`，与 stepaxis.cpp 对齐。

```diff
- int32_t speedInternal = motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
+ int32_t speedInternal = _config.homing_direct * motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
```

涉及的位置：
1. `performHomingSequence` 中 fast search（不在感应区时启动 search）
2. `performLeavingHome` 中 slow approach（离开感应区后慢逼近）
3. `performLeavingHome` 中 fast search restart（离开感应区后再次 search）
4. `performLeavingHome` 中 continue leaving（仍在感应区，移出方向 = -search 方向 = `-1 * _config.homing_direct * vel`）

`tmc/` 目录是符号链接，两 firmware 共享 motor 层；filterwheel.cpp 是独立文件，通过 `cp` 同步。

**编译通过**：octoaxes + octoaxesplus 两 env 均 SUCCESS，**未烧验证**。

### 为什么这次才发现这个 bug

历次 review 没检测出来的原因（事后分析）：

1. **filterwheel.cpp 和 stepaxis.cpp 是两个独立文件**，review 不会强制对比 `_config.homing_direct` 是否在两边都用
2. **行为表面正常**：W 最终能 home 完成（最坏多绕半圈），用户视觉无感
3. **协议层正确**：上位机发 `HOME_NEGATIVE` → firmware `handleHomeOrZero` 写 `_config.homing_direct=-1`，但 filterwheel.cpp 不读，是"firmware 接受命令后丢弃" — grep handleHomeOrZero 看不到问题
4. **W 缺方向回归测试**：历史测试只测"能 homing 完成"，没测"chip 朝上位机请求的方向"
5. **历次 review 主线在 X/Y/Z 高频路径**（VSTOP / 边界 margin / 方向感知闸门等），filterwheel 长期视为"小众功能 能 work 即可"
6. **改 1/64 量纲让 W 测试更敏感** — chip 启动位置碰巧"刚跨过 home 标志一点点"时，朝 + 方向需要绕近 1 圈，~4 秒延迟暴露出来

### 不影响 X/Y/Z

- X/Y/Z 实例化为 `StepAxis` 类（octoaxes.ino:100-102）
- W 实例化为 `FilterWheel`（line 103）
- 本次只改 `filterwheel.cpp`，stepaxis.cpp 不动，X/Y/Z 行为完全不变

### filtewheel 分支保留的工作（未进 develop）

| 类别 | 包含 | 状态 |
|---|---|---|
| W 量纲对齐旧 Squid 1/64 | config.h + constants.py × 2 profile + define.py | 完整工作 + GUI 实测通过 |
| chip 时序根因修复 | `motor_setVelocityInternal` VMAX/RAMPMODE 写入顺序交换 | 全 motor 层 API，影响所有轴 |
| 距离自适应 VMAX | `motor_moveToMicrosteps` 短距离 cap VMAX = vmax/8 | 影响所有轴，需要 X/Y/Z 回归 |
| W 编码器启用 | has_encoder=True | profile 改动 |
| 调试打印 | `[WHOMING]` / `[WMOVE]` always-on SerialUSB | 应清理或保留为编译开关 |
| 测试脚本 | test_w_homing_with_encoder.py / test_w_move_no_encoder.py | 新建 |

要进 develop 需要逐项评估 + X/Y/Z 回归测试。

### 下次

1. **烧 develop firmware 验证方向修复** — 跑 W homing，看是否朝 - 方向
2. **决定下一步从 filtewheel 拉哪些** — 量纲对齐？chip 时序？距离自适应？
3. **filtewheel 分支命名** — 拼写笔误（少 r），择期重命名为 filterwheel

---

## 上次会话

**日期**: 2026-05-19
**分支**: develop
**位置**: 90→98s 采集差距深度分析 + #2.2 修复落地 + 翻转第一轮 agent 误诊 + **二轮 review 后用户实测推翻"不在 firmware"假设**

### 2026-05-19 续（用户同硬件 A/B 实测后二轮 review）

用户反馈：**同硬件 A/B（同 stage / 相机 / USB / 物镜，只换 firmware）实测仍差 ~8s**。
推翻第一轮"8s 不在 firmware"的判断，firmware 是真主凶。

**二轮深读后找到的新 overhead**（之前漏掉的）：
- `moveToPositionMicrosteps` no-op check `motor_getPositionMicrosteps` (axis.cpp:553) — +100µs/move
- `clampTargetByDirection` 自己又读 1 次 SPI (axis.cpp:817) — +100µs/move
- `Axis::completeMovement` 两个 `[[maybe_unused]]` SPI（DEBUG 编译掉但 SPI 仍执行）(axis.cpp:453-454) — +200µs/move
- `send_position_update` 4 个 `findAxisByName` × String 构造 + equals (serial.cpp:203-206) — +40µs/tick × 10000 = 400ms
- `Axis::update` STATE_MOVING `checkLimitPosition` 每 iter 1 SPI EVENTS (axis.cpp:272) — 不延长 motion 但占 SPI bus

**可量化累计 ~1.0-1.1s**。仍缺 ~7s 静态 review 找不到。

**6 个仍未排除的可能藏区**（看不见，需打点）：
1. octoaxes `setMotionParameters` chip-level VMAX/AMAX/BOW 计算是否与旧 Squid 字面一致
2. `Axis::configureDriver` 启动时 SPI 寄存器写入差异
3. 某 cmd handler 内部 lib/framework 间接阻塞
4. STATE_MOVING SPI bus 抢占行为差异
5. **VMAX 同值重写是否真无 chip ramp 副作用（没查数据手册，可能错）**
6. ISR 干扰（trigger / PWM / SPI）

**结论**：静态 code review 已达极限，必须 firmware 打点测量。

### 本次完成

1. **深读代码挑战第一轮 agent 报告的 4 个修复点**：
   - #1.1 跳空闲轴：`Axis::update()` STATE_IDLE 已是空 break (axis.cpp:225-227)，节省 ~40ns/iter → drop
   - #1.2 send_pos_update 上移：`elapsedMicros` 不被 FastLED 阻塞影响 → drop
   - #2.1 删 line 715 VMAX 写：VMAX 写同一值不触发 chip ramp 重启（待验证），且改 motor_stop 语义有传染风险，节省 ~50ms → drop（已加入"待打点验证"清单）
   - #2.2 axis.cpp 重复 STATUS 读合并：唯一确认的真实节省 ~10-20ms → **DONE**

2. **#2.2 实施完毕**（编译通过 octoaxes 80412B / octoaxesplus 84188B，**未烧录**）：
   - `MotorControl.h:219` `void motor_moveToMicrosteps` → `bool motor_moveToMicrosteps`，返回 vstopWasActive
   - `MotorControl.cpp:665+` 末尾添加 `return vstopWasActive`
   - `octoaxes/axis.cpp` 两处 (line 560-562 + 623-625) 移除 `motor_readStatus` 改用返回值
   - `octoaxesplus/axis.cpp` 同步（两端 byte-identical 约束）
   - 其他 caller 丢弃返回值，C++ 默认行为零修改

3. **全面对比两套 firmware 主循环 + 命令路径**（覆盖 A-J 10 个角度），翻转 agent 假设：
   - **旧 Squid main loop 26 个 call/iter** vs **octoaxes 7 个/iter** — octoaxes 反而更轻
   - SPI 时钟 500 kHz 两端一致
   - FastLED APA102 BGR 1 MHz 两端一致
   - DEBUG_PRINT 在 production (NDEBUG) 编译掉
   - `motor_moveToMicrosteps` 多 2 SPI ≈ 40µs/move × 1000 = 40ms（占 8s 的 0.5%）
   - **静态可解释的 octoaxes overhead < 100ms，远不到 8s**

4. **结论修正**：**8s 大概率不在 firmware 层**，可能在：相机型号 / USB 链路 / 机械 / Laser AF Python 侧 / 物镜校准。

5. **完整报告**：`documents/baselines/acquisition_8s_deep_analysis_20260519.md`（取代 5-18 版）

### 下次（用户验收）

1. ~~跑同硬件 A/B~~ — **已完成**，同硬件差 ~8s，firmware 是真主凶
2. **写打点 #1（单 cmd 总耗时）firmware 代码**，写完不烧录等用户硬件空闲
   - 位置：`checkForCommand` 入口 + `send_position_update` 发完
   - 输出：`S:CMD_TIMING <cmd_id> <us>` ASCII 调试输出
   - 用途：区分主凶在 atomic cmd / move cmd / homing cmd
3. 用户空闲时烧带打点的 firmware → 跑 1 well 采集 → 串口 dump → 分析
4. **#2.2 烧录验证**（顺带带回归测试）— 跑现有 X/Y/Z/W 运动测试 + acquisition 确认无回归
5. 可选清理（独立于 8s 主凶，但消除噪声）：
   - #3 `completeMovement` 加 `#ifdef ENABLE_DEBUG` 包裹 SPI（节省 ~200ms）
   - #4 `send_position_update` 缓存 axis 指针（节省 ~400ms）
   - #5 `Axis::update` STATE_MOVING `checkLimitPosition` 加 10ms throttle

### 关键决策记录

1. **#2.2 单独干**：唯一确认的真实节省点，安全 ROI，~10-20ms
2. **#1.1 #1.2 drop 不变**：代码事实推翻
3. **#2.1 重新评估**：VMAX 同值写是否触发 chip ramp 重启，**未查 TMC4361A 数据手册**，可能是 1-5s 隐藏开销，需打点验证（被列入待验证清单 #5）
4. **不写微基准脚本**：用户选择直接 A/B firmware 而非单独测 cmd 延迟，更干净
5. **不烧录**：用户明确告知硬件在用，所有改动只编译验证
6. **二轮 review 修正**：之前"8s 不在 firmware"基于估算，被用户实测推翻。教训：估算永远是估算，没实测前不要下定论

### 备注

- 本轮节奏：用户给场景 → 派 agent 调查（agent 报错估算）→ 用户选只做 #2.2 → 深读挑战 agent → 翻转结论 → 用户实测同硬件 A/B 再翻转 → 二轮深读找到更多 overhead 但仍缺 ~7s → 决定打点
- 反面教材 #1：第一轮 agent 用"想当然"逻辑（main loop 加重 → ACK jitter）支撑数字估算，**没去对比代码本身**就编故事
- 反面教材 #2：我自己第一轮深读后下了"8s 不在 firmware"结论，过于自信。**没有实测就别下定论**。同硬件 A/B 直接推翻
- 反面教材 #3：估算 chip-level 行为（VMAX 同值写不触发 ramp 重启）没查数据手册就当事实，可能错
- ttl_test agent 流式超时：单 agent 50s 内必须返回，过长任务要分段
- 本轮全程未烧录、未触碰硬件

---

## 上次会话

**日期**: 2026-05-18
**分支**: develop
**位置**: joystick ↔ firmware 10 字节协议加 CRC-8-CCITT 校验 + 目录分离 + 落地文档

### 本次完成（3 个 commits）

```
c5e3867 docs(joystick): 兼容性矩阵补脚注 — 旧 Squid fw 源码已核实不读 byte[9]
8824204 docs(joystick): 10 字节协议落地文档 + byte[9] 兼容性闸门约定
fa625d1 feat(joystick): CRC-8-CCITT 协议 + 目录分离到 {octoaxes,octoaxesplus}
```

### 关键设计：byte[9] 兼容性闸门

老 joystick 历史上 `packet[9] = 0`（注释 `// CRC to be added` 留位等实现）。
本次实施在 byte[9] 引入**双语义**：

- `byte[9] == 0` → legacy 包（老 joystick，跳过 CRC 校验，照常解析）
- `byte[9] != 0` → 新 joystick，byte[9] 即 CRC-8-CCITT(buffer[0..8])，校验失败丢包

新 joystick 算出 CRC=0x00 时强制改为 0x01（约 1/256 概率），避免与 legacy
sentinel 撞车。

**兼容性矩阵 4 种组合全通**（已核实，含外部代码引用）：
- 老 joy ↔ 老 fw（旧 Squid / fa625d1 前 octoaxes）：完全不读 byte[9]，不变
- 老 joy → 新 fw：legacy 直通；`legacy_count++`
- 新 joy → 老 fw：源码已核实 — 旧 Squid `functions.cpp:509-546` 不读 byte[9]
- 新 joy → 新 fw：CRC 校验，失败丢包

### 目录分离

```
firmware/joystick/                          (旧位置)
  → firmware/joystick/octoaxes/             (git mv 保留历史，rename 76-100%)
  + firmware/joystick/octoaxesplus/         (新建，byte-identical 副本)
                                              TM1650.h → ../octoaxes/TM1650.h symlink
```

与 firmware/{octoaxes,octoaxesplus}/ 结构对称，方便后续按 profile 独立演进。

### 代码改动

**joystick 侧** (`firmware/joystick/{octoaxes,octoaxesplus}/control_panel_teensyLC.ino`)：
- 内嵌 CRC_TABLE（与 `firmware/octoaxes/serial.cpp:23` 同款 poly 0x07 / init 0x00）
- `crc8_ccitt()` helper
- 主循环替换 `packet[9] = 0` → CRC 计算 + 0→1 映射

**firmware 侧** (`firmware/{octoaxes,octoaxesplus}/joystick.{cpp,h}`)：
- `onJoystickPacketReceived` 加 CRC 兼容性闸门
- 3 个 uint32 计数器：`legacy_count` / `crc_ok` / `crc_fail`
- 新增 `joystick_print_stats()` 公开 API，复用 `serialProtocol.crc8ccitt`
- byte-identical 同步两 profile

**调试命令** (`firmware/{octoaxes,octoaxesplus}/serial.cpp`)：
- 新增 `S:JOYSTICK_STATS` 分发到 `joystick_print_stats()`
- 输出格式：`JOYSTICK_STATS legacy=N crc_ok=N crc_fail=N`
- 现场诊断 5 种指纹场景（见 documents/joystick_protocol.md §6）

### 落地文档

新建 `documents/joystick_protocol.md`（218 行 / 9 章节）：
1. 物理层（Serial1/Serial5 @ 115200，PacketSerial/COBS）
2. 10 字节字段表
3. byte[9] 双语义详解
4. CRC-8-CCITT 算法定义
5. 兼容性矩阵 + 旧 Squid 源码引用脚注
6. S:JOYSTICK_STATS 诊断速查
7. 已知限制 + 升级路径约束
8. 文件清单
9. 变更历史

### 编译验证

四工程全部 SUCCESS：

| 工程 | FLASH code | 增量 |
|---|---|---|
| firmware/octoaxes | 80732 B | +192 B（80540 baseline） |
| firmware/octoaxesplus | 82908 B | — |
| firmware/joystick/octoaxes/ (teensyLC) | 26620 B | 41.9% / 63488 |
| firmware/joystick/octoaxesplus/ (teensyLC) | 26620 B | — |

### 关键决策记录

1. **兼容性闸门用 byte[9] sentinel 而非长度变更**：老 joystick `packet[9]=0`
   已是事实标识，复用比扩 11 字节包代价低（扩包会让新 joy 无法回退老 fw）
2. **CRC=0 映射到 1**：损失 1/256 错误检出率以闭合 sentinel 冲突，可接受
3. **复用 firmware ↔ 上位机的 CRC-8-CCITT**：减一份算法负担；CRC_TABLE 直接拷贝
4. **目录分离到 firmware/joystick/{octoaxes,octoaxesplus}/**：与主 firmware 目录
   结构对称；当前两 profile 包内容相同（byte-identical），但**预留未来按硬件
   profile 独立演进**的工程化路径
5. **TM1650.h 用相对符号链接共享**：与 octoaxes/tmc ↔ octoaxesplus/tmc symlink
   同款模式，避免代码重复

### 当前状态

- ✅ joystick 协议 CRC 实施完成，4 工程编译通过
- ✅ 协议落地文档（含外部源码引用脚注）
- ✅ **硬件烧录验证（2026-05-18 实测部分闭环）**：
  1. ⏳ 回归：新 fw + 老 joystick — 未测（未换回老 joystick）
  2. ✅ **正向：新 fw + 新 joystick** — 实测 `crc_ok=110 → 5820 / 3.5s`，`crc_fail=0`
  3. ✅ **反向：新 joystick + 老 fw（fa625d1 前 octoaxes）** — 摇杆/焦点/按钮正常
  4. ✅ **干扰：crc_fail≈0** — 3.5s 内 5820 包零失败

### 实测过程亮点 + 顺手发现的 DEBUG_PRINTLN bug

1. **检测 fa625d1 前后固件**：脚本首次跑发现 `S:JOYSTICK_STATS` 没回包但
   `S:VERSION` 有回包 → 烧的是 fa625d1 前固件。重烧 develop HEAD 后仍无回包。
2. **DEBUG_PRINTLN 空宏 bug 暴露**：根因是 `joystick_print_stats` 错误地走
   `sendDebugInfo` → `DEBUG_PRINTLN` 宏在生产 env (teensy41) 下展开为空，
   响应被静默丢弃。fix(a716980): 改用 `SerialUSB.println` 直发，对齐
   `S:HWINFO / S:VERSION` 同款 pattern。
3. **第三次重烧后**：`crc_ok=110` → 3.5s 后 `5820`（增量 5710，速率 ~1631 pkts/s），
   与 joystick 主循环 500 Hz × 中断驱动加速吻合；`crc_fail=0` 全程。
4. **顺带验证反向兼容**：上一轮"烧错版本"的 octoaxes 主固件（pre-fa625d1）
   配新 joystick 摇杆 / 焦点轮 / 按钮全正常 → 反向兼容性 §5 矩阵第 3 行的
   旧 Squid 源码事实（`functions.cpp:509-546` 不读 byte[9]）在硬件上得到
   二次确认。

### 4 commit 累加（本次会话总计 6 个 develop commits）

```
a716980 fix(joystick): joystick_print_stats 改走 SerialUSB.println + 配套查询脚本
912eaab Merge origin/develop: 2026-05-18 illumination/LED + joystick CRC 双线汇合
1217e88 docs: 2026-05-18 joystick CRC 协议会话收尾
c5e3867 docs(joystick): 兼容性矩阵补脚注 — 旧 Squid fw 源码已核实不读 byte[9]
8824204 docs(joystick): 10 字节协议落地文档 + byte[9] 兼容性闸门约定
fa625d1 feat(joystick): CRC-8-CCITT 协议 + 目录分离到 {octoaxes,octoaxesplus}
```

### 下次继续

- 等用户烧录后回报 4 项验证结果，若失败按 `S:JOYSTICK_STATS` 计数器现场诊断
- octoaxesplus 待办：W1 PCB CLK 飞线 / Z 轴 PyQt 验证 / bring-up 工具归宿
- octoaxes 主线：W 轴 60ms 优化 / TMC2240 StallGuard4 调优（stash@{0}）
- 协议层潜在扩展：TMC4361A Target Pipeline / 多轴并行 home / MOVETO_BATCH

### 备注

- 三段式 commit 历经"实现 → 文档 → 脚注核实"逐步加固，体现"先落地后写文档"的反例修正：协议设计这类约定就该一开始就同步成文，避免依赖 commit 消息埋藏
- byte[9] sentinel 这种"已有字节复用新语义"模式很省，但**约定一旦发布永不可改**，文档第 7 章已显式登记不可改动项防止后人误踩
- 旧 Squid 源码引用脚注是回应"95% 推测→100% 核实"的升级，验证了"能查源码就别概率论"的取舍

---

## 上一会话

**日期**: 2026-05-18
**分支**: develop（已合并 origin/develop 把 maxpro 全部进展拉入主线）
**位置**: 融合 test 分支 ttl_test 到生产 illumination + LED 矩阵两条历史 bug 修复

### 本次完成（4 个 commits，全部已推 origin/develop）

```
fec1526 fix(firmware): LED 矩阵默认按字面 R/G/B 顺序，加 LED_MATRIX_SWAP_RG 兼容旧灯珠
75de141 fix(common GUI): LED 矩阵 Set/Clear 按钮按两步流程发命令，让明场光源能开关
020c5e2 feat(common GUI): IlluminationPanel 数据驱动化 + 融合 ttl_test_gui DAC 直控
8b5d400 feat(octoaxesplus firmware): 吸收 ttl_test DAC 鲁棒性补丁 + 4 个 ASCII 调试命令
```

### 1. develop 合并 origin/develop 现状梳理

`pull origin develop` 把 maxpro 25 个 commits（5-13 ~ 5-15）fast-forward 拉入 develop：
8 轴 AxisConfig 扩展、IC4 虚焊定位、software profile 拆分、协议 v2、W1/W2 firmware
handler、W2 端到端打通、profile 隔离工程化等全部进入主线。

master 仍领先 develop 12 个 commit（FilterWheel homing 两阶段 / handleMoveW 等旧 Squid
工作），暂未决定是否 cherry-pick。

### 2. 同步 test 分支 ttl_test bring-up 工具到生产（融合而非独立）

来源：`test` 分支 `3e41832` "保留测试LED光强的代码" 中 `firmware/ttl_test/` +
`software/ttl_test_gui.py`（独立 PIO 工程 + 独立 PyQt5 小窗，680 行）。

**决策**：不创建独立的 ttl_test_gui.py，把 ttl_test 验证过的全部能力融合进生产
illumination 标签页：

**firmware 端（commit 8b5d400，octoaxesplus 专属）**：
- `set_DAC8050x_default_gain()` 双写 + 2ms 间隔（规避 SPI 首事务被丢）
- `illumination_init()` 末尾加 `dac_zero_all()` 开机零位
- 新增 `read_DAC8050x_reg(addr)` 两帧 SPI 协议读
- 新增 `illumination_update()` 主循环钩子，做一次性 fallback 同步
- 4 个 ASCII 调试命令：`S:DAC_SET <ch> <raw>` / `S:DAC_GAIN <hex>` /
  `S:DAC_READ_ALL` / `S:DAC_READ <reg>`

**software 端（commit 020c5e2，profile-safe）**：
- `constants.py` 加 4 项 illumination 元数据（ILLUMINATION_PORTS /
  ILLUMINATION_DAC_CHANNELS / HAS_GAIN_SWITCH / HAS_DAC_READBACK）
- `IlluminationPanel` 数据驱动化：TTL 行按 ILLUMINATION_PORTS 动态生成（解决原
  5 路硬编码），新增 DAC 直控滑条区 / GAIN 切换按钮 / Read DAC 按钮
- 新 3 信号：dac_channel_cmd / dac_gain_cmd / dac_readback_cmd
- main_window 连接 + handle_received_data 识别 S:DAC_* 回包

协议拆分：TTL 按钮走二进制 cmd 37（生产路径，受 factor 缩放）；DAC 滑条走 ASCII
S:DAC_SET（bring-up 路径，绕过 factor 所见即所得）。

octoaxes profile 验证 12 项兼容性全通过（端口名/控件齐全/协议字节一致）。

### 3. LED 矩阵明场光源两条历史 bug 修复

#### Bug A：Set Matrix / Clear 按钮按下后矩阵不亮不熄（commit 75de141）

**根因**：2026-05-10 firmware 把 `set_illumination_led_matrix (cmd 13)` 改成"仅
缓存参数不点亮"（与旧 Squid functions.cpp:359-368 对齐，解决"开 D 通道时矩阵也亮"
双开 bug）。但 GUI 端没同步更新，"Set Matrix" 按钮只发 cmd 13 → 矩阵不亮；
"Clear" 按钮发 cmd 13 RGB=0,0,0 → 仅清缓存矩阵不熄。

**修复（common/gui，两 profile 同时受益）**：
- IlluminationPanel 新增 `led_matrix_off_cmd` 信号，Clear 按钮触发
- Set Matrix 工作流改为 cmd 13（缓存）+ cmd 10 TURN_ON_ILLUMINATION（真点亮）
- Clear 工作流改为 cmd 11 TURN_OFF_ILLUMINATION（真熄灭）

旧 Squid software 一直走 cmd 12 + cmd 10 / cmd 11 两步流程，颜色控制正常。

#### Bug B：R=255 显示绿色 / G=255 显示红色 / B=255 正常（commit fec1526）

**根因深挖**：旧 Squid + 当前 octoaxes/octoaxesplus 三方代码全部有
`led_set_*(scaled_g, scaled_r, scaled_b)` 强制把 R/G 实参对调。这是为补偿
"FastLED BGR 模板 vs 旧硬件 APA102 灯珠 BRG 字节排列"的不一致，双错位抵消后旧
Squid 颜色才正确。但新批次灯珠回归标准 BGR 排列，单错位变成 R/G 颠倒。

**修复（profile-safe，octoaxes + octoaxesplus 两端等价处理）**：
- 引入 `LED_RG_ARGS(r, g)` 宏，默认展开为 `(r, g)` 字面顺序
- 定义 `-D LED_MATRIX_SWAP_RG` 后展开为 `(g, r)` 对调，等价历史行为
- 所有 9 个 LED matrix pattern case（FULL/LEFT_HALF/RIGHT_HALF/LEFTB_RIGHTR/
  LOW_NA/LEFT_DOT/RIGHT_DOT/TOP_HALF/BOTTOM_HALF）统一用 `LED_RG_ARGS()` 包装
- LEFTB_RIGHTR 特殊 case 一并对齐宏控制

**新增 4 个 platformio env**（octoaxes + octoaxesplus 各 2 个）：
- `teensy41_legacyled` — 旧灯珠 + 联锁启用
- `teensy41_nointerlock_legacyled` — 旧灯珠 + 联锁禁用
- 默认 `teensy41` / `teensy41_nointerlock` 保持新行为（字面顺序），与当前硬件匹配

### 4. 实地验证（用户硬件）

- ✅ **octoaxes profile D1-D5 控制正常**：根因是之前烧的是默认 safe 版固件
  （`pio run -t upload` 不带 `-e`），换 `./download.sh nointerlock` 后立即可控
- ✅ **明场光源 Set/Clear 按钮工作正常**（commit 75de141 烧入后）
- ⏳ **LED 矩阵 R/G/B 颜色映射**：commit fec1526 烧入后待用户实测确认

### 关键决策记录

1. **ttl_test 融合而非独立**：把 bring-up 工具能力沉淀到生产 panel，避免维护
   两套 UI；DAC 直控滑条单独走 ASCII 协议保持"所见即所得"
2. **R/G 修复用编译宏 + 默认新行为**：避免老硬件机器烧新固件颜色反转，向后兼容
3. **不动 octoaxes 端 R/G 历史代码风险评估**：完成后两 profile 烧默认 env 颜色
   正确，烧 legacyled env 与旧 Squid byte-identical
4. **IlluminationPanel 改造按 profile-safe 原则**：用 constants 元数据动态渲染，
   octoaxes 5 路保留，octoaxesplus 自动扩 8 路 + DAC + GAIN + Read

### 2026-05-18 采集 90→98s 主凶定位（只调查，未动 firmware）

用户在硬件上跑 Wellplate Multipoint single-well 单 well（D6）+ Laser AF + BF/561/638 三通道 + 20x，发现 octoaxes 比旧 Squid 慢 8s（98 vs 90）。派 general-purpose agent 跨三个代码库做时序图 + handler diff，结论：

**FOV 估 ~200，单 FOV 多 40ms**

**主凶 #1（~6-7s / 75%）— ACK 心跳抖动 + BF FastLED.show 阻塞**
- octoaxes 主循环 `axisManager.updateAll()` + `joystick_update` + 二级 dispatch 比旧 Squid 重，每 iter ~50-150µs vs ~30-80µs
- `send_position_update` 10ms 心跳抖动到 ~10-13ms
- BF `turn_on_illumination → FastLED.show()` 128 LED APA102 串行 ~2ms + `noInterrupts` 阻塞，下一帧 ACK 推迟
- 每 FOV 16 atomic 命令 × ~2ms 抖动 ≈ 30ms

**主凶 #2（~1.5-2s / 25%）— `motor_moveToMicrosteps` SPI 翻倍**
- 旧 `tmc4361A_moveTo` 4 个 SPI op vs octoaxes 9-12 个 SPI（含 line 715 无条件 VMAX 写）
- VMAX 写可能触发 chip 内部 ramp 重启延迟 1-5ms
- Laser AF 单 FOV 5 次 move 累积放大

**修复方向（讨论稿，待硬件空闲实施）**：
1. `axisManager.updateAll` 加 `if (!isMoving) continue;` 跳过空闲轴
2. `send_position_update` 节流上移到 FastLED.show 之前
3. `motor_moveToMicrosteps` 去掉 line 715 无条件 VMAX 写
4. `axis.cpp:561` pre-status 改按需读

**完整报告**：`documents/baselines/acquisition_90vs98_analysis_20260518.md`（含 file:line 引用 / 数据信心度 / Python 侧不烧录的实测验证流程）

**下一步**：用户硬件在用，先不动 firmware。可选优先级：
- Option A：Python 侧启用 `wait_till_operation_is_completed` elapsed_ms 日志跑 10 FOV，按 cmd 类型聚合统计验证主凶 #1 / #2 占比
- Option B：把修复方向写成 diff 暂存，等硬件空闲再烧
- Option C：暂停，等用户硬件空闲再继续

---

### 2026-05-18 后续硬件确认（用户口头确认）

以下 5 项已实测通过 / 评估完毕，统一标记已确认：

1. ✅ **LED 矩阵 R/G/B 颜色映射**（commit fec1526 烧入后）
2. ✅ **DAC 直控滑条 + GAIN 切换 + Read 按钮**（commit 020c5e2 + 8b5d400 烧入后）
3. ✅ **W1 PCB CLK 走线**（飞线已修，W1 自动可用）
4. ✅ **Z 轴 PyQt 运动单独验证**（X/Y/W2 之外 Z 也通过）
5. ✅ **master 12 个 commit cherry-pick 评估**（结论已落）

### 下次继续（剩余）

1. bring-up 工具归宿决策（clk_test/hc154_test/pg_test/pin13_blink）
2. TMC2240 StallGuard4 调优（stash@{0} 待恢复，6 步流程已规划）
3. C 维度 HOME 复杂场景（AXES_XY 联合归位 + W1/W2 homing 实测）
4. **下一步主题：效率提升**（用户将给出具体方向）

### 备注

- 本次会话节奏：分析 → 协商方案 → 实施 → 编译验证 → 提交 → 实测排查（D1-D5
  烧录版本误判）→ 再迭代（Set/Clear / R/G 颜色）
- "为什么 octoaxes 不能控制 LED" 排查走了完整因果链：先排除 GUI 协议 →
  排除 firmware 联锁 → 用户确认 nointerlock 已烧 → 发现 cmd 13 仅缓存的
  历史行为变更 → 修复 → 再发现 R/G 颠倒 → 历史代码 hack 反向解读 → 编译宏修复
- 反面教材：第一次分析"旧 Squid 颜色对所以双错位抵消"基于未验证假设，被用户
  反问后修正为"旧 Squid 颜色对不对取决于硬件灯珠批次，需要实测"，避免了误诊
- ttl_test 融合给后续传感器 bring-up 工具入产树提供了模式：constants 元数据
  驱动 + GUI 按 flag 渲染，避免 profile-specific 独立窗口扩散

---

## 上一会话

**日期**: 2026-05-15（下半场）
**分支**: maxpro
**位置**: W2 端到端打通 + 协议 v2 实施 + GUI 滤光转盘 UI 修复 + profile 隔离工具化

### 本次完成（接续上半场之后又 9 个 commits，今日累计 14 个）

#### 下半场 9 个 commits（按时序）

```
3f3de06 fix: W1/W2 GUI 按钮（Homing/Previous/Next）不响应       ← 收官
ba77b20 fix: W1/W2 滤光转盘菜单 — 用 AXIS_CONFIG["type"] 动态判断
2c59071 feat(protocol v2): octoaxesplus 24→40 字节扩展位置广播
408a8e0 feat(octoaxesplus): W1/W2 MOVE/MOVETO firmware handler
21ac3ca test(software/common): verify_profiles.py 自动验证 profile 隔离
c905168 fix(software/common): define.py 补 W1/W2 命令映射 + CLAUDE.md common/ 原则
ff308ca docs(octoaxesplus): 协议 v2 设计文档
120972f fix(software/octoaxesplus): constants.py 去油 5 轴
7517f7a fix(software/common): AxisManager / axis_enabled_states 不硬编码 7 轴
```

#### 关键技术里程碑

1. **W2 端到端运动打通** ✅（用户实测确认）
   - firmware handleMoveW2 / handleMoveToW2 实施
   - firmware handleHomeOrZero 加 W→W1 兜底
   - GUI Previous/Next/Homing 按钮通

2. **24→40 字节位置广播协议 v2 实施**
   - cmd_id=0xFD 标识扩展包；按 firmware icID 索引 8 个 int32 位置
   - firmware sendExtendedResponse 新增；send_position_update 改用扩展包
   - serial_thread.py 双长度解析；handle_binary_response 用 AXIS_CONFIG[axis]["index"] 反查
   - octoaxes 24 字节包不动；octoaxesplus W1/W2 State/Position 现在能实时更新

3. **profile 隔离工程化**
   - software/common/ 修改原则写入 CLAUDE.md
   - verify_profiles.py 自动验证两 profile 隔离（AXIS_CONFIG/AxisManager/命令映射）
   - 修复 AxisManager 硬编码 7 轴；W1/W2 状态更新通；GUI 滤光转盘菜单按 type 判断

4. **W1 PCB 根因定位**
   - 万用表测：W1 连接器 pin 16 (CLK) = 0V（拔板 0V，插板 1V）— TMC4361_CLK 走线在主板上**未连接到 W1 槽位**
   - PCB 硬件 bug，firmware 不能修复，记录待后续 PCB 改版/飞线

### 关键决策记录（下半场）

1. **协议 v2 实施而非延后**：W1/W2 GUI 状态显示需要 40 字节包；用户选择"现在做"
2. **octoaxes 主线保持 24 字节**：兼容旧 Squid GUI
3. **40 字节包按 icID 索引**：协议与轴命名解耦，便于未来扩展
4. **W2 Homing 协议码复用 AXIS.W=5**：firmware 端 W→W1 兜底（findAxisByNameWithFallback）
5. **GUI 类型判断改用 AXIS_CONFIG[axis]["type"]**：filter_wheel / objective / step_motor 三种类型动态分发

### 当前状态

- ✅ X / Y / Z PyQt 运动验证通过（XY 昨日，Z 今日上半场未单独验证但等同）
- ✅ W2 全链路（firmware 协议、SPI 通信、GUI 菜单、GUI 按钮）打通，电机实际转动确认
- ❌ W1 PCB CLK 走线未接（硬件 bug，firmware/software 已就位，等飞线）
- ✅ 24/40 字节协议双轨在 GUI 自动识别（octoaxes 24 / octoaxesplus 40）
- ✅ verify_profiles.py 工具化 common/ 修改的 profile 隔离验证
- ⏳ 未跟踪：firmware/clk_test/ hc154_test/ pg_test/ pin13_blink/ 4 个 bring-up 工具（待 .gitignore 或归档决策）
- ⏳ 未跟踪：documents/squid++（双相机）原理图.pdf

### 下次继续

1. **W1 PCB CLK 飞线**（你已知 PCB 缺陷，需手工飞线后 W1 自动可用）
2. **Z 轴 PyQt 运动单独验证**（X/Y 已通，Z 待确认）
3. **C 维度 HOME 复杂场景**：handleHomeOrZero 的 AXES_XY 联合归位等扩展
4. **bring-up 工具归宿**：clk_test/hc154_test/pg_test/pin13_blink → `.gitignore` 还是归档到 `firmware/tests/`
5. **8 轴扩展**（Z2/F2/R/T）— 协议层、commandprocessor 已铺好，需要时实例化即可
6. **TMC2240 StallGuard4 调优** + **W 轴 60ms 优化**（octoaxes 主线遗留）

### 备注

- 今日单日 14 个 commits 接近极限节奏；workflow: 设计文档先行 → 实施 → 测试 → 提交 多次循环
- 协议 v2 实施验证了"先文档后代码"流程，避免实施时反复推翻
- profile 拆分后 common/ 隔离原则非常重要，违反一次（241399d → 后续 120972f / 7517f7a 等多次补救）
- bring-up 反面教材：今日中段出现"在错的 cwd 烧 octoaxes 当 octoaxesplus"事件（pio 上传卡 USB 异常后状态混乱），通过 firmware.hex 时间戳定位

---

## 上一会话

**日期**: 2026-05-15（上半场）
**分支**: maxpro
**位置**: octoaxesplus XYZW1W2 五轴方案落地 + software 目录拆分 + 文档体系完善

### 本次完成（4 个 commits）

#### 1. 轴定义现状梳理文档（commit 4916ba0）
`documents/octoaxesplus_axis_definitions.md` 249 行，按 6 层梳理 octoaxesplus
轴定义现状：HC154 通道分配 / tmc_ic_configs[] / AxisConfig 模板 / Axis 实例化
/ axesmrg 映射 / 上位机 constants。明确"跨层一致性约束 cheatsheet"作为后续
任何轴调整的工作清单。

#### 2. W1/W2 启用实施方案（commit 386455a）
`documents/octoaxesplus_xyzw1w2_plan.md`：把 octoaxesplus 从 XYZ 三轴调试模式
切到 XYZW1W2 五轴运行模式的方案文档。包含改动前后对比、6 层修改清单、影响
范围、测试计划。

#### 3. 启用 XYZW1W2 五轴（commit 2b17bd5）
按方案文档实施代码改动：
- `octoaxesplus/config.h`：Pins:: 加 W1/W2_AXIS_CS；HC154 枚举加 W1/W2 别名
  （与 Z2/T 同值，C++ 允许同值多名）；AxisConfigs:: 加 W1_AXIS=W_AXIS 等
- `octoaxesplus/tmc/hal/TMC_SPI.cpp`（符号链接共享）：tmc_ic_configs[3] 改
  HC154_AXIS_W1 (ch6)，[4] 改 HC154_AXIS_W2 (ch4)；octoaxes 主线 #else 分支
  不受影响
- `octoaxesplus/octoaxesplus.ino`：new FilterWheel(W1_AXIS_CS, 3, "W1") +
  W2，addAxis，注释更新"XYZW1W2 五轴"
- `octoaxesplus/axesmrg.cpp`：beginAll 加 "W1"/"W2" 分支
- `software/utils/constants.py`：删 Z1/F1/Z2/F2/R/T 6 条；Z 改成 octoaxes +
  octoaxesplus 共享；加 W1/W2 (index=3/4)

两工程编译均 SUCCESS（octoaxesplus 82460 bytes，octoaxes 80540 不变）。
Python `axes_for_model("octoaxesplus")` 返回 5 轴。

#### 4. software 拆分 octoaxes / octoaxesplus profile（commit 241399d）
firmware 已经按硬件型号拆为两个独立工程，软件端继续单一 `software/` +
`enabled_for` 标记反而复杂。**Plan C 落地**：

  software/
  ├── common/         共享代码（git mv 全部 software/{define,gui,hardware,utils,tests} 过来，
  │                   保留 33 个文件 100% rename 历史；utils/ 不含 constants.py）
  ├── octoaxes/       main.py + constants.py（取自 origin/develop）+ run.bat
  └── octoaxesplus/   main.py + constants.py（直接 rename from utils/constants.py，
                      保留 maxpro 完整 W1/W2 改动历史）+ run.bat

**import 兼容**：profile main.py 设置 sys.path 后，把 profile 的 constants
注册为 `sys.modules["utils.constants"]` 别名 → 共享代码的
`from utils.constants import AXIS_CONFIG` 零修改命中 profile 配置。

实测：模拟 sys.path + sys.modules 桥接，两 profile 都能加载预期 AXIS_CONFIG
（octoaxes 7 轴 / octoaxesplus 5 轴），utils.helpers / utils.debug 等共享
模块 import 正常。

### 关键决策记录

1. **W1/W2 沿用 W_AXIS 滤光转盘模板**：const struct copy 安全默认；硬件实测
   后再单独 init
2. **Z2/F2/R/T AxisConfig 保留作未来 8 轴扩展模板**：未被实例化但 const
   struct 编译无副作用
3. **firmware axisName "Z" 不改回 "Z1"**：与上位机命令一致；axesmrg.cpp
   保留 "Z"/"Z1" 双名兼容
4. **software 拆分用 Plan C（共享 common 包）**：99% 代码共享避免重复维护；
   Profile 切换零代码改动通过 sys.modules 桥接实现

### 当前状态

- ✅ XYZ 运动 PyQt 验证通过（2026-05-14）
- ✅ W1/W2 五轴方案 firmware 编译通过
- ✅ software 拆分完成 + import 链验证通过
- ⏳ Z 轴 PyQt 运动验证（下次硬件就位后）
- ⏳ W1/W2 滤光转盘硬件接入 + 转动校准
- ⏳ 实际启动两 profile 的 PyQt GUI（需要 .venv + 硬件就位）
- ⏳ POWER_GOOD bypass 待 PCB 飞线后撤销
- ⏳ bring-up 工具（clk_test/hc154_test/pg_test/pin13_blink）归宿决定

### 下次继续

1. Z 轴运动 PyQt 验证
2. W1/W2 滤光转盘硬件接入 + 实测
3. 两 profile PyQt GUI 实际启动验证（main.py 桥接代码效果）
4. POWER_GOOD bypass + bring-up 工具归宿处理
5. 后续考虑：8 轴模式恢复（F1/Z2/F2/R/T 扩展）/ Phase 3.2 GUI S:HWINFO
   profile 自动选择

---

## 上一会话

**日期**: 2026-05-14
**分支**: maxpro
**位置**: octoaxesplus 真机 bring-up — IC4 虚焊根因定位 + SPI 通信恢复 + XY 轴运动验证成功

### 本次完成

#### 1. Axis::begin 两个隐患修复（commit b7331fb）
- **隐患 A**：octoaxesplus 上 `_csPin` 字段实际是 HC154 通道号 (0-15) 而非 Teensy
  物理 pin 号，但 axis.cpp:52-53 仍按物理 pin 处理 → 误操作 Teensy 物理 pin
  8/9/10 = `CAMERA_TRIGGER_2` / `CAMERA_TRIGGER_1` / `ILLUMINATION_D8`，初始化
  时误触发两路相机和一路激光 TTL。修复：`#ifndef USE_HC154_CS` 保护这两行。
- **隐患 B**：`motor_initMotionController` 已返回 bool（SPI 通信失败时 return
  false），但 axis.cpp:75 直接调用不检查，导致 chip 没初始化但 begin 仍报成功，
  上层 beginAll 完全不知道。修复：检查返回值，失败时 DEBUG_PRINT + return false。
- 两 axis.cpp byte-identical 同步到 octoaxes + octoaxesplus。

#### 2. beginAll 失败不再卡死 firmware（commit 28e8eee）
b7331fb 引入的回归：Axis::begin 如实返回 false → beginAll 失败 →
initializeSystem 失败 → setup() 进 `while(1) delay(1000)` 卡死。bring-up
期间 SPI 必然不通，结果连 S:VERSION 都不响应。修复：把
`if (!beginAll()) return false` 改为日志警告 + 继续，serial 通信和调试命令
全部保持可用。同步两工程。

#### 3. SPI 不通根因诊断历程（典型反面教材，3 个错误假设 → 真凶 IC4 虚焊）

**症状**：所有 TMC4361A 的 SPI 读返回全 0x00，S:HWINFO 驱动型号 UNKNOWN。

**错误假设链**（逐个被实验否定）：

a. **PWM 4-bit 必需**（B1，2026-05-13 提出）— 起初观察 octoaxes 默认 8-bit
   + duty 128 在 16MHz 下"理论上违反 PJRC 限制"，改为 4-bit + duty 8。后续
   pin 37 实测 1.6V DC 一度认为佐证 B1 必需，最终发现 IC4 才是根因，**B1 改动
   既非问题也非解药**，已撤销恢复 octoaxes 基线。

b. **LTC2903 RST# 锁住 TMC4361A**（2026-05-14 提出）— 注意到原理图标错的
   IC6 LTC2903 +24V_XY net 浮空，怀疑其 RST# 同时拉低所有 TMC4361A NRSTN。
   用户核对原理图：**LTC2903 RST# 完全悬空，不连任何东西**，假设作废。

c. **squid++ 主板 SCK 走线 / Teensy pin 13 被短到 3V**（2026-05-14 提出）—
   连续测量 Teensy pin 13 强拉 LOW 仍读 3V，得出"主板硬件短路"结论。烧最简
   1Hz 闪烁固件确认时，用户发现 **"搞错了 Teensy 板子"** —— 之前测的电压不
   在被 USB 控制的那块上。换正确的 Teensy 后 LED 1Hz 正常闪烁，pin 13 翻转
   正常。**整段诊断作废**。

**真正根因（IC4 虚焊）** — 用户手动检查 PCB 焊点发现 IC4 的引脚虚焊。补焊后：
- `S:HWINFO` 三轴全部正确识别 `TMC4361A+TMC2240`（之前是 UNKNOWN）
- `S:SPITEST` STATUS 寄存器返回真实非零值（0x80000003 / 0x88000003）
- 修复 S:SPITEST 命令中 `VERSION_NO` 寄存器地址笔误（0x09 → 0x7F，原写法
  读到的是 `ENC_OUT_DATA`）后，VERSION_NO 返回 `0x00000002`（TMC4361A 硅版本号）
- 上位机 PyQt 测试 **XY 轴运动正常**

**经验教训**：
1. 多块同型号硬件并存时，bring-up 测量必须**反复确认"测的是哪一块"**
2. PCB 焊点先用 放大镜/X-ray 巡查再做软件诊断会节省大量时间
3. 不同假设要逐个用实验否定，不要并列多个假设一起改

#### 4. PWM 4-bit 改动撤销（工作树未提交）
`octoaxesplus.ino::initializeClock` 撤销 `analogWriteResolution(4) + duty 8`
回到 octoaxes 主线 `default 8-bit + duty 128`。注释保留撤销原因供后续参考。

#### 5. S:SPITEST 寄存器地址笔误修复（工作树未提交）
`serial.cpp:495` `tmc4361A_readRegister(i, 0x09)` 注释虽然写 "VERSION_NO"，
但实际 0x09 是 `ENC_OUT_DATA`。`VERSION_NO` 真实地址是 `0x7F`。修正后能正确
读出 chip 硅版本。

### 关键决策记录

1. **诊断历程优先保留**：尽管错误假设被推翻，仍详细记录在 SESSION.md 作为
   反面教材，避免下次遇到 octoaxesplus 系列 bring-up 重复相同弯路。
2. **POWER_GOOD bypass 保留不提交**：仍是 IC6 LTC2903 +24V_XY 标签 schematic
   bug 的临时绕过。待 PCB 飞线或改版后恢复轮询。
3. **PWM 8-bit 默认 + duty 128**：与 octoaxes 主线对齐，IC4 修好后验证可行。

### 当前状态

- ✅ X 轴 / Y 轴 上位机 PyQt 运动验证通过
- ⏳ Z 轴运动尚未验证（用户下一步）
- ⏳ 5 轴扩展（F1/Z2/F2/R/T）仍被 commit 1ce942a 注释，等需要时打开
- ⏳ S:SPITEST 寄存器修复 / PWM revert / docs 改动尚未 commit
- ⏳ bring-up 工具（`firmware/clk_test/` `hc154_test/` `pg_test/` `pin13_blink/`）
  归宿待定（`.gitignore` 还是归档到 `firmware/tests/`）

### 下次继续

1. Z 轴运动验证
2. 提交：PWM revert + S:SPITEST 0x7F 修复 + SESSION/TODO doc 更新
3. 决定 bring-up 工具归宿
4. （可选）打开 5 轴扩展模式
5. POWER_GOOD bypass 待 PCB 飞线后恢复轮询

---

## 更早会话

**日期**: 2026-05-13
**分支**: maxpro
**位置**: develop → maxpro 大合并 + octoaxesplus 同步主线 + 8 轴扩展 + 上位机 constants.py Phase 3.1

### 本次完成（7 commits，按时间顺序）

#### 1. Merge develop → maxpro（commit c03e7c4）

合并 develop 主线 62 commits（2026-04-17 → 2026-05-12 全部进展）到 maxpro
分支。冲突解决策略：以 develop 为骨架，叠加 maxpro 双相机 PCB 资源章节。

**冲突文件 4 个**：
- `constants.py` — 自动合并（develop 是 maxpro 的严格超集，X/Y limits 收紧 +
  has_encoder=False + 全 7 轴 actuator_* 字段）
- `TODO.md` — 自动合并（双方条目都保留，maxpro octoaxesplus 段在 lines 123-138）
- `CLAUDE.md` — 手工合并（develop 当前状态块为底，新增完整"octoaxesplus 工程"
  章节涵盖 74HC154/CAMERA_TRIGGER 8 路/MCP23S17/pin28 修复/照明 8 端口适配）
- `SESSION.md` — 手工合并（develop 整版骨架，maxpro 04-14/04-16/04-17 四段
  按时间倒序插入历史）

**自动合并** 21 个 firmware/software 文件 + 10 个新 baseline 文件。

编译验证：octoaxes 2.33s + octoaxesplus 14.27s 均 SUCCESS。

#### 2. 同步 octoaxes 主线进展到 octoaxesplus（commit 266e589）

octoaxesplus 是 2026-04-14 物理拷贝出的双相机变体（commit 6907952），不是
git 分支，期间 octoaxes 主线在 develop 上累积 38+ 改进未流入。本次按 A/B/C
三类策略一次性反向同步，**保留** squid++ 双相机硬件资源调整。

**A 类（10 文件 byte-identical 直接 cp）**：axis.cpp/h、build_opt.h、
commandprocessor.cpp、filterwheel.cpp、illumination.h、joystick.cpp、
serial.cpp/h、stepaxis.cpp。拿到 38+ 改进：静默 reject 修复、
checkMovementComplete 用 chip STATUS、handleInitialize 重跑 beginAll 做
chip-level reset、AF_LASER 修复、Y homing 调试基础设施（S:SET_HOMING_VEL）、
下降沿立即发包、二进制调试协议、TMC2240 SG 守卫等。

**B 类（4 文件 3-way 合并）**：
- `config.h`：增 VMAX 30/30/3.8 + HOMING_VEL_Y 30 + SG 注释；保留 74HC154
- `illumination.cpp`：增 matrix early init + AF_LASER fix + set_led_matrix
  仅缓存；保留 TTL 8 端口 + DAC HC154
- `platformio.ini`：增 nointerlock 环境（pin 38）；保留 USE_HC154_CS
- `octoaxesplus.ino`：增 chip-level reset + matrix early init + 删 DAC
  pinMode；保留 hc154_init/mcp23s17_init/D8 interlock；**反转** octoaxes 的
  X/Y swap（squid++ HC154 命名与硬件对齐，不需要老 Squid 反向接线兼容）

**C 类（1 新增）**：`download.sh` 复制改适（pin 38 联锁 + D1-D8）。

**等式确认**：octoaxes 与 octoaxesplus 当前仅 7 文件差异，全部 squid++
硬件适配；A 类 10 文件 byte-identical 验证通过；3 环境编译均 SUCCESS。

#### 3. 8 轴 AxisConfig 扩展 + Axis 实例化（commit 64fa643）

squid++ 双相机硬件 8 轴基础设施 Phase 1（仅 firmware）：

- `tmc/hal/TMC_SPI.h`：`TMC4361A_IC_COUNT` 用 ifdef USE_HC154_CS 区分
  （squid++ = 8，octoaxes = 7），互不影响
- `tmc/hal/TMC_SPI.cpp`：HC154 分支 `tmc_ic_configs[]` 扩到 8 条，顺序
  Y(0)/X(1)/Z1(2)/F1(3)/Z2(4)/F2(5)/R(6)/T(7) 全 CLOCK_STANDARD
- `octoaxesplus/config.h`：加 Z2_AXIS_CS=6/F2_AXIS_CS=5/R_AXIS_CS=3/
  T_AXIS_CS=4 HC154 通道号；Z2_AXIS=Z_AXIS / F2_AXIS=W_AXIS /
  R_AXIS=T_AXIS=EXPAND1_AXIS（const struct 拷贝，硬件实测后再单独定义）
- `octoaxesplus/octoaxesplus.ino`：4 个新 Axis 实例化（z2 StepAxis、f2
  FilterWheel、r/t Objectives）+ addAxis 顺序对齐 icID
- `octoaxesplus/axesmrg.cpp`：`beginAll()` 加 Z1/F1/Z2/F2/R/T axisName
  分支映射；Z/W 兼容旧别名保留

编译：octoaxesplus 8 轴 SUCCESS（FLASH +1024B / RAM +1024B）；octoaxes
不受影响。

#### 4. 上位机 constants.py Phase 3.1（commit e9fd888）

`software/utils/constants.py` AXIS_CONFIG 7 → 13 条目：
- **共享 2 个**：X / Y（`enabled_for=["octoaxes","octoaxesplus"]`）
- **octoaxes 5 个**：Z / W / E1 / E3 / E4（`enabled_for=["octoaxes"]`）
- **octoaxesplus 6 个新增**：Z1 / F1 / Z2 / F2 / R / T
  （`enabled_for=["octoaxesplus"]`）

复用同类硬件参数：Z1/Z2 同 Z（screw 0.3 mm, 256 微步, 500 mA）；F1/F2
同 W（screw 100, 8 微步）；R/T 同 E1（screw 1.0, 64 微步）。Z1 与 Z 共用
firmware icID=2、F1 与 W 共用 icID=3，由 enabled_for 互斥保证运行时只
激活一组。

新增 `axes_for_model(model)` helper 返回指定固件型号下激活子集 + 常量
`FIRMWARE_MODELS`、`DEFAULT_FIRMWARE_MODEL = "octoaxes"`（向后兼容）。

向后兼容：现有 GUI import 全部保留，octoaxes 4 轴用户 X/Y/Z/W 配置
byte-identical，AXIS_MM_PER_STEP 自动派生 13 项。

#### 5. TRIGGER_IN/OUT 1-2 引脚定义 + helper（commit 4ca1626）

squid++ 双相机外部触发 IN/OUT 接入基础设施（pin 1-4）：
- config.h 加 TRIGGER_OUT1=1 / TRIGGER_IN1=2 / TRIGGER_OUT2=3 / TRIGGER_IN2=4
- trigger.h 加 NUM_EXT_TRIGGERS=2 + ext_trigger_out/in_pins[] 数组 + API
- trigger.cpp: OUT pinMode OUTPUT/LOW + IN pinMode INPUT_PULLUP；实现
  ext_trigger_set_out / ext_trigger_pulse_out / ext_trigger_read_in，
  全部带越界检查

#### 6. 硬件资源使用率审计 + CAM_TRI_READY + 删除 EXPAND CS 别名（commit TBD）

**审计成果**：扫描 config.h 全部 44 个硬件 Pin 常量的使用率：
- ✅ 33 个已实际使用（pinMode/digitalRead/digitalWrite 等）
- 📌 5 个占位（IIC_WP/SDA/SCL + RX2/TX2，无对应外设方案）
- 🗑️ 4 个完全无引用（EXPAND1-4_AXIS_CS=11 旧别名）→ 删除
- 🔴 2 个 squid++ 文档要求但缺定义（CAM_TRI_READY1/2 pin 7/6）→ 补齐

**本次补齐**：
- 加 `CAM_TRI_READY1=7` / `CAM_TRI_READY2=6` 引脚常量（注释标注 squid++ 文档
  描述与名称不一致，待核实原表）
- trigger.h 加 `cam_tri_ready_pins[NUM_EXT_TRIGGERS]` 数组 + `cam_tri_read_ready()`
  API 声明
- trigger.cpp: trigger_init 加 INPUT_PULLUP 抗悬空；实现 cam_tri_read_ready helper

**本次清理**：
- 删除 EXPAND1-4_AXIS_CS 4 个常量（用 R/T_AXIS_CS 取代）
- **保留** EXPAND1_AXIS / EXPAND3_AXIS / EXPAND4_AXIS AxisConfig（作为 R/T
  扩展轴的 const struct 拷贝模板源）
- 加注释说明删除原因 + 模板复用关系

**Teensy 隐式使用 pin**（无需 Pins:: 常量）：
- pin 11/12/13 = Teensy SPI 默认 → tmc/hal/TMC_SPI + DAC + MCP23S17 共用
- pin 20/21 = Teensy Serial5 默认 → joystick.cpp
- pin 26/27 = FastLED 通过 LED_MATRIX_DATA/CLOCK 显式使用

**未解疑问**：
- pin 5 标 RESERVED 但描述"相机2_等待触发"，与 pin 6 CAM_TRI_READY2 重叠
- pin 6/7 CAM_TRI_READY1/2 名称与文档描述（"相机1_触发"/"相机1_等待触发"）不一致
- 均待核实原始 xlsx 是否笔误

### 2026-05-13 后续：octoaxesplus 真机调试 — 偏离 octoaxes 基线的修改清单（待审查）

**前提**：`firmware/octoaxes/` 在真实硬件上**长期验证可行**（XYZW 4 轴 / Y homing
256/30 / TMC2660 + TMC2240 互换均通过）。`firmware/octoaxesplus/` 真机 bring-up
中累计若干"为绕开新硬件问题"而做的临时修改，其中部分**改了 octoaxes 同一段
已验证代码**，需要逐条复核哪些是 squid++ 必需、哪些可能是误判异常的绕过。

#### A. 已 commit 但本质是"绕过"，不是 squid++ 硬件需求

##### A1. commit `1ce942a` "切到 XYZ 三轴调试模式"
- `z1Axis` 重命名为 `zAxis`、axisName `"Z1" → "Z"`
  - **理由**：`commandprocessor.cpp` 硬编码 `findAxisByName("Z")`，上位机
    MOVE_Z/MOVETO_Z/HOME_Z 必须找到 "Z" 轴。但 `axesmrg.cpp::beginAll` 已支持
    `"Z"/"Z1"` 双名映射，理论上 axisName="Z1" 也能工作 —— **需要重新验证**
    为什么 "Z1" 不行（是 findAxisByName 真的查不到？还是 begin 路径有别的依赖？）
- 5 个未接硬件轴注释掉（f1/z2/f2/r/t + addAxis）
  - **理由**：硬件未接，避免 `beginAll()` 对不存在的 TMC4361A SPI 初始化 →
    未定义行为
  - **隐含异常**：如果 SPI 通路本身可靠，对没接的 chip 做读会拿到 0xFF 但
    `axis.begin` 不应该崩。注释掉是症状压制，**根本问题（SPI 可靠性 / chip
    存在检测）未根治**

#### B. 未 commit 的工作区改动（`git diff` 可见）

##### B1. `octoaxesplus.ino::initializeClock` —— PWM 占空比/分辨率改动
- octoaxes 基线：默认 8-bit 分辨率 + `analogWrite(clk_pin, 128)` 跑 16 MHz
  PWM，**长期硬件验证可行**
- octoaxesplus 改为 `analogWriteResolution(4)` + `analogWrite(clk_pin, 8)`
  - **声称理由**：PJRC 表 8-bit 分辨率上限 1 MHz，16 MHz 超限 → pin 卡 HIGH
  - **矛盾**：octoaxes 同样写 8-bit + 16 MHz 长期工作，理论上应该一起失败
  - **可能解释**：① PJRC 自动降分辨率到能匹配频率的位数 ② octoaxes 真正的
    时钟通路是 TMC4361_EXPAND_CLK pin 28（被 octoaxesplus 删除），pin 37
    STANDARD_CLK 是否真在工作 octoaxes 也没严格验证过
  - **推荐**：**回滚到 octoaxes 写法**（`analogWrite(clk_pin, 128)` 不调
    `analogWriteResolution`），用 `firmware/clk_test/` 工程在 squid++ 板上
    单独验证 pin 37 实测波形/电平。若失败再针对性修；若成功则证明这次
    "改 PWM 分辨率"是误判，应撤销

##### B2. `octoaxesplus.ino::initializePowerManagement` —— POWER_GOOD 轮询绕过
- octoaxes 基线：`while(!digitalRead(POWER_GOOD))` 等 LTC2903 RST# 拉高，
  超时 5s
- octoaxesplus 改为 `pinMode INPUT_PULLUP` + `delay(500)` 直接放行
  - **声称理由**：原理图 +24V_XY 标签错误（IC6 LTC2903 V3/V4 输入悬空），
    pin 0 永远 LOW → 永远 5s 超时返回 false
  - **影响**：bypass 后**失去** "电源未就绪时不初始化 TMC chip" 的硬件保护，
    LM2576/LDO 真正稳定的时间没有任何监测
  - **推荐**：① 用 `firmware/pg_test/` 验证 pin 0 实际电平 ② 如确认是 schematic
    bug，**改 PCB 飞线** 而不是删轮询 ③ 短期保留 bypass 也要把 500ms 改成可
    配置 + 加 DEBUG_PRINTLN 警告

##### B3. `serial.cpp` 新增 3 个调试命令（130 行）
- `S:CLKMODE <off|high|low|slow|16m>` — 切换 pin 37 PWM 状态，万用表 DC 档
  实测平均电压（slow 100Hz 与 16m PWM 都应 ≈1.65V）
- `S:CSHOLD <channel>` — 持续选通 HC154 某通道不归零，万用表实测各 CS 电平
- `S:SPITEST [icID]` — 绕过 axis.begin 直接 SPI 读 TMC4361A `VERSION_NO`/
  `STATUS`，用于排查 SPI 不通根因（配合 S:CSHOLD 锁定问题在 HC154 / CLK /
  5V_AXIS 哪一层）
- **评价**：是真有用的诊断工具，bring-up 完成后**保留或剥离到独立 debug 模块**

#### C. 未跟踪测试工程（`firmware/clk_test/` `hc154_test/` `pg_test/`）
- 用于 isolate 排查 B1/B2/B3 的硬件可靠性
- **决定后是否纳入 git** —— 若属一次性 bring-up 工具，加 `.gitignore`；若可复用
  到下一块 PCB bring-up，归档到 `firmware/tests/` 或类似目录

#### 待审查异常清单（按优先级）

1. **PWM 分辨率改动是否必需** — B1 回滚验证（最可疑，octoaxes 是反例）
2. **POWER_GOOD bypass 改 PCB 飞线** — B2 不是固件能根治的
3. **5 轴注释掉的根本原因** — A1，应该让 `axis.begin` 对未接 chip 优雅退出而
   非注释源码
4. **axisName "Z1" 为什么不被 findAxisByName 命中** — A1，重新阅 axesmrg.cpp
   双名映射代码确认是否真有 bug
5. **下一步要做但被 bring-up 阻断的任务**：Phase 3.2-3.4 上位机 8 轴支持 /
   双相机握手 / TRIGGER_IN/OUT 联动 等等

### 关键决策记录

1. **TMC4361A_IC_COUNT** 用 ifdef USE_HC154_CS 区分（与现有 ifdef 模式一致），
   而非把 octoaxes 也升到 8 加 stub
2. **R/T 都用 Objectives 类**（与现有 EXPAND1_AXIS 物镜参数一致）
3. **Z2/F2/R/T 用 const struct 拷贝**（C++ 隐式 copy ctor）作为初始默认，
   硬件实测想调单独参数时把 `=` 改成完整初始化器即可
4. **squid++ X/Y 不需要老 Squid 反向接线 swap** — squid++ HC154 通道命名
   （HC154_AXIS_X=10）与物理硬件对齐，与 octoaxes 主线的 octoaxes.ino:86-101
   注释相反但有原理依据
5. **Phase 3.1 只做 metadata** — 不动 GUI 渲染、不动协议；Phase 3.2-3.4
   留下次会话（需要硬件验证识别固件型号 + 收 40 字节包）

### 下次继续

1. **Phase 3.2-3.4 上位机 8 轴完整支持**（需硬件连接验证）：
   - 3.2：GUI 启动用 S:HWINFO 识别固件型号 + 按 profile 过滤渲染
   - 3.3：固件响应包 24 → 40 字节（增 Z2/F2/R/T 4×int32 = 16 字节）
   - 3.4：GUI 8 轴位置控件渲染 + serial_thread.py RESPONSE_LENGTH 动态
2. **CAM_TRI_READY1/2（pin 7/6）双相机握手** — trigger 模块加 READY 输入等待
3. **TRIGGER_IN/OUT1-2（pin 1-4）外部触发联动**
4. **MCP23S17 接入 Axis 层** — TARGET_REACHED 用于运动完成判定（可选优化）
5. **核实 squid++ 原表笔误** — GPB2 INTR_T/F2、GPB6 INTR_Z2/F1
6. **LT3932 SYNC 方案** — squid++ 是否真取消 PWM 还是挪走
7. **Z2/F2/R/T 真实硬件参数实测调优**
8. **TMC2240 StallGuard4 调优 + chip-level latch 恢复**（2026-05-12 提出，
   stash@{0} 保留 Phase A 调试工具，6 步流程见原 SESSION 记录）
9. **TMC2240 StealthChop 调优**
10. **清理 TMC2240 调试代码**
11. **`tags` 加入 `.gitignore`**（实际已加，待核实并标完成）

---

### 2026-05-12 - 文档同步 + Homing 调试打印清理 + Y homing 异响参数确定（256/30）(develop)

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

#### 8. 移动完成下降沿立即发包优化（commit a6c5786，硬件实测）

讨论"框架效率优化方向"后，先动协议层最大杠杆点：**判完延迟**。

**背景**：原 `send_position_update` 固定 10ms 心跳，上位机 `wait_till_operation_is_completed`
等 `status=COMPLETED` 包，最坏要等下次心跳整 10ms。短距离命令（10μm=122ms）的
基线开销中这一段占 5%+。

**A.1 心跳频率 10ms→2ms 的代价分析**：放大 CRC 错误绝对发生频率、`loop()` 调度
压力、SPI 总线流量、PC 端读线程压力（5×），且对底层电气信号无收益。**不采用**。

**A.2 benchmark 5 帧防抖优化**：旧 Squid `wait_till_operation_is_completed` 没有
5 帧防抖（那是 benchmark 脚本独有），所以 A.2 对旧 Squid **无效**。

**实施方案 — 完成边缘事件驱动**：心跳频率保持 10ms 不变，`send_position_update`
增加 `any_moving` 下降沿检测，所有轴从 moving→idle 那一帧绕过 10ms 节流立即发
一帧 COMPLETED：

```cpp
bool any_moving = ...;   // 计算上移到节流之前
bool falling_edge = _last_any_moving && !any_moving;
_last_any_moving = any_moving;
if (_us_since_last_pos_update < INTERVAL_SEND_POS_US && !falling_edge)
    return;
```

- 每次完成 transition 只触发一次（下一帧 `_last_any_moving=false` 自动消抖）
- 流量影响：每条 MOVE +1 个 24 字节包，可忽略
- **对旧 Squid + octoaxes GUI + benchmark 三端同时有效**（都是等 status=COMPLETED）

**benchmark 实测对比**（TMC2240，`teensy41_nointerlock`，vs 2026-05-12 baseline）：

| 距离 | X 优化后 | X baseline | ΔX | Y 优化后 | Y baseline | ΔY |
|---|---|---|---|---|---|---|
| 10μm | 116.3 | 122.5 | **-6.2** | 116.1 | 122.6 | **-6.5** |
| 100μm | 193.3 | 197.0 | -3.7 | 193.2 | 197.3 | -4.1 |
| 1mm | 363.7 | 366.4 | -2.7 | 363.8 | 365.9 | -2.1 |
| 5mm | 617.9 | 620.2 | -2.3 | 617.6 | 619.8 | -2.2 |
| 10mm | 823.1 | 830.0 | **-6.9** | 822.3 | 828.6 | -6.3 |
| 30mm | 1448.4 | 1453.6 | -5.2 | 1448.8 | 1455.5 | **-6.7** |

X/Y 一致省 **2-7ms**，平均约 5ms，与「下降沿绕过 0-10ms 心跳延迟，平均 5ms」
预期吻合。短距离收益占比最大（10μm 省 5%）。

**Z 几乎无收益**（< 1ms 在噪声内）：Z vmax=3.8mm/s + accel=20mm/s² 比 X/Y 慢
一个量级，最后一段减速到 XACTUAL=XTARGET 的精确时刻可能与心跳 phase 强相关，
相当于物理完成时刻已对齐心跳——下降沿没空间可省。但 Z 慢瓶颈本就在 vmax/accel，
不是主战场。

FLASH 持平：teensy41=72060 / teensy41_nointerlock=71996。新基线 CSV/MD 归档至
`software/tests/results/benchmark_xyz_20260512_160839.{csv,md}`（results 在
`.gitignore` 内，未入库；如需正式归档可移到 `documents/baselines/`）。

#### 9. B.6 STATUS.TARGET_REACHED_F 判完优化（commit 5e487aa，硬件实测）

讨论旧 Squid 判完方式对比时定位优化方向：

| | 旧 Squid | octoaxes (改前) | octoaxes (改后) |
|---|---|---|---|
| SPI 读次数 | 2 (XACTUAL + STATUS) | 2 (XACTUAL + XTARGET) | **1** (STATUS only) |
| target 来源 | 软件缓存 `X_commanded_target_position` | chip XTARGET 寄存器 | 不需要 (chip 内部判) |
| 判完条件 | XACTUAL==target AND TARGET_REACHED AND VEL_STATE==0 AND RAMP_STATE==0 | XACTUAL==XTARGET | TARGET_REACHED_F bit |

`Axis::checkMovementComplete` 改用已存在但未被调用的
`motor_isTargetReached(icID)` API（`MotorControl.cpp:801`，linker 之前会移除
该函数体；本次首次调用 FLASH +192 字节）。`motor_moveToMicrosteps` 已主动读
EVENTS 清 sticky bits，TARGET_REACHED_F 在 STATUS 寄存器是 instantaneous flag
（XACTUAL==XTARGET 时实时置 1），不会有上次完成的残留误触发。

顺手清掉 `_lastPosition` 死字段（axis.h 字段 + 构造函数 + startMovement +
checkMovementComplete 共 4 处写入，全仓 grep 确认 0 处读取，纯 dead state）。

**benchmark 实测**（TMC2240，vs a6c5786 A.0 基线）：

| 距离 | X B.6 | X A.0 | ΔX | 累计 vs 原始 baseline |
|---|---|---|---|---|
| 10μm | 115.6 | 116.3 | -0.7 | **-6.9** (122.5→115.6, -5.6%) |
| 100μm | 192.6 | 193.3 | -0.7 | -4.4 |
| 1mm | 363.6 | 363.7 | -0.1 | -2.8 |
| 5mm | 616.9 | 617.9 | -1.0 | -3.3 |
| 10mm | 822.2 | 823.1 | -0.9 | -7.8 |
| 30mm | 1448.0 | 1448.4 | -0.4 | -5.6 |

Y 全档同步省 0.4-1.0ms，Z 几乎无变化（非 SPI bound）。480 trial 无异常，
chip TARGET_REACHED_F 信号可靠（无误触发漏触发）。

**主要价值不在性能**（小幅 0.5-1ms），在于：
- 判完用 chip 权威信号，更可靠
- 给 B.5 Target Pipeline 铺路：pipeline 中 XACTUAL 会快速跨越 XTARGET，
  软件比较「XACTUAL == XTARGET」可能漏掉，TARGET_REACHED_F 由 chip 内部捕获

**遗留 B.6.1 候选**：旧 Squid `tmc4361A_isRunning` 单次 STATUS 读同时检查
3 个 bit（TARGET_REACHED + VEL_STATE + RAMP_STATE 都满足才算停），比 B.6
单 bit 更严格。如果 chip 在 ramp 末尾出现「XACTUAL == XTARGET 但 velocity
未归零」的边缘 case，B.6 会误判完成。SPI 成本相同（同一次 STATUS 读取多个
bit），可作下一步迭代。

#### 10. B.6.1 三 bit 严格判完（commit a0e03d5，硬件实测）

按 B.6 末尾预告实施。`motor_isTargetReached` 由「单 bit (TARGET_REACHED_F)」
改为「三 bit AND」（对齐旧 Squid `tmc4361A_isRunning` 取反语义）：

```cpp
uint32_t status = tmc4361A_readRegister(icID, TMC4361A_STATUS);
return (status & TMC4361A_TARGET_REACHED_F_MASK) &&         // bit 0
       !(status & (TMC4361A_VEL_STATE_F_MASK |              // bits 3-4 == 0
                   TMC4361A_RAMP_STATE_F_MASK));             // bits 5-6 == 0
```

mask 通过 TMC4361A.h 间接引入 TMC4361A_HW_Abstraction.h，单次 STATUS 读
多 bit 提取零额外 SPI 成本。

**实测验证**（两次跑 benchmark，第一次 Y home 24.32s 是样本干涉，重测正常）：

| 距离 | B.6.1 X | B.6 X | Δ |
|---|---|---|---|
| 10μm | 115.7 / 115.2 | 115.6 / 115.6 | ±0.2 |
| 100μm | 192.5 / 193.1 | 192.6 / 193.1 | 0 |
| 1mm | 364.1 / 364.0 | 363.6 / 363.8 | +0.3 |
| 5mm | 616.8 / 621.8 | 616.9 / 618.1 | +1.8 |
| 10mm | 822.4 / 819.9 | 822.2 / 819.7 | +0.2 |
| 30mm | 1447.9 / 1444.2 | 1448.0 / 1444.3 | 0 |

**B.6.1 与 B.6 时间完全一致**（差异 ±2ms 在测量噪声内）。Y 全档同步。
480 trial 全过。

**结论**：
- 多 bit 比较零额外 SPI 成本 ✓
- 现有 vmax/accel 参数下，chip ramp 末尾「位置到但速度未归零」边缘 case
  **不存在或窗口太短**，B.6 单 bit 也未误判过——B.6.1 是预防性升级
- 主要价值：为未来 Target Pipeline 高速切换 / 参数调优后 ramp 变化更剧烈
  的场景做防御性铺垫

FLASH 持平 72188 字节（多 bit 提取无额外代码体积）。

#### 11. Y homing 异响诊断 + 参数确定（256 微步 + 30 mm/s，硬件实测确认）

**问题**：老 Squid firmware + 老 Squid software 组合 Y homing 无异响；octoaxes firmware
+ 老 Squid software 组合 Y homing 有异响 + 慢。硬件相同（TMC2660），软件相同，唯一
变量是 firmware。本次完整诊断。

**对比 firmware 差异（grep + decode 完成）**：

| 项目 | 老 Squid firmware（正常） | octoaxes firmware（异响） | 来源 |
|---|---|---|---|
| Y homing 速度 | 0.5 × MAX_VEL_Y(30) = **15 mm/s** | **10 mm/s**（绝对值） | def.h ratio vs config.h 绝对值 |
| Y homing 微步 | 与运行同步（HCS_v2.ini = 32） | 切换到 256 | `switchToHomingMicrosteps` |
| CHOPCONF HSTRT | 0 | 4 | TMC4361A_TMC2660_Utils.cpp:462 vs axis.cpp:92 |
| CHOPCONF HEND | 0 | -2 | 同上 |
| CHOPCONF TOFF/TBL | 3/2 | 3/2 | ✓ 一致 |

**调优顺序（按硬件实测顺序）**：

1. **chopper 对齐**（axis.cpp HSTRT 4→0, HEND -2→0，三 TMC2660 轴同改对齐老 Squid
   `CHOPCONF=0x000900C3` 零滞回静音）。**实测异响未消除**。
2. **homing velocity 10→15 mm/s**（config.h，与老 Squid 0.5×30 对齐）。**实测异响未消除**。
3. **禁掉 homing 微步切换**（axis.cpp `configureDriver` 加 `_config.homingMicrostepping
   = microstepping`，让 Y homing 跟运行同微步 = 32）。**实测异响未消除**。

三步串行调优后异响依然 → 推断**根因不在已识别的 firmware 参数差**，可能在 ramp 加
速曲线 / 电流缩放 / 机械共振 / 接地。

**诊断工具建设**（不重烧 firmware 扫参数空间）：

- 新增 firmware 调试命令 `S:SET_HOMING_VEL <axis> <vel_mm>`（serial.cpp +40 行，FLASH
  +8K 因 String 库引入）—— 运行时写 `_config.homingVelocityMM`，与现有
  `CONFIGURE_STEPPER_DRIVER` 配合可在线扫 (微步 × 速度) 矩阵
- ASCII 命令路径关键发现：**firmware 调试协议需要 `0x55 0xAA` 二进制前缀**
  （`DEBUG_PROTOCOL_HEADER_1/2`），不是直接 `S:` ASCII。第一次 diag 脚本漏 header
  导致 10 组合全超时，self-check 加入后立即定位
- 新增 `software/tests/check_homing_vel_cmd.py` —— 快速验证 firmware 是否烧入新命令
  + 0x55 0xAA 协议示范
- 新增 `software/tests/diag_y_homing_noise.py` —— 交互式扫描脚本，10 个默认组合
  (microstepping ∈ {32, 256} × velocity ∈ {1, 5, 10, 15, 30})，支持 `--combos` 自定义；
  每组合显示参数 → 用户按 Enter 触发 HOME → 听声打分 1-5 → 自动回中心；CSV 归档

**实测最优配置（用户耳听确认）**：

- **微步 = 256**（始终 homing 期切换到 256）
- **速度 = 30 mm/s**（即 vmax 满速）

**最终代码改动**（commit d876121 + b24f6f7）：

- `firmware/octoaxes/config.h:172`：`HOMING_VELOCITY_Y_MM = 30`（之前 15）
- `firmware/octoaxes/axis.cpp:configureDriver`：撤回 `_config.homingMicrostepping = 
  microstepping` 同步行，让 Y homing 永远走 config.h 默认 `HOMING_MICROSTEPPING_Y = 256`
  即便老 Squid software 下发 32 微步运行
- chopper HSTRT=0/HEND=0 改动保留（与老 Squid 全局默认对齐，未观察到副作用）
- 调试命令 `S:SET_HOMING_VEL` 保留（FLASH +8K 接受，未来扫参方便）

**理论分析**：30 mm/s 是 vmax 满速，homing 不再有「低速 chopper 噪声敏感」段；256
微步在该速度下 step pulse rate 较高但 INTERPOL 平滑，整体能量集中在更高频段，
人耳不敏感。本质上是**让 homing 避开了 SpreadCycle 低速段**，而非修了根因——根因
（chopper 模式 / 电流 / 接地）依然存在，只是配置上跳过了触发条件。

**新 Y homing 性能**：
- 76mm / 30 mm/s + ramp ≈ 3-4s（之前 10 mm/s 时约 8s）
- 用时减半，且无异响

FLASH：teensy41=72284 / teensy41_nointerlock=80220（+8K 来自 String 库支持
S:SET_HOMING_VEL 的 substring/toFloat）。

**遗留问题**：根因未确定。如未来要彻底消除 Y 低速 chopper 噪声（用于慢速精确逼近
场景），需深入 chopper 模式（StealthChop / SpreadCycle 切换）或电流相位补偿
（SMARTEN/CoolStep）调优。但当前 homing 走满速完全避开，**生产可接受**。

### 下次继续

**TMC2240 StallGuard4 调优 + chip-level latch 恢复修复**（中等优先）：
1. 调研 TMC2240 datasheet 中 SG4_THRS / SGT / SEMIN / SEMAX 参数语义，实测找到
   既不误触发又能检测真实 stall 的阈值
2. 修 `motor_moveToMicrosteps` recovery：同时清 STATUS 中 ACTIVE_STALL_F / HOME_ERROR_F
   latch（写 EVENTS 触发 / 暂时禁 STOP_ON_STALL / SW_RESET ramp generator）
3. 上层设计 stall 处理语义：触发时上报上位机由操作员决定（撤回 / 接受 / reset），
   不让 chip 默默死锁

**Y homing 异响** — 2026-05-12 已用「微步 256 + 速度 30 mm/s」工作方案解决（避开
chopper 低速段，见本会话 #9）。如未来需要彻底根治低速 chopper 噪声（精确逼近
场景），方向参考：
- TMC2660：SMARTEN/CoolStep 低速电流补偿、SGCSCONF 调优、机械接地排查
- TMC2240（如未来切回）：启用 StealthChop2 + TPWMTHRS 阈值门控、CURRENT_RANGE 审查

**其他**：

1. **W 轴换孔时间优化（61.3ms → ≤60ms）**（高难度，ASTART 已到 BOW 截断硬约束）
2. **清理 TMC2240 Cover40 debug 打印**（中等优先）
3. **修正 W 轴 config.h LEFT_SW → RGHT_SW + 极性**（需硬件实测，暂搁置）
4. **XYZ 大距离 5% ramp 差距**（需 firmware 调试打点，TMC2240 上待重测确认）

**框架效率优化后续候选**（2026-05-12 讨论，下降沿优化已落地为第一项）：

- ✅ A.0 完成边缘立即发包（已完成，X/Y 省 5ms）
- B.5 **Target Pipeline (TMC4361A §9.2)**：当前 ramp 期间预写下一个 XTARGET，
  扫描/stitching 路径理论提速 20-50%（中难度）
- B.6 **STATUS.TARGET_REACHED 替代 XACTUAL==XTARGET 判完**：节省 1 SPI 周期 +
  更可靠（低难度，可与 B.5 一起做）
- C.9 **多轴并行 home**：现 `home_xyz` 串行 X→Y→Z（约 14s），并行可省 2/3 时间
- A.3 **MOVETO_BATCH 命令**：扫描场景一次下发 N 个目标点，减少串口往返（高难度）
- C.8 **Look-ahead corner blending**：stitching 路径不停顿过拐角（高难度）

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

### 2026-04-17 - octoaxesplus MCP23S17_1 扩展 IO 驱动 + pin 28 冲突修复 (maxpro)

#### 1. MCP23S17_1 扩展 IO 驱动（commit 8be11c7）

基于用户 AskUserQuestion 四问确认后实现：

- **决策**：TARGET_* 全部为输入（TMC4361A 的 TARGET_REACHED_OUT 接入）；基础驱动 + init；轮询模式（INTA/INTB 未接 Teensy）；CS 走 HC154 通道 0
- **新增文件**：
  - `firmware/octoaxesplus/mcp23s17.h` — 寄存器地址、opcode、GPA/B INTR/TARGET 位掩码（按 squid++ §3 命名）、公开 API
  - `firmware/octoaxesplus/mcp23s17.cpp` — SPI1 事务封装（beginTransaction + hc154_select）
- **init 配置**：
  - `IOCON=0x00`（BANK=0/HAEN=0/顺序寻址，兼容 A0-A2=GND）
  - `IODIRA/B=0xFF`（16 路全输入）
  - `GPPUA/B=0xFF`（100kΩ 上拉，信号悬空时默认高电平容错）
  - `GPINTENA/B=0x00`（关硬件中断）
- **API**：`readReg/writeReg` 通用访问，`readPortA/B`，`readGPIO()` 一次事务连读 A+B（利用 SEQOP 地址自增）
- **片选路径**：事务前 `hc154_select(HC154_MCP23S17_1=0)`，事务后归零到 `HC154_EXPAND_NSCS1`
- `octoaxesplus.ino` 在 `initializeSPIAndPins()` 之后调 `mcp23s17_init()`
- 编译：octoaxesplus SUCCESS 9.34s / octoaxes SUCCESS 1.70s

#### 2. Pin 28 冲突修复 + IIC/Serial2 占位（commit 567b599）

核查文档 §1 发现 **14 个引脚未定义** + **pin 28 运行时冲突**：

- `TMC4361_EXPAND_CLK = 28`（初始化时输出 2 MHz PWM）与 `ILLUMINATION_D5 = 28`（TTL5）共用 pin，激光 TTL 被时钟干扰
- **修复**（仅 octoaxesplus 侧，octoaxes 保留）：
  - `config.h` 删除 `TMC4361_EXPAND_CLK` 常量（加注释说明取消原因）
  - `octoaxesplus.ino` 删除第二次 `initializeClock()` 调用
  - `CLOCK_EXPAND` 在 TMC_SPI.cpp 中仅作运行时 ID（值 0/1），不引用 pin，未动
- **补充占位**（pin 14/16/17/18/19）：
  - `IIC_WP=14, IIC_SDA=18, IIC_SCL=19`（Wire1 占位，待外设方案）
  - `RX2=16, TX2=17`（Serial2 占位）
- 编译：两工程均 SUCCESS

#### 3. 硬件测试可行性评估

- **可测**：烧写/串口、SPI 设备扫描（4 TMC + DAC + MCP）、TTL 照明、单轴慢速移动
- **不建议**：8 轴完整运动（还缺 Z2/F2/R/T 实例化）、双相机采集（CAM_TRI_READY 握手未实现）
- **硬件核对清单**：POWER_GOOD 拉低、pin 37 TEENSY_CLK、HC154 A0-A3 接线、INTERLOCK 拉低、MCP23S17 硬件地址 A0-A2 接 GND、74HC154 /E 使能

---

### 2026-04-16 - octoaxesplus 8 轴 config.h 主体重构 (maxpro)

#### 1. SPI HAL 加 HC154 分支（TMC_SPI.cpp/h）

`tmc/` 是 octoaxesplus→octoaxes 的符号链接，为避免破坏 octoaxes，采用编译时 ifdef 分支：

- `octoaxesplus/platformio.ini` common `build_flags` 增加 `-D USE_HC154_CS`
- `TMC_SPI.cpp` 用 `#ifdef USE_HC154_CS` 切换两套实现：
  - `tmc_ic_configs[]` 初始化列表：HC154 分支下 csPin 语义为通道号（Y=9/X=10/Z1=8/F1=7；E1-E3-E4 占 Y11 EXPAND_NSCS1 占位）
  - `tmc_spi_init()` HC154 分支只调 `Pins::hc154_init()`，不 pinMode
  - `tmc4361A_readWriteSPI()` 事务前调 `Pins::hc154_select(csPin)`，事务后归零到 `HC154_EXPAND_NSCS1`
- `#include "config.h"` 裸路径（symlink 解析后相对路径会指向 octoaxes，必须用 src_dir 搜索路径）

#### 2. config.h 删除 DEPRECATED_PIN + CS 转 HC154 通道号

- 删除 `DEPRECATED_PIN = 255` 常量和所有 7 处使用
- 轴 CS 改为 HC154 通道号：`X_AXIS_CS=10, Y_AXIS_CS=9, Z_AXIS_CS=8(Z1), W_AXIS_CS=7(F1)`
- `DAC8050x_CS=2`（DAC80508_1）
- `EXPAND1-4_AXIS_CS=11`（EXPAND_NSCS1 占位）
- TTL 引脚重映射至 squid++ 分配：`D1..D5 = 32/31/30/29/28`，新增 `D6/D7/D8 = 25/24/10`
- `ILLUMINATION_INTERLOCK` 从 pin 2 改为 pin 38
- `LED_DRIVER_SYNC=255`（无效；squid++ 无该引脚，analogWrite 空操作）
- `IlluminationConfig` 光源码新增 `D6=16, D7=17, D8=18`

#### 3. octoaxesplus.ino 去掉直接 pinMode CS

- `initializeSPIAndPins()` 删除 `STANDARD_CONTROL_PINS/CONTROL_PINS` 的 pinMode/digitalWrite 循环，改为 `Pins::hc154_init()` + `SPI.begin()`（提前于 illumination_init 的 DAC 通信）
- `initializePowerManagement()` 删除 `pinMode(Pins::DAC8050x_CS, OUTPUT)`（CS 走 HC154）
- loop interlock 关灯扩展到 D1-D8

#### 4. illumination.cpp TTL 8 端口 + DAC CS 走 HC154

- `illumination_init()` 新增 D6/D7/D8 pinMode
- `set_DAC8050x_*()` 3 处 SPI 事务：`digitalWrite(DAC8050x_CS, LOW/HIGH)` → `Pins::hc154_select(Pins::DAC8050x_CS)` / `hc154_select(HC154_EXPAND_NSCS1)`
- `port_index_to_pin` / `illumination_source_to_port_index` / `port_index_to_dac_channel` 支持 0-7
- `turn_on/off_illumination` / `set_illumination` switch 增加 D6/D7/D8 case（DAC 通道 5/6/7）

#### 5. 编译验证

- `firmware/octoaxesplus && pio run`: **SUCCESS** 8.05s（Teensy 4.1）
- `firmware/octoaxes && pio run`: **SUCCESS** 1.70s（共享 tmc/ 在无 `USE_HC154_CS` 时保持旧行为）

---

### 2026-04-14 - octoaxesplus 74HC154 映射 + CAMERA_TRIGGER 扩展到 8 路 + Pins 冲突落地 (maxpro)

#### 1. 74HC154 4→16 片选映射（commit e00fb60）

在 `config.h::Pins` 内新增 squid++ 双相机的 74HC154 译码器支持：
- A0-A3 地址引脚常量（pin 33/34/35/36）
- `HC154_Channel` 枚举（16 通道按 squid++ 配置 §2 命名：MCP23S17_1/DAC80508_x/轴 R/T/F2/Z2/F1/Z1/Y/X/EXPAND_NSCS1 等）
- `hc154_init()` + `hc154_select(channel)` 内联函数
- 决策：`inline` 函数直接写在 Pins 命名空间内；旧 CS 常量保留；用标准 `digitalWrite`

#### 2. CAMERA_TRIGGER 扩展 4→8 路

- `config.h` 旧 `CAMERA_TRIGGER_1..4 = 29/30/31/32`（继承自 octoaxes）替换为 squid++ 的 8 路：`1=9, 2=8, 3=23, 4=22, 5=15, 6=41, 7=40, 8=39`
- `CAMERA_TRIGGER_1/2` 即 squid++ 表中 `CAM_TRI_OUT1/OUT2`（相机 1/2）
- `trigger.h`: `NUM_TRIGGER_CHANNELS` 4→8，`camera_trigger_pins[]` 扩展到 8 项
- trigger.cpp / commandprocessor.cpp 全部通过 `NUM_TRIGGER_CHANNELS` 符号访问，零修改自动适配

#### 3. Pins 命名空间内 7 处引脚号冲突落地（方案 A1）

扩展后在同一 `Pins::` 命名空间内发现 **7 处旧常量与新常量赋了相同 pin 值**：
- pin 22：`ILLUMINATION_D3` ↔ `CAMERA_TRIGGER_4`
- pin 23：`ILLUMINATION_D5` ↔ `CAMERA_TRIGGER_3`
- pin 33：`DAC8050x_CS` ↔ `HC154_A0`
- pin 34：`W_AXIS_CS` ↔ `HC154_A1`
- pin 35：`Z_AXIS_CS` ↔ `HC154_A2`
- pin 36：`Y_AXIS_CS` ↔ `HC154_A3`
- pin 41：`X_AXIS_CS` ↔ `CAMERA_TRIGGER_6`

编译层面合法（两个 `const int` 可赋相同值），但运行时任何 `pinMode` 调用都会踩错对象。

**方案 A1 处理**：
- 在 `Pins` 内新增 `static constexpr int DEPRECATED_PIN = 255;`（无效引脚号，`pinMode/digitalWrite` 在 Teensy 上为 no-op）
- 7 处旧常量的值全部改为 `DEPRECATED_PIN`，**保留符号名**，加 `// DEPRECATED: 旧=N，squid++ 改走 X` 注释
- 收益：illumination.cpp / octoaxesplus.ino 共 ~50 处引用零修改，下一步 8 轴重构时再真正删符号

编译验证：`pio run` 成功（Teensy 4.1，1.96s）。

---

### 2026-04-14（前半）- octoaxesplus 空工程 + squid++ 硬件配置文档 (maxpro)

- `firmware/octoaxesplus/` 空工程：platformio.ini 复用 octoaxes 配置，`tmc → ../octoaxes/tmc` 符号链接共享，`octoaxesplus.ino` 空 setup/loop，编译通过（commit 612ae3a）
- `documents/squid++（双相机）配置.md`：由 xlsx 转 Markdown，3 表（Teensy 48 引脚 / 74HC154 Y0-Y15 / MCP23S17_1 GPA0-GPB7）（commit e52347b）
- 发现原表 GPB2 INTR_T 标注「F2 轴」、GPB6 INTR_Z2 标注「F1 轴」，疑为笔误待核实

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
