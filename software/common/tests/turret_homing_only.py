#!/usr/bin/env python3
"""Turret 纯 homing 观测脚本 (2026-06-05)

目的：**只让 firmware config.h 默认参数起作用**，排除上位机 GUI 的电流/速度/加速度
覆盖（GUI 换位时会下发 1000mA RMS 等"柔和参数"覆盖 config.h 的 1800mA 峰值）。

流程（极简，只发两条命令）：
  1. （可选）INITIALIZE(254)：重跑 beginAll，把 chip 恢复到 config.h 默认
     （1800mA 峰值 + 默认 homing 速度/加速度），洗掉之前 GUI 下发的覆盖值。
  2. HOME_OR_ZERO(5) axis=Turret(7) dir=<--dir>。
  3. 之后高频轮询 S:DUMPREGS Turret，把 XACTUAL/VACTUAL/STATUS/限位 打成时间序列，
     直接观察：位置是否跑飞、撞限位时 chip 是否停车、STOPL 运动中是否 active。

**不发** CONFIGURE_STEPPER_DRIVER / SET_MAX_VELOCITY_ACCELERATION / SET_LEAD_SCREW_PITCH
等任何 configure 命令 —— 这正是"纯 firmware 默认"的含义。

⚠️ homing 方向：firmware 的 HOME 处理器会用命令 data[3] 覆盖 config.h 的 homing_direct：
    --dir 0 (HOME_POSITIVE) → firmware homing_direct = +1
    --dir 1 (HOME_NEGATIVE) → firmware homing_direct = -1
  GUI 在 Turret(movement_sign=-1) 下实际发的是 dir=0，所以默认 --dir 0 复现 GUI 现象。
  想测反方向就 --dir 1。

用法：
  python3 software/common/tests/turret_homing_only.py                 # 自动找口, dir=0, 8s
  python3 software/common/tests/turret_homing_only.py --dir 1         # 反方向
  python3 software/common/tests/turret_homing_only.py --port /dev/ttyACM0 --duration 12
  python3 software/common/tests/turret_homing_only.py --no-init       # 不重置, 用当前 chip 状态
"""

import argparse
import os
import sys
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    from utils.helpers import find_teensy_port  # noqa: E402
except Exception:
    find_teensy_port = None

# ---- 协议常量 ----
CMD_HOME_OR_ZERO = 5
CMD_INITIALIZE = 254
AXIS_TURRET = 7          # 协议轴码（define.py AXIS.TURRET）
HOME_POSITIVE = 0
HOME_NEGATIVE = 1

DEBUG_HEADER = bytes([0x55, 0xAA])   # ASCII 调试命令前缀（S:...）

# STATUS 位（TMC4361A）
STATUS_STOPL = 1 << 7    # 物理 left 限位 active
STATUS_STOPR = 1 << 8    # 物理 right 限位 active
STATUS_VSTOPL = 1 << 9
STATUS_VSTOPR = 1 << 10
STATUS_STALL = 1 << 11
STATUS_HOME_ERR = 1 << 12


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) if (crc & 0x80) else (crc << 1)
            crc &= 0xFF
    return crc


def send_cmd(ser, seq, cmd, b2=0, b3=0, b4=0, b5=0, b6=0):
    pkt = bytearray([seq & 0xFF, cmd, b2, b3, b4, b5, b6, 0])
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)
    ser.flush()


def send_debug_cmd(ser, cmd):
    ser.write(DEBUG_HEADER + cmd.encode("utf-8") + b"\n")
    ser.flush()


def read_dumpregs(ser, axis="Turret", timeout=1.5):
    """发 S:DUMPREGS <axis>，收到 END 或超时；返回解析出的寄存器 dict。
    会自动跳过夹杂的二进制位置上报帧（非 UTF-8 行）。"""
    send_debug_cmd(ser, f"S:DUMPREGS {axis}")
    deadline = time.perf_counter() + timeout
    buf = bytearray()
    regs = {}
    while time.perf_counter() < deadline:
        n = ser.in_waiting
        if n:
            buf.extend(ser.read(n))
            while b"\n" in buf:
                nl = buf.find(b"\n")
                raw = bytes(buf[:nl]).rstrip(b"\r")
                del buf[: nl + 1]
                try:
                    text = raw.decode("utf-8").strip()
                except UnicodeDecodeError:
                    continue  # 二进制位置帧，跳过
                if not text:
                    continue
                if text.startswith("S:DUMP "):
                    parts = text.split()
                    # parts[0]=S:DUMP parts[1]=axisName parts[2:]=KEY=VAL
                    for tok in parts[2:]:
                        if "=" in tok:
                            k, v = tok.split("=", 1)
                            try:
                                regs[k] = int(v, 0)
                            except ValueError:
                                regs[k] = v
                if "S:DUMPREGS:END" in text:
                    return regs
        else:
            time.sleep(0.003)
    return regs


def fmt_flags(status):
    f = []
    if status & STATUS_STOPL:
        f.append("STOPL")
    if status & STATUS_STOPR:
        f.append("STOPR")
    if status & STATUS_VSTOPL:
        f.append("VSTOPL")
    if status & STATUS_VSTOPR:
        f.append("VSTOPR")
    if status & STATUS_STALL:
        f.append("STALL")
    if status & STATUS_HOME_ERR:
        f.append("HOME_ERR")
    return ",".join(f) if f else "-"


def main():
    ap = argparse.ArgumentParser(description="Turret 纯 homing 观测（只用 firmware 默认参数）")
    ap.add_argument("--port", default=None, help="串口（默认自动探测）")
    ap.add_argument("--baud", type=int, default=2000000, help="波特率（USB CDC 实际忽略）")
    ap.add_argument("--dir", type=int, choices=[0, 1], default=0,
                    help="homing 方向 data[3]：0=HOME_POSITIVE(homing_direct=+1) / 1=HOME_NEGATIVE(-1)")
    ap.add_argument("--duration", type=float, default=8.0, help="homing 后观测时长(s)")
    ap.add_argument("--interval", type=float, default=0.25, help="寄存器轮询间隔(s)")
    ap.add_argument("--no-init", action="store_true",
                    help="不发 INITIALIZE（用当前 chip 状态，不恢复 config.h 默认）")
    args = ap.parse_args()

    port = args.port
    if port is None and find_teensy_port is not None:
        try:
            port = find_teensy_port()
        except Exception:
            port = None
    if port is None:
        port = "/dev/ttyACM0"

    print(f"[INFO] 打开串口 {port} @ {args.baud}")
    ser = serial.Serial(port, args.baud, timeout=0.05)
    time.sleep(0.3)
    ser.reset_input_buffer()

    seq = 0

    def next_seq():
        nonlocal seq
        seq = (seq + 1) & 0xFF
        return seq

    # ---- 1. 恢复 firmware 默认 ----
    if not args.no_init:
        print("[STEP] INITIALIZE(254) —— 重跑 beginAll，恢复 config.h 默认（1800mA 峰值等）")
        send_cmd(ser, next_seq(), CMD_INITIALIZE)
        time.sleep(2.0)          # 等 beginAll 完成
        ser.reset_input_buffer()
    else:
        print("[STEP] 跳过 INITIALIZE（--no-init），用当前 chip 状态")

    # ---- baseline dump ----
    print("\n[BASELINE] homing 前寄存器快照：")
    regs = read_dumpregs(ser)
    if regs:
        print(f"  XACTUAL={regs.get('XACTUAL')} VACTUAL={regs.get('VACTUAL')} VMAX={regs.get('VMAX')}")
        print(f"  STATUS=0x{regs.get('STATUS',0):08X} EVENTS=0x{regs.get('EVENTS',0):08X} "
              f"REFCONF=0x{regs.get('REFCONF',0):08X}")
        print(f"  STATUS 位: {fmt_flags(regs.get('STATUS',0))}  "
              f"state={regs.get('state')} isMoving={regs.get('isMoving')}")
    else:
        print("  [WARN] 没读到 DUMPREGS（确认固件支持 S:DUMPREGS 且轴名 Turret 正确）")

    # ---- 2. 发 HOME ----
    dir_name = "HOME_POSITIVE(homing_direct=+1)" if args.dir == HOME_POSITIVE else "HOME_NEGATIVE(homing_direct=-1)"
    print(f"\n[STEP] HOME_OR_ZERO Turret  dir={args.dir} ({dir_name})")
    send_cmd(ser, next_seq(), CMD_HOME_OR_ZERO, b2=AXIS_TURRET, b3=args.dir)

    # ---- 3. 时间序列观测 ----
    print(f"\n[OBSERVE] 轮询 {args.duration}s（间隔 {args.interval}s）：")
    print(f"{'t(s)':>6} {'XACTUAL':>10} {'VACTUAL':>9} {'STATUS':>10} {'flags':<18} {'state':>6} {'mov':>3}")
    t0 = time.perf_counter()
    last_xactual = None
    saw_stopl = False
    while True:
        t = time.perf_counter() - t0
        if t > args.duration:
            break
        regs = read_dumpregs(ser)
        if regs:
            xa = regs.get("XACTUAL")
            va = regs.get("VACTUAL")
            st = regs.get("STATUS", 0)
            flags = fmt_flags(st)
            if st & STATUS_STOPL:
                saw_stopl = True
            print(f"{t:6.2f} {str(xa):>10} {str(va):>9} 0x{st:08X} {flags:<18} "
                  f"{str(regs.get('state')):>6} {str(regs.get('isMoving')):>3}")
            last_xactual = xa
        time.sleep(args.interval)

    # ---- 收尾结论 ----
    print("\n[FINAL] homing 后寄存器快照：")
    regs = read_dumpregs(ser)
    if regs:
        st = regs.get("STATUS", 0)
        print(f"  XACTUAL={regs.get('XACTUAL')} VACTUAL={regs.get('VACTUAL')} VMAX={regs.get('VMAX')}")
        print(f"  STATUS=0x{st:08X}  位: {fmt_flags(st)}  EVENTS=0x{regs.get('EVENTS',0):08X}")
        print(f"  state={regs.get('state')} isMoving={regs.get('isMoving')}")
        va = regs.get("VACTUAL", 0)
        print("\n[判读]")
        if va not in (0, None) and abs(va) > 0:
            print(f"  ✗ VACTUAL={va} ≠ 0 → 电机仍在转，homing 没停下来（跑飞）")
        else:
            print("  ✓ VACTUAL=0 → 电机已停")
        print(f"  运动期间是否曾读到 STOPL active: {'是 ✓' if saw_stopl else '否 ✗（运动中 chip 没认到左限位）'}")
    ser.close()


if __name__ == "__main__":
    main()
