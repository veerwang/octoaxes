#!/usr/bin/env python3
"""W (Filter Wheel) 轴运动速度基线测试 (2026-05-26)

输出：
  documents/baselines/benchmark_w_<TS>.csv  原始数据
  documents/baselines/benchmark_w_<TS>.md   汇总报告

测试方法（每个距离 × 2 × TRIALS_PER_DIR）：
  - 正/负向交替（+d, -d, +d, -d, ...），位置回到起点
  - 测「命令往返耗时」：上位机发 MOVE_W → 固件回 COMPLETED (status=0) 的总时间
  - 统计 mean / std / min / max（ms）

距离档位（微步，W 量纲 1mm pitch / 64 µstep / 200 FS = 12800 µstep/rev）：
  50 (1.4°) / 200 (5.6°) / 800 (22.5°) / 1600 (45°=1 slot) /
  3200 (90°) / 4800 (135°) / 6400 (180°=4 slot)

前置：W 已上电；脚本会发送 configure_squidfilter 等价序列对齐旧 Squid 配置，
然后 home+offset 到 1 号孔位起点（slot 1 center）。
"""
import argparse
import csv
import os
import statistics
import struct
import subprocess
import sys
import time
from collections import OrderedDict

import serial

PORT = "/dev/ttyACM0"
BAUD = 115200
RX_LEN = 24

# ============================================================================
# 协议层
# ============================================================================

CMD_MOVE_W = 4
CMD_HOME_OR_ZERO = 5
CMD_CONFIGURE_STEPPER_DRIVER = 21
CMD_SET_MAX_VELOCITY_ACCELERATION = 22
CMD_SET_LEAD_SCREW_PITCH = 23
CMD_INITFILTERWHEEL = 253
CMD_CONFIGURE_STAGE_PID = 25
CMD_ENABLE_STAGE_PID = 26
CMD_DISABLE_STAGE_PID = 27
CMD_SET_PID_ARGUMENTS = 29

# W PID 默认参数（与旧 Squid _def.py PID_P_W / PID_I_W / PID_D_W 一致）
W_PID_P = 4096      # 1 << 12
W_PID_I = 1
W_PID_D = 1
W_ENCODER_TRANSITIONS_PER_REV = 4000   # 与 test_w_round_trip.py 一致
W_ENCODER_FLIP_DIR = False

AXIS_W = 5
HOME_NEGATIVE = 1
HOME_OR_ZERO_ZERO = 2

# W 默认参数（与 firmware/octoaxes/config.h W_AXIS 一致）
W_PITCH_MM = 1.0
W_MICROSTEPPING = 16   # 2026-05-27 ms=8→16 降噪（ms=8 共振杂音；用速度换静音，1 slot 72→87ms）
W_CURRENT_MA = 3100
W_HOLD_RATIO = 0.5
W_VMAX_MM_S = 4.2
W_ACCEL_MM_S2 = 400

# W 量纲（从 W_MICROSTEPPING 派生，跟着 CLI 覆盖一起变）
W_STEPS_PER_REV = 200 * W_MICROSTEPPING
W_STEPS_PER_SLOT = W_STEPS_PER_REV // 8                                 # 8 槽位
W_OFFSET_MICROSTEPS = int(round(0.008 * W_STEPS_PER_REV / W_PITCH_MM))  # SQUID_FILTERWHEEL_OFFSET=0.008mm


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) if (crc & 0x80) else (crc << 1)
            crc &= 0xFF
    return crc


def _send_packet(ser, b):
    b[7] = crc8(b[:7])
    ser.write(bytes(b))


def send_move_w(ser, cmd_id, delta_microsteps):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_MOVE_W
    val = delta_microsteps & 0xFFFFFFFF
    pkt[2] = (val >> 24) & 0xFF
    pkt[3] = (val >> 16) & 0xFF
    pkt[4] = (val >> 8) & 0xFF
    pkt[5] = val & 0xFF
    _send_packet(ser, pkt)


def send_home_w(ser, cmd_id, direction=HOME_NEGATIVE):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_HOME_OR_ZERO
    pkt[2] = AXIS_W
    pkt[3] = direction
    _send_packet(ser, pkt)


def send_init_filter_wheel(ser, cmd_id):
    """旧 Squid 必须先发 INITFILTERWHEEL 才会 enable_filterwheel=true，
    否则后续 set_leadscrew_pitch/MOVE_W/HOME 全被静默跳过。
    octoaxes (2026-05-26+) 此命令是 no-op + 日志，不冲突。"""
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_INITFILTERWHEEL
    _send_packet(ser, pkt)


def send_set_leadscrew(ser, cmd_id, pitch_mm):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_LEAD_SCREW_PITCH
    pkt[2] = AXIS_W
    p = int(pitch_mm * 1000) & 0xFFFF
    pkt[3] = (p >> 8) & 0xFF
    pkt[4] = p & 0xFF
    _send_packet(ser, pkt)


def send_configure_driver(ser, cmd_id, microstepping, current_ma, hold_ratio):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_CONFIGURE_STEPPER_DRIVER
    pkt[2] = AXIS_W
    pkt[3] = 255 if microstepping == 256 else (0 if microstepping == 1 else microstepping)
    pkt[4] = (current_ma >> 8) & 0xFF
    pkt[5] = current_ma & 0xFF
    pkt[6] = int(hold_ratio * 255) & 0xFF
    _send_packet(ser, pkt)


def send_set_pid_arguments(ser, cmd_id, p, i, d):
    """SET_PID_ARGUMENTS: cmd[2]=axis, cmd[3:4]=P (uint16 BE), cmd[5]=I, cmd[6]=D"""
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_PID_ARGUMENTS
    pkt[2] = AXIS_W
    pkt[3] = (p >> 8) & 0xFF
    pkt[4] = p & 0xFF
    pkt[5] = i & 0xFF
    pkt[6] = d & 0xFF
    _send_packet(ser, pkt)


def send_configure_stage_pid(ser, cmd_id, flip_direction, transitions_per_rev):
    """CONFIGURE_STAGE_PID: cmd[2]=axis, cmd[3]=flip, cmd[4:5]=tpr (uint16 BE)"""
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_CONFIGURE_STAGE_PID
    pkt[2] = AXIS_W
    pkt[3] = 1 if flip_direction else 0
    pkt[4] = (transitions_per_rev >> 8) & 0xFF
    pkt[5] = transitions_per_rev & 0xFF
    _send_packet(ser, pkt)


def send_enable_stage_pid(ser, cmd_id):
    """ENABLE_STAGE_PID: cmd[2]=axis"""
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_ENABLE_STAGE_PID
    pkt[2] = AXIS_W
    _send_packet(ser, pkt)


def send_set_max_velocity_accel(ser, cmd_id, vel_mm_s, accel_mm_s2):
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_MAX_VELOCITY_ACCELERATION
    pkt[2] = AXIS_W
    v = int(vel_mm_s * 100) & 0xFFFF
    a = int(accel_mm_s2 * 10) & 0xFFFF
    pkt[3] = (v >> 8) & 0xFF
    pkt[4] = v & 0xFF
    pkt[5] = (a >> 8) & 0xFF
    pkt[6] = a & 0xFF
    _send_packet(ser, pkt)


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
                del self.buf[:nl + 1]
                continue
            if len(self.buf) > 4 * RX_LEN:
                del self.buf[0]
                continue
            break
        return results


def wait_completed(reader, expected_cmd_id, timeout_s=15.0,
                   min_idle_frames=None, expect_motion=True):
    if min_idle_frames is None:
        min_idle_frames = _IDLE_FRAMES
    """等 cmd_id 回 COMPLETED + 连续 5 帧 idle 防抖。"""
    deadline = time.perf_counter() + timeout_s
    last_w = None
    idle_streak = 0
    saw_in_progress = not expect_motion
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            if not resp:
                continue
            last_w = resp["w"]
            if resp["cmd_id"] != expected_cmd_id:
                continue
            if resp["status"] == 1:
                saw_in_progress = True
                idle_streak = 0
            elif resp["status"] == 0 and saw_in_progress:
                idle_streak += 1
                if idle_streak >= min_idle_frames:
                    return time.perf_counter(), last_w
        time.sleep(0.0005)
    return None, last_w


def get_current_w(reader, ser, timeout_s=1.0):
    deadline = time.perf_counter() + timeout_s
    last = None
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            if resp:
                last = resp
        if last:
            return last["w"]
        time.sleep(0.02)
    return None


# ============================================================================
# 测试主流程
# ============================================================================

# 距离档位以角度定义，跨微步配置直接物理可比
DISTANCES_ANGLE_DEG = [1.4, 5.6, 22.5, 45.0, 90.0, 135.0, 180.0]
DISTANCES_USTEPS = [max(1, int(round(deg / 360.0 * W_STEPS_PER_REV))) for deg in DISTANCES_ANGLE_DEG]
TRIALS_PER_DIR = 10
DEFAULT_IDLE_FRAMES = 5      # 全局默认；可通过 --idle-frames 覆盖
_IDLE_FRAMES = DEFAULT_IDLE_FRAMES


def configure_w(ser, reader, cmd_id):
    """对齐旧 Squid configure_squidfilter(W) 协议序列。

    步骤：INITFILTERWHEEL（旧 Squid 必须，octoaxes no-op）→ set_leadscrew_pitch
    → configure_motor_driver → set_max_velocity_acceleration。
    """
    print("[1] INITFILTERWHEEL (cmd 253) — 旧 Squid 启用 filter wheel；octoaxes no-op")
    send_init_filter_wheel(ser, cmd_id)
    wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
    cmd_id = (cmd_id + 1) % 256
    time.sleep(0.5)  # 旧 Squid cephla.py 在 init_filter_wheel 后等 0.5s

    print("[2] 配置 W 轴（set_leadscrew_pitch / configure_motor_driver / set_max_velocity_acceleration）")
    for sender, args in [
        (send_set_leadscrew, (W_PITCH_MM,)),
        (send_configure_driver, (W_MICROSTEPPING, W_CURRENT_MA, W_HOLD_RATIO)),
        (send_set_max_velocity_accel, (W_VMAX_MM_S, W_ACCEL_MM_S2)),
    ]:
        sender(ser, cmd_id, *args)
        wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
        cmd_id = (cmd_id + 1) % 256
    print(f"  ✓ W 配置: pitch={W_PITCH_MM}mm, ms={W_MICROSTEPPING}, vmax={W_VMAX_MM_S}mm/s, accel={W_ACCEL_MM_S2}mm/s²")
    return cmd_id


def configure_pid(ser, reader, cmd_id):
    """启用 W 轴 PID 闭环。
    顺序：SET_PID_ARGUMENTS → CONFIGURE_STAGE_PID (encoder init + push P/I/D 到 chip)
         → ENABLE_STAGE_PID (chip PID_BPG0 写入)
    """
    print(f"[2b] 启用 W PID 闭环（P={W_PID_P} I={W_PID_I} D={W_PID_D} tpr={W_ENCODER_TRANSITIONS_PER_REV}）")
    send_set_pid_arguments(ser, cmd_id, W_PID_P, W_PID_I, W_PID_D)
    wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
    cmd_id = (cmd_id + 1) % 256
    send_configure_stage_pid(ser, cmd_id, W_ENCODER_FLIP_DIR, W_ENCODER_TRANSITIONS_PER_REV)
    wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
    cmd_id = (cmd_id + 1) % 256
    send_enable_stage_pid(ser, cmd_id)
    wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
    cmd_id = (cmd_id + 1) % 256
    print(f"  ✓ PID 已启用")
    return cmd_id


def home_and_offset(ser, reader, cmd_id):
    print("[3] HOME W (HOME_NEGATIVE) ...")
    t0 = time.perf_counter()
    send_home_w(ser, cmd_id, HOME_NEGATIVE)
    t_end, w = wait_completed(reader, cmd_id, timeout_s=30.0)
    if t_end is None:
        raise RuntimeError("W HOME 超时")
    print(f"  ✓ HOME 完成 {(t_end - t0)*1000:.0f}ms, W={w}")
    cmd_id = (cmd_id + 1) % 256

    print(f"[4] Offset MOVE_W +{W_OFFSET_MICROSTEPS} (slot 1 center) ...")
    send_move_w(ser, cmd_id, W_OFFSET_MICROSTEPS)
    t_end, w = wait_completed(reader, cmd_id, timeout_s=10.0)
    if t_end is None:
        raise RuntimeError("W offset MOVE 超时")
    print(f"  ✓ Offset 完成, W={w}")
    cmd_id = (cmd_id + 1) % 256
    time.sleep(0.5)  # 让 chip 静稳
    reader.drain()
    return cmd_id


def benchmark_distance(ser, reader, dist_usteps, cmd_id):
    rows = []
    pos_dt = []
    neg_dt = []

    for trial in range(TRIALS_PER_DIR):
        for direction in (+1, -1):
            payload = dist_usteps * direction
            start_w = get_current_w(reader, ser, timeout_s=0.3)
            t0 = time.perf_counter()
            send_move_w(ser, cmd_id, payload)
            t_end, end_w = wait_completed(reader, cmd_id, timeout_s=15.0)
            if t_end is None:
                print(f"  dist={dist_usteps} dir={direction:+d} trial={trial} TIMEOUT")
                cmd_id = (cmd_id + 1) % 256
                continue
            dt_ms = (t_end - t0) * 1000
            rows.append({
                "distance_usteps": dist_usteps,
                "direction": direction,
                "trial": trial,
                "dt_ms": dt_ms,
                "start_usteps": start_w if start_w is not None else 0,
                "end_usteps": end_w,
            })
            (pos_dt if direction > 0 else neg_dt).append(dt_ms)
            cmd_id = (cmd_id + 1) % 256

    def stats(lst):
        if not lst:
            return None
        return {
            "n": len(lst),
            "mean": statistics.mean(lst),
            "stdev": statistics.stdev(lst) if len(lst) > 1 else 0,
            "min": min(lst),
            "max": max(lst),
        }

    return rows, {"distance_usteps": dist_usteps, "pos": stats(pos_dt), "neg": stats(neg_dt)}, cmd_id


# ============================================================================
# 报告
# ============================================================================


def write_csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "distance_usteps", "direction", "trial", "dt_ms",
            "start_usteps", "end_usteps"
        ])
        w.writeheader()
        for r in rows:
            w.writerow(r)


def angle_label(usteps):
    deg = usteps / W_STEPS_PER_REV * 360
    if usteps == W_STEPS_PER_SLOT:
        return f"{usteps} µstep ({deg:.1f}° = 1 slot)"
    if usteps % W_STEPS_PER_SLOT == 0:
        return f"{usteps} µstep ({deg:.0f}° = {usteps // W_STEPS_PER_SLOT} slots)"
    return f"{usteps} µstep ({deg:.1f}°)"


def write_markdown(path, summaries, meta):
    lines = []
    lines.append("# W (Filter Wheel) 轴运动速度基线报告")
    lines.append("")
    lines.append(f"**生成时间**: {meta['timestamp']}")
    lines.append(f"**Firmware commit**: `{meta['firmware_commit']}`")
    lines.append(f"**PID 闭环**: {'**ON** (P={} I={} D={} tpr={})'.format(W_PID_P, W_PID_I, W_PID_D, W_ENCODER_TRANSITIONS_PER_REV) if meta.get('pid_enabled') else 'OFF (开环)'}")
    lines.append(f"**测试条件**: 命令往返耗时（上位机发 MOVE_W → 固件回 COMPLETED + 5 帧 idle 防抖）")
    lines.append(f"**重复**: 每方向 {TRIALS_PER_DIR} 次 trial，正负方向交替（位置回到起点）")
    lines.append("")
    lines.append("## W 轴配置（执行时下发）")
    lines.append("")
    lines.append(f"- 螺距 (pitch): {W_PITCH_MM} mm")
    lines.append(f"- 微步 (microstepping): {W_MICROSTEPPING}")
    lines.append(f"- 整步数/转 (full steps per rev): 200")
    lines.append(f"- 每转微步数: {W_STEPS_PER_REV} µstep")
    lines.append(f"- 每槽位微步数: {W_STEPS_PER_SLOT} µstep (= 45°)")
    lines.append(f"- 最大速度: {W_VMAX_MM_S} mm/s")
    lines.append(f"- 最大加速度: {W_ACCEL_MM_S2} mm/s²")
    lines.append(f"- ASTART: 0（与旧 Squid sRampInit 对齐）")
    lines.append(f"- 电机峰值电流: {W_CURRENT_MA} mA (I_hold ratio = {W_HOLD_RATIO})")
    lines.append("")
    lines.append("## 指标说明")
    lines.append("")
    lines.append("- **mean / std / min / max**：单位 ms")
    lines.append("- **dt**：上位机 perf_counter 测量，包含 USB 串口往返 + 固件命令处理 + 电机 ramp + 10ms 上报间隔 + 5 帧 idle 防抖 (~50ms)")
    lines.append("- **+**：next direction（chip XACTUAL +µstep）")
    lines.append("- **−**：previous direction（chip XACTUAL -µstep）")
    lines.append("")
    lines.append("## 结果")
    lines.append("")
    lines.append("| 距离 | 方向 | n | mean (ms) | std (ms) | min (ms) | max (ms) |")
    lines.append("|------|------|---|-----------|----------|----------|----------|")
    for s in summaries:
        label = angle_label(s["distance_usteps"])
        for dirname, key in [("+", "pos"), ("−", "neg")]:
            d = s[key]
            if d is None:
                lines.append(f"| {label} | {dirname} | 0 | N/A | N/A | N/A | N/A |")
            else:
                lines.append(f"| {label} | {dirname} | {d['n']} | {d['mean']:.2f} | {d['stdev']:.2f} | {d['min']:.2f} | {d['max']:.2f} |")
    lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


# ============================================================================
# 入口
# ============================================================================


def get_git_commit():
    try:
        r = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                           capture_output=True, text=True, timeout=2,
                           cwd=os.path.dirname(__file__))
        return r.stdout.strip() if r.returncode == 0 else "unknown"
    except Exception:
        return "unknown"


def main():
    global W_PID_P, W_PID_I, W_PID_D, _IDLE_FRAMES
    parser = argparse.ArgumentParser(description="W 轴运动速度基线测试")
    parser.add_argument("--label", default="", help="附加到输出文件名的标签（如 octoaxes / oldsquid）")
    parser.add_argument("--pid", action="store_true", help="启用 W PID 闭环（默认 false 仅开环）")
    parser.add_argument("--pid-p", type=int, default=W_PID_P, help="PID P 增益（uint16）")
    parser.add_argument("--pid-i", type=int, default=W_PID_I, help="PID I 增益（uint8）")
    parser.add_argument("--pid-d", type=int, default=W_PID_D, help="PID D 增益（uint8）")
    parser.add_argument("--idle-frames", type=int, default=DEFAULT_IDLE_FRAMES,
                        help=f"wait_completed 连续 idle 帧防抖数（默认 {DEFAULT_IDLE_FRAMES}，1=贴近 GUI wait_till_operation_is_completed）")
    args = parser.parse_args()
    W_PID_P = args.pid_p
    W_PID_I = args.pid_i
    W_PID_D = args.pid_d
    _IDLE_FRAMES = args.idle_frames

    ts = time.strftime("%Y%m%d_%H%M%S")
    commit = get_git_commit()
    suffix = f"_{args.label}" if args.label else ""
    print(f"\n[INFO] benchmark_w_speed | TS={ts}{suffix} | commit={commit}")
    print(f"[INFO] PORT={PORT} BAUD={BAUD}\n")

    ser = serial.Serial(PORT, BAUD, timeout=0.05)
    time.sleep(1.0)
    reader = ResponseReader(ser)
    reader.drain()

    cmd_id = 1
    cmd_id = configure_w(ser, reader, cmd_id)
    if args.pid:
        cmd_id = configure_pid(ser, reader, cmd_id)
    else:
        print("[2b] PID 关闭（开环模式）")
    cmd_id = home_and_offset(ser, reader, cmd_id)

    print("\n[5] 测试距离档位")
    print(f"  距离档位: {DISTANCES_USTEPS} µstep")
    print(f"  每档位 {TRIALS_PER_DIR} 次正向 + {TRIALS_PER_DIR} 次负向交替\n")

    all_rows = []
    summaries = []
    for dist in DISTANCES_USTEPS:
        deg = dist / W_STEPS_PER_REV * 360
        print(f"  → {dist} µstep ({deg:.1f}°) ...")
        rows, summary, cmd_id = benchmark_distance(ser, reader, dist, cmd_id)
        all_rows.extend(rows)
        summaries.append(summary)
        p = summary["pos"]; n = summary["neg"]
        if p and n:
            print(f"    + mean {p['mean']:.1f} ms (std {p['stdev']:.1f}) | − mean {n['mean']:.1f} ms (std {n['stdev']:.1f})")

    ser.close()

    out_dir = os.path.join(os.path.dirname(__file__), "..", "..", "..", "documents", "baselines")
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    csv_path = os.path.join(out_dir, f"benchmark_w_{ts}{suffix}.csv")
    md_path = os.path.join(out_dir, f"benchmark_w_{ts}{suffix}.md")
    write_csv(csv_path, all_rows)
    write_markdown(md_path, summaries, {"timestamp": ts, "firmware_commit": commit, "pid_enabled": args.pid})

    print(f"\n[OUTPUT]")
    print(f"  CSV: {csv_path}")
    print(f"  MD : {md_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
