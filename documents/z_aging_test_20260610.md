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

## 6. GUI 集成 + 无限模式（2026-06-11）

### 6.1 命令行脚本无限模式

`z_aging_test.py --cycles 0` = **无限循环直到 Ctrl-C**，用于长期老化；中断/异常汇报累计完成圈数。

### 6.2 集成进 GUI 测试面板（IntegrationTestPanel）

把老化测试做成「集成测试」Tab 的一个**专属测试项**，长时序在后台 QThread 运行、不阻塞 UI：

- **`ZAgingWorker(QThread)`**（`software/common/gui/test_panel.py`）：单圈 HOME→正26→反25，循环
  N 圈。完成判定同脚本（预期耗时 sleep + 位置稳定 + 单步位移≈标称校验）。位置/状态读
  `main_window.axis_manager.get_axis_status("Z")`（GUI 持续接收的 24 字节广播包），命令走
  `serial_thread.send_binary_command`（线程安全）。固件配置已由 GUI 启动 `_configure_actuators`
  下发，worker 不重复下发。profile-safe（读 AXIS_CONFIG["Z"]，新旧 Z 自动适配）。
- **轮数输入框（默认 200）**放在**老化测试那一行的 Action 单元格**（`轮数:[200] [Run]`），
  明确是本测试专属参数，不放顶部全局栏以免误解。
- **Run/Stop 单项停止**：运行中按钮变**红色 Stop**，点击后 worker 走完当前步安全停下，
  汇报「已完成 N/M 圈」；Reset Results / 关窗 / 未连接 均安全处理（关窗 `closeEvent` stop+wait
  防悬挂线程）。该测试**排除在 Run All 批量之外**（长时序）。
- 实时进度（第几圈/第几步/Z 位置/Δ）显示在该行 Details；失步/卡住立即停并报第几圈第几步。
- 接线：`main_window` 创建面板后注入 `test_panel.main_window = self`，worker 据此发命令 + 读状态。
