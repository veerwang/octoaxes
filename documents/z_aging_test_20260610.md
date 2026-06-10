# 新 Z 轴老化测试 + MOVETO 高速失步定位（2026-06-10）

## 1. 背景与目标

为新 Z（MOONS' LE143S-W0601，导程 1mm，TMC2240，竖直安装）建立**老化（burn-in）测试**，
并在过程中定位「单发 MOVETO 大移动只动一次就停」的根因。

测试脚本：`software/common/tests/z_aging_test.py`（profile-safe，默认 octoaxes）。

## 2. 最终测试用例（用户定义）

单圈逻辑：

1. **HOME** Z 回零 → 等待 0.5s
2. **正方向点动** ×26：相对 +1000µm（GUI 正方向）→ 每步到位后停顿 0.5s（到 ≈+26mm）
3. **反方向点动** ×25：相对 -1000µm → 每步停顿 0.5s（回 ≈+1mm）

循环 N 圈（每圈重新 HOME，净位移不累积）。参数可调：
`--cycles --step-um --fwd --bwd --dwell --expect-vel`。

坐标系与 GUI `move_axis` 完全一致：正方向 1000µm = 固件 **-51200 微步**（Z movement_sign=-1）。
启动自带下发（镜像 `main_window._configure_actuators`，仅 Z）：
SET_LEAD_SCREW_PITCH(23) + CONFIGURE_STEPPER_DRIVER(21, 细分256) + SET_LIM(9) +
SET_LIM_SWITCH_POLARITY(20) + S:SET_HOMING_VEL，保证细分/导程/限位与 GUI 一致。

## 3. 关键发现

### 3.1 「MOVETO 只动一次就停」的根因 = 高速失步（非位置限位）

最初版本用单发 MOVETO 直接去 27mm（90% 上限），**电机停在 ≈19.8mm**，dump：

```
XACTUAL=-1013510  XTARGET=-1382400  VACTUAL=0  VMAX=0  state=6(STATE_ERROR)
软限位 VSTOP_L=-1536000 / VSTOP_R=5120 → 停点在软限位内、未撞软限位
STOPL/STOPR 当时均不 active → 未撞物理限位
```

因果链（逐行核对固件 `axis.cpp`）：

1. 完成判定靠芯片 `TARGET_REACHED` 位（`checkMovementComplete`：XACTUAL==XTARGET）。
2. 大移动加速到巡航速度后在 ≈19.8mm **失步**，chip 停住（VMAX=0），XACTUAL 永远到不了 XTARGET。
3. `TARGET_REACHED` 不置位 → `STATE_MOVING` 等不到完成 → `MOVEMENT_TIMEOUT` → `handleError`
   → 进 **STATE_ERROR（state=6）**。表现为「动一次就僵住」。

固件状态枚举：`0=IDLE, 1-4=homing, 5=MOVING, 6=ERROR`。

### 3.2 点动能完美越过 19.8mm → 排除机械紧点

改用 1mm 点动序列后，**26 步正向爬到 26mm、25 步反向回到 1mm，全程 51 步每步精确
51200 微步、~0.72s，顺利越过 19.8mm，无任何失步/卡顿**。

→ 该位置**没有机械紧点/挡块**。坐实 3.1 的结论：MOVETO 失败是**纯高速失步**——
大移动维持高巡航速度时竖直 Z 转矩不足；1mm 点动峰值速度低（三角速度曲线、never 达 VMAX），
所以每次都能到位、可无限连续点动（与 GUI 手动点动正常一致）。

### 3.3 脚本检测 bug（已修）

调试中发现脚本「只识别到一步」是**检测逻辑**问题，非硬件：
旧版用 `VACTUAL==0` 轮询判停，但 `read_regs`（z_homing_safedist）带重试、单次可能耗
0.5~1.5s，在 deadline 内确认不了停车 → 误判超时。
**修复**：改为「按单步预期耗时 sleep 让 chip 走完 + 读位置判稳定（连续两次 XACTUAL 相同=停）」，
不依赖易丢的 VACTUAL。每步稳定 0.72s 识别。

预期耗时 = 距离/速度 + 加减速余量；deadline = 预期×factor + margin（默认 ≈1.95s），
超时立即 dump 诊断（XACTUAL/VACTUAL/STATUS），失步在 ~2s 内快速暴露，不再干等 15s。

### 3.4 小观察（不阻塞）

GUI **+21mm 以上** STATUS 多出 bit14（`0x01001801→0x01005801`），回到 20mm 以下消失。
接近行程远端的某状态标志，不影响运动，记录备查。

## 4. 结论与治本方向

- **老化测试**：点动序列方案验证可用，1 圈（26+25 步）端到端通过。可 `--cycles N` 跑长期。
- **正常大移动**：若要单发 MOVETO 到 20mm 以上不失步，需**降低 Z 巡航速度/加速度**
  （当前 `default_velocity=3mm/s` 偏快），留足转矩余量。后续可验证「降速后单发 MOVETO 26mm」。
- 硬件无 19.8mm 机械问题；行程远端真实上限位仍建议在该板用 `z_find_upper_limit.py` 实测后
  收敛 `limits` 上限（当前新 Z `limits=(-100,30000)` 沿用 octoaxesplus 借板值）。

## 5. 环境备注

测试时连接固件 **VERSION 106**（旧），X/Y/Z/W 全 TMC2240。本次结论（点动可行、MOVETO 高速失步）
在该固件下成立；换最新主线固件（118+）后建议复测一遍以确认无差异。
