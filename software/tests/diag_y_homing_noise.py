#!/usr/bin/env python3
"""
Y 轴 homing 异响诊断脚本

目的：在不重烧 firmware 的前提下，扫描 Y homing 的 (微步 × 速度) 组合，
让用户耳听判断哪种参数组合最安静。

前置条件：
  - GUI 未连接（脚本独占 Teensy 串口）
  - firmware 已烧入支持 `S:SET_HOMING_VEL <axis> <vel>` 调试命令的版本

流程：
  [0] configure_actuators（pitch + 微步 + 电流 + vmax/accel + polarity + margin）
  [1] widen_soft_limits（消除 GUI 残留 SET_LIM）
  [2] HOME 所有 X/Y/Z（建立坐标系）
  [3] Y MOVETO 中心 (~41mm)
  [4] 组合循环：
       for (ms, vel) in combos:
         CONFIGURE_STEPPER_DRIVER Y ms
         S:SET_HOMING_VEL Y vel
         等用户按 Enter
         HOME Y
         记 dt
         询问用户评分 (1=很安静 / 5=很吵)
         MOVETO Y 中心
  [5] 输出 CSV
"""
import argparse
import csv
import os
import struct
import sys
import time
from collections import OrderedDict
from datetime import datetime

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from utils.helpers import find_teensy_port  # noqa: E402


# ============================================================================
# 协议（与 benchmark_xyz_speed.py 一致：8 字节命令 + 24 字节响应）
# ============================================================================

CMD_MOVE = {"X": 0, "Y": 1, "Z": 2}
CMD_MOVETO = {"X": 6, "Y": 7, "Z": 8}
CMD_SET_LIM = 9
CMD_HOME = 5
CMD_SET_LIM_SWITCH_POLARITY = 20
CMD_CONFIGURE_STEPPER_DRIVER = 21
CMD_SET_MAX_VELOCITY_ACCELERATION = 22
CMD_SET_LEAD_SCREW_PITCH = 23
CMD_SET_HOME_SAFETY_MERGIN = 28
RESPONSE_LEN = 24

LIM_X_POS, LIM_X_NEG = 0, 1
LIM_Y_POS, LIM_Y_NEG = 2, 3
LIM_Z_POS, LIM_Z_NEG = 4, 5

HOME_AXIS = {"X": 0, "Y": 1, "Z": 2}
AXIS_PROTOCOL = {"X": 0, "Y": 1, "Z": 2}
HOME_POSITIVE = 0
HOME_NEGATIVE = 1


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def send_cmd(ser, cmd_id, cmd, payload_int32):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = cmd
    val = payload_int32 & 0xFFFFFFFF
    pkt[2] = (val >> 24) & 0xFF
    pkt[3] = (val >> 16) & 0xFF
    pkt[4] = (val >> 8) & 0xFF
    pkt[5] = val & 0xFF
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)


def parse_response(pkt):
    if len(pkt) != RESPONSE_LEN:
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
    }


class ResponseReader:
    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()

    def drain(self):
        if self.ser.in_waiting:
            self.buf.extend(self.ser.read(self.ser.in_waiting))
        results = []
        while len(self.buf) > 0:
            if len(self.buf) >= RESPONSE_LEN:
                candidate = bytes(self.buf[:RESPONSE_LEN])
                if crc8(candidate[:23]) == candidate[23]:
                    results.append(parse_response(candidate))
                    del self.buf[:RESPONSE_LEN]
                    continue
            nl = self.buf.find(b"\n")
            if nl >= 0:
                del self.buf[: nl + 1]
                continue
            if len(self.buf) > 4 * RESPONSE_LEN:
                del self.buf[0]
                continue
            break
        return results


def wait_completed(reader, expected_cmd_id, timeout_s=15.0,
                   min_idle_frames=5, expect_motion=True):
    deadline = time.perf_counter() + timeout_s
    last_pos = {"x": None, "y": None, "z": None, "w": None}
    idle_streak = 0
    saw_in_progress = not expect_motion
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            if not resp or resp.get("crc_error"):
                continue
            last_pos = {k: resp[k] for k in "xyzw"}
            if resp["cmd_id"] != expected_cmd_id:
                continue
            if resp["status"] == 1:
                saw_in_progress = True
                idle_streak = 0
            elif resp["status"] == 0:
                if saw_in_progress:
                    idle_streak += 1
                    if idle_streak >= min_idle_frames:
                        return time.perf_counter(), last_pos
        time.sleep(0.0005)
    return None, last_pos


# ============================================================================
# 轴参数（与 benchmark_xyz_speed.py / GUI 一致）
# ============================================================================

AXIS_PARAMS = OrderedDict([
    ("X", {"pitch_mm": 2.54, "microstepping": 256, "fs_per_rev": 200,
           "current_ma": 1000, "hold_ratio": 0.25, "movement_sign": 1,
           "vmax_mm_s": 30, "accel_mm_s2": 500,
           "home_pol": 1, "home_margin_um": 50,
           "test_range_um": (10_000, 112_000)}),
    ("Y", {"pitch_mm": 2.54, "microstepping": 256, "fs_per_rev": 200,
           "current_ma": 1000, "hold_ratio": 0.25, "movement_sign": 1,
           "vmax_mm_s": 30, "accel_mm_s2": 500,
           "home_pol": 1, "home_margin_um": 50,
           "test_range_um": (6_000, 76_000)}),
    ("Z", {"pitch_mm": 0.3, "microstepping": 256, "fs_per_rev": 200,
           "current_ma": 500, "hold_ratio": 0.5, "movement_sign": -1,
           "vmax_mm_s": 3.8, "accel_mm_s2": 20,
           "home_pol": 0, "home_margin_um": 50,
           "test_range_um": (100, 6_500)}),
])


def usteps_per_um(axis):
    p = AXIS_PARAMS[axis]
    usteps_per_rev = p["microstepping"] * p["fs_per_rev"]
    um_per_rev = p["pitch_mm"] * 1000.0
    return usteps_per_rev / um_per_rev


def um_to_usteps(axis, um):
    return int(round(um * usteps_per_um(axis)))


def usteps_to_um(axis, usteps):
    return usteps / usteps_per_um(axis)


def axis_center_um(axis):
    lo, hi = AXIS_PARAMS[axis]["test_range_um"]
    return (lo + hi) / 2


# ============================================================================
# 命令封装
# ============================================================================

def _pkt_set_lead_screw_pitch(cmd_id, axis_code, pitch_mm):
    pitch_x1000 = int(round(pitch_mm * 1000)) & 0xFFFF
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_LEAD_SCREW_PITCH
    pkt[2] = axis_code & 0xFF
    pkt[3] = (pitch_x1000 >> 8) & 0xFF
    pkt[4] = pitch_x1000 & 0xFF
    pkt[7] = crc8(pkt[:7])
    return pkt


def _pkt_config_stepper(cmd_id, axis_code, microstepping, current_ma, hold_ratio):
    if microstepping == 1:
        ms_byte = 0
    elif microstepping >= 256:
        ms_byte = 255
    else:
        ms_byte = int(microstepping) & 0xFF
    current_int = int(round(current_ma)) & 0xFFFF
    hold_byte = max(0, min(255, int(round(hold_ratio * 255))))
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_CONFIGURE_STEPPER_DRIVER
    pkt[2] = axis_code & 0xFF
    pkt[3] = ms_byte
    pkt[4] = (current_int >> 8) & 0xFF
    pkt[5] = current_int & 0xFF
    pkt[6] = hold_byte
    pkt[7] = crc8(pkt[:7])
    return pkt


def _pkt_set_max_velocity_accel(cmd_id, axis_code, vmax_mm_s, accel_mm_s2):
    vel_int = int(round(vmax_mm_s * 100)) & 0xFFFF
    acc_int = int(round(accel_mm_s2 * 10)) & 0xFFFF
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_MAX_VELOCITY_ACCELERATION
    pkt[2] = axis_code & 0xFF
    pkt[3] = (vel_int >> 8) & 0xFF
    pkt[4] = vel_int & 0xFF
    pkt[5] = (acc_int >> 8) & 0xFF
    pkt[6] = acc_int & 0xFF
    pkt[7] = crc8(pkt[:7])
    return pkt


def _pkt_set_lim_switch_polarity(cmd_id, axis_code, polarity):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_LIM_SWITCH_POLARITY
    pkt[2] = axis_code & 0xFF
    pkt[3] = polarity & 0xFF
    pkt[7] = crc8(pkt[:7])
    return pkt


def _pkt_set_home_margin(cmd_id, axis_code, margin_um):
    val = int(margin_um) & 0xFFFFFFFF
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_HOME_SAFETY_MERGIN
    pkt[2] = axis_code & 0xFF
    pkt[3] = (val >> 24) & 0xFF
    pkt[4] = (val >> 16) & 0xFF
    pkt[5] = (val >> 8) & 0xFF
    pkt[6] = val & 0xFF
    pkt[7] = crc8(pkt[:7])
    return pkt


def _pkt_set_lim(cmd_id, limit_code, value_int32):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_LIM
    pkt[2] = limit_code & 0xFF
    val = value_int32 & 0xFFFFFFFF
    pkt[3] = (val >> 24) & 0xFF
    pkt[4] = (val >> 16) & 0xFF
    pkt[5] = (val >> 8) & 0xFF
    pkt[6] = val & 0xFF
    pkt[7] = crc8(pkt[:7])
    return pkt


def _pkt_home(cmd_id, axis_code, direction=HOME_NEGATIVE):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_HOME
    pkt[2] = axis_code & 0xFF
    pkt[3] = direction & 0xFF
    pkt[7] = crc8(pkt[:7])
    return pkt


# ============================================================================
# 高层流程
# ============================================================================

def get_current_position(reader, timeout_s=2.0):
    deadline = time.perf_counter() + timeout_s
    last = None
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            if resp and not resp.get("crc_error"):
                last = resp
        if last:
            return {k: last[k] for k in "xyzw"}
        time.sleep(0.02)
    return None


def configure_actuators(ser, reader, cmd_id):
    print("\n[0] 配置 actuator（pitch + 微步 + 电流 + vmax/accel + polarity + margin）")
    for axis in ["X", "Y", "Z"]:
        p = AXIS_PARAMS[axis]
        ac = AXIS_PROTOCOL[axis]
        for pkt in [
            _pkt_set_lead_screw_pitch(cmd_id, ac, p["pitch_mm"]),
        ]:
            ser.write(pkt)
        wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
        cmd_id = (cmd_id + 1) % 256

        ser.write(_pkt_config_stepper(cmd_id, ac, p["microstepping"], p["current_ma"], p["hold_ratio"]))
        wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
        cmd_id = (cmd_id + 1) % 256

        ser.write(_pkt_set_max_velocity_accel(cmd_id, ac, p["vmax_mm_s"], p["accel_mm_s2"]))
        wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
        cmd_id = (cmd_id + 1) % 256

        ser.write(_pkt_set_lim_switch_polarity(cmd_id, ac, p["home_pol"]))
        wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
        cmd_id = (cmd_id + 1) % 256

        ser.write(_pkt_set_home_margin(cmd_id, ac, p["home_margin_um"]))
        wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
        cmd_id = (cmd_id + 1) % 256

        print(f"  ✓ {axis}: ms={p['microstepping']} current={p['current_ma']}mA "
              f"vmax={p['vmax_mm_s']} accel={p['accel_mm_s2']} pol={p['home_pol']}")
    return cmd_id


def widen_soft_limits(ser, reader, cmd_id):
    print("\n[1] 放宽 X/Y/Z 软限位（消除 GUI 残留 SET_LIM）")
    for axis, neg_code, pos_code in [
        ("X", LIM_X_NEG, LIM_X_POS),
        ("Y", LIM_Y_NEG, LIM_Y_POS),
        ("Z", LIM_Z_NEG, LIM_Z_POS),
    ]:
        wide_usteps = um_to_usteps(axis, 100_000)
        for code, val in [(neg_code, -wide_usteps), (pos_code, +wide_usteps)]:
            ser.write(_pkt_set_lim(cmd_id, code, val))
            wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
            cmd_id = (cmd_id + 1) % 256
    print("  ✓ 三轴软限位放宽至 ±100mm")
    return cmd_id


def home_all_axes(ser, reader, cmd_id):
    print("\n[2] HOME X/Y/Z（建立坐标系）")
    for axis in ["X", "Y", "Z"]:
        sign = AXIS_PARAMS[axis]["movement_sign"]
        direction = HOME_NEGATIVE if sign == 1 else HOME_POSITIVE
        t0 = time.perf_counter()
        ser.write(_pkt_home(cmd_id, HOME_AXIS[axis], direction=direction))
        t_end, _ = wait_completed(reader, cmd_id, timeout_s=30.0)
        if t_end is None:
            raise RuntimeError(f"{axis} HOME 超时")
        dt = (t_end - t0)
        time.sleep(0.3)
        print(f"  ✓ {axis} HOME 完成 ({dt:.2f}s)")
        cmd_id = (cmd_id + 1) % 256
    return cmd_id


def move_axis_to(ser, reader, axis, target_um, cmd_id, timeout_s=30.0):
    """MOVETO 用户坐标系下的绝对位置。"""
    sign = AXIS_PARAMS[axis]["movement_sign"]
    target_usteps = um_to_usteps(axis, target_um) * sign
    send_cmd(ser, cmd_id, CMD_MOVETO[axis], target_usteps)
    t_end, _ = wait_completed(reader, cmd_id, timeout_s=timeout_s)
    if t_end is None:
        raise RuntimeError(f"{axis} MOVETO {target_um}μm 超时")
    time.sleep(0.2)
    return (cmd_id + 1) % 256


DEBUG_HEADER = bytes([0x55, 0xAA])


def set_homing_velocity(ser, axis_name, vel_mm_s, verbose=True):
    """通过 ASCII 命令 S:SET_HOMING_VEL 设运行时 homingVelocityMM。
    Firmware 调试协议要求 ASCII 命令前置 0x55 0xAA 头（serial.h DEBUG_PROTOCOL_HEADER_1/2）。
    """
    msg = DEBUG_HEADER + f"S:SET_HOMING_VEL {axis_name} {vel_mm_s:.3f}\n".encode("ascii")
    ser.write(msg)
    # 等 firmware ACK（ASCII 响应行）
    deadline = time.perf_counter() + 1.0
    line = b""
    while time.perf_counter() < deadline:
        if ser.in_waiting:
            line += ser.read(ser.in_waiting)
            if b"S:SET_HOMING_VEL:OK" in line:
                if verbose:
                    ok_line = next(
                        (l for l in line.split(b"\n") if b"S:SET_HOMING_VEL:OK" in l),
                        None,
                    )
                    if ok_line:
                        print(f"  → {ok_line.decode('ascii', errors='replace').strip()}")
                return True
            if b"S:SET_HOMING_VEL:ERR" in line:
                err_line = next(
                    (l for l in line.split(b"\n") if b"S:SET_HOMING_VEL:ERR" in l),
                    None,
                )
                print(f"  ❌ {err_line.decode('ascii', errors='replace').strip() if err_line else line[:200]}")
                return False
        time.sleep(0.01)
    print(f"  ⚠ S:SET_HOMING_VEL 无响应（1s 超时）")
    return False


def configure_y_microstepping(ser, reader, ms, cmd_id):
    """运行时改 Y 微步，并同步本地 AXIS_PARAMS 用于单位换算。"""
    p = AXIS_PARAMS["Y"]
    ser.write(_pkt_config_stepper(cmd_id, AXIS_PROTOCOL["Y"], ms, p["current_ma"], p["hold_ratio"]))
    wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
    p["microstepping"] = ms
    return (cmd_id + 1) % 256


# ============================================================================
# 主流程
# ============================================================================

DEFAULT_COMBOS = [
    # (microstepping, velocity_mm_s)
    (256, 15),   # 当前 octoaxes baseline（前两次烧录后状态）
    (32, 15),    # 老 Squid baseline (microstepping 32 + 0.5 × 30 = 15)
    (256, 5),    # 低速 + 高微步
    (32, 5),     # 低速 + 低微步
    (256, 1),    # 极低速 + 高微步
    (32, 1),     # 极低速 + 低微步
    (256, 30),   # 满速 + 高微步
    (32, 30),    # 满速 + 低微步
    (256, 10),   # 中速 + 高微步
    (32, 10),    # 中速 + 低微步
]


def parse_combos(s):
    """解析 --combos 字符串：'256:15,32:15,256:5,...'"""
    combos = []
    for chunk in s.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        ms_str, vel_str = chunk.split(":")
        combos.append((int(ms_str), float(vel_str)))
    return combos


def prompt_rating():
    """让用户对当前组合打分。"""
    while True:
        ans = input("    评分 (1=很安静 / 2 / 3 / 4 / 5=很吵 / s=跳过 / q=退出): ").strip().lower()
        if ans == "q":
            return None, None
        if ans == "s":
            return -1, ""
        try:
            rating = int(ans)
            if 1 <= rating <= 5:
                notes = input("    备注（可空）: ").strip()
                return rating, notes
        except ValueError:
            pass
        print("    输入无效，请输 1-5 或 s/q")


def main():
    parser = argparse.ArgumentParser(description="Y homing 异响诊断 (微步 × 速度) 扫描")
    parser.add_argument("--port", default=None, help="串口（默认自动检测 Teensy）")
    parser.add_argument("--baud", type=int, default=2_000_000, help="波特率")
    parser.add_argument("--yes", action="store_true", help="跳过开始确认 prompt")
    parser.add_argument("--combos", default=None,
                        help="自定义组合 'ms:vel,ms:vel,...' 例：'32:5,32:15,256:5'")
    args = parser.parse_args()

    port = args.port or find_teensy_port()
    print(f"打开串口 {port} @ {args.baud}")
    ser = serial.Serial(port, args.baud, timeout=0.1)
    time.sleep(2.0)  # 等 Teensy USB 枚举完
    ser.reset_input_buffer()

    reader = ResponseReader(ser)
    cmd_id = 1

    try:
        # 启动自检：验证 firmware 支持 S:SET_HOMING_VEL 命令（避免 10 组合白跑）
        print("\n[预检] 验证 firmware 是否支持 S:SET_HOMING_VEL 命令...")
        ser.reset_input_buffer()
        ser.write(DEBUG_HEADER + b"S:SET_HOMING_VEL Y 15.0\n")  # 设回默认值
        time.sleep(0.5)
        check_buf = ser.read(ser.in_waiting)
        if b"S:SET_HOMING_VEL:OK" not in check_buf:
            print("  ❌ Firmware 未识别 S:SET_HOMING_VEL")
            print("     原因 1: firmware 未烧入支持版本 → `cd firmware/octoaxes && ./download.sh nointerlock`")
            print("     原因 2: 0x55 0xAA 前缀未生效（脚本已带，理论不会触发）")
            return
        print("  ✓ Firmware 支持新命令")

        cmd_id = configure_actuators(ser, reader, cmd_id)
        cmd_id = widen_soft_limits(ser, reader, cmd_id)
        cmd_id = home_all_axes(ser, reader, cmd_id)

        y_center = axis_center_um("Y")
        print(f"\n[3] Y MOVETO 中心 {y_center:.0f}μm（{y_center/1000:.1f}mm）")
        cmd_id = move_axis_to(ser, reader, "Y", y_center, cmd_id)

        combos = parse_combos(args.combos) if args.combos else DEFAULT_COMBOS
        print(f"\n[4] 开始扫描 {len(combos)} 个组合\n")

        if not args.yes:
            input("📢 准备听音了吗？按 Enter 开始 (Ctrl+C 取消)...")

        results = []
        for i, (ms, vel) in enumerate(combos, 1):
            print(f"\n--- [{i}/{len(combos)}] 组合: 微步={ms}, 速度={vel} mm/s ---")
            cmd_id = configure_y_microstepping(ser, reader, ms, cmd_id)
            if not set_homing_velocity(ser, "Y", vel):
                print("  ⚠ 设置速度失败，跳过")
                continue

            input("    按 Enter 开始 HOME...")
            t0 = time.perf_counter()
            ser.write(_pkt_home(cmd_id, HOME_AXIS["Y"], direction=HOME_NEGATIVE))
            t_end, _ = wait_completed(reader, cmd_id, timeout_s=120.0)
            cmd_id = (cmd_id + 1) % 256
            if t_end is None:
                print("    ❌ HOME 超时")
                continue
            dt = t_end - t0
            print(f"    HOME 完成 ({dt:.2f}s)")
            time.sleep(0.3)

            rating, notes = prompt_rating()
            if rating is None:
                print("用户取消")
                break
            results.append({
                "microstepping": ms,
                "velocity_mm_s": vel,
                "home_dt_s": round(dt, 3),
                "rating": rating if rating != -1 else "skipped",
                "notes": notes if rating != -1 else "",
            })

            # 回中心准备下一组
            print(f"    回 Y 中心 {y_center:.0f}μm")
            cmd_id = move_axis_to(ser, reader, "Y", y_center, cmd_id)

        # 保存结果
        if results:
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            results_dir = os.path.join(os.path.dirname(__file__), "results")
            os.makedirs(results_dir, exist_ok=True)
            csv_path = os.path.join(results_dir, f"y_homing_noise_diag_{ts}.csv")
            with open(csv_path, "w", newline="") as f:
                writer = csv.DictWriter(
                    f, fieldnames=["microstepping", "velocity_mm_s", "home_dt_s", "rating", "notes"]
                )
                writer.writeheader()
                writer.writerows(results)
            print(f"\n✅ 结果保存到 {csv_path}")

            # 简要汇总（按评分排序）
            print("\n汇总（按评分升序，1=最安静）：")
            sorted_rows = sorted(
                [r for r in results if r["rating"] != "skipped"],
                key=lambda r: (r["rating"], r["velocity_mm_s"], r["microstepping"]),
            )
            for r in sorted_rows:
                notes_str = f"  // {r['notes']}" if r["notes"] else ""
                print(f"  评分 {r['rating']} | ms={r['microstepping']:>3} vel={r['velocity_mm_s']:>5} | dt={r['home_dt_s']:>5.2f}s{notes_str}")

    finally:
        ser.close()
        print("\n串口已关闭")


if __name__ == "__main__":
    main()
