#!/usr/bin/env python3
"""W 轴 homing 序列完整回归测试 (2026-05-21)

严格复刻 GUI `send_homing` 的协议序列：
  1. CONFIGURE_STAGE_PID(W)     启用 ABN 编码器，让位置上报走 ENC_POS
  2. HOME_OR_ZERO(W, NEGATIVE)  发起 homing
  3. wait_until_idle            等 IN_PROGRESS → COMPLETED + 5 帧连续 idle
  4. MOVE_W(+offset)            发送 offset 移动（来自 SQUID_FILTERWHEEL_OFFSET）
  5. wait_until_idle            等 offset 完成
  6. 读 ENC_POS                 编码器位置确认

所有耗时 + chip 位置 trace 输出供回归对比。
"""

import struct
import sys
import time
import serial

PORT = "/dev/ttyACM0"
BAUD = 115200
RX_LEN = 24

# 协议轴码
AXIS_W = 5

# 命令字
CMD_HOME_OR_ZERO = 5
CMD_MOVE_W = 4
CMD_CONFIGURE_STAGE_PID = 25

# data[3] for HOME_OR_ZERO
HOME_POSITIVE = 0
HOME_NEGATIVE = 1

# GUI 端常量（与 software/common/define.py + software/octoaxes/constants.py 一致）
SQUID_FILTERWHEEL_OFFSET_MM = -0.011    # 2026-05-22 实测匹配硬件 1 号孔位 -141 µstep
W_MOVEMENT_SIGN = 1                      # 与旧 Squid 一致（home 朝 - 方向 search）
# W 轴量纲（2026-05-21 量纲对齐后 1mm pitch, 64 microstep，与旧 Squid 一致）
W_SCREW_PITCH_MM = 1.0
W_MICROSTEPPING = 64
W_FULLSTEPS = 200
W_STEPS_PER_MM = W_FULLSTEPS * W_MICROSTEPPING / W_SCREW_PITCH_MM   # = 12800
W_MM_PER_STEP = 1.0 / W_STEPS_PER_MM                                  # = 7.8125e-05

# 编码器分辨率（与 firmware ABN config 一致）
W_ENCODER_TPR = 4000
W_ENCODER_FLIP = 0


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) if (crc & 0x80) else (crc << 1)
            crc &= 0xFF
    return crc


def send_cmd(ser, seq, cmd, b2=0, b3=0, b4=0, b5=0, b6=0):
    pkt = bytearray(8)
    pkt[0] = seq & 0xFF
    pkt[1] = cmd & 0xFF
    pkt[2] = b2 & 0xFF
    pkt[3] = b3 & 0xFF
    pkt[4] = b4 & 0xFF
    pkt[5] = b5 & 0xFF
    pkt[6] = b6 & 0xFF
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)
    return pkt


def parse_response(pkt):
    if len(pkt) != RX_LEN:
        return None
    if crc8(pkt[:23]) != pkt[23]:
        return {"crc_error": True}
    return {
        "cmd_id": pkt[0],
        "status": pkt[1],
        "x": struct.unpack(">i", bytes(pkt[2:6]))[0],
        "y": struct.unpack(">i", bytes(pkt[6:10]))[0],
        "z": struct.unpack(">i", bytes(pkt[10:14]))[0],
        "w": struct.unpack(">i", bytes(pkt[14:18]))[0],
        "fw_ver": pkt[22],
    }


class ResponseReader:
    """字节级解析器，处理 24 字节响应和 ASCII debug 行"""
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
                if crc8(cand[:23]) == cand[23]:
                    results.append(parse_response(cand))
                    del self.buf[:RX_LEN]
                    continue
            nl = self.buf.find(b"\n")
            if nl >= 0:
                line = bytes(self.buf[:nl]).decode("latin1", errors="replace").strip()
                if line:
                    print(f"  [TEXT] {line}")
                del self.buf[: nl + 1]
                continue
            if len(self.buf) > 4 * RX_LEN:
                del self.buf[0]
                continue
            break
        return results


def wait_until_idle(reader, timeout_s=15.0, min_idle_frames=5, expect_motion=True):
    """与 main_window.py:wait_until_idle 行为一致：见 IN_PROGRESS 后连续 5 帧 idle 才视为完成

    返回 (success: bool, elapsed_s: float, last_w: int, all_packets: list)
    """
    t_start = time.perf_counter()
    deadline = t_start + timeout_s
    idle_streak = 0
    saw_in_progress = not expect_motion
    last_w = None
    last_status = None
    all_packets = []

    while time.perf_counter() < deadline:
        for resp in reader.drain():
            if not resp or resp.get("crc_error"):
                continue
            all_packets.append((time.perf_counter() - t_start, resp["status"], resp["w"]))
            last_w = resp["w"]
            if resp["status"] != last_status:
                tag = {0: "COMPLETED", 1: "IN_PROGRESS"}.get(resp["status"], f"?{resp['status']}")
                print(f"  [{(time.perf_counter() - t_start) * 1000:7.1f} ms] {tag:11s} W={resp['w']:9d}")
                last_status = resp["status"]
            if resp["status"] == 1:
                saw_in_progress = True
                idle_streak = 0
            elif resp["status"] == 0:
                if saw_in_progress:
                    idle_streak += 1
                    if idle_streak >= min_idle_frames:
                        return (True, time.perf_counter() - t_start, last_w, all_packets)
        time.sleep(0.005)
    return (False, time.perf_counter() - t_start, last_w, all_packets)


def microsteps_to_deg(us, steps_per_rev=W_FULLSTEPS * W_MICROSTEPPING):
    return us / steps_per_rev * 360


def microsteps_to_mm(us, mm_per_step=W_MM_PER_STEP):
    return us * mm_per_step


def main():
    print(f"[INFO] 连接 {PORT}")
    print(f"[INFO] W 量纲: pitch={W_SCREW_PITCH_MM} mm, microstep={W_MICROSTEPPING}, "
          f"steps_per_mm={W_STEPS_PER_MM}, mm_per_step={W_MM_PER_STEP}")
    print(f"[INFO] SQUID_FILTERWHEEL_OFFSET={SQUID_FILTERWHEEL_OFFSET_MM} mm")
    ser = serial.Serial(PORT, BAUD, timeout=0.05, write_timeout=0.5)
    time.sleep(1.0)
    reader = ResponseReader(ser)
    reader.drain()
    time.sleep(0.3)
    reader.drain()

    # ------------------------------------------------------------------
    # Step 1: 启用 W 编码器 (CONFIGURE_STAGE_PID)
    # ------------------------------------------------------------------
    print(f"\n[STEP 1] 启用 W ABN 编码器 (CONFIGURE_STAGE_PID, tpr={W_ENCODER_TPR})")
    pkt = send_cmd(ser, seq=1, cmd=CMD_CONFIGURE_STAGE_PID,
                   b2=AXIS_W, b3=W_ENCODER_FLIP,
                   b4=(W_ENCODER_TPR >> 8) & 0xFF, b5=W_ENCODER_TPR & 0xFF)
    print(f"  → {pkt.hex()}")
    time.sleep(0.3)
    reader.drain()

    # ------------------------------------------------------------------
    # Step 2: 读初始位置（编码器启用后）
    # ------------------------------------------------------------------
    time.sleep(0.3)
    pkts = []
    t_end = time.time() + 0.5
    while time.time() < t_end:
        pkts.extend(reader.drain())
        time.sleep(0.05)
    w_init = pkts[-1]["w"] if pkts else 0
    fw_ver = pkts[-1]["fw_ver"] if pkts else 0
    print(f"\n[INIT] fw_ver=0x{fw_ver:02X}, W (ENC_POS) = {w_init} us = "
          f"{microsteps_to_mm(w_init):+.4f} mm = {microsteps_to_deg(w_init):+.2f}°")

    # ------------------------------------------------------------------
    # Step 3: 发 HOME_OR_ZERO(W, NEGATIVE)
    # ------------------------------------------------------------------
    # GUI 派生：home_dir = 1 if sign == 1 else 0
    home_dir = HOME_NEGATIVE if W_MOVEMENT_SIGN == 1 else HOME_POSITIVE
    home_dir_name = "NEGATIVE" if home_dir == HOME_NEGATIVE else "POSITIVE"
    print(f"\n[STEP 2] 发 HOME_OR_ZERO W {home_dir_name}")
    send_cmd(ser, seq=2, cmd=CMD_HOME_OR_ZERO, b2=AXIS_W, b3=home_dir)

    print(f"  等待 wait_until_idle (timeout 15s)...")
    success, homing_elapsed, w_after_home, _ = wait_until_idle(reader, timeout_s=15.0)
    print(f"\n  Homing {'COMPLETED' if success else 'TIMEOUT'} in {homing_elapsed * 1000:.1f} ms")
    print(f"  W after homing = {w_after_home} us = "
          f"{microsteps_to_mm(w_after_home):+.4f} mm = {microsteps_to_deg(w_after_home):+.2f}°")
    if not success:
        print(f"  [ERROR] homing 超时，跳过 offset")
        ser.close()
        return 1

    # ------------------------------------------------------------------
    # Step 4: 发 MOVE_W offset (与 GUI _move_step_axis_relative_position 一致)
    # ------------------------------------------------------------------
    offset_um = int(SQUID_FILTERWHEEL_OFFSET_MM * 1000)
    offset_microsteps = int(offset_um / 1000.0 / W_MM_PER_STEP)
    print(f"\n[STEP 3] 发 MOVE_W offset = {offset_um} μm = {offset_microsteps} 微步 "
          f"= {microsteps_to_deg(offset_microsteps):+.2f}° (1 个孔位偏移)")
    val = offset_microsteps & 0xFFFFFFFF
    send_cmd(ser, seq=3, cmd=CMD_MOVE_W,
             b2=(val >> 24) & 0xFF, b3=(val >> 16) & 0xFF,
             b4=(val >> 8) & 0xFF, b5=val & 0xFF)

    print(f"  等待 wait_until_idle (timeout 15s)...")
    success, offset_elapsed, w_final, packets = wait_until_idle(reader, timeout_s=15.0)
    print(f"\n  Offset {'COMPLETED' if success else 'TIMEOUT'} in {offset_elapsed * 1000:.1f} ms")

    # ------------------------------------------------------------------
    # Step 5: 编码器位置确认 + trace 分析
    # ------------------------------------------------------------------
    # 等待几个 idle 帧让 chip 完全停稳
    time.sleep(0.3)
    pkts = []
    t_end = time.time() + 0.3
    while time.time() < t_end:
        pkts.extend(reader.drain())
        time.sleep(0.05)
    w_settled = pkts[-1]["w"] if pkts else w_final

    print(f"\n[FINAL] W (ENC_POS) = {w_settled} us")
    print(f"        = {microsteps_to_mm(w_settled):+.4f} mm")
    print(f"        = {microsteps_to_deg(w_settled):+.2f}°")
    print(f"        期望 +{offset_microsteps} 微步 = +{microsteps_to_deg(offset_microsteps):.2f}°")
    error_us = w_settled - offset_microsteps
    print(f"        误差 = {error_us:+d} 微步 = {microsteps_to_deg(error_us):+.3f}°")

    # 过冲分析
    if packets:
        w_max = max(packets, key=lambda s: s[2])
        w_min = min(packets, key=lambda s: s[2])
        print(f"\n[TRACE] Offset 阶段轨迹:")
        print(f"  W max  = {w_max[2]} us @ t={w_max[0] * 1000:.1f} ms "
              f"({microsteps_to_deg(w_max[2]):+.2f}°)")
        print(f"  W min  = {w_min[2]} us @ t={w_min[0] * 1000:.1f} ms "
              f"({microsteps_to_deg(w_min[2]):+.2f}°)")
        overshoot = max(0, w_max[2] - offset_microsteps)
        if overshoot > 0:
            print(f"  Overshoot = +{overshoot} 微步 = +{microsteps_to_deg(overshoot):.2f}° "
                  f"(target {offset_microsteps} 微步)")

    print(f"\n[SUMMARY]")
    print(f"  Homing 耗时:  {homing_elapsed * 1000:.0f} ms")
    print(f"  Offset 耗时:  {offset_elapsed * 1000:.0f} ms")
    print(f"  最终 ENC_POS: {w_settled} us = {microsteps_to_deg(w_settled):+.2f}°")
    print(f"  位置误差:     {error_us:+d} 微步")

    ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
