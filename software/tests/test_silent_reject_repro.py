#!/usr/bin/env python3
"""
复现：Axis::moveRelativeMicrosteps 在 non-IDLE 状态时静默 return false bug

Bug 描述（SESSION.md 2026-05-08 推测，本脚本目的是验证）：
- `firmware/octoaxes/axis.cpp:573` 看到 `_currentState != STATE_IDLE` 直接 return false
- 上层 commandprocessor 不检查返回值；_isMoving 不会因 reject 而改变
- 全局 cmd_id 在 serial.cpp:102 被刷成新值（reject 不影响）
- 下一帧 send_position_update 上报 cmd_id=新值 + status 来自 any_moving 状态

→ 上位机收到「cmd 101 → status=COMPLETED」误以为命令成功，但电机只走了 cmd 100 的距离

复现策略（双场景对照）：

  Scenario CONTROL（控制组）：
    1. MOVETO 中心
    2. 记 p0
    3. send MOVE_X +20mm；等 COMPLETED；记 p1（应 ≈ p0+20mm）
    4. send MOVE_X +5mm；等 COMPLETED；记 p2（应 ≈ p0+25mm）
    → 累计位移 ≈ 25mm，证明协议正常工作

  Scenario BUG（实验组）：
    1. MOVETO 中心
    2. 记 p0
    3. send MOVE_X +20mm（不等完成）
    4. sleep 200ms（在 cmd1 的 ~1s 运动中段）
    5. send MOVE_X +5mm（应被静默 reject）
    6. 等 cmd2 的 status=COMPLETED 帧（实际是 cmd1 运动完成）
    7. 记 p_final
    → 若有 bug：p_final - p0 ≈ 20mm（不是 25mm）
    → 若无 bug：要么 cmd2 报错，要么 p_final - p0 ≈ 25mm（被排队/重试）

判定（脚本末尾）：
- BUG_DETECTED 当且仅当：CONTROL 走够 25mm + BUG 只走 20mm（差距 ≥ 4mm）
- 同时打印 cmd2 的响应时序，辅助理解 firmware 行为

安全：
- X 测试范围 50-80mm（home 后绝对位置），远离限位
- 全程不动 Y/Z
- 若 X 未 home，脚本会拒绝运行（避免位置未知撞限位）

用法：
  python3 software/tests/test_silent_reject_repro.py
"""
import argparse
import os
import struct
import sys
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from utils.helpers import find_teensy_port  # noqa: E402


# 与 benchmark_xyz_speed.py / commandprocessor.cpp 一致
CMD_MOVE_X = 0
CMD_MOVETO_X = 6
CMD_HOME = 5
CMD_SET_LIM = 9
CMD_CONFIGURE_STEPPER_DRIVER = 21
CMD_SET_LEAD_SCREW_PITCH = 23
CMD_SET_LIM_SWITCH_POLARITY = 20
CMD_SET_HOME_SAFETY_MERGIN = 28
CMD_SET_MAX_VELOCITY_ACCELERATION = 22
RESPONSE_LEN = 24
STATUS_COMPLETED = 0
STATUS_IN_PROGRESS = 1

# SET_LIM 限位码（commandprocessor.cpp LIM_CODE_*）
LIM_X_POS = 0
LIM_X_NEG = 1

# HOME 协议参数（与 benchmark_xyz_speed.py 一致）
HOME_AXIS_X = 0
HOME_NEGATIVE = 1     # X movement_sign=+1 → direction=NEGATIVE

# X 轴参数（与 benchmark_xyz_speed.py 一致）
X_PITCH_MM = 2.54
X_MICROSTEPPING = 256
X_FS_PER_REV = 200
X_USTEPS_PER_UM = (X_MICROSTEPPING * X_FS_PER_REV) / (X_PITCH_MM * 1000.0)


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) if (crc & 0x80) else (crc << 1)
            crc &= 0xFF
    return crc


def send_cmd(ser, cmd_id, cmd, payload_bytes):
    """payload_bytes 长度 ≤ 5，自动填充到 5 字节 + CRC"""
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = cmd
    for i, b in enumerate(payload_bytes[:5]):
        pkt[2 + i] = b
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)


def send_int32_cmd(ser, cmd_id, cmd, value):
    """payload = int32 大端"""
    v = value & 0xFFFFFFFF
    send_cmd(ser, cmd_id, cmd, [
        (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
    ])


def parse_response(pkt):
    if len(pkt) != RESPONSE_LEN or crc8(pkt[:23]) != pkt[23]:
        return None
    return {
        "cmd_id": pkt[0],
        "status": pkt[1],
        "x": struct.unpack(">i", bytes(pkt[2:6]))[0],
        "y": struct.unpack(">i", bytes(pkt[6:10]))[0],
        "z": struct.unpack(">i", bytes(pkt[10:14]))[0],
        "w": struct.unpack(">i", bytes(pkt[14:18]))[0],
    }


class Reader:
    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()

    def drain(self):
        if self.ser.in_waiting:
            self.buf.extend(self.ser.read(self.ser.in_waiting))
        out = []
        while len(self.buf) >= RESPONSE_LEN:
            cand = bytes(self.buf[:RESPONSE_LEN])
            r = parse_response(cand)
            if r:
                out.append(r)
                del self.buf[:RESPONSE_LEN]
            else:
                del self.buf[0]
        return out


def wait_for_cmd_completion(reader, target_cmd_id, timeout_s=10.0,
                             min_idle_frames=3, log_history=None):
    """等待目标 cmd_id 出现 status=COMPLETED 帧。
    要求先看到 IN_PROGRESS 再连续 min_idle_frames 个 COMPLETED 才算真完成。
    log_history（list 或 None）：如果给，把过程中所有 cmd_id 匹配的响应记下来供分析。
    返回 (elapsed_s, last_response_dict) 或 (None, last_response_dict) 超时。
    """
    start = time.perf_counter()
    deadline = start + timeout_s
    saw_in_progress = False
    idle_streak = 0
    last = None
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            last = resp
            if resp["cmd_id"] != target_cmd_id:
                continue
            if log_history is not None:
                log_history.append((time.perf_counter() - start, resp["status"], resp["x"]))
            if resp["status"] == STATUS_IN_PROGRESS:
                saw_in_progress = True
                idle_streak = 0
            elif resp["status"] == STATUS_COMPLETED and saw_in_progress:
                idle_streak += 1
                if idle_streak >= min_idle_frames:
                    return time.perf_counter() - start, resp
        time.sleep(0.0005)
    return None, last


def wait_cmd_first_completed(reader, target_cmd_id, timeout_s=10.0,
                              log_history=None):
    """变体：返回 target_cmd_id 首次出现 status=COMPLETED 的时间。
    不要求先看 IN_PROGRESS — 用来检测 silent reject 的「秒级 COMPLETED」。
    """
    start = time.perf_counter()
    deadline = start + timeout_s
    last = None
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            last = resp
            if resp["cmd_id"] != target_cmd_id:
                continue
            if log_history is not None:
                log_history.append((time.perf_counter() - start, resp["status"], resp["x"]))
            if resp["status"] == STATUS_COMPLETED:
                return time.perf_counter() - start, resp
        time.sleep(0.0005)
    return None, last


def read_current_x(reader, timeout_s=1.0):
    """从位置流读一帧获取当前 X（微步）"""
    deadline = time.perf_counter() + timeout_s
    last = None
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            last = resp
        if last:
            return last["x"]
        time.sleep(0.01)
    return None


def configure_x_axis(ser, reader):
    """对齐 GUI startup：pitch + microstepping/current + vmax/accel + polarity + margin
    + 软限位放宽（仅 X 轴）

    协议参考 software/tests/benchmark_xyz_speed.py（已验证），轴码 0=X。
    """
    cid = 50
    axis = 0

    # SET_LEAD_SCREW_PITCH (cmd 23): byte[2]=axis, byte[3..4]=pitch_mm*1000 uint16 BE
    pitch_x1000 = int(round(X_PITCH_MM * 1000)) & 0xFFFF  # 2540
    pkt = bytearray(8)
    pkt[0] = cid; pkt[1] = CMD_SET_LEAD_SCREW_PITCH; pkt[2] = axis
    pkt[3] = (pitch_x1000 >> 8) & 0xFF
    pkt[4] = pitch_x1000 & 0xFF
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)
    time.sleep(0.05)
    cid += 1

    # CONFIGURE_STEPPER_DRIVER (cmd 21):
    #   byte[2]=axis, byte[3]=ms_byte (256→255), byte[4..5]=current_mA uint16 BE,
    #   byte[6]=hold_ratio*255
    ms_byte = 255 if X_MICROSTEPPING >= 256 else (X_MICROSTEPPING & 0xFF)
    current_ma = 1000
    hold_byte = int(round(0.25 * 255))  # 64
    pkt = bytearray(8)
    pkt[0] = cid; pkt[1] = CMD_CONFIGURE_STEPPER_DRIVER; pkt[2] = axis
    pkt[3] = ms_byte
    pkt[4] = (current_ma >> 8) & 0xFF
    pkt[5] = current_ma & 0xFF
    pkt[6] = hold_byte
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)
    time.sleep(0.05)
    cid += 1

    # SET_MAX_VELOCITY_ACCELERATION (cmd 22):
    #   byte[2]=axis, byte[3..4]=vmax×100 uint16 BE, byte[5..6]=accel×10 uint16 BE
    vmax_x100 = int(30 * 100) & 0xFFFF
    accel_x10 = int(500 * 10) & 0xFFFF
    pkt = bytearray(8)
    pkt[0] = cid; pkt[1] = CMD_SET_MAX_VELOCITY_ACCELERATION; pkt[2] = axis
    pkt[3] = (vmax_x100 >> 8) & 0xFF
    pkt[4] = vmax_x100 & 0xFF
    pkt[5] = (accel_x10 >> 8) & 0xFF
    pkt[6] = accel_x10 & 0xFF
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)
    time.sleep(0.1)
    cid += 1

    # SET_LIM_SWITCH_POLARITY (cmd 20): X polarity=1（对齐 configuration_HCS_v2.ini）
    pkt = bytearray(8)
    pkt[0] = cid; pkt[1] = CMD_SET_LIM_SWITCH_POLARITY; pkt[2] = axis; pkt[3] = 1
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)
    time.sleep(0.05)
    cid += 1

    # SET_HOME_SAFETY_MERGIN (cmd 28): X 50μm
    margin_um = 50
    pkt = bytearray(8)
    pkt[0] = cid; pkt[1] = CMD_SET_HOME_SAFETY_MERGIN; pkt[2] = axis
    pkt[3] = (margin_um >> 24) & 0xFF
    pkt[4] = (margin_um >> 16) & 0xFF
    pkt[5] = (margin_um >> 8) & 0xFF
    pkt[6] = margin_um & 0xFF
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)
    time.sleep(0.05)
    cid += 1


def home_x(ser, reader, cmd_id):
    """X 轴 home，复用 chip SW_RESET 路径清除 VSTOP latch 和 EVENTS 残留。

    完成后 X ≈ home_safety_margin (50μm)，处于已知干净状态。
    """
    print("[HOME] X 轴 homing（最长 30s）...", end=" ", flush=True)
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF; pkt[1] = CMD_HOME; pkt[2] = HOME_AXIS_X
    pkt[3] = HOME_NEGATIVE     # X movement_sign=+1 → 朝负方向 home
    pkt[7] = crc8(pkt[:7])
    t0 = time.perf_counter()
    ser.write(pkt)
    elapsed, _ = wait_for_cmd_completion(reader, cmd_id, timeout_s=30.0)
    if elapsed is None:
        raise RuntimeError("X HOME 超时（>30s）")
    # homing 完成后状态机还要 STATE_LEAVING_HOME → STATE_IDLE 过渡，多等 300ms
    time.sleep(0.3)
    p = read_current_x(reader)
    print(f"完成 ({elapsed:.2f}s, X = {usteps_to_mm(p):.4f}mm)")
    return p


def widen_x_soft_limits(ser, reader, cmd_id):
    """放宽 X 软限位到 ±150mm，消除任何残留窄限位。HOME 之后调用。"""
    print(f"[SET_LIM] X 软限位放宽到 (-50, 150)mm...", end=" ", flush=True)
    wide_lo = int(round(-50_000 * X_USTEPS_PER_UM))
    wide_hi = int(round(150_000 * X_USTEPS_PER_UM))
    for limit_code, val in [(LIM_X_NEG, wide_lo), (LIM_X_POS, wide_hi)]:
        v = val & 0xFFFFFFFF
        pkt = bytearray(8)
        pkt[0] = cmd_id & 0xFF; pkt[1] = CMD_SET_LIM; pkt[2] = limit_code
        pkt[3] = (v >> 24) & 0xFF
        pkt[4] = (v >> 16) & 0xFF
        pkt[5] = (v >> 8) & 0xFF
        pkt[6] = v & 0xFF
        pkt[7] = crc8(pkt[:7])
        ser.write(pkt)
        time.sleep(0.05)
        cmd_id = (cmd_id + 1) % 256
    print("OK")
    return cmd_id


def moveto_x_um(ser, reader, target_um, cmd_id):
    """绝对移动到 X = target_um（微步换算后发 MOVETO_X）

    校验：完成后实际位置应距目标 < 500μm，否则抛 RuntimeError（防止 MOVETO 被静默
    rejected 而脚本继续往下跑）。
    """
    target_usteps = int(round(target_um * X_USTEPS_PER_UM))
    send_int32_cmd(ser, cmd_id, CMD_MOVETO_X, target_usteps)
    elapsed, last = wait_for_cmd_completion(reader, cmd_id, timeout_s=20.0)
    if elapsed is None:
        raise RuntimeError(
            f"MOVETO X={target_um/1000:.1f}mm 超时 (cmd_id={cmd_id})。"
            "可能 MOVETO 被静默 rejected（软限位收太窄？configure 未生效？）"
        )
    # 验证实际到达
    time.sleep(0.1)
    actual = read_current_x(reader)
    actual_um = (actual / X_USTEPS_PER_UM)
    if abs(actual_um - target_um) > 500:
        raise RuntimeError(
            f"MOVETO X={target_um/1000:.1f}mm 完成但实际到 {actual_um/1000:.4f}mm "
            f"(差 {(actual_um-target_um):+.1f}μm)，疑似软限位 clamp"
        )
    return elapsed, last


def move_rel_x_um(ser, delta_um, cmd_id):
    """相对移动 X +delta_um"""
    delta_usteps = int(round(delta_um * X_USTEPS_PER_UM))
    send_int32_cmd(ser, cmd_id, CMD_MOVE_X, delta_usteps)


def usteps_to_mm(usteps):
    return usteps / X_USTEPS_PER_UM / 1000.0


# ============================================================================
# 测试场景
# ============================================================================

CENTER_X_UM = 60_000   # 60mm（X 范围 10-112mm 的中段偏左，留足正向运动余量）
MOVE_A_UM = 20_000     # +20mm
MOVE_B_UM = 5_000      # +5mm
GAP_MS_BUG = 200       # cmd1 → cmd2 间隔（cmd1 还在跑）


def run_scenario_control(ser, reader):
    """控制组：两次 MOVE 串行，验证协议正常。"""
    print("\n=== SCENARIO CONTROL: 两次 MOVE 串行 ===")

    # MOVETO 中心
    print(f"  MOVETO X = {CENTER_X_UM/1000:.1f} mm...")
    moveto_x_um(ser, reader, CENTER_X_UM, cmd_id=10)
    p0 = read_current_x(reader)
    print(f"  p0 = {usteps_to_mm(p0):.4f} mm")

    # MOVE +20mm，等完成
    move_rel_x_um(ser, MOVE_A_UM, cmd_id=11)
    e1, _ = wait_for_cmd_completion(reader, 11, timeout_s=15.0)
    p1 = read_current_x(reader)
    print(f"  cmd 11 MOVE +{MOVE_A_UM/1000:.0f}mm: completed in {e1*1000:.1f}ms, "
          f"p1 = {usteps_to_mm(p1):.4f} mm (Δ={usteps_to_mm(p1-p0)*1000:.1f}μm)")

    # MOVE +5mm，等完成
    move_rel_x_um(ser, MOVE_B_UM, cmd_id=12)
    e2, _ = wait_for_cmd_completion(reader, 12, timeout_s=15.0)
    p2 = read_current_x(reader)
    print(f"  cmd 12 MOVE +{MOVE_B_UM/1000:.0f}mm: completed in {e2*1000:.1f}ms, "
          f"p2 = {usteps_to_mm(p2):.4f} mm (Δ={usteps_to_mm(p2-p1)*1000:.1f}μm)")

    total_delta_mm = usteps_to_mm(p2 - p0)
    print(f"  累计位移: {total_delta_mm:.4f} mm（期望 ≈ {(MOVE_A_UM+MOVE_B_UM)/1000:.0f} mm）")
    return total_delta_mm


def run_scenario_bug(ser, reader):
    """实验组：cmd2 在 cmd1 仍在运动中发出。"""
    print("\n=== SCENARIO BUG: cmd2 在 cmd1 移动中发出 ===")

    # MOVETO 中心
    print(f"  MOVETO X = {CENTER_X_UM/1000:.1f} mm...")
    moveto_x_um(ser, reader, CENTER_X_UM, cmd_id=20)
    p0 = read_current_x(reader)
    print(f"  p0 = {usteps_to_mm(p0):.4f} mm")

    # 1. 发 cmd1 但不等
    t_send1 = time.perf_counter()
    move_rel_x_um(ser, MOVE_A_UM, cmd_id=21)
    print(f"  [t=0ms]  发 cmd 21 MOVE +{MOVE_A_UM/1000:.0f}mm（不等待）")

    # 2. sleep 让 cmd1 跑起来
    time.sleep(GAP_MS_BUG / 1000.0)
    p_mid = read_current_x(reader)
    print(f"  [t={GAP_MS_BUG}ms] 中段位置 = {usteps_to_mm(p_mid):.4f} mm "
          f"(Δ={usteps_to_mm(p_mid-p0)*1000:.1f}μm)")

    # 3. 发 cmd2（cmd1 还在跑）
    t_send2 = time.perf_counter()
    move_rel_x_um(ser, MOVE_B_UM, cmd_id=22)
    print(f"  [t={(t_send2-t_send1)*1000:.0f}ms] 发 cmd 22 MOVE +{MOVE_B_UM/1000:.0f}mm "
          "（cmd1 仍 in flight）")

    # 4. 跟踪 cmd 22 响应流
    history_22 = []
    e2_first, _ = wait_cmd_first_completed(reader, 22, timeout_s=15.0, log_history=history_22)
    p_final = read_current_x(reader)

    if e2_first is None:
        print("  ⚠ cmd 22 在 15s 内未见 COMPLETED")
        return None

    # 5. 分析 cmd 22 的响应轨迹
    print(f"\n  cmd 22 首次 COMPLETED 距发出: {e2_first*1000:.1f}ms")
    print(f"  cmd 22 响应轨迹（前 8 条）:")
    for (t, st, x) in history_22[:8]:
        sname = {0: "COMPLETED", 1: "IN_PROGRESS"}.get(st, f"?{st}")
        print(f"    t+{t*1000:6.1f}ms  status={sname:12s}  x={usteps_to_mm(x)*1000:8.1f}μm")
    if len(history_22) > 8:
        print(f"    ... 共 {len(history_22)} 条")

    # 6. 等额外一段时间，确保没有 deferred 移动
    time.sleep(1.5)
    p_settled = read_current_x(reader)
    print(f"\n  cmd 22 COMPLETED 后再等 1.5s，X = {usteps_to_mm(p_settled):.4f} mm")

    total_delta_mm = usteps_to_mm(p_settled - p0)
    print(f"  累计位移: {total_delta_mm:.4f} mm")
    print(f"    若无 bug 期望 ≈ {(MOVE_A_UM+MOVE_B_UM)/1000:.0f} mm（25mm，两条 MOVE 都执行）")
    print(f"    若有 bug 实际 ≈ {MOVE_A_UM/1000:.0f} mm（20mm，cmd 22 被静默 reject）")
    return total_delta_mm


def main():
    global GAP_MS_BUG
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default=None, help="串口路径（默认自动查找）")
    parser.add_argument("--skip-control", action="store_true",
                        help="跳过控制组（已确认协议正常时用）")
    parser.add_argument("--gap-ms", type=int, default=GAP_MS_BUG,
                        help=f"cmd1→cmd2 间隔（默认 {GAP_MS_BUG}ms）")
    args = parser.parse_args()
    GAP_MS_BUG = args.gap_ms

    port = args.port or find_teensy_port()
    if not port:
        print("✘ 找不到 Teensy 串口")
        sys.exit(1)
    print(f"使用串口: {port}")

    ser = serial.Serial(port, 2_000_000, timeout=0.05)
    time.sleep(0.5)
    reader = Reader(ser)
    reader.drain()  # 清空 boot 噪声

    print("配置 X 轴...")
    configure_x_axis(ser, reader)
    reader.drain()

    p_now = read_current_x(reader)
    if p_now is None:
        print("✘ 读不到位置上报，固件没启动？")
        sys.exit(2)
    print(f"配置完成，当前 X = {usteps_to_mm(p_now):.4f} mm")

    # Home X 清除 chip VSTOP latch / EVENTS sticky bit，确保测试从干净状态开始
    home_x(ser, reader, cmd_id=60)
    reader.drain()

    # Home 之后再放宽软限位（避免 X home 安全位置 < 收紧的下限）
    widen_x_soft_limits(ser, reader, cmd_id=70)
    reader.drain()

    control_delta = None
    if not args.skip_control:
        control_delta = run_scenario_control(ser, reader)

    bug_delta = run_scenario_bug(ser, reader)

    # 判定
    print("\n========== 判定 ==========")
    if control_delta is not None:
        print(f"CONTROL 累计位移: {control_delta:.4f} mm "
              f"(期望 {(MOVE_A_UM+MOVE_B_UM)/1000}mm)")
    if bug_delta is not None:
        print(f"BUG     累计位移: {bug_delta:.4f} mm")
        expected_no_bug = (MOVE_A_UM + MOVE_B_UM) / 1000.0
        expected_buggy = MOVE_A_UM / 1000.0
        if abs(bug_delta - expected_buggy) < 1.0:
            print(f"✗ BUG 复现成功：cmd 22 被静默 reject，实际位移 ≈ {expected_buggy}mm")
            print(f"  上位机却收到 cmd 22 status=COMPLETED → 误以为成功")
        elif abs(bug_delta - expected_no_bug) < 1.0:
            print(f"✓ 行为正常：累计位移 ≈ {expected_no_bug}mm，cmd 22 似乎被某种方式执行了")
            print(f"  可能 firmware 有排队机制 / cmd 22 实际到达时 cmd 21 已完成")
        else:
            print(f"? 异常位移，需进一步分析")

    ser.close()


if __name__ == "__main__":
    main()
