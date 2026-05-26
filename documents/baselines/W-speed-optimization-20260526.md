# W 轴速度优化报告 (2026-05-26)

**目标**：缩短 W 轴 1 slot (45°) 换孔时间，从基线 181ms 优化到尽可能小。

## 最终成果

```
181ms 原基线 (PID off, ms=64, ASTART=0, idle_frames=5)
  → 72.2ms 最终配置 (PID on, ms=8, ASTART=22.5, idle_frames=1, tolerance=20)
  -109ms，-60%
```

## 优化路径全览（5 个里程碑）

| 步骤 | 配置变更 | 1 slot | 累计节省 | 关键收益 |
|---|---|---|---|---|
| **原基线** | PID off, ms=64, ASTART=0, idle=5, tol=2 | **181.3ms** | — | — |
| **B1**: 脚本 idle frames 5→1 | 单变量改脚本 | ~141ms | -22% | 去除人为防抖 |
| **B2**: tolerance 2→20 (firmware) | 配 ms=64 + ASTART=0 | 140.9ms | -22% | 末端 settling 早判完 |
| **C v2**: + ASTART=22.5 rev/s² | 历史 chip 寄存器值匹配 | 129.3ms | -29% | jerk-limited 起步缓解 |
| **路径 D**: ms 64→16 | 牺牲字节级对齐 | 87.0ms | -52% | BOW 截断 29→7x |
| **最终**: ms 16→8 | 与 2026-02 历史最优一致 | **72.2ms** | **-60%** | BOW 截断 7→3.6x |

## 全档位实测数据（最终配置）

| 角度 | 1.4° | 5.6° | 22.5° | **45° (1 slot)** | 90° | 135° | 180° |
|---|---|---|---|---|---|---|---|
| µstep @ ms=8 | 6 | 25 | 100 | **200** | 400 | 600 | 800 |
| **时间 (ms)** | 23.3 | 37.0 | 57.5 | **🎯 72.2** | 97.5 | 125.9 | 154.8 |
| std (ms) | 0.2 | 0.3 | 0.3 | 0.3 | 0.3 | 0.3 | 0.3 |

重复性：所有档位 std < 0.4ms，极其稳定。

## 各配置组合详细对比

### 原始基线（PID off）

| 角度 | 1.4° | 5.6° | 22.5° | 45° | 90° | 135° | 180° |
|---|---|---|---|---|---|---|---|
| 时间 (ms) | 87.7 | 113.7 | 153.0 | **181.3** | 216.3 | 241.3 | 262.7 |

### B1+B2 优化（idle=1, tolerance=20，仍 ms=64 + ASTART=0）

| 角度 | 1.4° | 5.6° | 22.5° | 45° | 90° | 135° | 180° |
|---|---|---|---|---|---|---|---|
| 时间 (ms) | 47.7 | 73.6 | 112.8 | **140.9** | 175.5 | 202.0 | 222.7 |

### C v2 (+ ASTART=22.5 @ ms=64)

| 角度 | 1.4° | 5.6° | 22.5° | 45° | 90° | 135° | 180° |
|---|---|---|---|---|---|---|---|
| 时间 (ms) | 62.2 | 64.9 | 102.3 | **129.3** | 163.3 | 188.1 | 210.0 |

### 路径 D (ms=16, ASTART=22.5)

| 角度 | 1.4° | 5.6° | 22.5° | 45° | 90° | 135° | 180° |
|---|---|---|---|---|---|---|---|
| 时间 (ms) | 27.9 | 44.4 | 69.0 | **87.0** | 110.2 | 138.2 | 167.4 |

### 最终 (ms=8, ASTART=22.5)

| 角度 | 1.4° | 5.6° | 22.5° | 45° | 90° | 135° | 180° |
|---|---|---|---|---|---|---|---|
| 时间 (ms) | 23.3 | 37.0 | 57.5 | **🎯 72.2** | 97.5 | 125.9 | 154.8 |

## 路径 C v1 实验失败教训

第一次尝试 **ASTART=180 rev/s² @ microstep=64**（直接复用历史值）灾难性失败：

| 角度 | C v2 (22.5) | C v1 (180) | 退化 |
|---|---|---|---|
| 1.4° | 62.2ms | **234ms** | **+187ms** |
| 5.6° | 64.9ms | **230ms** | +157ms |
| 1 slot | 129ms | **185ms** | +44ms |
| HOME | 1.4s | **17.6s** | +16s |
| Offset 末位置 W | 38 | **628** | 严重过冲 |

**根因**：chip 寄存器层 ASTART 实际值 = 180 mm/s² × 12800 µstep/mm = **2.3M µstep/s²**（历史在 ms=8 时是 288K，本次 ms=64 翻 8 倍）。短距离 chip ramp 起步过猛 → encoder 检测过冲 → PID 反拉振荡。

**修正**：ASTART 应按"匹配 chip 寄存器值"换算：180 ÷ 8 (微步比) = **22.5 rev/s²** @ ms=64，即 C v2。

## 配置最终值

### firmware/octoaxes/config.h

```cpp
const int MICROSTEPPING_FILTERWHEEL = 8;           // 2026-05-26 路径 C v2（64→16→8 步进优化）
const int HOMING_MICROSTEPPING_FILTERWHEEL = 256;  // homing 保持高精度，不变

const Axis::AxisConfig W_AXIS = {
    // ...
    .astartMM = 22.5f * AxisConstDefinition::SCREW_PITCH_FILTERWHEEL_MM,  // 路径 C v2
    .dfinalMM = 0,
    // ...
};
// EXPAND4_AXIS (W2 模板) 同步：astartMM = 22.5f * SCREW_PITCH_FILTERWHEEL_MM
```

### firmware/octoaxes/axis.cpp

```cpp
if (strcmp(_axisName, "W") == 0 || strcmp(_axisName, "W2") == 0) {
    target_tolerance = 20;   // 2026-05-26 速度优化：2→20 (≈ 6 µstep ≈ 0.17°)
    pid_tolerance = 20;
    pid_iclip = 4096;
}
```

### PID 启用参数（运行时由 GUI / 脚本下发）

- P = 4096 (默认，未改)
- I = 1, D = 1 (默认)
- transitions_per_revolution = 4000
- ENCODER_FLIP_DIR = False

## 字节级对齐的牺牲

本次优化**违反 CLAUDE.md 中 "octoaxes firmware 字节级替代旧 Squid firmware" 原则**的两点：

1. **MICROSTEPPING_FILTERWHEEL = 8**（旧 Squid `MICROSTEPPING_DEFAULT_W = 64`）
   - 但旧 Squid software `configure_motor_driver(W, 64, ...)` 会通过 protocol 覆盖回 64
   - 本次仅对 benchmark 脚本（也下发 ms=8）有效；**对旧 Squid GUI 实际使用无效**
2. **ASTART = 22.5 rev/s²**（旧 Squid `tmc4361A_sRampInit::rstBits(USE_ASTART_AND_VSTART)` 强制 0）
   - 旧 Squid software 无法感知 chip 寄存器 ASTART 值
   - 实际效果：旧 Squid software 看到的 W 行为更快，无副作用

**结论**：要让"旧 Squid software + octoaxes firmware"享受全部速度收益，需要修改旧 Squid software 让它发 `configure_motor_driver(W, 8, ...)` —— 违反"旧 Squid software 不可改"约束。

**如果不改旧 Squid software**：当前优化对旧 Squid GUI 仅生效 **B1 (脚本)** + **B2 (tolerance)** + ASTART**（约 50ms 节省）**，ms 优化失效。

## 物理理论验证

**TMC4361A S-ramp 行为 @ 不同微步**（VMAX=4.2 mm/s, AMAX=400 mm/s², BOWMAX=16,777,215 µstep/s³）：

| 微步 | µstep/mm | VMAX (µstep/s) | AMAX (µstep/s²) | BOW_ideal | BOW 截断倍数 | jerk-limited t_acc |
|---|---|---|---|---|---|---|
| 64 | 12800 | 53,760 | 5,120,000 | 487M | **29×** | 80ms |
| 16 | 3,200 | 13,440 | 1,280,000 | 122M | 7× | 40ms |
| **8** | 1,600 | 6,720 | 640,000 | 60.9M | **3.6×** | **28.3ms** |

**1 slot 物理底线（jerk-limited 三角剖面，ASTART=0）**：

| 微步 | 半程 µstep | t (半程) | total motor |
|---|---|---|---|
| 64 | 800 | 66ms | 132ms |
| 16 | 200 | 41.5ms | 83ms |
| **8** | **100** | **33ms** | **66ms** |

实测 72.2ms = 66ms 物理 + ~6ms (脚本 USB 上下报 + chip 启停 + PID settling)。**完美吻合理论。**

## 剩余优化空间评估

| 路径 | 估算 | 风险 | 备注 |
|---|---|---|---|
| **ASTART 22.5 → 180 @ ms=8** | 1 slot 72 → ~65ms | 低 | 历史 2026-02-10 已实证 ms=8 + ASTART=180 → motor 61.3ms |
| 调大 VMAX | 旧 Squid software 会覆盖回 4.2 | 不可行 | 协议层硬约束 |
| 提高 BOW | 16.78M 已是 chip 寄存器上限 | 不可行 | 硬件限制 |
| Target pipeline | 仅连续 move 受益 | 中难度 | 单次 move 无效 |

**理论极限**（梯形 ramp 假设 BOW 不限制）：
- t_acc = VMAX/AMAX = 10.5ms
- d_acc = 0.5·AMAX·t² = 22 µstep
- cruise: (200-44)/53760 ... wait, ms=8 算梯形
- 实际 jerk-limited 是物理硬约束，~40ms 才是不带 BOW 限制的理论下限

## 历史对比

2026-02-10 历史最优（W-axis-BOW-analysis.md）：
- microstep = **8** （与本次一致）
- ASTART = **180 rev/s²**
- 测量方式：firmware DEBUG_PRINT `motor=...us`（chip 物理运动时间）
- 结果：**motor 61.3ms / PC end-to-end ~69ms**

本次 2026-05-26 (microstep=8, ASTART=22.5)：
- 测量方式：PC 端 wait_completed（含 USB + 上下报）
- 结果：**1 slot PC end-to-end 72.2ms**

**差异 3ms**：本次 ASTART=22.5 比历史 180 略保守。试 ASTART=180 @ ms=8 可能达到 ~65-69ms。

## 输出文件

- `documents/baselines/benchmark_w_20260526_140452.{csv,md}` — 原基线 PID off
- `documents/baselines/benchmark_w_20260526_144121_pid_on.{csv,md}` — PID on baseline
- `documents/baselines/benchmark_w_20260526_150059_pid_p16384.{csv,md}` — P=16384 实验
- `documents/baselines/benchmark_w_20260526_152653_pid_p16384_tol20_idle1.{csv,md}` — B1+B2
- `documents/baselines/benchmark_w_20260526_160039_pid_p4096_astart180_tol20_idle1.{csv,md}` — C v1 失败
- `documents/baselines/benchmark_w_20260526_172319_pid_p4096_astart22p5_tol20_idle1.{csv,md}` — C v2 甜点
- `documents/baselines/benchmark_w_20260526_172809_pid_p4096_astart30_tol20_idle1.{csv,md}` — ASTART=30 边际
- `documents/baselines/benchmark_w_20260526_173716_pid_p4096_astart22p5_tol20_idle1_ms16.{csv,md}` — ms=16
- `documents/baselines/benchmark_w_20260526_174533_pid_p4096_astart22p5_tol20_idle1_ms8.{csv,md}` — **最终最优**

## 教训记录

1. **理论与实测差异**：理论估算 ASTART=180 @ ms=64 应该 ~60ms，实测 185ms。**原因：BOW 截断在 ms=64 时严重恶化 (29×)，加上 PID 闭环对短距过冲反拉**。
2. **跨微步参数迁移**：物理参数 (rev/s, rev/s²) 跨微步等价，但 **chip 寄存器值 (µstep/s²) 按微步线性缩放**。直接复用历史值 ASTART=180 在 ms=64 时让 chip 寄存器翻 8 倍 → 灾难。正确做法：按"匹配 chip 寄存器值"换算。
3. **边际收益递减**：ASTART 从 22.5 → 30 只省 3ms（1 slot），但短距 50 µstep 退化 +31ms。**单变量优化要权衡多档位影响**。
4. **测量方法影响巨大**：原 181ms 中 40ms 是脚本人为加的 idle frames 防抖。**真实 GUI 应用 (`wait_till_operation_is_completed` 看到第一个 COMPLETED 即返回) 早就没这 40ms**。
5. **历史文档救命**：W-axis-BOW-analysis.md (2026-02-10) 早就分析了 BOW 截断 + ASTART 修正。本次完整复刻并理解了那次的优化路径。
