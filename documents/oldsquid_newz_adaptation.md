# 旧 Squid software 适配新 Z 轴（MOONS' LE143S-W0601）

> 日期：2026-06-09
> 适用场景：客户使用**旧 Squid 上位机软件**（非 octoaxes/octoaxesplus GUI）+ **新 Z 轴硬件**
> 旧 Squid 源码参考路径：`/home/hds/github.com/veerwang/lihongquan/Squid/software`
> 配套产物：`Squid/software/configuration_HCS_v2_newZ.ini`（基于 `configuration_HCS_v2.ini` 生成）

## 结论

**可以支持。** 但旧 Squid 的部署配置（`.ini`）必须改 3 个必须项 + 2 个建议项，固件烧 octoaxes 固件即可（无需为新 Z 改固件），并物理换装新 Z 硬件。

## 新旧 Z 硬件差异

| 参数 | 旧 Z | 新 Z LE143S-W0601 |
|---|---|---|
| 丝杠导程 | 0.3mm | **1.0mm**（×3.33） |
| 额定电流 | 500mA | **1.5A** |
| 驱动板 | TMC2660（R=0.43Ω） | **TMC2240 ICS**（currentRange=1 / I_FS=2A） |
| 行程 | ~6mm | **~34.5mm**（实测上限位，octoaxesplus 板） |
| 编码器 | 关闭 | 有（0.1µm/pulse），默认仍开环 |
| 1.8°/200 整步 | 同 | 同 |

## 为什么旧 Squid 不能即插即用

旧 Squid 启动时 `control/microcontroller.py::configure_actuators()` 会把它**自己 `.ini` 里写死的 Z 参数下发覆盖固件默认值**，逐项发送：

```
set_leadscrew_pitch(AXIS.Z, SCREW_PITCH_Z_MM)            # cmd 23
configure_motor_driver(AXIS.Z, MICROSTEPPING_DEFAULT_Z,  # cmd 21
                       Z_MOTOR_RMS_CURRENT_mA, Z_MOTOR_I_HOLD)
set_max_velocity_acceleration(AXIS.Z, ...)               # cmd 22
set_limit_switch_polarity(AXIS.Z, Z_HOME_SWITCH_POLARITY)# cmd 20
set_home_safety_margin(...)                              # cmd 28
```

这些值来自部署用的 `configurations/configuration_*.ini`（覆盖 `control/_def.py` 默认）。
若 `.ini` 仍是旧 Z 值，新 Z 硬件就会按旧 Z 跑 → 位置错、扭矩不足、限位极性反。

> **重要**：实际部署的 `configuration_HCS_v2.ini` 与 `_def.py` 默认值**不同**——
> 它 `homing_enabled_z = True` 且 `[LIMIT_SWITCH_POLARITY] z_home = 0`（`_def.py` 默认是
> `HOMING_ENABLED_Z=False` / `Z_HOME=2(DISABLED)`）。所以**必须以实际部署的 .ini 为准**分析，
> 不能套 `_def.py` 默认值。

## 必改 / 建议改（全部在 `.ini`，无需改 Python 源码）

以 `configuration_HCS_v2.ini` 为基准：

| 段 | 参数 | 旧 Z | 新 Z 改为 | 必要性 | 原因 |
|---|---|---|---|---|---|
| `[GENERAL]` | `screw_pitch_z_mm` | 0.3 | **1.0** | 🔴 必须 | 导程 3.33×。不改 → 命令 1mm 实走 3.33mm |
| `[GENERAL]` | `z_motor_rms_current_ma` | 500 | **1500** | 🔴 必须 | 新 Z 额定 1.5A，500mA 扭矩不足丢步 |
| `[GENERAL]` | `z_motor_i_hold` | 0.5 | **0.75** | 🟡 建议 | 竖直 Z 防重力下坠 |
| `[LIMIT_SWITCH_POLARITY]` | `z_home` | 0 | **1** | 🔴 必须 | 本配置 homing_enabled_z=True，新 Z 限位极性=1 |
| `[SOFTWARE_POS_LIMIT]` | `z_positive` | 6.5 | **34** | 🟡 视需要 | 新 Z 行程 ~34.5mm，限位太小卡住移动；按实际机械装配核定 |

### 不需改但可选

| 参数 | 当前值 | 说明 |
|---|---|---|
| `microstepping_default_z` | 32 | **不影响位置正确性**（微步在命令换算两端抵消），仅影响对焦分辨率；如需更细可改 256 |
| `use_encoder_z` / `has_encoder_z` | False | 开环。新 Z 有编码器，仅闭环对焦时才改 True（需配 `encoder_resolution_um_z=0.1`） |
| `max_velocity_z_mm` / `max_acceleration_z_mm` | 3.8 / 100 | 导程变大后同 mm/s 对应转速更低，安全，可保留；有提速空间 |
| `default_z_pos_mm` | 2.287 | 绝对物理位置（mm），导程换算后仍有效，保留 |

## 两个关键坑

### 坑 1：电流的 RMS/峰值语义错位（最易踩）

`.ini` 变量名是 `z_motor_rms_current_ma`（RMS 语义），但：
- 旧 Z = **TMC2660**：固件按真 RMS 算
- 新 Z = **TMC2240**：固件的 `calculateCurrentScale_TMC2240` 把这个值当**峰值**算
  （`commandprocessor.cpp` 注释写 "RMS" 是历史误标）

所以新 Z 填 **1500 = 1.5A 峰值**正好对 —— **别**被 "rms" 名字误导去填 1060(=1500/√2)。
（详见仓库记忆 `cmd21-current-rms-peak-mismatch`）

### 坑 2（最深）：限位极性 —— 改 .ini 的 z_home **还不够**，必须连 `_def.py` 一起改

新 Z 需要限位极性 **1**（active-high），旧 Z 是 0。直觉上把 `.ini` 的 `z_home` 改 1 即可，**但实测无效**——
新 Z 在旧 Squid 下被限位假触发挡死、动不了。根因是旧 Squid `_def.py` 的 **import 顺序 bug**：

```python
# control/_def.py 行 ~1201-1203（在 configuration*.ini 加载之前 = 行 1292）
X_HOME_SWITCH_POLARITY = LIMIT_SWITCH_POLARITY.X_HOME   # 捕获类默认 1
Y_HOME_SWITCH_POLARITY = LIMIT_SWITCH_POLARITY.Y_HOME   # 捕获类默认 1
Z_HOME_SWITCH_POLARITY = LIMIT_SWITCH_POLARITY.Z_HOME   # 捕获类默认 2 = DISABLED!
```

- 这三个**模块变量**在 .ini 加载之前就被定死成 `LIMIT_SWITCH_POLARITY` 的**类默认值**。
- .ini 之后只更新了**类属性** `LIMIT_SWITCH_POLARITY.Z_HOME=1`，但模块变量 `Z_HOME_SWITCH_POLARITY` 不再重新求值，仍是 **2 (DISABLED)**。
- `configure_actuators()` 发的是模块变量 → 旧 Squid 给 Z 发 `set_limit_switch_polarity(Z, 2=DISABLED)` → 固件 `if(data[3]==DISABLED) return;` → **忽略，Z 极性停在固件开机默认**。
- 旧 Z 巧合可用（开机默认 0 = 旧 Z 正确）；新 Z 失效（开机默认 0 ≠ 新 Z 需要的 1）。
- 所以**改 .ini 的 z_home 对旧 Squid 完全无效**（它根本不读这值）。

**修复（必做）**：在 `control/_def.py` 的 .ini 加载块**之后**（约行 1383，`DEFAULT_TRIGGER_MODE = ...` 后）重新求值：

```python
X_HOME_SWITCH_POLARITY = LIMIT_SWITCH_POLARITY.X_HOME
Y_HOME_SWITCH_POLARITY = LIMIT_SWITCH_POLARITY.Y_HOME
Z_HOME_SWITCH_POLARITY = LIMIT_SWITCH_POLARITY.Z_HOME
```

修复后 `.ini` 的 `z_home=1` 才真正发给固件 → 固件 cmd 20 应用极性 1 → 新 Z 工作。这是通用 bug，对所有机器有益。

> X/Y 的 `x_home/y_home` 同样有此 bug，但固件 **FIX B** 让 X/Y 的 cmd 20 不写芯片（X/Y 极性固定用固件 init 值），所以 X/Y 不受影响——无论 .ini/_def.py 发什么，X/Y 都用 octoaxes 硬件正确的 active-low(0)。

## 固件侧

- 烧 **octoaxes 固件**（drop-in 替代旧 Squid 固件），**无需为新 Z 改固件**：
  - `Z_AXIS.currentRange = 1` 已写死（TMC2240 I_FS=2A，对旧 Z 的 TMC2660 也安全——后者忽略此字段走 R_sense）
  - **限位极性软件化 + FIX B（2026-06-09）**：cmd 20 写芯片**只对 Z 生效**（`polarityAffectsChip` 仅 Z=true）；X/Y 等固定硬件极性轴只改结构体不碰芯片，与旧 Squid 固件一致，避免旧 Squid 下发的 X/Y 极性误翻芯片。
  - **Z 开机默认极性 = 0（旧 Z）**：octoaxes 主线实装旧 Z，固件默认支持旧 Z；新 Z 由上位机 cmd 20 下发 1 覆盖。
  - `DRIVER_AUTO` 上电自动识别 TMC2660 / TMC2240，一个固件通吃新旧 Z

## 已知限制

- 旧 Squid 的开关只在 `.ini` 层；本仓库 octoaxes/octoaxesplus GUI 的 `Z_AXIS_VARIANT` 软件开关对旧 Squid **无效**（两套软件独立）。
- 若客户需要更复杂的新 Z 行为（闭环 PID 对焦、变体一键切换、限位实时监视），建议用 octoaxes GUI 而非旧 Squid。

## 操作清单（部署步骤）

1. 物理换装新 Z 硬件（电机 LE143S-W0601 + TMC2240 驱动板）
2. 烧 octoaxes 固件（替代旧 Squid 固件）
3. 旧 Squid 加载 `configuration_HCS_v2_newZ.ini`（替换原配置或启动参数指定）
4. 启动后验证：
   - 命令 Z 移动 1mm → 实测位移 ≈1mm（确认导程下发生效，无 3.33× 失配）
   - Z homing 在限位点正确硬停 + 退回（确认极性 1 生效）
   - 1.5A 下平稳不丢步、hold 0.75 防下坠
   - `z_positive` 按实际机械行程上限核定（建议先用千分表 / z_limit_monitor 量一次）

## 参考

- 配套示例配置：`Squid/software/configuration_HCS_v2_newZ.ini`
- 新 Z 规格：仓库记忆 `newz-axis-le143s-spec`
- 电流语义：仓库记忆 `cmd21-current-rms-peak-mismatch`
- Z 变体软件化（固件侧）：SESSION.md 2026-06-09 条目
