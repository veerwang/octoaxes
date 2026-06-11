#!/usr/bin/env python3
"""Z 轴老化（burn-in）测试 (2026-06-10)

单圈逻辑（用户定义 2026-06-10 改版）：
  1. HOME Z（回零）→ 等待 0.5s（= --dwell）再开始移动
  2. 正方向点动：相对 +1000µm（GUI 正方向）→ 停顿 0.5s，重复 26 次（到 ≈+26mm）
  3. 反方向点动：相对 -1000µm（GUI 反方向）→ 停顿 0.5s，重复 25 次（回 ≈+1mm）
循环 N 圈（默认 1，先验证逻辑）。每圈开头都重新 HOME，避免净位移累积。

为什么用相对点动而不是单发 MOVETO：
  实测单发 MOVETO 到 20mm 以上会在 ~19.8mm 因高速失步停住（chip VMAX=0），
  而 1mm 短点动峰值速度低、never 触发失步，可逐段爬过。本用例即复现可行的点动序列。

坐标系与 GUI 完全一致（move_axis）：
  · 正方向(Forward) 距离 d µm → value = movement_sign × (+d) → microsteps=int(value/1000/mm_per_step)
    Z movement_sign=-1 → 正方向 1000µm = 固件 -51200 微步（朝 GUI 正/上）。
  · 反方向(Backward) = movement_sign × (-d)。
  · 启动先自带下发（镜像 main_window._configure_actuators，仅 Z）：
    SET_LEAD_SCREW_PITCH(23)+CONFIGURE_STEPPER_DRIVER(21,细分256)+SET_LIM(9)
    +SET_LIM_SWITCH_POLARITY(20)+S:SET_HOMING_VEL，保证细分/导程/限位一致。

⚠️ 安全：直接驱动竖直 Z 真实电机。运行前确认行程内无障碍、急停可达、Ctrl-C 可中断。

用法：
  python3 software/common/tests/z_aging_test.py                       # 默认 1 圈
  python3 software/common/tests/z_aging_test.py --cycles 100           # 100 圈老化
  python3 software/common/tests/z_aging_test.py --cycles 0             # 无限循环到 Ctrl-C
  python3 software/common/tests/z_aging_test.py --step-um 1000 --fwd 26 --bwd 25 --dwell 0.5
  python3 software/common/tests/z_aging_test.py --dry-run              # 只打印参数，不连硬件
"""

import argparse
import os
import sys
import time

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(THIS_DIR, "..", "..", ".."))


def load_profile_constants(profile):
    """按 profile 加载 constants（与 main.py 同款 sys.path 注入）。"""
    common = os.path.join(REPO, "software", "common")
    prof = os.path.join(REPO, "software", profile)
    for p in (common, prof):
        if p not in sys.path:
            sys.path.insert(0, p)
    import constants  # noqa: E402
    return constants


# 协议常量（与 software/common/define.py 一致）
AXIS_Z = 2
CMD_MOVE_Z = 2                      # 相对移动
CMD_HOME_OR_ZERO = 5
CMD_SET_LIM = 9
CMD_CONFIGURE_STEPPER_DRIVER = 21
CMD_SET_LEAD_SCREW_PITCH = 23
CMD_SET_LIM_SWITCH_POLARITY = 20
LIM_CODE_Z_POSITIVE = 4
LIM_CODE_Z_NEGATIVE = 5
FULLSTEPS_PER_REV = 200


def main():
    ap = argparse.ArgumentParser(description="Z 轴老化测试（home→正向点动×N→反向点动×M）")
    ap.add_argument("--profile", default="octoaxes",
                    choices=["octoaxes", "octoaxesplus"])
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=2000000)
    ap.add_argument("--cycles", type=int, default=1,
                    help="循环圈数（默认 1；0 = 无限循环直到 Ctrl-C 中断）")
    ap.add_argument("--step-um", type=float, default=1000.0, help="单次点动距离 µm（默认 1000）")
    ap.add_argument("--fwd", type=int, default=26, help="正方向点动次数（默认 26）")
    ap.add_argument("--bwd", type=int, default=25, help="反方向点动次数（默认 25）")
    ap.add_argument("--dwell", type=float, default=0.5, help="每步到位后停顿秒数（默认 0.5）")
    ap.add_argument("--home-timeout", type=float, default=70.0)
    ap.add_argument("--expect-vel", type=float, default=None,
                    help="预估巡航速度 mm/s（默认取 constants Z default_velocity）")
    ap.add_argument("--step-time-factor", type=float, default=3.0,
                    help="单步 deadline = 预期耗时×该倍数 + 余量（默认 3）")
    ap.add_argument("--step-time-margin", type=float, default=0.5,
                    help="单步 deadline 固定余量秒（默认 0.5）")
    ap.add_argument("--stall-frac", type=float, default=0.5,
                    help="单步实际位移 < 该比例×标称 即判堵转/失步（默认 0.5）")
    ap.add_argument("--dry-run", action="store_true", help="只算并打印参数，不连硬件")
    args = ap.parse_args()

    consts = load_profile_constants(args.profile)
    zc = consts.AXIS_CONFIG["Z"]
    low_um, high_um = zc["limits"]
    pitch_mm = zc["actuator_screw_pitch_mm"]
    ms = zc["actuator_microstepping"]
    sign = zc["movement_sign"]
    mm_per_step = pitch_mm / (FULLSTEPS_PER_REV * ms)

    step_um = args.step_um
    # GUI move_axis：正方向 value = sign*(+step)，反方向 value = sign*(-step)
    fwd_delta_us = int(sign * (+step_um) / 1000.0 / mm_per_step)   # 单步固件微步增量（正向）
    bwd_delta_us = int(sign * (-step_um) / 1000.0 / mm_per_step)   # 反向
    nominal_us = abs(fwd_delta_us)
    # 行程预估（GUI µm）：正向到 +fwd*step，回到 +(fwd-bwd)*step
    peak_um = args.fwd * step_um
    end_um = (args.fwd - args.bwd) * step_um

    # 单步预期耗时：距离/速度 + 加减速余量；deadline = 预期×factor + margin
    move_vel = args.expect_vel or zc.get("default_velocity", 3.0)
    accel = zc.get("default_acceleration", 20.0)
    step_mm = step_um / 1000.0
    expected_step_t = step_mm / move_vel + move_vel / accel  # 匀速段 + 一次完整加/减速近似
    step_deadline = expected_step_t * args.step_time_factor + args.step_time_margin

    variant = getattr(consts, "Z_AXIS_VARIANT", "?")
    print("=" * 66)
    print(f"Z 老化测试（点动序列）| profile={args.profile} | Z_AXIS_VARIANT={variant}")
    print(f"  limits=({low_um},{high_um})µm | pitch={pitch_mm}mm | ms={ms} | sign={sign}")
    print(f"  mm_per_step={mm_per_step:.8g} ({1/mm_per_step:.0f} 微步/mm)")
    print(f"  单步 {step_um:.0f}µm = {nominal_us} 微步 | 正向 ×{args.fwd} | 反向 ×{args.bwd} | 停顿 {args.dwell}s")
    print(f"  正向单步固件微步增量 = {fwd_delta_us:+d}（GUI 正/上）| 反向 = {bwd_delta_us:+d}")
    print(f"  预估峰值位置 ≈ GUI +{peak_um/1000:.0f}mm | 单圈结束 ≈ GUI +{end_um/1000:.0f}mm")
    print(f"  单步预期耗时 ≈ {expected_step_t:.2f}s (vel={move_vel}mm/s acc={accel}mm/s²) "
          f"→ deadline {step_deadline:.2f}s（超时即读状态判卡）")
    print(f"  循环圈数 = {'∞（无限，Ctrl-C 停）' if args.cycles <= 0 else args.cycles}")
    print("=" * 66)
    if peak_um > high_um:
        print(f"[WARN] 峰值 +{peak_um/1000:.0f}mm 超过软上限 {high_um/1000:.0f}mm，"
              f"超界点动会被固件 clamp/拦截。")

    if args.dry_run:
        print("[DRY-RUN] 未连接硬件，仅打印上述计算值。")
        return 0

    import serial
    sys.path.insert(0, THIS_DIR)
    import z_homing_safedist as z   # 复用已验证的 send_cmd / read_regs 助手

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    time.sleep(0.4)
    ser.reset_input_buffer()
    seq = [0]

    def send(cmd_id, b2=0, b3=0, b4=0, b5=0, b6=0):
        seq[0] = (seq[0] + 1) & 0xFF
        z.send_cmd(ser, seq[0], cmd_id, b2, b3, b4, b5, b6)

    def regs():
        return z.read_regs(ser, axis="Z")

    def gui_um(x):
        return (x / sign * mm_per_step * 1000) if x is not None else None

    # ---- 启动配置下发（镜像 _configure_actuators，仅 Z）----
    print("\n[配置] 下发 Z 导程/驱动/限位/极性/homing 速度 ...")
    px = int(round(pitch_mm * 1000))
    send(CMD_SET_LEAD_SCREW_PITCH, AXIS_Z, (px >> 8) & 0xFF, px & 0xFF)
    time.sleep(0.05)
    ms_byte = 0 if ms == 1 else (255 if ms >= 256 else (int(ms) & 0xFF))
    cur = int(round(zc["actuator_motor_current_ma"])) & 0xFFFF
    hold = max(0, min(255, int(round(zc["actuator_motor_hold_ratio"] * 255))))
    send(CMD_CONFIGURE_STEPPER_DRIVER, AXIS_Z, ms_byte,
         (cur >> 8) & 0xFF, cur & 0xFF, hold)
    time.sleep(0.05)
    lo = int((low_um / 1000.0) / mm_per_step) * sign
    hi = int((high_um / 1000.0) / mm_per_step) * sign
    if lo > hi:
        lo, hi = hi, lo
    for code, val in [(LIM_CODE_Z_POSITIVE, hi), (LIM_CODE_Z_NEGATIVE, lo)]:
        v = val & 0xFFFFFFFF
        send(CMD_SET_LIM, code, (v >> 24) & 0xFF, (v >> 16) & 0xFF,
             (v >> 8) & 0xFF, v & 0xFF)
        time.sleep(0.05)
    pol = zc.get("switch_polarity")
    if pol is not None:
        send(CMD_SET_LIM_SWITCH_POLARITY, AXIS_Z, int(pol) & 0xFF)
        time.sleep(0.05)
    hv = zc.get("homing_velocity_mm")
    if hv is not None:
        z.send_debug_cmd(ser, f"S:SET_HOMING_VEL Z {float(hv)}")
        time.sleep(0.05)
    print(f"  pitch={pitch_mm}mm ms={ms} cur={cur}mA hold={hold} "
          f"softlim=({lo},{hi})µstep pol={pol} homing_vel={hv}")

    home_dir = 1 if sign == 1 else 0

    def wait_home():
        send(CMD_HOME_OR_ZERO, AXIS_Z, home_dir)
        t0 = time.perf_counter()
        moved = False
        while time.perf_counter() - t0 < args.home_timeout:
            r = regs()
            va = r.get("VACTUAL", 0) or 0
            x = r.get("XACTUAL")
            if abs(va) > 1000:
                moved = True
            if moved and va == 0 and x is not None and abs(x) < 8000:
                time.sleep(0.3)
                r = regs()
                if (r.get("VACTUAL", 0) or 0) == 0:
                    return r
            time.sleep(0.2)
        return None

    def jog(delta_us):
        """发相对点动 → 先按预期耗时 sleep 让 chip 走完 → 再读位置判稳定。

        不依赖易丢的 VACTUAL 轮询（read_regs 带重试单次可能 ~0.5-1.5s，
        会误判超时）。改用「位置不再变化」作为停车判据，更可靠也快。
        返回 (regs, settled)：settled=True 位置已稳定（停车）；False=deadline 内仍在动。
        """
        v = delta_us & 0xFFFFFFFF
        send(CMD_MOVE_Z, (v >> 24) & 0xFF, (v >> 16) & 0xFF,
             (v >> 8) & 0xFF, v & 0xFF)
        # 先等单步预期耗时，让 chip 把这一步走完（正常情况下到此已停）
        time.sleep(expected_step_t)
        # 再确认位置稳定（连续两次 XACTUAL 相同 = 停车）；最多再等 step_deadline
        t0 = time.perf_counter()
        r = regs()
        prev = r.get("XACTUAL")
        while time.perf_counter() - t0 < step_deadline:
            time.sleep(0.05)
            r = regs()
            x = r.get("XACTUAL")
            if x is not None and x == prev:
                return r, True          # 位置稳定 = 已停
            prev = x if x is not None else prev
        return r, False                 # deadline 内位置仍在变 = 真卡/慢

    stall_min = nominal_us * args.stall_frac
    cycles_ok = 0
    infinite = args.cycles <= 0           # --cycles 0 = 无限循环到 Ctrl-C
    total_label = "∞" if infinite else str(args.cycles)
    try:
        abort = False
        c = 0
        while infinite or c < args.cycles:
            c += 1
            print(f"\n──── 第 {c}/{total_label} 圈 ────")
            print("  [HOME] ...")
            r = wait_home()
            if r is None:
                print("  [FAIL] HOME 超时未完成。中止。")
                abort = True
                break
            x_prev = r.get("XACTUAL", 0)
            print(f"      home 完成: XACTUAL={x_prev} GUI={gui_um(x_prev):.0f}µm（等待 {args.dwell}s 再移动）")
            time.sleep(args.dwell)

            def run_dir(label, n, delta):
                nonlocal x_prev, abort
                for i in range(1, n + 1):
                    t_step = time.perf_counter()
                    r, settled = jog(delta)
                    dt = time.perf_counter() - t_step
                    x = r.get("XACTUAL")
                    va = r.get("VACTUAL")
                    moved = abs((x - x_prev)) if x is not None else 0
                    g = gui_um(x)
                    tag = "" if settled else "  ⏱超时未停!"
                    print(f"  [{label} {i:>2}/{n}] XACTUAL={x} GUI={g:.0f}µm "
                          f"Δ={moved}µstep VACTUAL={va} STATUS=0x{r.get('STATUS',0):08X} "
                          f"耗时{dt:.2f}s{tag}")
                    if not settled:
                        print(f"  [FAIL] {label} 第 {i} 步 {dt:.2f}s 内未到位（预期≈{expected_step_t:.2f}s）"
                              f" → 卡住/失步。诊断：")
                        d = regs()
                        print(f"        XACTUAL={d.get('XACTUAL')} VACTUAL={d.get('VACTUAL')} "
                              f"STATUS=0x{d.get('STATUS',0):08X}")
                        abort = True
                        return
                    if moved < stall_min:
                        print(f"  [FAIL] {label} 第 {i} 步实际位移 {moved} < "
                              f"{stall_min:.0f}µstep（疑似失步/限位/到边界）。中止。")
                        abort = True
                        return
                    x_prev = x if x is not None else x_prev
                    time.sleep(args.dwell)

            print(f"  正方向点动 ×{args.fwd}（每步 {step_um:.0f}µm，停 {args.dwell}s）...")
            run_dir("FWD", args.fwd, fwd_delta_us)
            if abort:
                break
            print(f"  反方向点动 ×{args.bwd}（每步 {step_um:.0f}µm，停 {args.dwell}s）...")
            run_dir("BWD", args.bwd, bwd_delta_us)
            if abort:
                break

            cycles_ok += 1
            print(f"  [OK] 第 {c} 圈完成（累计 {cycles_ok} 圈，末位 GUI={gui_um(x_prev):.0f}µm）")
    except KeyboardInterrupt:
        print("\n[INFO] 用户中断 (Ctrl-C)。")
    finally:
        ser.close()

    print("\n" + "=" * 66)
    print(f"老化测试结束：完成 {cycles_ok}/{total_label} 圈")
    print("=" * 66)
    if infinite:
        return 0 if not abort else 1
    return 0 if cycles_ok == args.cycles else 1


if __name__ == "__main__":
    sys.exit(main())
