#!/usr/bin/env python3
"""octoaxesplus W2 编码器反馈验证（纯编码器，不开 PID）(2026-07-15)

流程：
  CONFIGURE_STAGE_PID(axis=6, flip, tpr=4000) 使能 W2 编码器
  → S:ENCPOS 基线 → MOVE_W2 逐槽转动 → 每步读 S:ENCPOS
  → 对比 W2 的 enc 与 xactual 增量（符号定 flip、比值验 tpr）
  → 反向转回起点，检查回零残差

判读：
  enc 恒为 0             → 编码器没接线/未使能
  Δenc/Δxactual ≈ +1     → flip=False 正确
  Δenc/Δxactual ≈ -1     → 需要 flip=True
  |ratio| 偏离 1         → tpr(4000) 不对，按比值反推真实线数

用法: python3 w2_encoder_check.py [port] [--flip]
"""

import re
import sys
import time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "/dev/ttyACM0"
FLIP = "--flip" in sys.argv
RX_LEN = 24

AXIS_W2 = 6
CMD_MOVE_W2 = 19
CMD_CONFIGURE_STAGE_PID = 25

TPR = 4000                      # 1000 lines x4 quadrature (pending confirmation by this script)
STEPS_PER_SLOT = 200 * 64 // 8  # 1600 µsteps/slot (ms=64, 8 slots)
N_SLOTS = 4                     # rotate 4 slots forward, then return in one move

DEBUG_HEADER = bytes([0x55, 0xAA])
ENCPOS_RE = re.compile(r"S:ENCPOS:(\w+):enc=(-?\d+) xactual=(-?\d+)")


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) if (crc & 0x80) else (crc << 1)
            crc &= 0xFF
    return crc


def send_cmd(ser, seq, cmd, b2=0, b3=0, b4=0, b5=0, b6=0):
    pkt = bytearray([seq, cmd, b2, b3, b4, b5, b6, 0])
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)


def read_encpos(ser, timeout_s=3.0):
    """发 S:ENCPOS，从二进制帧+ASCII 混流中抠出各轴 enc/xactual，直到 END 行"""
    ser.write(DEBUG_HEADER + b"S:ENCPOS\n")
    buf = bytearray()
    result = {}
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if ser.in_waiting:
            buf.extend(ser.read(ser.in_waiting))
            text = buf.decode("latin1", errors="replace")
            for m in ENCPOS_RE.finditer(text):
                result[m.group(1)] = (int(m.group(2)), int(m.group(3)))
            if "S:ENCPOS:END" in text:
                return result
        time.sleep(0.02)
    return result


def main():
    print(f"[INFO] 连接 {PORT}  flip={int(FLIP)} tpr={TPR}")
    ser = serial.Serial(PORT, 115200, timeout=0.05)
    time.sleep(1.0)
    ser.reset_input_buffer()

    seq = [1]
    def next_seq():
        s = seq[0]
        seq[0] = (s + 1) & 0xFF
        return s

    print(f"\n[STEP] CONFIGURE_STAGE_PID: W2(axis=6) flip={int(FLIP)} tpr={TPR}（仅使能编码器，无 PID）")
    send_cmd(ser, next_seq(), CMD_CONFIGURE_STAGE_PID,
             b2=AXIS_W2, b3=1 if FLIP else 0,
             b4=(TPR >> 8) & 0xFF, b5=TPR & 0xFF)
    time.sleep(0.5)
    ser.reset_input_buffer()

    base = read_encpos(ser)
    if "W2" not in base:
        print("[FAIL] S:ENCPOS 没有 W2 行 —— 固件可能不认轴码 6 的 cmd25 或 W2 未实例化")
        return 1
    enc0, x0 = base["W2"]
    print(f"[BASE] W2 enc={enc0} xactual={x0}")

    samples = [(enc0, x0)]
    for i in range(N_SLOTS):
        val = STEPS_PER_SLOT & 0xFFFFFFFF
        send_cmd(ser, next_seq(), CMD_MOVE_W2,
                 b2=(val >> 24) & 0xFF, b3=(val >> 16) & 0xFF,
                 b4=(val >> 8) & 0xFF, b5=val & 0xFF)
        time.sleep(1.5)  # 1-slot move + settling (fixed wait is most reliable, no status-bit dependency)
        ser.reset_input_buffer()
        pos = read_encpos(ser)
        if "W2" not in pos:
            print(f"[FAIL] 第 {i+1} 槽后读不到 W2 ENCPOS")
            return 1
        enc, x = pos["W2"]
        d_enc, d_x = enc - samples[-1][0], x - samples[-1][1]
        ratio = (d_enc / d_x) if d_x else float("nan")
        print(f"[SLOT +{i+1}] enc={enc} xactual={x}  Δenc={d_enc} Δxactual={d_x} ratio={ratio:+.4f}")
        samples.append((enc, x))

    # return in one move
    back = (-STEPS_PER_SLOT * N_SLOTS) & 0xFFFFFFFF
    send_cmd(ser, next_seq(), CMD_MOVE_W2,
             b2=(back >> 24) & 0xFF, b3=(back >> 16) & 0xFF,
             b4=(back >> 8) & 0xFF, b5=back & 0xFF)
    time.sleep(2.5)
    ser.reset_input_buffer()
    pos = read_encpos(ser)
    encN, xN = pos.get("W2", (None, None))
    print(f"[RETURN] enc={encN} xactual={xN}（回起点，残差 enc-enc0={encN - enc0 if encN is not None else '?'}）")

    # summary
    d_enc_total = samples[-1][0] - samples[0][0]
    d_x_total = samples[-1][1] - samples[0][1]
    print("\n===== 判读 =====")
    if d_enc_total == 0:
        print("✗ 编码器全程无计数 —— 未接线 / cmd25 未生效")
        return 1
    ratio = d_enc_total / d_x_total if d_x_total else float("nan")
    print(f"正向 {N_SLOTS} 槽累计: Δenc={d_enc_total} Δxactual={d_x_total} ratio={ratio:+.4f}")
    if abs(ratio - 1.0) < 0.05:
        print(f"✓ 方向一致且比值≈1 —— flip={int(FLIP)} 正确、tpr={TPR} 正确")
    elif abs(ratio + 1.0) < 0.05:
        print(f"✗ 方向相反 —— 换 flip（当前 {int(FLIP)} → {int(not FLIP)}）重跑")
    else:
        real_tpr = TPR * abs(1.0 / ratio) if ratio else 0
        print(f"✗ 比值偏离 ±1 —— tpr 不对，实际约 {real_tpr:.0f}（按 |1/ratio|×{TPR} 反推）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
