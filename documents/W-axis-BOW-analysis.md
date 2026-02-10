# W 轴 S-ramp BOW 截断分析

**日期**: 2026-02-10
**分支**: develop
**问题**: W 轴电机运动时间锁死在 70ms，无论速度/加速度如何调整都不变

## 背景

W 轴 (FilterWheel) 换孔距离 12.5mm，优化目标 ≤60ms。
经过多轮测试，从基准 144ms 优化到 70ms (细分8) 后遇到瓶颈。

| 轮次 | 细分 | 速度(rev/s) | 加速度(rev/s²) | 电机时间 | 丢步 |
|------|------|------------|---------------|---------|------|
| 6 | 8 | 3.828 | 350 | 70ms | 无 |
| 7 | 8 | 3.828 | 370 | 70ms | 无 |
| 8 | 8 | 4.2 | 370 | 70ms | 无 |
| 9 | 8 | 4.2 | 400 | 70ms | 无 |

**现象**: 提高速度（3.828→4.2）和加速度（350→400）对电机时间完全无影响。

## 根因分析

### 1. 内部参数计算

W 轴配置 (microstepping=8, screwPitch=100mm, fullSteps=200):

```
stepsPerMM = 200 × 8 / 100 = 16 µsteps/mm
VMAX_reg   = 256 × 420 × 16 = 1,720,320
AMAX_reg   = 4 × 40,000 × 16 = 2,560,000
```

物理单位:
- 速度 v = 420 mm/s = 6,720 µsteps/s
- 加速度 a = 40,000 mm/s² = 640,000 µsteps/s²

### 2. BOW 计算与截断

`motor_adjustBows()` 使用公式 `BOW = AMAX² / VMAX`:

```
BOW_ideal  = 640,000² / 6,720 = 60,952,381
BOWMAX     = 2²⁴ - 1 = 16,777,215
BOW_actual = 16,777,215 (截断 3.6 倍)
```

BOW 寄存器是 24 位无符号整数，最大值 16,777,215。
计算值 60,952,381 远超最大值，被截断到 BOWMAX。

### 3. 核心发现：AMAX 永远达不到

在 S-ramp 加速段，加速度以 BOW (jerk) 速率从 0 线性增加，同时速度以二次方增长:

```
a(t) = BOW × t
v(t) = BOW × t² / 2
```

速度达到 VMAX 时的时间:

```
t_acc = √(2 × VMAX / BOW)
      = √(2 × 6,720 / 16,777,215)
      = 28.3ms
```

此时加速度为:

```
a(t_acc) = BOW × t_acc
         = 16,777,215 × 0.02831
         = 475,086 µsteps/s²
```

**475,086 < AMAX(640,000)**，所以 AMAX 从未被达到！
速度先达到了 VMAX，整个加速段都是纯 jerk-limited。
这就是为什么改 a=370 和 a=400 结果完全一样。

### 4. 理论运动时间验证

| 阶段 | 计算公式 | 时间 | 距离 |
|------|---------|------|------|
| 加速 (jerk-limited) | t = √(2×VMAX/BOW) | 28.3ms | 63.5 µsteps |
| 匀速 (VMAX) | t = d_remaining/VMAX | 10.9ms | 73 µsteps |
| 减速 (jerk-limited) | 同加速 | 28.3ms | 63.5 µsteps |
| **总计** | | **67.5ms** | **200 µsteps** |

理论 67.5ms vs 实测 70ms，吻合良好（差异来自轮询延迟和启停开销）。

## 解决方案

### 方案 A: 梯形斜坡 — 失败

将 RAMPMODE 从 S-shaped 改为 Trapezoidal，理论时间 ~40ms。

梯形斜坡理论时间:

```
t_acc   = VMAX / AMAX = 6,720 / 640,000 = 10.5ms
d_acc   = VMAX² / (2 × AMAX) = 35.3 µsteps
d_total_acc = 35.3 × 2 = 70.6 µsteps (加速+减速)
d_const = 200 - 70.6 = 129.4 µsteps
t_const = 129.4 / 6,720 = 19.3ms
t_total = 10.5 + 19.3 + 10.5 = 40.3ms
```

**结果**: 丢步（电机中频共振），降低加速度到 200 rev/s² 仍然丢步。
梯形斜坡的加速度阶跃变化激发了电机共振，方案放弃。

### 方案 B: S-ramp + ASTART/DFINAL — 成功

保持 S-ramp 平滑性，使用 TMC4361A 的 ASTART 功能跳过零加速度启动阶段。

**原理**: ASTART 设置斜坡起始加速度，电机不再从 a=0 开始，而是从 a=ASTART 开始，
跳过 jerk-limited 阶段的低加速度部分，缩短总运动时间。

**实现**:
- `GENERAL_CONF` 使能 `USE_ASTART_AND_VSTART` (bit 0)
- `ASTART` = 150 rev/s² 对应的内部值
- `DFINAL` = 同 ASTART（减速末段加速度）

**关键修复**: sRampInit (motor_moveToMicrosteps 中) 原来无条件清除 USE_ASTART_AND_VSTART，
导致 homing 后 ASTART 失效。修改为根据 `motorParams[icID].astart > 0` 保留使能。

### 方案 B 附加修复: Homing 完成竞态条件

**问题**: homing 完成时 `restoreNormalMicrosteps()` 写 VMAX 到硬件，但 RAMPMODE 仍在速度模式。
单纯交换调用顺序也不行（motor_setCurrentPositionMicrosteps 设 velocity_mode=true，
后续写 VMAX 导致电机持续旋转）。

**修复**: 三步序列确保安全
```cpp
motor_setCurrentPositionMicrosteps(_icID, 0);  // VMAX=0 停车，设零
motor_moveToMicrosteps(_icID, 0);              // 触发 sRampInit 切回位置模式
restoreNormalMicrosteps();                      // 安全恢复细分和 VMAX/AMAX
```

### 测试结果

| 阶段 | 方案 | motor 时间 | 丢步 |
|------|------|-----------|------|
| 基准 (S-ramp, BOW 截断) | — | 70ms | 无 |
| ASTART=150, AMAX=200 (遗留) | B | 64ms | 无 |
| ASTART=150, AMAX=400 (修正) | B | **62ms** | 无 |

最终: **70ms → 62ms (-11%)**，24 次 12.5mm 移动全部 err=0。

## 参考

- TMC4361A S-ramp 6 阶段: BOW1(jerk↑) → AMAX(匀加速) → BOW2(jerk↓) → VMAX(匀速) → BOW3(jerk↓) → DMAX(匀减速) → BOW4(jerk↑)
- TMC4361A RAMPMODE 寄存器: bit[1:0] = 01(梯形) / 10(S形), bit[2] = 1(位置模式)
- BOW 寄存器: 24 位无符号, 最大值 16,777,215
- ASTART/DFINAL: USE_ASTART_AND_VSTART (GENERAL_CONF bit 0) 使能
- `motor_adjustBows()`: MotorControl.cpp:42-89
