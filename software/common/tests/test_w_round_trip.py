#!/usr/bin/env python3
"""W 轴 round-trip 位置准确性测试 (2026-05-25)

**完全复刻 GUI W Test 按钮** (main_window.py:run_w_test) 的流程：
  启用编码器 → send_homing (含自动 offset) → 循环 N 轮 (next ×7 + previous ×7)
  每个 move 后 sleep(0.5) 与 GUI 一致

增强：每步比对 ENC_POS 与期望位置，输出 ✓/✗（GUI 没做位置验证）。
"""

import struct
import sys
import time
import serial

PORT = "/dev/ttyACM0"
BAUD = 115200
RX_LEN = 24

AXIS_W = 5
CMD_HOME_OR_ZERO = 5
CMD_MOVE_W = 4
CMD_CONFIGURE_STAGE_PID = 25

HOME_NEGATIVE = 1

# W 量纲（与 software 配置一致）
W_STEPS_PER_REV = 200 * 64       # = 12800
W_STEPS_PER_SLOT = W_STEPS_PER_REV // 8   # = 1600 (8 槽)
W_OFFSET_MICROSTEPS = 102        # SQUID_FILTERWHEEL_OFFSET = 0.008 mm
TOLERANCE_MICROSTEPS = 30        # ±30 µstep ≈ ±0.84° 视觉无法察觉

# 测试参数
TEST_ROUNDS = 10         # 总轮数
SLOTS_PER_DIRECTION = 7  # 每轮 next ×7, previous ×7 (完整一圈)


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


def parse_response(pkt):
    if len(pkt) != RX_LEN or crc8(pkt[:23]) != pkt[23]:
        return None
    return {
        "cmd_id": pkt[0],
        "status": pkt[1],
        "w": struct.unpack(">i", bytes(pkt[14:18]))[0],
    }


class ResponseReader:
    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()

    def drain(self):
        if self.ser.in_waiting:
            self.buf.extend(self.ser.read(self.ser.in_waiting))
        results = []
        while self.buf:
            if len(self.buf) >= RX_LEN:
                cand = bytes(self.buf[:RX_LEN])
                p = parse_response(cand)
                if p:
                    results.append(p)
                    del self.buf[:RX_LEN]
                    continue
            nl = self.buf.find(b"\n")
            if nl >= 0:
                line = bytes(self.buf[:nl]).decode("latin1", errors="replace").strip()
                if line and not line.startswith("[WHOMING") and not line.startswith("[WMOVE"):
                    print(f"  [TEXT] {line}")
                del self.buf[:nl + 1]
                continue
            if len(self.buf) > 4 * RX_LEN:
                del self.buf[0]
                continue
            break
        return results


def wait_until_idle(reader, timeout_s=15.0, min_idle_frames=5):
    """等 IN_PROGRESS → COMPLETED 持续 5 帧"""
    t_start = time.perf_counter()
    deadline = t_start + timeout_s
    idle_streak = 0
    saw_in_progress = False
    last_w = None
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            last_w = resp["w"]
            if resp["status"] == 1:
                saw_in_progress = True
                idle_streak = 0
            elif resp["status"] == 0 and saw_in_progress:
                idle_streak += 1
                if idle_streak >= min_idle_frames:
                    return (True, time.perf_counter() - t_start, last_w)
        time.sleep(0.005)
    return (False, time.perf_counter() - t_start, last_w)


def send_move_w(ser, seq, delta_microsteps):
    val = delta_microsteps & 0xFFFFFFFF
    send_cmd(ser, seq, CMD_MOVE_W,
             b2=(val >> 24) & 0xFF, b3=(val >> 16) & 0xFF,
             b4=(val >> 8) & 0xFF, b5=val & 0xFF)


def main():
    print(f"[INFO] 连接 {PORT}")
    print(f"[INFO] W 量纲: {W_STEPS_PER_REV} µstep/rev, {W_STEPS_PER_SLOT} µstep/slot")
    print(f"[INFO] 测试参数: {TEST_ROUNDS} 轮, 每轮 next ×{SLOTS_PER_DIRECTION} / previous ×{SLOTS_PER_DIRECTION}")
    print(f"[INFO] 容差: ±{TOLERANCE_MICROSTEPS} µstep ≈ ±{TOLERANCE_MICROSTEPS/W_STEPS_PER_REV*360:.2f}°")

    ser = serial.Serial(PORT, BAUD, timeout=0.05)
    time.sleep(1.0)
    reader = ResponseReader(ser)
    reader.drain()

    seq = [1]
    def next_seq():
        s = seq[0]
        seq[0] = (s + 1) & 0xFF
        return s

    # Step 1: 启用编码器
    print(f"\n[STEP] 启用 W 编码器 (CONFIGURE_STAGE_PID, tpr=4000)")
    send_cmd(ser, next_seq(), CMD_CONFIGURE_STAGE_PID,
             b2=AXIS_W, b3=0, b4=0x0F, b5=0xA0)
    time.sleep(0.5)
    reader.drain()

    # Step 2: send_homing 完整流程（与 GUI main_window.py:send_homing 一致）
    #   = HOME_OR_ZERO → wait_until_idle → MOVE_W (offset) → wait_until_idle
    print(f"\n[STEP] send_homing (HOME + offset，与 GUI 完全一致)")
    send_cmd(ser, next_seq(), CMD_HOME_OR_ZERO, b2=AXIS_W, b3=HOME_NEGATIVE)
    ok, elapsed_home, w_home = wait_until_idle(reader, timeout_s=20)
    if not ok:
        print(f"  ✗ Homing 超时")
        return 1
    print(f"  ✓ Homing {elapsed_home*1000:.0f} ms, W={w_home}")
    # GUI send_homing 紧接着发 offset MOVE_W（在同函数内，无 sleep 分隔）
    send_move_w(ser, next_seq(), W_OFFSET_MICROSTEPS)
    ok, elapsed_off, w = wait_until_idle(reader, timeout_s=10)
    print(f"  ✓ Offset MOVE_W +{W_OFFSET_MICROSTEPS} {elapsed_off*1000:.0f} ms, W={w}")
    # GUI run_w_test 在 send_homing 后 sleep(1.0) 让 chip 完全静稳
    # (GUI 后接的 wait_until_idle 是 polling 已 IDLE 立即返回 True，相当于 no-op，省略)
    time.sleep(1.0)
    reader.drain()  # 清掉等待期间的位置包

    expected_pos = w  # 从实测位置开始算（包含 offset 完成后的精确值）

    # Step 3: 循环 next × M / previous × M (与 GUI run_w_test 一致，每步 sleep 0.5)
    fail_count = 0
    total_count = 0
    for round_idx in range(1, TEST_ROUNDS + 1):
        print(f"\n=== Round {round_idx}/{TEST_ROUNDS} ===")

        # Next ×M
        for i in range(1, SLOTS_PER_DIRECTION + 1):
            time.sleep(0.5)   # ★ GUI run_w_test 同款间隔
            send_move_w(ser, next_seq(), W_STEPS_PER_SLOT)
            ok, elapsed, w = wait_until_idle(reader, timeout_s=5)
            expected_pos += W_STEPS_PER_SLOT
            err = w - expected_pos
            total_count += 1
            if not ok:
                print(f"  Next #{i}: ✗ 超时, W={w}")
                fail_count += 1
            elif abs(err) > TOLERANCE_MICROSTEPS:
                print(f"  Next #{i}: ✗ W={w}, expected={expected_pos}, err={err:+d}")
                fail_count += 1
            else:
                print(f"  Next #{i}: ✓ W={w}, expected={expected_pos}, err={err:+d}, {elapsed*1000:.0f} ms")

        # Previous ×M
        for i in range(1, SLOTS_PER_DIRECTION + 1):
            time.sleep(0.5)   # ★ GUI run_w_test 同款间隔
            send_move_w(ser, next_seq(), -W_STEPS_PER_SLOT)
            ok, elapsed, w = wait_until_idle(reader, timeout_s=5)
            expected_pos -= W_STEPS_PER_SLOT
            err = w - expected_pos
            total_count += 1
            if not ok:
                print(f"  Prev #{i}: ✗ 超时, W={w}")
                fail_count += 1
            elif abs(err) > TOLERANCE_MICROSTEPS:
                print(f"  Prev #{i}: ✗ W={w}, expected={expected_pos}, err={err:+d}")
                fail_count += 1
            else:
                print(f"  Prev #{i}: ✓ W={w}, expected={expected_pos}, err={err:+d}, {elapsed*1000:.0f} ms")

    # Summary
    print(f"\n[SUMMARY] {total_count - fail_count}/{total_count} 通过, "
          f"{fail_count} 失败 ({fail_count/total_count*100:.0f}%)")
    print(f"  期望最终位置: {expected_pos}")
    print(f"  实测最终位置: {w}")
    print(f"  累计漂移: {w - expected_pos:+d} µstep "
          f"({(w - expected_pos)/W_STEPS_PER_REV*360:+.2f}°)")

    ser.close()
    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
