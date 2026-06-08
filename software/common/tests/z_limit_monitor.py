#!/usr/bin/env python3
"""限位开关实时监视器 (2026-06-06)

连续读取指定轴的 TMC4361A STATUS 寄存器，实时显示左/右限位状态，用于人工触发
限位、核对"程序显示 vs 实际触发的限位"是否一致（验证限位接线/极性）。

  STOPL_ACTIVE_F (STATUS bit7) = 左限位 active
  STOPR_ACTIVE_F (STATUS bit8) = 右限位 active

显示：
  - 底部一行实时状态（左/右 ● 激活 / ○ 空闲），随手动触发即时变化
  - 每次状态翻转打印一条带时间戳的事件行（手动压一次 = 一条记录，便于核对）

用法：
  python3 software/common/tests/z_limit_monitor.py                 # 自动找口, 监视 Z
  python3 software/common/tests/z_limit_monitor.py --axis Z
  python3 software/common/tests/z_limit_monitor.py --port /dev/ttyACM0 --axis Z
  Ctrl-C 退出。
"""

import argparse
import os
import re
import sys
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    from utils.helpers import find_teensy_port  # noqa: E402
except Exception:
    find_teensy_port = None

DEBUG_HEADER = bytes([0x55, 0xAA])

STATUS_STOPL = 1 << 7   # 左限位 active
STATUS_STOPR = 1 << 8   # 右限位 active


def send_debug_cmd(ser, cmd):
    ser.write(DEBUG_HEADER + cmd.encode("utf-8") + b"\n")
    ser.flush()


def read_status(ser, axis, timeout=0.8):
    """发 S:DUMPREGS <axis>，解析返回的 STATUS 寄存器值；读不到返回 None。

    关键：固件在持续推送 24/40 字节二进制位置帧，会粘在 ASCII 文本行前面。
    用容错解码（latin1，不抛异常）把整段缓冲转成字符串，再正则抠出
    'S:DUMP <axis> ... STATUS=0x........'，对二进制干扰免疫。"""
    send_debug_cmd(ser, f"S:DUMPREGS {axis}")
    deadline = time.perf_counter() + timeout
    buf = bytearray()
    pat = re.compile(rf"S:DUMP\s+{re.escape(axis)}\s+STATUS=0x([0-9A-Fa-f]+)")
    status = None
    while time.perf_counter() < deadline:
        n = ser.in_waiting
        if n:
            buf.extend(ser.read(n))
            text = buf.decode("latin1", errors="ignore")
            m = pat.search(text)
            if m:
                status = int(m.group(1), 16)
            if "S:DUMPREGS:END" in text:
                return status
        else:
            time.sleep(0.003)
    return status


def fmt_live(stopl, stopr, status):
    L = "● 激活" if stopl else "○ 空闲"
    R = "● 激活" if stopr else "○ 空闲"
    return f"左限位 STOPL: {L}    右限位 STOPR: {R}    STATUS=0x{status:08X}"


def main():
    ap = argparse.ArgumentParser(description="限位开关实时监视器")
    ap.add_argument("--port", default=None, help="串口（默认自动探测）")
    ap.add_argument("--baud", type=int, default=2000000, help="波特率（USB CDC 实际忽略）")
    ap.add_argument("--axis", default="Z", help="轴名（默认 Z）")
    ap.add_argument("--interval", type=float, default=0.12, help="轮询间隔(s)")
    args = ap.parse_args()

    port = args.port
    if port is None and find_teensy_port is not None:
        try:
            port = find_teensy_port()
        except Exception:
            port = None
    if port is None:
        port = "/dev/ttyACM0"

    print(f"[INFO] 打开串口 {port} @ {args.baud}，监视轴 {args.axis}")
    print("[INFO] 手动触发左/右限位，观察下方实时行 + 事件记录。Ctrl-C 退出。\n")
    ser = serial.Serial(port, args.baud, timeout=0.05)
    time.sleep(0.3)
    ser.reset_input_buffer()

    # 初始读一次确认通信
    st0 = read_status(ser, args.axis)
    if st0 is None:
        print(f"[WARN] 读不到 {args.axis} 的 STATUS —— 确认固件支持 S:DUMPREGS 且轴名正确（如 Z）")
    else:
        print(f"[INFO] 初始 STATUS=0x{st0:08X}（通信正常）。现在请手动触发限位……\n")
    prev_l = prev_r = None
    prev_status = None
    t0 = time.perf_counter()
    nread = 0

    try:
        while True:
            st = read_status(ser, args.axis)
            if st is None:
                print("\r[WARN] 本次未读到 STATUS（稍后重试）" + " " * 20, end="", flush=True)
                time.sleep(args.interval)
                continue
            nread += 1
            stopl = bool(st & STATUS_STOPL)
            stopr = bool(st & STATUS_STOPR)
            t = time.perf_counter() - t0

            # 全 STATUS 任意 bit 变化 → 打印 old→new + 翻转位（诊断：信号是否落在别的 bit）
            if prev_status is not None and st != prev_status:
                flipped = st ^ prev_status
                bits = ",".join(f"bit{b}" for b in range(32) if flipped & (1 << b))
                print(f"\r[{t:7.2f}s] STATUS 变化 0x{prev_status:08X} → 0x{st:08X}  翻转: {bits}" + " " * 10)
            prev_status = st

            # 左右限位翻转 → 永久事件行
            if prev_l is not None and stopl != prev_l:
                evt = "触发(active)" if stopl else "释放(idle)"
                print(f"\r[{t:7.2f}s] ◀ 左限位 STOPL {evt}" + " " * 30)
            if prev_r is not None and stopr != prev_r:
                evt = "触发(active)" if stopr else "释放(idle)"
                print(f"\r[{t:7.2f}s] ▶ 右限位 STOPR {evt}" + " " * 30)
            prev_l, prev_r = stopl, stopr

            # 底部实时行（\r 覆盖刷新）
            print(f"\r  [{nread}] {fmt_live(stopl, stopr, st)}", end="", flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[INFO] 退出。")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
