#!/usr/bin/env python3
"""
卡死现场取证：发送 S:DUMPREGS [axis] 命令打印 TMC4361A 关键寄存器

用法：
  python3 software/tests/dump_axis_state.py X         # dump X 轴
  python3 software/tests/dump_axis_state.py X,Y,Z     # 多轴
  python3 software/tests/dump_axis_state.py           # 所有轴

解读关键字段（参考 TMC4361A datasheet）：

STATUS bits:
  bit 5  STOPL_ACTIVE_F   物理 left 限位开关当前 active（实时）
  bit 6  STOPR_ACTIVE_F   物理 right 限位开关当前 active（实时）
  bit 11 VSTOPL_ACTIVE_F  虚拟 left 限位被锁住（chip 内部 latch）
  bit 12 VSTOPR_ACTIVE_F  虚拟 right 限位被锁住（chip 内部 latch）
  bit 14 TARGET_REACHED   XACTUAL == XTARGET
  bit 31 RAMP_STATE       ramp generator 正在运行

EVENTS bits（sticky，读后自动清）：
  bit 7  STOPL_EVENT      物理 left 限位被触发过（不论现在是否还 active）
  bit 8  STOPR_EVENT      物理 right 限位被触发过
  bit 11 VSTOPL_ACTIVE    虚拟 left 限位 enter 事件
  bit 12 VSTOPR_ACTIVE    虚拟 right 限位 enter 事件

REFERENCE_CONF bits:
  bit 0  STOP_LEFT_EN
  bit 1  STOP_RIGHT_EN
  bit 6  VIRTUAL_LEFT_LIMIT_EN
  bit 7  VIRTUAL_RIGHT_LIMIT_EN

卡死判断 cheat sheet：
  * XACTUAL ≈ XTARGET && VACTUAL=0 && (STOPL/R_EVENT 或 VSTOPL/R_ACTIVE_F = 1)
    → chip latched，hard-stop 状态，所有后续 MOVE 都无法启动 ramp
  * XACTUAL != XTARGET && VACTUAL=0 && RAMP_STATE=0
    → ramp generator 已停止但未到目标，状态异常
  * isMoving=1 但 VACTUAL=0 持续多次
    → 软件 _isMoving 与 chip 状态脱节
"""
import argparse
import os
import sys
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from utils.helpers import find_teensy_port  # noqa: E402


DEBUG_HEADER = bytes([0x55, 0xAA])


def send_debug_cmd(ser, cmd):
    ser.write(DEBUG_HEADER + cmd.encode("utf-8") + b"\n")


def read_until_end(ser, end_marker=b"S:DUMPREGS:END", timeout=5.0):
    """读到 END 标记或超时。每行返回。"""
    deadline = time.perf_counter() + timeout
    buf = bytearray()
    lines = []
    while time.perf_counter() < deadline:
        n = ser.in_waiting
        if n:
            buf.extend(ser.read(n))
            while b"\n" in buf:
                nl = buf.find(b"\n")
                line = bytes(buf[:nl]).rstrip(b"\r")
                del buf[: nl + 1]
                # 过滤掉 24 字节二进制位置上报（不会有 \n 在前 22 字节，所以多半是 ASCII）
                try:
                    text = line.decode("utf-8")
                    if text.strip():
                        lines.append(text.strip())
                        if end_marker.decode() in text:
                            return lines
                except UnicodeDecodeError:
                    pass  # 跳过含二进制的行
        else:
            time.sleep(0.005)
    return lines


# STATUS 寄存器 (0x0F) 位解码 — 参考 TMC4361A_HW_Abstraction.h
STATUS_BITS = [
    (0,  "TARGET_REACHED_F",   "XACTUAL == XTARGET"),
    (1,  "POS_COMP_REACHED_F", "位置比较点到达"),
    (2,  "VEL_REACHED_F",      "速度到达目标"),
    (7,  "STOPL_ACTIVE_F",     "物理 left 限位 active (latched)"),
    (8,  "STOPR_ACTIVE_F",     "物理 right 限位 active (latched)"),
    (9,  "VSTOPL_ACTIVE_F",    "虚拟 left 限位 latched ⚠"),
    (10, "VSTOPR_ACTIVE_F",    "虚拟 right 限位 latched ⚠"),
    (11, "ACTIVE_STALL_F",     "StallGuard 触发 latched ⚠⚠"),
    (12, "HOME_ERROR_F",       "Homing 错误 latched ⚠"),
    (13, "FS_ACTIVE_F",        "全步模式活跃"),
    (14, "ENC_FAIL_F",         "编码器失效"),
    (15, "N_ACTIVE_F",         "N(zero) 标志"),
]

# EVENTS 寄存器 (0x0E) 位解码（sticky，读后自动清）
EVENT_BITS = [
    (7,  "POS_REACHED_EVENT",  "位置到达事件"),
    (11, "STOPL_EVENT",        "物理 left 限位触发事件"),
    (12, "STOPR_EVENT",        "物理 right 限位触发事件"),
    (13, "VSTOPL_ACTIVE",      "虚拟 left 限位事件"),
    (14, "VSTOPR_ACTIVE",      "虚拟 right 限位事件"),
    (23, "ACTIVE_STALL_EVENT", "StallGuard 触发事件"),
    (25, "COVER_DONE",         "SPI Cover 传输完成（正常）"),
]


def decode_bits(value, bits, label_prefix=""):
    set_bits = []
    for bit, name, desc in bits:
        if value & (1 << bit):
            set_bits.append(f"  {label_prefix}bit{bit:2d} {name:20s} {desc}")
    return set_bits


def parse_and_diagnose(lines):
    """从 S:DUMP 行解析寄存器，输出诊断结论。"""
    # 期望 4 行/轴：STATUS+EVENTS+RAMPMODE, XACTUAL+XTARGET+VACTUAL+VMAX, VSTOP_L+VSTOP_R+REFCONF+STEP_CONF, isMoving+state...
    by_axis = {}
    for line in lines:
        if not line.startswith("S:DUMP "):
            continue
        parts = line.split()
        if len(parts) < 3:
            continue
        axis = parts[1]
        if axis not in by_axis:
            by_axis[axis] = {}
        for tok in parts[2:]:
            if "=" in tok:
                k, v = tok.split("=", 1)
                try:
                    by_axis[axis][k] = int(v, 0)  # 自动判别 0x / 十进制
                except ValueError:
                    by_axis[axis][k] = v

    for axis, regs in by_axis.items():
        print(f"\n=== 轴 {axis} ===")
        print(f"  XACTUAL = {regs.get('XACTUAL'):>10}    XTARGET = {regs.get('XTARGET'):>10}")
        print(f"  VACTUAL = {regs.get('VACTUAL'):>10}    VMAX    = {regs.get('VMAX'):>10}")
        print(f"  VSTOP_L = {regs.get('VSTOP_L'):>10}    VSTOP_R = {regs.get('VSTOP_R'):>10}")
        status = regs.get("STATUS", 0)
        events = regs.get("EVENTS", 0)
        refconf = regs.get("REFCONF", 0)
        print(f"  STATUS  = 0x{status:08X}    EVENTS = 0x{events:08X}    REFCONF = 0x{refconf:08X}")
        print(f"  isMoving={regs.get('isMoving')} state={regs.get('state')} softLimEn={regs.get('softLimEn')}")
        print()
        for line in decode_bits(status, STATUS_BITS, "STATUS "):
            print(line)
        for line in decode_bits(events, EVENT_BITS, "EVENTS "):
            print(line)

        # 自动诊断
        xa = regs.get("XACTUAL", 0)
        xt = regs.get("XTARGET", 0)
        va = regs.get("VACTUAL", 0)
        vmax = regs.get("VMAX", 0)
        sw_state = regs.get("state", -1)
        # STATUS 位 — 参考 TMC4361A_HW_Abstraction.h
        vstopL_f = bool(status & 0x200)   # bit 9
        vstopR_f = bool(status & 0x400)   # bit 10
        active_stall = bool(status & 0x800)   # bit 11
        home_error = bool(status & 0x1000)    # bit 12
        stopL_active = bool(status & 0x80)    # bit 7
        stopR_active = bool(status & 0x100)   # bit 8
        # EVENTS 位
        stopL_evt = bool(events & (1 << 11))
        stopR_evt = bool(events & (1 << 12))
        stall_evt = bool(events & (1 << 23))
        vstopL_l = regs.get("VSTOP_L", 0)
        vstopR_l = regs.get("VSTOP_R", 0)
        print("  诊断：")
        # 状态机
        STATE_NAMES = {0: "IDLE", 1: "HOMING_INIT", 2: "HOMING_SEARCH",
                       3: "HOMING_SET_ZERO", 4: "LEAVING_HOME", 5: "MOVING", 6: "ERROR"}
        if sw_state >= 0:
            sn = STATE_NAMES.get(sw_state, "?")
            marker = " ⚠" if sw_state == 6 else ""
            print(f"    软件状态：state={sw_state} ({sn}){marker}")
        # latch 类型
        if active_stall:
            print(f"    ⚠⚠ ACTIVE_STALL_F latched — StallGuard 误触发（TMC2240 用 StallGuard4 与 TMC2660 算法不同）")
            print(f"       chip 拒绝启动 ramp，必须清 latch 或 SW_RESET")
        if home_error:
            print(f"    ⚠ HOME_ERROR_F latched — 上次 homing 未到限位开关")
        if vstopL_f or vstopR_f:
            print(f"    ⚠ chip VSTOP_{'L' if vstopL_f else 'R'}_ACTIVE_F latched")
            margin_l = xa - vstopL_l
            margin_r = vstopR_l - xa
            print(f"    XACTUAL 距 VSTOP_L={margin_l}μstep, 距 VSTOP_R={margin_r}μstep")
            if abs(margin_l) < 100 or abs(margin_r) < 100:
                print(f"    → 紧贴虚拟限位边界（< 100μstep），符合 commit 17b8f71 修复场景的复发")
        if stopL_active or stopR_active:
            print(f"    物理限位 STOP{'L' if stopL_active else 'R'}_ACTIVE_F latched")
        if stopL_evt or stopR_evt:
            print(f"    ⚠ EVENTS: STOP{'L' if stopL_evt else 'R'}_EVENT sticky（物理限位曾被压下）")
        if stall_evt:
            print(f"    ⚠ EVENTS: ACTIVE_STALL_EVENT sticky")
        # VMAX/运动状态
        if vmax == 0:
            print(f"    ⚠ VMAX=0 — chip 无法启动新的 ramp（handleError → motor_stop 把 VMAX 清零了）")
        if va == 0 and xa != xt:
            print(f"    ⚠ 电机静止但未到目标 (XACTUAL={xa} != XTARGET={xt})")
        if regs.get("isMoving") == 1 and va == 0:
            print(f"    ⚠ 软件 _isMoving=true 但 chip VACTUAL=0 → 状态机脱节")
        latched_any = active_stall or home_error or vstopL_f or vstopR_f or stopL_active or stopR_active
        if not latched_any and va == 0 and xa == xt and sw_state == 0:
            print(f"    ✓ 完全 idle（无 latch，软件 IDLE，XACTUAL==XTARGET）")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("axis", nargs="?", default="", help="X / Y / Z / W / 多轴逗号 / 留空 = 所有")
    parser.add_argument("--port", default=None)
    args = parser.parse_args()

    port = args.port or find_teensy_port()
    if not port:
        print("✘ 找不到 Teensy 串口"); sys.exit(1)
    print(f"使用串口: {port}")

    ser = serial.Serial(port, 2_000_000, timeout=0.05)
    time.sleep(0.5)
    ser.reset_input_buffer()

    axes = [a.strip() for a in args.axis.split(",")] if args.axis else [""]
    for axis in axes:
        cmd = "S:DUMPREGS"
        if axis:
            cmd += " " + axis
        print(f"\n>>> {cmd}")
        send_debug_cmd(ser, cmd)
        lines = read_until_end(ser, timeout=3.0)
        # 只打印 S:DUMP 开头的行
        dump_lines = [l for l in lines if l.startswith("S:DUMP")]
        for line in dump_lines:
            print(line)
        parse_and_diagnose(dump_lines)

    ser.close()


if __name__ == "__main__":
    main()
