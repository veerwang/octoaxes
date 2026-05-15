# octoaxesplus 启用 W1 / W2 轴实施方案

> **日期**：2026-05-15
> **分支**：maxpro
> **状态**：实施方案（pending 实现）
> **依赖**：`documents/octoaxesplus_axis_definitions.md`（现状梳理基线）

---

## 目标

在已工作的 X/Y/Z 三轴基础上，启用 **W1 / W2** 两个滤光转盘轴，达成
**octoaxesplus 5 轴运行模式**。

| 上位机 axisName | firmware icID | firmware axisName | 类型 | HC154 通道 | 占用原资源 |
|---|---|---|---|---|---|
| Y | 0 | "Y" | StepAxis | 9 | — |
| X | 1 | "X" | StepAxis | 10 | — |
| Z | 2 | "Z" | StepAxis | 8 | — |
| **W1** | **3** | **"W1"** | **FilterWheel** | **6** | **原 Z2 的 CS（HC154 ch6）** |
| **W2** | **4** | **"W2"** | **FilterWheel** | **4** | **原 T 的 CS（HC154 ch4）** |

---

## 改动前后对比

### tmc_ic_configs[] (TMC_SPI.cpp)

| icID | 改动前 (XYZ 调试模式) | 改动后 (XYZ + W1 + W2) |
|---|---|---|
| 0 | HC154_AXIS_Y (ch9) | HC154_AXIS_Y (ch9) ✓ 不变 |
| 1 | HC154_AXIS_X (ch10) | HC154_AXIS_X (ch10) ✓ 不变 |
| 2 | HC154_AXIS_Z1 (ch8) | HC154_AXIS_Z1 (ch8) ✓ 不变 |
| 3 | HC154_AXIS_F1 (ch7) | **HC154_AXIS_W1 (ch6)** ⚠ 改 |
| 4 | HC154_AXIS_Z2 (ch6) | **HC154_AXIS_W2 (ch4)** ⚠ 改 |
| 5 | HC154_AXIS_F2 (ch5) | （保留占位，未实例化） |
| 6 | HC154_AXIS_R (ch3) | （保留占位） |
| 7 | HC154_AXIS_T (ch4) | （保留占位） |

`TMC4361A_IC_COUNT` 保持 8（未来扩展轴预留）。

### Axis 实例化 (octoaxesplus.ino)

```cpp
// 改动后
Axis *yAxis  = new StepAxis    (Pins::Y_AXIS_CS,  0, "Y");
Axis *xAxis  = new StepAxis    (Pins::X_AXIS_CS,  1, "X");
Axis *zAxis  = new StepAxis    (Pins::Z_AXIS_CS,  2, "Z");
Axis *w1Axis = new FilterWheel (Pins::W1_AXIS_CS, 3, "W1");  // 新增
Axis *w2Axis = new FilterWheel (Pins::W2_AXIS_CS, 4, "W2");  // 新增

axisManager.addAxis(yAxis);
axisManager.addAxis(xAxis);
axisManager.addAxis(zAxis);
axisManager.addAxis(w1Axis);   // 新增
axisManager.addAxis(w2Axis);   // 新增
```

### 上位机 constants.py AXIS_CONFIG

| axisName | enabled_for 改动 | 备注 |
|---|---|---|
| X / Y | 不变（已共享） | — |
| Z | `["octoaxes"]` → `["octoaxes", "octoaxesplus"]` | 共享 |
| W | `["octoaxes"]` 不变 | octoaxes 专用 |
| E1 / E3 / E4 | `["octoaxes"]` 不变 | octoaxes 专用 |
| **Z1 / F1 / Z2 / F2 / R / T** | **整条删除** | 此次方案不用 |
| **W1** | **新增 `["octoaxesplus"]`** | filterwheel, index=3 |
| **W2** | **新增 `["octoaxesplus"]`** | filterwheel, index=4 |

---

## 6 层修改清单

按 `octoaxesplus_axis_definitions.md` 的"跨层一致性约束 cheatsheet"逐层覆盖：

### 1. config.h: HC154_Channel 枚举 + Pins::*_AXIS_CS 别名

**`firmware/octoaxesplus/config.h:155-172`**（HC154_Channel 枚举）：
- 加 `HC154_AXIS_W1 = 6`（与 `HC154_AXIS_Z2` 同值，作为别名）
- 加 `HC154_AXIS_W2 = 4`（与 `HC154_AXIS_T` 同值，作为别名）
- C++ 允许枚举中同值多名

**`firmware/octoaxesplus/config.h:70-79`**（Pins:: 命名空间）：
- 加 `const int W1_AXIS_CS = 6;`
- 加 `const int W2_AXIS_CS = 4;`
- 保留 `Z2_AXIS_CS` / `T_AXIS_CS`（注释为 deprecated，方便未来恢复 Z2/T 用途）

### 2. TMC_SPI.cpp: tmc_ic_configs[]

**`firmware/octoaxesplus/tmc/hal/TMC_SPI.cpp:51-73`**：
- `icID=3` 项：从 `HC154_AXIS_F1` (ch7) 改成 `HC154_AXIS_W1` (ch6)
- `icID=4` 项：从 `HC154_AXIS_Z2` (ch6) 改成 `HC154_AXIS_W2` (ch4)
- icID=5/6/7 保留占位（unused，TMC4361A_IC_COUNT 不变）
- 注释更新反映 W1/W2 用途

### 3. config.h: AxisConfig 模板

**`firmware/octoaxesplus/config.h:580-590` 附近**：
- 加 `const Axis::AxisConfig W1_AXIS = W_AXIS;`（const struct copy from W_AXIS 滤光转盘默认）
- 加 `const Axis::AxisConfig W2_AXIS = W_AXIS;`
- 保留 Z2/F2/R/T AxisConfig 别名（未实例化也无副作用）

### 4. octoaxesplus.ino: Axis 实例化 + addAxis

**`firmware/octoaxesplus/octoaxesplus.ino:88-118`**：
- 加 `Axis *w1Axis = new FilterWheel(Pins::W1_AXIS_CS, 3, "W1");`
- 加 `Axis *w2Axis = new FilterWheel(Pins::W2_AXIS_CS, 4, "W2");`
- 加 `addAxis(w1Axis)` + `addAxis(w2Axis)`
- 更新模式注释：从 "XYZ 三轴调试" → "XYZ + W1 + W2 五轴模式"
- F1/Z2/F2/R/T 注释行可清理（避免与新方案混淆）

### 5. axesmrg.cpp: beginAll axisName 映射

**`firmware/octoaxesplus/axesmrg.cpp:56-82`**：
- 加 `else if (axisName.equals("W1")) success = axes[i]->begin(W1_AXIS);`
- 加 `else if (axisName.equals("W2")) success = axes[i]->begin(W2_AXIS);`
- 保留 "W"/"F1" 双名映射（octoaxes 主线兼容）

### 6. software/utils/constants.py: AXIS_CONFIG

**`software/utils/constants.py:27-228`**：
- Z 条目 `enabled_for` 改为 `["octoaxes", "octoaxesplus"]`
- 删除 Z1 / F1 / Z2 / F2 / R / T 6 个条目（不再有效）
- 加 W1 条目（filter_wheel, index=3, enabled_for=["octoaxesplus"]）
- 加 W2 条目（filter_wheel, index=4, enabled_for=["octoaxesplus"]）
- 更新顶部注释 + `axes_for_model` 文档

---

## 影响范围 + 风险评估

### 不影响

- **octoaxes 主线**：未触及 `firmware/octoaxes/` 任何文件，octoaxes 行为零变化
- **共享 axis.cpp / stepaxis.cpp / filterwheel.cpp / motor 子系统**：不动
- **已 commit 的 2 个 fix**（b7331fb / 28e8eee）：不冲突

### 影响

- octoaxesplus PyQt 上位机 GUI 渲染（如果按 octoaxesplus profile 显示轴控件）：
  - 5 轴显示（X/Y/Z/W1/W2）替代之前的 7 轴 stub（Z1/F1/Z2/F2/R/T）
  - 旧字符串 Z1/F1 等若有持久化配置（settings.json），可能需要清理

### 风险

- **W1/W2 硬件未独立验证**：W_AXIS 模板是 octoaxes W 轴的滤光转盘参数，
  实际 W1/W2 硬件电机型号未必相同，可能需要后续根据实测调整电流/微步/速度等
- **firmware/upper 协议字段是否需要扩展**：响应包 24 字节布局当前只承载 XYZW 4 轴位置，
  W1/W2 是否要占用 W 槽位（现 octoaxes-only），或者保留位上加新字段，待 GUI 集成时定

---

## 测试计划

1. **编译验证**：`pio run -e teensy41`，零错误零新警告
2. **boot 验证**：烧写后 S:VERSION 响应正常；S:HWINFO 应识别 5 个轴
   `Y/X/Z/W1/W2 都是 TMC4361A+TMC2240`（前提：W1/W2 驱动板已插上）
3. **S:SPITEST 0..4** 应返回 `VERSION_NO=0x00000002` 全部成功
4. **上位机 PyQt 启动**：5 轴控件显示正常，连接发位置上报包不卡
5. **W1 / W2 单步运动**：手动发命令转动滤光转盘验证方向 + 校准位置正确

---

## 待回滚的临时模式

实施完后这些"调试模式"标记应清理：
- `octoaxesplus.ino:87-95` 注释提到的"XYZ 三轴调试模式" 改成 "XYZ + W1 + W2 五轴模式"
- commit 1ce942a 的妥协（axisName "Z" 而非 "Z1"）：本方案下 axisName "Z" **保持不变** —— 因为新上位机方案也是 "Z" 不是 "Z1"，
  之前的妥协现在变成了正式选择
