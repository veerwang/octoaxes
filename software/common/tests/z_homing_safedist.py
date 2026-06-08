#!/usr/bin/env python3
"""自动测量 homing 退回安全距离 (2026-06-06)

背景：新 Z（LE143S）homing 撞限位后退回 0.7mm（Z_SAFEPOSITION）仍离不开回零感应区，
说明传感器迟滞 > 0.7mm。本脚本自动测出"从触发点退离到释放点"需要多少距离，
据此设定 firmware 的 homeSafetyPositionMM（Z_SAFEPOSITION）。

原理（纯位置模式小步进，安全）：
  Phase 0：若 STOPL 已激活，先小步退离直到释放（回到感应区外）
  Phase 1：小步逼近限位，直到 STOPL 激活 → 记触发点 A (XACTUAL)
  Phase 2：小步退离，直到 STOPL 释放 → 记释放点 B (XACTUAL)
  需要的退回距离 = |A − B|（含迟滞）+ 安全余量

方向约定（新 Z，已验证）：firmware 正方向(+microsteps)=物理左=朝左限位；退离=负方向。
若你的轴相反，用 --approach-dir -1 翻转。

⚠️ 前提：Z 先大致移动到左限位附近（建议先 homing 一次，或手动 jog 到接近左限位），
   这样 Phase 1 在 --max-steps 内能找到限位。脚本只读 STOPL(bit7)。

用法：
  python3 software/common/tests/z_homing_safedist.py
  python3 software/common/tests/z_homing_safedist.py --port /dev/ttyACM0 --step-mm 0.05
  python3 software/common/tests/z_homing_safedist.py --approach-dir -1   # 方向相反时
"""

import argparse
import math
import os
import re
import sys
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    from utils.helpers import find_teensy_port
except Exception:
    find_teensy_port = None

CMD_MOVE_Z = 2
DEBUG_HEADER = bytes([0x55, 0xAA])
STATUS_STOPL = 1 << 7
STATUS_STOPR = 1 << 8


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


_PAT_ST = None
_PAT_XA = None


def read_regs(ser, axis="Z", timeout=1.5, retries=4):
    """容错解码+正则抠 XACTUAL/VACTUAL/STATUS（免疫二进制位置帧粘连）。

    关键修复：固件持续推二进制位置帧，DUMPREGS 的 STATUS 行常被粘连/冲掉，
    且 END 可能先于 STATUS 到达。故：① 每次先 reset_input_buffer 清干净再发；
    ② 一直读到 STATUS 与 XACTUAL 都匹配才返回（不靠 END）；③ 整体重试 retries 次。"""
    global _PAT_ST, _PAT_XA
    if _PAT_ST is None:
        _PAT_ST = re.compile(rf"S:DUMP\s+{re.escape(axis)}\s+STATUS=0x([0-9A-Fa-f]+)")
        _PAT_XA = re.compile(rf"S:DUMP\s+{re.escape(axis)}\s+XACTUAL=(-?\d+)\s+XTARGET=(-?\d+)\s+VACTUAL=(-?\d+)")
    for _ in range(retries):
        ser.reset_input_buffer()
        send_debug_cmd(ser, f"S:DUMPREGS {axis}")
        deadline = time.perf_counter() + timeout
        buf = bytearray()
        regs = {}
        while time.perf_counter() < deadline:
            if ser.in_waiting:
                buf.extend(ser.read(ser.in_waiting))
                text = buf.decode("latin1", errors="ignore")
                m = _PAT_ST.search(text)
                if m:
                    regs["STATUS"] = int(m.group(1), 16)
                m2 = _PAT_XA.search(text)
                if m2:
                    regs["XACTUAL"] = int(m2.group(1))
                    regs["VACTUAL"] = int(m2.group(3))
                if "STATUS" in regs and "XACTUAL" in regs:
                    return regs
            else:
                time.sleep(0.003)
        if "STATUS" in regs:   # 至少拿到 STATUS 也接受
            return regs
    return regs


def stopl_on(regs):
    return bool(regs.get("STATUS", 0) & STATUS_STOPL)


def move_rel(ser, seq_box, microsteps, settle_timeout=4.0):
    """发 MOVE_Z 相对运动，等到 VACTUAL=0 settle。返回 settle 后的 regs。"""
    seq_box[0] = (seq_box[0] + 1) & 0xFF
    val = microsteps & 0xFFFFFFFF
    send_cmd(ser, seq_box[0], CMD_MOVE_Z,
             b2=(val >> 24) & 0xFF, b3=(val >> 16) & 0xFF,
             b4=(val >> 8) & 0xFF, b5=val & 0xFF)
    time.sleep(0.15)
    deadline = time.perf_counter() + settle_timeout
    regs = {}
    while time.perf_counter() < deadline:
        regs = read_regs(ser)
        if regs.get("VACTUAL", 1) == 0:
            time.sleep(0.05)
            regs = read_regs(ser)
            if regs.get("VACTUAL", 1) == 0:
                return regs
        time.sleep(0.03)
    return regs


def main():
    ap = argparse.ArgumentParser(description="自动测量 Z homing 退回安全距离")
    ap.add_argument("--port", default=None)
    ap.add_argument("--baud", type=int, default=2000000)
    ap.add_argument("--axis", default="Z")
    ap.add_argument("--step-mm", type=float, default=0.05, help="单步距离(mm)")
    ap.add_argument("--steps-per-mm", type=float, default=51200.0,
                    help="微步/mm（新 Z：200×256/1.0mm=51200）")
    ap.add_argument("--approach-dir", type=int, choices=[1, -1], default=1,
                    help="朝限位方向：+1=firmware正(物理左, 新 Z 默认) / -1 相反")
    ap.add_argument("--safety-mm", type=float, default=0.5, help="推荐值在迟滞上加的安全余量")
    ap.add_argument("--max-steps", type=int, default=240, help="每阶段最大步数（防跑飞）")
    args = ap.parse_args()

    port = args.port
    if port is None and find_teensy_port is not None:
        try:
            port = find_teensy_port()
        except Exception:
            port = None
    if port is None:
        port = "/dev/ttyACM0"

    step_us = int(round(args.step_mm * args.steps_per_mm))
    approach = args.approach_dir * step_us     # 朝限位
    retreat = -args.approach_dir * step_us     # 退离

    print(f"[INFO] 端口 {port} 轴 {args.axis} | 步长 {args.step_mm}mm={step_us}微步 | "
          f"朝限位方向 {'+ (firmware正/物理左)' if args.approach_dir==1 else '- '}")
    print("[INFO] ⚠️ 确保 Z 已大致在左限位附近（建议先 homing 或 jog 过去）。Ctrl-C 退出。\n")

    ser = serial.Serial(port, args.baud, timeout=0.05)
    time.sleep(0.3)
    ser.reset_input_buffer()
    seq_box = [0]

    r = read_regs(ser)
    if "STATUS" not in r:
        print("[ERROR] 读不到寄存器（确认固件支持 S:DUMPREGS、轴名 Z）。退出。")
        ser.close()
        return
    print(f"[起始] XACTUAL={r.get('XACTUAL')} STOPL={'ON' if stopl_on(r) else 'off'} "
          f"STATUS=0x{r.get('STATUS',0):08X}")

    def to_mm(us):
        return us / args.steps_per_mm

    try:
        # Phase 0：清出感应区
        n = 0
        while stopl_on(r) and n < args.max_steps:
            prev = r.get("XACTUAL")
            r = move_rel(ser, seq_box, retreat)
            n += 1
            print(f"  [P0 退离] step{n} XACTUAL={r.get('XACTUAL')} STOPL={'ON' if stopl_on(r) else 'off'}")
            if r.get("XACTUAL") == prev:
                print("  [WARN] XACTUAL 没变（可能撞软限位/被 chip 挡）。若一直 ON，试 --approach-dir 反向。")
                break
        if stopl_on(r):
            print("[ERROR] Phase0 未能退出感应区。检查方向(--approach-dir)/行程。退出。")
            ser.close()
            return

        # Phase 1：逼近找触发点 A
        A = None
        n = 0
        while n < args.max_steps:
            prev = r.get("XACTUAL")
            r = move_rel(ser, seq_box, approach)
            n += 1
            on = stopl_on(r)
            print(f"  [P1 逼近] step{n} XACTUAL={r.get('XACTUAL')} STOPL={'ON' if on else 'off'}")
            if on:
                A = r.get("XACTUAL")
                break
            if r.get("XACTUAL") == prev:
                print("  [WARN] XACTUAL 没变（撞软限位/硬限位但 STOPL 没触发？）。停止逼近。")
                break
        if A is None:
            print(f"[ERROR] 逼近 {args.max_steps} 步未触发 STOPL。Z 离限位太远或方向反。退出。")
            ser.close()
            return
        print(f"[触发点 A] XACTUAL={A}")

        # Phase 2：退离找释放点 B
        B = None
        n = 0
        while n < args.max_steps:
            r = move_rel(ser, seq_box, retreat)
            n += 1
            on = stopl_on(r)
            print(f"  [P2 退离] step{n} XACTUAL={r.get('XACTUAL')} STOPL={'ON' if on else 'off'}")
            if not on:
                B = r.get("XACTUAL")
                break
        if B is None:
            print(f"[ERROR] 退离 {args.max_steps} 步 STOPL 仍未释放。迟滞超大或方向异常。退出。")
            ser.close()
            return
        print(f"[释放点 B] XACTUAL={B}")

        # 结果
        hyst_us = abs(A - B)
        hyst_mm = to_mm(hyst_us)
        recommend = hyst_mm + args.safety_mm
        recommend_round = math.ceil(recommend * 10) / 10.0   # 向上取到 0.1mm
        print("\n================= 测量结果 =================")
        print(f"  触发点 A = {A} 微步")
        print(f"  释放点 B = {B} 微步")
        print(f"  需要退回(含迟滞) |A-B| = {hyst_us} 微步 = {hyst_mm:.3f} mm")
        print(f"  + 安全余量 {args.safety_mm} mm")
        print(f"  ➜ 建议 Z_SAFEPOSITION (homeSafetyPositionMM) ≥ {recommend:.3f} mm  →  取 {recommend_round} mm")
        print("  （当前 firmware 是 0.7mm）")
        print("===========================================")
    except KeyboardInterrupt:
        print("\n[INFO] 用户中断。")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
