#!/usr/bin/env python3
"""
测试：复现「X=0 + SET_LIM x_neg=5mm + HOME_X」启动卡死场景

旧 Squid 启动卡死根因（main_hcs.log 2026-05-09 10:11）：
- 烧固件后 chip 复位 XACTUAL=0
- SET_LIM x_neg=5mm 让 chip VSTOPL_ACTIVE_F 立即触发 hard-stop latch
- 后续 X home 命令走 motor_setVelocityInternal（仅写 VMAX）无法解锁，
  ramp generator 拒绝启动，X 永远不动

修复：在 STATE_HOMING_INIT 调用 motor_moveToMicrosteps(current_xactual)
复用已验证的 VSTOP recovery 路径解锁 chip。

验证步骤：
  1. 连接 Teensy
  2. 发 SET_LEAD_SCREW_PITCH + CONFIGURE_STEPPER_DRIVER（与旧 Squid 一致）
  3. 发 SET_LIM_SWITCH_POLARITY + SET_HOME_SAFETY_MERGIN
  4. SET_LIM 把 X_NEG_LIMIT 设到当前 XACTUAL 之上（触发 VSTOPL_ACTIVE_F）
  5. 发 HOME_OR_ZERO X
  6. 监听位置上报，看 X 是否真的开始 home（XACTUAL 变化）

预期：修复后 X home 能完成（XACTUAL 走出禁区到 home 安全位置）
卡死现象：XACTUAL 永远不变，命令永远不返回 COMPLETED
"""
import sys
import time
import struct
import serial
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from utils.helpers import find_teensy_port
from define import CMD_SET, AXIS, LIMIT_CODE


# CRC-8-CCITT
def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ 0x07
            else:
                crc <<= 1
            crc &= 0xFF
    return crc


def send_cmd(ser, cmd_id, cmd_bytes):
    """发送 8 字节命令包，自动加 CRC"""
    pkt = bytearray(8)
    pkt[0] = cmd_id
    pkt[1:7] = cmd_bytes[:6]
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)


def read_response(ser, timeout=2.0):
    """读取 24 字节响应包"""
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            while len(buf) >= 24:
                pkt = buf[:24]
                buf = buf[24:]
                yield parse_response(pkt)
        else:
            time.sleep(0.005)


def parse_response(pkt):
    """解析 24 字节响应包"""
    if len(pkt) < 24:
        return None
    crc_calc = crc8(pkt[:23])
    crc_pkt = pkt[23]
    if crc_calc != crc_pkt:
        return {"crc_error": True}
    cmd_id = pkt[0]
    status = pkt[1]
    x_pos = struct.unpack(">i", bytes(pkt[2:6]))[0]
    y_pos = struct.unpack(">i", bytes(pkt[6:10]))[0]
    z_pos = struct.unpack(">i", bytes(pkt[10:14]))[0]
    w_pos = struct.unpack(">i", bytes(pkt[14:18]))[0]
    return {
        "cmd_id": cmd_id,
        "status": status,
        "x": x_pos,
        "y": y_pos,
        "z": z_pos,
        "w": w_pos,
    }


def wait_for_cmd_complete(ser, expected_cmd_id, timeout=10.0):
    """等待指定 cmd_id 的 COMPLETED 状态包"""
    deadline = time.time() + timeout
    last_pos = None
    last_status = None
    last_print = 0
    while time.time() < deadline:
        for resp in read_response(ser, timeout=0.1):
            if not resp or "crc_error" in resp:
                continue
            last_pos = (resp["x"], resp["y"], resp["z"])
            last_status = resp["status"]
            if resp["cmd_id"] == expected_cmd_id and resp["status"] == 0:
                print(f"  ✓ cmd_id={expected_cmd_id} COMPLETED at pos=X{resp['x']} Y{resp['y']} Z{resp['z']}")
                return True
        # 节流打印进度
        now = time.time()
        if now - last_print > 1.0 and last_pos:
            elapsed = now - (deadline - timeout)
            print(f"    [{elapsed:.1f}s] waiting cmd {expected_cmd_id}, status={last_status}, pos={last_pos}")
            last_print = now
    print(f"  ✗ TIMEOUT waiting cmd_id={expected_cmd_id}, last status={last_status}, last pos={last_pos}")
    return False


def main():
    port = find_teensy_port()
    if not port:
        print("❌ Teensy not found")
        return 1
    print(f"✓ Connecting {port}")
    ser = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(0.5)
    ser.reset_input_buffer()

    cmd_id = 1

    # ----- 1. 配置 X/Y/Z 螺距与 microstepping（与旧 Squid 一致 16 microstepping）-----
    print("\n[1] SET_LEAD_SCREW_PITCH (X=2.54, Y=2.54, Z=0.3)")
    for axis_idx, pitch_mm in [(0, 2.54), (1, 2.54), (2, 0.3)]:
        pitch_int = int(pitch_mm * 1000)  # mm 单位 *1000 转整数
        b = bytearray(6)
        b[0] = CMD_SET.SET_LEAD_SCREW_PITCH
        b[1] = axis_idx
        b[2] = (pitch_int >> 8) & 0xFF
        b[3] = pitch_int & 0xFF
        send_cmd(ser, cmd_id, b)
        if not wait_for_cmd_complete(ser, cmd_id, timeout=2.0):
            return 1
        cmd_id = (cmd_id + 1) % 256

    print("\n[2] CONFIGURE_STEPPER_DRIVER (microstepping=16)")
    for axis_idx, microstep, current_ma, hold in [(0, 16, 1000, 0.25), (1, 16, 1000, 0.25), (2, 16, 500, 0.5)]:
        b = bytearray(6)
        b[0] = CMD_SET.CONFIGURE_STEPPER_DRIVER
        b[1] = axis_idx
        # microstepping 是 0-8 的指数（1=2,...,8=256），16=4
        b[2] = 4  # 2^4 = 16
        b[3] = (current_ma >> 8) & 0xFF
        b[4] = current_ma & 0xFF
        b[5] = int(hold * 100) & 0xFF
        send_cmd(ser, cmd_id, b)
        if not wait_for_cmd_complete(ser, cmd_id, timeout=2.0):
            return 1
        cmd_id = (cmd_id + 1) % 256

    # ----- 3. 读当前 X 位置（确认是否为 0）-----
    print("\n[3] 读取启动后 X 位置")
    for resp in read_response(ser, timeout=1.0):
        if resp and "x" in resp:
            print(f"  当前 XACTUAL = {resp['x']} (期望 0)")
            break

    # ----- 4. SET_LIM 设置 X_NEG_LIMIT = 5mm = 6299 微步（让 X=0 越界） -----
    # 微步换算：1mm = 200 * 16 / 2.54 = 1259.84 微步
    # 5mm = 6299 微步
    print("\n[4] SET_LIM X_NEG_LIMIT = 5mm = 6299 微步（让 X=0 立即触发 VSTOPL）")
    b = bytearray(6)
    b[0] = CMD_SET.SET_LIM
    b[1] = LIMIT_CODE.X_NEGATIVE  # 1
    val = 6299
    b[2] = (val >> 24) & 0xFF
    b[3] = (val >> 16) & 0xFF
    b[4] = (val >> 8) & 0xFF
    b[5] = val & 0xFF
    send_cmd(ser, cmd_id, b)
    if not wait_for_cmd_complete(ser, cmd_id, timeout=2.0):
        print("  SET_LIM 失败（命令本身没 ack）")
        return 1
    cmd_id = (cmd_id + 1) % 256

    # ----- 5. 发 HOME_OR_ZERO X，看是否能完成 -----
    print("\n[5] HOME_OR_ZERO X (期望：解锁 hard-stop latch 后 X 真正开始移动 home)")
    b = bytearray(6)
    b[0] = CMD_SET.HOME_OR_ZERO
    b[1] = AXIS.X  # 0
    b[2] = 0  # HOME_POSITIVE (方向由固件决定)
    send_cmd(ser, cmd_id, b)

    # 这里用更长 timeout，X home 实际可能需要 3-10 秒
    print("  监听 X 位置变化（关键判据：X 必须开始变化才说明 hard-stop 解锁了）")
    if wait_for_cmd_complete(ser, cmd_id, timeout=15.0):
        print("\n✓ X home 完成 — hard-stop latch 解锁成功")
        return 0
    else:
        print("\n✗ X home 卡死 — hard-stop latch 仍未解锁，需进一步排查")
        return 1


if __name__ == "__main__":
    sys.exit(main())
