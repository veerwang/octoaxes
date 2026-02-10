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

### 方案: 切换为梯形斜坡 (Trapezoidal Ramp)

将 W 轴 RAMPMODE 从 6 (S-shaped + Position) 改为 5 (Trapezoidal + Position)，
绕过 BOW 参数限制，直接使用 AMAX 控制加速度。

梯形斜坡理论时间:

```
t_acc   = VMAX / AMAX = 6,720 / 640,000 = 10.5ms
d_acc   = VMAX² / (2 × AMAX) = 35.3 µsteps
d_total_acc = 35.3 × 2 = 70.6 µsteps (加速+减速)
d_const = 200 - 70.6 = 129.4 µsteps
t_const = 129.4 / 6,720 = 19.3ms
t_total = 10.5 + 19.3 + 10.5 = 40.3ms
```

**预计 40ms 电机时间 + 8ms 通信开销 ≈ 48ms，达成 ≤60ms 目标。**

### 风险评估

- 梯形斜坡的加速度阶跃变化可能增加机械振动
- microstepping=8 提供阻尼效果，可减轻此影响
- 需要实测验证是否出现丢步

## 参考

- TMC4361A S-ramp 6 阶段: BOW1(jerk↑) → AMAX(匀加速) → BOW2(jerk↓) → VMAX(匀速) → BOW3(jerk↓) → DMAX(匀减速) → BOW4(jerk↑)
- TMC4361A RAMPMODE 寄存器: bit[1:0] = 01(梯形) / 10(S形), bit[2] = 1(位置模式)
- BOW 寄存器: 24 位无符号, 最大值 16,777,215
- `motor_adjustBows()`: MotorControl.cpp:42-89
