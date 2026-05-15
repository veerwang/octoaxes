# octoaxesplus 轴定义梳理（当前状态）

> **更新日期**：2026-05-15
> **分支**：maxpro
> **目的**：调整硬件资源 / 轴定义前的现状盘点，便于规划改动

octoaxesplus（squid++ 双相机变体）轴定义分布在 6 个层次，从硬件物理映射到上位机
GUI profile 一路传递。本文档汇总当前各层定义，作为后续调整的基线。

---

## 1. 硬件物理映射 —— HC154 4→16 通道分配

定义位置：`firmware/octoaxesplus/config.h:155-172`（`HC154_Channel` 枚举）+
`config.h:70-79`（`Pins::*_AXIS_CS` 别名）。

| HC154 通道 | 枚举名 | 用途 | `Pins::*` CS 别名 |
|---|---|---|---|
| 0 | `HC154_MCP23S17_1` | 扩展 IO（8 轴 INTR/TARGET 输入） | — |
| 1 | `HC154_DAC80508_2` | 备用 DAC | — |
| 2 | `HC154_DAC80508_1` | 8LED 模拟输出 | `DAC8050x_CS` |
| **3** | **`HC154_AXIS_R`** | **物镜转换器 旋转** | `R_AXIS_CS` |
| **4** | **`HC154_AXIS_T`** | **物镜转换器 平移** | `T_AXIS_CS` |
| **5** | **`HC154_AXIS_F2`** | **双滤光轮 F2** | `F2_AXIS_CS` |
| **6** | **`HC154_AXIS_Z2`** | **双焦点 Z2** | `Z2_AXIS_CS` |
| **7** | **`HC154_AXIS_F1`** | **滤光转盘 F1** | `W_AXIS_CS` *（沿用 W 别名）* |
| **8** | **`HC154_AXIS_Z1`** | **主焦点 Z1** | `Z_AXIS_CS` *（沿用 Z 别名）* |
| **9** | **`HC154_AXIS_Y`** | **物理 Y 电机** | `Y_AXIS_CS` |
| **10** | **`HC154_AXIS_X`** | **物理 X 电机** | `X_AXIS_CS` |
| 11 | `HC154_EXPAND_NSCS1` | SPI 空闲归零位 | — |
| 12 | `HC154_DAC80508_4` | 备用 DAC | — |
| 13-15 | `HC154_MCP23S17_2/3/4` | 备用扩展 IO | — |

**Pins::*\_AXIS_CS 字面值**（实际就是 HC154 通道号 0-15，**不是** Teensy 物理 pin）：

```cpp
const int X_AXIS_CS  = 10;   // HC154 Y10
const int Y_AXIS_CS  = 9;    // HC154 Y9
const int Z_AXIS_CS  = 8;    // HC154 Y8 (= Z1)
const int W_AXIS_CS  = 7;    // HC154 Y7 (= F1)，沿用 W 别名占位
const int Z2_AXIS_CS = 6;    // HC154 Y6
const int F2_AXIS_CS = 5;    // HC154 Y5
const int R_AXIS_CS  = 3;    // HC154 Y3
const int T_AXIS_CS  = 4;    // HC154 Y4
```

---

## 2. tmc_ic_configs[] —— icID → HC154 通道映射

定义位置：`firmware/octoaxesplus/tmc/hal/TMC_SPI.cpp:51-73`
（条件编译 `#ifdef USE_HC154_CS` 分支）。

| icID | HC154 通道 | 标识 | 时钟源 |
|---|---|---|---|
| 0 | `HC154_AXIS_Y` = 9 | Y | `CLOCK_STANDARD` (pin 37) |
| 1 | `HC154_AXIS_X` = 10 | X | `CLOCK_STANDARD` |
| 2 | `HC154_AXIS_Z1` = 8 | Z1（主焦点） | `CLOCK_STANDARD` |
| 3 | `HC154_AXIS_F1` = 7 | F1（滤光转盘 1） | `CLOCK_STANDARD` |
| 4 | `HC154_AXIS_Z2` = 6 | Z2（双焦点） | `CLOCK_STANDARD` |
| 5 | `HC154_AXIS_F2` = 5 | F2（滤光转盘 2） | `CLOCK_STANDARD` |
| 6 | `HC154_AXIS_R` = 3 | R（物镜旋转） | `CLOCK_STANDARD` |
| 7 | `HC154_AXIS_T` = 4 | T（物镜平移） | `CLOCK_STANDARD` |

**约束**：
- `TMC4361A_IC_COUNT` 在 `TMC_SPI.h:27` 定义为 8（`USE_HC154_CS` 分支）
- 此数组顺序**必须**与 `octoaxesplus.ino::addAxis()` 调用顺序一一对应
- octoaxes 主线（同文件 `#else` 分支）只 7 项，icID 顺序也不同

---

## 3. AxisConfig 模板（config.h）

定义位置：`firmware/octoaxesplus/config.h:342-595`。

| 模板名 | 起始行 | 来源 | 默认推荐类型 | screwPitch (mm) | microstep | 电流 / 速度 |
|---|---|---|---|---|---|---|
| **`X_AXIS`** | 342 | **独立完整初始化** | StepAxis | 2.54 | 256 | 1000 mA / 25 mm/s |
| **`Y_AXIS`** | 382 | **独立完整初始化** | StepAxis | 2.54 | 256 | 1000 mA / 25 mm/s |
| **`Z_AXIS`** | 421 | **独立完整初始化** | StepAxis | 0.3 | 256 | 500 mA / 3 mm/s |
| **`W_AXIS`** | 459 | **独立完整初始化** | FilterWheel | 100 | 8 | 滤光转盘默认 |
| **`EXPAND1_AXIS`** | 497 | **独立完整初始化** | Objectives | 1.0 | 64 | 物镜默认 |
| `EXPAND3_AXIS` | 535 | **独立完整初始化** | StepAxis | 0.3 | 256 | 同 Z |
| `EXPAND4_AXIS` | 592 | **独立完整初始化** | FilterWheel | 100 | 8 | 滤光转盘默认 |
| **`Z2_AXIS`** | 580 | **`= Z_AXIS`**（const struct copy） | StepAxis | 同 Z | 同 Z | 待实测调整 |
| **`F2_AXIS`** | 583 | **`= W_AXIS`** | FilterWheel | 同 W | 同 W | 待实测调整 |
| **`R_AXIS`** | 586 | **`= EXPAND1_AXIS`** | Objectives | 1.0 | 64 | 待实测调整 |
| **`T_AXIS`** | 589 | **`= EXPAND1_AXIS`** | Objectives | 1.0 | 64 | 待实测调整 |

**关键 AxisConfig 字段**：

- `clockFrequency` — TMC4361A 外部时钟频率（16 MHz）
- `homingSwitch` — `LEFT_SW` 或 `RGHT_SW`
- `leftSwitchPolarity` / `rightSwitchPolarity` / `leftFlipped` / `rightFlipped`
- `enableLeftLimitSwitch` / `enableRightLimitSwitch`
- `r_sense` — 检流电阻（每类轴定义）
- `screwPitchMM` / `fullStepsPerRev` / `microstepping` / `homingMicrostepping`
- `maxVelocityMM` / `maxAccelerationMM` / `homingVelocityMM`
- `motorCurrentMA` / `holdCurrent` / `currentRange`
- `homeSafetyMarginMM` / `homeSafetyPositionMM`
- `enableStallSensitivity` / `stallSensitivity`（仅 TMC2660 SG2 用，TMC2240 跳过）
- `useSShapedRamp` / `astartMM` / `dfinalMM`
- `homing_timeout_ms` / `homing_direct`（+1 或 -1）
- `driverType` — `DRIVER_AUTO`（运行时检测 TMC2660 / TMC2240）
- `enableEncoder` / `encoderLinesPerRev` / `invertEncoderDir`

---

## 4. Axis 实例化（octoaxesplus.ino）

定义位置：`firmware/octoaxesplus/octoaxesplus.ino:88-118`。

**当前状态（XYZ 三轴调试模式，commit 1ce942a 起）**：

```cpp
// 启用的 3 轴
Axis *yAxis  = new StepAxis   (Pins::Y_AXIS_CS,  0, "Y");   // icID=0, HC154 ch9
Axis *xAxis  = new StepAxis   (Pins::X_AXIS_CS,  1, "X");   // icID=1, HC154 ch10
Axis *zAxis  = new StepAxis   (Pins::Z_AXIS_CS,  2, "Z");   // icID=2, HC154 ch8
                                                              // ⚠ axisName 是 "Z" 不是 "Z1"

// 注释掉的 5 轴（待 5 轴硬件接上后取消注释）
// Axis *f1Axis = new FilterWheel(Pins::W_AXIS_CS,  3, "F1");   // icID=3, HC154 ch7
// Axis *z2Axis = new StepAxis   (Pins::Z2_AXIS_CS, 4, "Z2");   // icID=4, HC154 ch6
// Axis *f2Axis = new FilterWheel(Pins::F2_AXIS_CS, 5, "F2");   // icID=5, HC154 ch5
// Axis *rAxis  = new Objectives (Pins::R_AXIS_CS,  6, "R");    // icID=6, HC154 ch3
// Axis *tAxis  = new Objectives (Pins::T_AXIS_CS,  7, "T");    // icID=7, HC154 ch4

// 添加到 AxisManager（顺序就是 icID）
if (!axisManager.addAxis(yAxis)  || !axisManager.addAxis(xAxis)  ||
    !axisManager.addAxis(zAxis)
    // || !axisManager.addAxis(f1Axis)
    // || !axisManager.addAxis(z2Axis) || !axisManager.addAxis(f2Axis)
    // || !axisManager.addAxis(rAxis)  || !axisManager.addAxis(tAxis)
    ) { ... }
```

**注意**：
- `StepAxis` / `FilterWheel` / `Objectives` 是三种 Axis 派生类，决定 begin/update/homing 行为
- `tmc_ic_configs[]` 数组保持 8 项 —— icID 3-7 槽位空置但不被访问，无副作用

---

## 5. axesmrg.cpp::beginAll axisName → AxisConfig 映射

定义位置：`firmware/octoaxesplus/axesmrg.cpp:56-82`。

`AxisManager::beginAll()` 遍历所有已添加的轴，按 `getAxisName()` 字符串选用对应
的 AxisConfig 模板：

| axisName 字符串 | 选用的 AxisConfig 模板 |
|---|---|
| `"X"` | `AxisConfigs::X_AXIS` |
| `"Y"` | `AxisConfigs::Y_AXIS` |
| **`"Z"` 或 `"Z1"`** | `AxisConfigs::Z_AXIS`（双名兼容） |
| **`"W"` 或 `"F1"`** | `AxisConfigs::W_AXIS`（双名兼容） |
| `"Z2"` | `AxisConfigs::Z2_AXIS` |
| `"F2"` | `AxisConfigs::F2_AXIS` |
| `"R"` | `AxisConfigs::R_AXIS` |
| `"T"` | `AxisConfigs::T_AXIS` |
| `"E1"` | `AxisConfigs::EXPAND1_AXIS` |
| `"E3"` | `AxisConfigs::EXPAND3_AXIS` |
| `"E4"` | `AxisConfigs::EXPAND4_AXIS` |
| 其他 | unknown，axis begin 失败 |

---

## 6. 上位机 constants.py 轴定义（GUI / 协议层）

定义位置：`software/utils/constants.py:27-228`。

**`axes_for_model("octoaxesplus")` 返回 8 个激活轴**（按 firmware icID 排序）：

| 上位机 axisName | `index` (= firmware icID) | `type` | 是否共享 octoaxes |
|---|---|---|---|
| **X** | 1 | step_motor | ✓ shared |
| **Y** | 0 | step_motor | ✓ shared |
| **Z1** | 2 | step_motor | ⚠ 与 octoaxes Z 同 firmware icID（互斥 `enabled_for` 保证只激活一组） |
| **F1** | 3 | filter_wheel | ⚠ 与 octoaxes W 同 firmware icID |
| **Z2** | 4 | step_motor | octoaxesplus 新增 |
| **F2** | 5 | filter_wheel | octoaxesplus 新增 |
| **R** | 6 | objective | octoaxesplus 新增 |
| **T** | 7 | objective | octoaxesplus 新增 |

**关键 AXIS_CONFIG 字段**（每个轴 dict）：

- `display_name` — GUI 显示标签
- `type` — `step_motor` / `filter_wheel` / `objective`
- `has_limits` / `limits` — 软限位（μm 或 wheel position）
- `movement_sign` — `+1` 或 `-1`（GUI ↔ firmware 方向转换）
- `index` — firmware icID（与 firmware addAxis 顺序对应）
- `default_velocity` / `default_acceleration` — 启动序列默认值
- `has_encoder` / `encoder_transitions_per_rev` / `encoder_flip_direction`
- `actuator_screw_pitch_mm` / `actuator_microstepping` —
  **来源唯一**，`AXIS_MM_PER_STEP` 从这两个字段派生
- `actuator_motor_current_ma` / `actuator_motor_hold_ratio`
- `enabled_for` — `["octoaxes"]` / `["octoaxesplus"]` / 共享时两个都填

---

## ⚠ 当前已知不一致点（XYZ 调试模式遗留）

1. **firmware axisName `"Z"` vs 上位机 `"Z1"`**
   - 现状：commit 1ce942a 把 firmware 内 axisName 改为 "Z" 以兼容 `commandprocessor.cpp::findAxisByName("Z")` 硬编码
   - 上位机 `axes_for_model("octoaxesplus")` 仍返回 "Z1"
   - 风险：上位机如果按 "Z1" 发命令，`findAxisByName("Z1")` 找不到（firmware 注册的是 "Z"）
   - 当前能跑：上位机 PyQt 测试发的可能仍是 "Z" 命令（旧兼容路径），具体取决于 GUI 是否启用 octoaxesplus profile

2. **5 轴（F1/Z2/F2/R/T）实例化全注释**
   - `octoaxesplus.ino:92-96` + `:103-105` 全部注释掉
   - `tmc_ic_configs[]` 仍保留 8 项，icID 3-7 槽位空置但不被 SPI 访问
   - 上位机 GUI 如果按 octoaxesplus profile 显示 8 轴控件，会无法控制（firmware 拒绝命令）

3. **Z2/F2/R/T AxisConfig 是 const struct copy 不是独立 init**
   - `config.h:580-589` 用 `= Z_AXIS` / `= W_AXIS` / `= EXPAND1_AXIS` 隐式 copy ctor
   - 硬件实测后需要把 `=` 改成完整初始化器调单独参数
   - 例如 R/T 物镜的微步、电流可能跟 EXPAND1 默认不一样

4. **W_AXIS_CS 是 F1 用的别名**
   - `config.h:73` 注释明确 `W_AXIS_CS = 7` = HC154 通道 7 = AXIS_F1
   - 与 octoaxes 主线的 W 轴（pin 34 物理 GPIO CS）含义完全不同
   - 沿用 W_AXIS_CS 名称是为了 `Axis::new FilterWheel(Pins::W_AXIS_CS, 3, "F1")` 让 W_AXIS AxisConfig 复用机制无缝

---

## 跨层一致性约束 cheatsheet

任何轴定义改动，**必须同步以下 4 个位置**（顺序不限但全部要覆盖）：

| 层 | 文件 | 改什么 |
|---|---|---|
| 1. HC154 通道映射 | `firmware/octoaxesplus/config.h:155-191` | `HC154_Channel` 枚举 + `Pins::*_AXIS_CS` 别名 |
| 2. icID → 通道 | `firmware/octoaxesplus/tmc/hal/TMC_SPI.cpp:51-73` | `tmc_ic_configs[]` |
| 3. AxisConfig 模板 | `firmware/octoaxesplus/config.h:342-595` | `AxisConfigs::*_AXIS` const struct |
| 4. Axis 实例化 + addAxis 顺序 | `firmware/octoaxesplus/octoaxesplus.ino:88-118` | `new StepAxis/FilterWheel/Objectives(...)` + `addAxis(...)` |
| 5. axisName → 模板 | `firmware/octoaxesplus/axesmrg.cpp:56-82` | `beginAll()` 内的 if/else 分支 |
| 6. 上位机 profile | `software/utils/constants.py:27-228` | `AXIS_CONFIG` dict + `enabled_for` 列表 |

---

## 参考 commit

- `1ce942a` debug(octoaxesplus): 切到 XYZ 三轴调试模式（2026-05-13）
- `64fa643` feat(octoaxesplus): 8 轴 AxisConfig 扩展 + Axis 实例化（Z2/F2/R/T）（2026-05-13）
- `e9fd888` feat(constants): Phase 3.1 - AXIS_CONFIG 8 轴扩展 + enabled_for 标记（2026-05-13）
- `d616e1a` chore(octoaxesplus): 硬件资源使用率审计 + CAM_TRI_READY 补齐 + 删 EXPAND CS 别名（2026-05-13）
- `b7331fb` fix(axis): 修复 Axis::begin 两个隐患（csPin 双义性 + 返回值未检查）（2026-05-14）
- `28e8eee` fix(setup): beginAll() 部分失败不再卡死 firmware，便于 bring-up 调试（2026-05-14）
- `7512a71` docs+debug: 2026-05-14 IC4 虚焊定位记录 + S:SPITEST 寄存器修复 + 3 个 bring-up 调试命令（2026-05-14）
