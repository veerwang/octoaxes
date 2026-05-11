#!/usr/bin/env python3
"""
XYZ 轴运动速度基线测试

输出：
  software/tests/results/benchmark_xyz_<TS>.csv  原始数据
  software/tests/results/benchmark_xyz_<TS>.md   汇总报告

测试方法（每个轴 × 每个距离）：
  - 20 次 trial，正/负向交替（+d, -d, +d, -d, ...），位置回到起点
  - 测「命令往返耗时」：上位机发 MOVE 命令 → 固件回 COMPLETED (status=0) 的总时间
  - 统计 mean / std / min / max（ms）

距离档位：10μm / 100μm / 1mm / 5mm / 10mm / 30mm
  对超出轴行程的档位，自动 skip 并在报告里标 N/A。

前置条件：
  - 各轴已 home（脚本只 MOVETO 到中心，不自动 home）
  - GUI 未连接（独占 Teensy 串口）
"""
import argparse
import csv
import os
import statistics
import struct
import sys
import time
from collections import OrderedDict

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from utils.helpers import find_teensy_port  # noqa: E402


# ============================================================================
# 协议层（与 test_homing_with_vstop_latch.py 一致：8 字节命令 + 24 字节响应）
# ============================================================================

CMD_MOVE = {"X": 0, "Y": 1, "Z": 2}
CMD_MOVETO = {"X": 6, "Y": 7, "Z": 8}
CMD_SET_LIM = 9
CMD_HOME = 5
CMD_CONFIGURE_STEPPER_DRIVER = 21
CMD_SET_LEAD_SCREW_PITCH = 23
RESPONSE_LEN = 24

# SET_LIM 限位码（与固件 commandprocessor.cpp LIM_CODE_* 一致）
LIM_X_POS, LIM_X_NEG = 0, 1
LIM_Y_POS, LIM_Y_NEG = 2, 3
LIM_Z_POS, LIM_Z_NEG = 4, 5

# HOME_OR_ZERO 协议轴（与 firmware AxisProtocolMapping 一致）
HOME_AXIS = {"X": 0, "Y": 1, "Z": 2}
# CONFIGURE_STEPPER_DRIVER / SET_LEAD_SCREW_PITCH 也用相同协议轴号
AXIS_PROTOCOL = {"X": 0, "Y": 1, "Z": 2}
HOME_POSITIVE = 0  # data[3]: 0=POSITIVE/FORWARD, 1=NEGATIVE/BACKWARD, 2=ZERO（仅清零不真 home）
HOME_NEGATIVE = 1
# 兼容性差异：
#   - octoaxes firmware: 忽略 data[3]，按 _config.homing_direct 决定方向（X/Y/Z 都是 -1，即朝负方向）
#   - 老 Squid firmware: 按 data[3] 决定方向，对 X 而言 0=朝+，1=朝-
# 老 Squid 上位机的算法（microcontroller.py:88）：data[3] = (sign+1)/2
#   X/Y sign=+1 → BACKWARD(1=朝-)，Z sign=-1 → FORWARD(0=朝+)
# 与之对齐：本脚本用 movement_sign 派生 home direction，老 Squid firmware 上行为一致


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


def send_cmd(ser, cmd_id, cmd, payload_int32):
    """发送 MOVE/MOVETO 命令。payload 是 int32 微步（大端）。"""
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
    """字节级解析器，区分 24 字节二进制响应与 ASCII 调试行。"""

    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()

    def drain(self):
        """读所有可用字节、解出所有完整响应包，返回 list[parsed dict]。"""
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
            # 不够一帧，等更多数据
            if len(self.buf) > 4 * RESPONSE_LEN:
                # 防止永久阻塞：丢一字节重新对齐
                del self.buf[0]
                continue
            break
        return results


def wait_completed(reader, expected_cmd_id, timeout_s=15.0,
                   min_idle_frames=5, expect_motion=True):
    """等待 cmd_id 回 COMPLETED。

    防止 firmware 状态机抖动期间（IN_PROGRESS → 短暂 IDLE → IN_PROGRESS 又回去）
    误判完成：要求连续 `min_idle_frames` 个 status=0 帧才视为真正空闲。
    firmware 位置上报间隔 10ms，min_idle_frames=5 ≈ 50ms 持续 idle。

    expect_motion=True（默认）：MOVE/MOVETO/HOME 等会触发运动的命令。
      必须先看到 IN_PROGRESS（status=1）才信任后续 status=0，避免命令到达
      前的 stale status=0 误判完成。
    expect_motion=False：SET_LIM 等瞬时命令，axis 一直 IDLE，跳过 IN_PROGRESS 要求。
    """
    """等待指定 cmd_id 的 COMPLETED 响应，返回耗时（s）及末次位置 dict。

    末次位置 dict 含 x/y/z/w 微步。
    """
    deadline = time.perf_counter() + timeout_s
    last_pos = {"x": None, "y": None, "z": None, "w": None}
    idle_streak = 0
    saw_in_progress = not expect_motion  # SET_LIM 等瞬时命令直接跳过 IN_PROGRESS 要求
    while time.perf_counter() < deadline:
        for resp in reader.drain():
            if not resp or resp.get("crc_error"):
                continue
            last_pos = {k: resp[k] for k in "xyzw"}
            if resp["cmd_id"] != expected_cmd_id:
                continue  # 别的 cmd 的 echo
            if resp["status"] == 1:
                saw_in_progress = True
                idle_streak = 0
            elif resp["status"] == 0:
                if saw_in_progress:
                    idle_streak += 1
                    if idle_streak >= min_idle_frames:
                        return time.perf_counter(), last_pos
                # expect_motion 模式下没看到 IN_PROGRESS 的 status=0 视为抖动，不计数
        time.sleep(0.0005)
    return None, last_pos


# ============================================================================
# 轴参数（与 software/utils/constants.py / firmware/octoaxes/config.h 一致）
# ============================================================================

AXIS_PARAMS = OrderedDict(
    [
        # 与 software/utils/constants.py 的 AXIS_CONFIG 对齐。
        # movement_sign: 上位机坐标系到 firmware 坐标系的方向映射
        #   X/Y = +1（一致），Z = -1（GUI 的"上"对应 firmware 的负方向）
        # MOVE/MOVETO 发给 firmware 前要乘 movement_sign。
        # test_range_um: 上位机坐标系下的安全测试范围（home 后绝对位置 μm）
        # 用户指定 2026-05-11：X 10mm-112mm / Y 6mm-76mm / Z 0.1mm-6.5mm
        ("X", {"pitch_mm": 2.54, "microstepping": 256, "fs_per_rev": 200,
               "current_ma": 1000, "hold_ratio": 0.25,
               "movement_sign": 1,
               "test_range_um": (10_000, 112_000)}),
        ("Y", {"pitch_mm": 2.54, "microstepping": 256, "fs_per_rev": 200,
               "current_ma": 1000, "hold_ratio": 0.25,
               "movement_sign": 1,
               "test_range_um": (6_000, 76_000)}),
        ("Z", {"pitch_mm": 0.3, "microstepping": 256, "fs_per_rev": 200,
               "current_ma": 500, "hold_ratio": 0.5,
               "movement_sign": -1,
               "test_range_um": (100, 6_500)}),
    ]
)
# 测试 alternating +/- 模式下，d 必须 ≤ min(center-low, high-center) - SAFETY_MARGIN_UM
SAFETY_MARGIN_UM = 1000  # 中心两侧各留 1mm 缓冲


def usteps_per_um(axis):
    """单位换算：每微米多少微步。"""
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


def fits_in_travel(axis, dist_um):
    """alternating +/- 模式下：dist_um ≤ min(center-low, high-center) - SAFETY_MARGIN_UM。"""
    lo, hi = AXIS_PARAMS[axis]["test_range_um"]
    center = (lo + hi) / 2
    half = min(hi - center, center - lo)
    return dist_um <= (half - SAFETY_MARGIN_UM)


# ============================================================================
# 测试主流程
# ============================================================================

DISTANCES_UM = [10, 100, 1000, 5000, 10000, 30000]  # 10μm / 100μm / 1mm / 5mm / 10mm / 30mm
TRIALS_PER_DIR = 10
DEFAULT_AXES = ["X", "Y", "Z"]


def get_current_position(reader, ser, timeout_s=2.0):
    """从响应流读出最新位置。固件每 10ms 自发上报，无需主动 query。"""
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


def _send_set_lim(ser, cmd_id, limit_code, value_int32):
    """SET_LIM packet: byte[0]=cmd_id, byte[1]=9 (CMD_SET_LIM),
    byte[2]=limit_code, byte[3..6]=int32 BE value, byte[7]=CRC.

    与固件 handleSetLim 一致（commandprocessor.cpp:171）：
      data[2] = 限位码，data[3..6] = 微步值大端序。
    """
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
    ser.write(pkt)


def _send_set_lead_screw_pitch(ser, cmd_id, axis_code, pitch_mm):
    """SET_LEAD_SCREW_PITCH (cmd 23): byte[2]=axis, byte[3..4]=pitch_mm*1000 uint16 BE."""
    pitch_x1000 = int(round(pitch_mm * 1000)) & 0xFFFF
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_SET_LEAD_SCREW_PITCH
    pkt[2] = axis_code & 0xFF
    pkt[3] = (pitch_x1000 >> 8) & 0xFF
    pkt[4] = pitch_x1000 & 0xFF
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)


def _send_configure_stepper_driver(ser, cmd_id, axis_code, microstepping, current_ma, hold_ratio):
    """CONFIGURE_STEPPER_DRIVER (cmd 21):
        byte[2]=axis, byte[3]=ms_byte (1→0, 256→255, else→ms),
        byte[4..5]=current_mA uint16 BE, byte[6]=hold_ratio*255.
    """
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
    ser.write(pkt)


def configure_actuators(ser, reader, axes, cmd_id):
    """对齐 GUI 的 _configure_actuators：每轴下发 pitch + 微步/电流。

    防止 firmware 残留旧 Squid 的 16 微步配置等 → benchmark 单位换算与 firmware
    不一致 → 电机走不到目标。
    """
    print("\n[0a] 配置 actuator（pitch + 微步 + 电流，对齐 GUI startup）")
    for axis in axes:
        if axis not in AXIS_PROTOCOL:
            continue
        p = AXIS_PARAMS[axis]
        # SET_LEAD_SCREW_PITCH
        _send_set_lead_screw_pitch(ser, cmd_id, AXIS_PROTOCOL[axis], p["pitch_mm"])
        wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
        cmd_id = (cmd_id + 1) % 256
        # CONFIGURE_STEPPER_DRIVER
        _send_configure_stepper_driver(ser, cmd_id, AXIS_PROTOCOL[axis],
                                        p["microstepping"], p["current_ma"], p["hold_ratio"])
        wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
        cmd_id = (cmd_id + 1) % 256
        print(f"  ✓ {axis}: pitch={p['pitch_mm']}mm, ms={p['microstepping']}, "
              f"current={p['current_ma']}mA, hold={p['hold_ratio']}")
    return cmd_id


def _send_home(ser, cmd_id, axis_code, direction=HOME_POSITIVE):
    """HOME_OR_ZERO packet: byte[0]=cmd_id, byte[1]=5, byte[2]=axis_code,
    byte[3]=direction, byte[7]=CRC。固件忽略 direction，由轴 config 决定。
    """
    pkt = bytearray(8)
    pkt[0] = cmd_id & 0xFF
    pkt[1] = CMD_HOME
    pkt[2] = axis_code & 0xFF
    pkt[3] = direction & 0xFF
    pkt[7] = crc8(pkt[:7])
    ser.write(pkt)


def home_all_axes(ser, reader, axes, cmd_id):
    """逐轴 HOME。homing 完成后位置 ≈ 0（确切值 = home_safety_margin，<1mm）。

    timeout 给 30s（X/Y 行程 100mm+ 满速 25mm/s 至少 4s + 加减速 + 双阶段精确逼近）。
    """
    print(f"\n[1] HOME 轴 {','.join(axes)}（每轴最长 30s）")
    for axis in axes:
        if axis not in HOME_AXIS:
            print(f"  ⚠ 跳过 {axis}（不支持 home）")
            continue
        # 按 movement_sign 派生 home direction（与老 Squid microcontroller.py:88 一致）：
        #   sign = +1 → BACKWARD(1=朝-)  /  sign = -1 → FORWARD(0=朝+)
        # 对 octoaxes firmware 无影响（忽略 data[3]）；对老 Squid firmware 关键
        sign = AXIS_PARAMS[axis]["movement_sign"]
        direction = HOME_NEGATIVE if sign == 1 else HOME_POSITIVE
        print(f"  [{axis}] HOME 开始（dir={'-' if direction == HOME_NEGATIVE else '+'}）...", end=" ", flush=True)
        t0 = time.perf_counter()
        _send_home(ser, cmd_id, HOME_AXIS[axis], direction=direction)
        t_end, end_pos = wait_completed(reader, cmd_id, timeout_s=30.0)
        if t_end is None:
            print(f"\n  ❌ {axis} HOME 超时（>30s），中止")
            raise RuntimeError(f"{axis} HOME timeout")
        dt = (t_end - t0) * 1000
        # 防御：HOME 完成后状态机还要走 STATE_LEAVING_HOME → STATE_IDLE，
        # status=0 broadcast 可能在 STATE_LEAVING_HOME 期间也发出（isMoving=false 瞬间）
        time.sleep(0.3)
        axis_key = axis.lower()
        verify_pos = get_current_position(reader, ser, timeout_s=1.0)
        end_um = usteps_to_um(axis, verify_pos[axis_key]) if verify_pos else usteps_to_um(axis, end_pos[axis_key])
        print(f"完成 ({dt/1000:.2f}s, 终止位置 {end_um:+.1f}μm)")
        cmd_id = (cmd_id + 1) % 256
    return cmd_id


def widen_soft_limits(ser, reader, cmd_id):
    """把 X/Y/Z 软限位放到很宽，消除 GUI 之前 SET_LIM 残留对 benchmark 的干扰。

    GUI 在启动序列里发 SET_LIM 把限位卡在 (±10μm, ±物理上限)，chip 保留这些
    设置直到下次 RESET / 软复位。benchmark 不发 RESET 也不知道这些限位，
    每个 block 首次 + 方向移动若 target 撞限位会触发 clampTargetByDirection
    的 no-op 短路（commit d92fa2d）—— firmware 立即回 status=0 假完成。

    解决：发 SET_LIM 把每轴上下限拉到 ±100mm 等效微步（远超物理行程），
    chip 软限位实际失效，clamp 不再生效，benchmark MOVE 全按命令真实执行。
    """
    print("\n[0] 把 chip 软限位放宽（消除 GUI 残留 SET_LIM 干扰）")
    for axis, neg_code, pos_code in [
        ("X", LIM_X_NEG, LIM_X_POS),
        ("Y", LIM_Y_NEG, LIM_Y_POS),
        ("Z", LIM_Z_NEG, LIM_Z_POS),
    ]:
        wide_usteps = um_to_usteps(axis, 100_000)  # ±100mm 等效微步
        for code, val in [(neg_code, -wide_usteps), (pos_code, +wide_usteps)]:
            _send_set_lim(ser, cmd_id, code, val)
            t_end, _ = wait_completed(reader, cmd_id, timeout_s=2.0, expect_motion=False)
            if t_end is None:
                print(f"  ⚠ SET_LIM {axis} code={code} 超时")
            cmd_id = (cmd_id + 1) % 256
    print("  ✓ X/Y/Z 软限位已放宽至 ±100mm 等效微步")
    return cmd_id


def move_to_center(ser, reader, axis, cmd_id, tolerance_um=500):
    """如果离中心 > tolerance，发 MOVETO 把轴拉回中心；返回新 cmd_id。

    坐标转换：用户坐标系 × movement_sign = firmware 坐标系。
    GUI main_window.py:1160 同样做 `value = sign * pos_um`。
    """
    sign = AXIS_PARAMS[axis]["movement_sign"]
    pos = get_current_position(reader, ser)
    if not pos:
        raise RuntimeError("无法读取当前位置")
    axis_key = axis.lower()
    # firmware 位置 → 用户坐标系
    current_um = usteps_to_um(axis, pos[axis_key]) * sign
    target_um = axis_center_um(axis)
    if abs(current_um - target_um) <= tolerance_um:
        print(f"  [{axis}] 位置 {current_um:+.1f}μm 接近中心 {target_um:+.1f}μm，跳过 MOVETO")
        return cmd_id
    print(f"  [{axis}] 当前 {current_um:+.1f}μm → MOVETO 中心 {target_um:+.1f}μm")
    # 用户坐标系 → firmware 坐标系
    target_usteps_firmware = um_to_usteps(axis, target_um) * sign
    send_cmd(ser, cmd_id, CMD_MOVETO[axis], target_usteps_firmware)
    t_end, end_pos = wait_completed(reader, cmd_id, timeout_s=30.0)
    if t_end is None:
        raise RuntimeError(f"{axis} MOVETO 中心超时")
    time.sleep(0.2)
    verify_pos = get_current_position(reader, ser, timeout_s=1.0)
    if verify_pos:
        actual_um = usteps_to_um(axis, verify_pos[axis.lower()]) * sign
        delta_um = actual_um - target_um
        if abs(delta_um) > 100:
            print(f"  ⚠ [{axis}] MOVETO 实际到达 {actual_um:+.1f}μm，与目标 {target_um:+.1f}μm 差 {delta_um:+.1f}μm")
        else:
            print(f"  [{axis}] 到达 {actual_um:+.1f}μm ✓")
    return (cmd_id + 1) % 256


def benchmark_axis_distance(ser, reader, axis, dist_um, cmd_id):
    """对 (axis, dist_um) 做 2*TRIALS_PER_DIR 次交替测试。

    返回 (rows_pos, rows_neg, next_cmd_id)，每行是
    dict(axis, distance_um, direction, trial, dt_ms, start_usteps, end_usteps)。
    """
    rows = []
    pos_dt = []
    neg_dt = []
    axis_key = axis.lower()
    sign = AXIS_PARAMS[axis]["movement_sign"]
    dist_usteps = um_to_usteps(axis, dist_um)

    for trial in range(TRIALS_PER_DIR):
        for direction in (+1, -1):
            # direction 是用户坐标系下的方向；乘 sign 转 firmware 坐标系
            payload = dist_usteps * direction * sign
            # 读 start 位置（firmware 坐标系 μstep，CSV 原始记录不转换）
            start_pos = get_current_position(reader, ser, timeout_s=0.3)
            start_usteps = start_pos[axis_key] if start_pos else 0

            t0 = time.perf_counter()
            send_cmd(ser, cmd_id, CMD_MOVE[axis], payload)
            t_end, end_pos = wait_completed(reader, cmd_id, timeout_s=15.0)
            if t_end is None:
                print(f"  [{axis}] dist={dist_um}μm dir={direction:+d} trial={trial} TIMEOUT")
                cmd_id = (cmd_id + 1) % 256
                continue
            dt_ms = (t_end - t0) * 1000

            end_usteps = end_pos[axis_key]
            rows.append({
                "axis": axis,
                "distance_um": dist_um,
                "direction": direction,
                "trial": trial,
                "dt_ms": dt_ms,
                "start_usteps": start_usteps,
                "end_usteps": end_usteps,
            })
            (pos_dt if direction > 0 else neg_dt).append(dt_ms)
            cmd_id = (cmd_id + 1) % 256

    # 汇总
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

    summary = {
        "axis": axis,
        "distance_um": dist_um,
        "pos": stats(pos_dt),
        "neg": stats(neg_dt),
    }
    return rows, summary, cmd_id


# ============================================================================
# 报告输出
# ============================================================================


def write_csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "axis", "distance_um", "direction", "trial", "dt_ms",
            "start_usteps", "end_usteps"
        ])
        w.writeheader()
        for r in rows:
            w.writerow(r)


def fmt_dist(um):
    if um >= 1000:
        return f"{um/1000:g}mm"
    return f"{um}μm"


def write_markdown(path, summaries, meta):
    """summaries: list of {axis, distance_um, pos:{n,mean,stdev,min,max}, neg:{...}}"""
    lines = []
    lines.append(f"# XYZ 轴运动速度基线报告")
    lines.append("")
    lines.append(f"**生成时间**: {meta['timestamp']}")
    lines.append(f"**Firmware**: commit `{meta['firmware_commit']}`")
    lines.append(f"**测试条件**: 命令往返耗时（上位机发 MOVE → 固件回 COMPLETED）")
    lines.append(f"**重复**: 每方向 {TRIALS_PER_DIR} 次 trial，正负方向交替（位置回到起点）")
    lines.append("")
    lines.append("## 指标说明")
    lines.append("")
    lines.append("- **mean / std / min / max**：单位 ms")
    lines.append("- **dt**：上位机 perf_counter 测量，包含 USB 串口往返 + 固件 SPI 配置 + 电机 ramp + 10ms 上报间隔")
    lines.append("- 大档位短距离的 mean 接近 ~10ms（受位置上报间隔下界制约），大档位长距离主要受 ramp 加减速时间制约")
    lines.append("")

    for axis in ["X", "Y", "Z"]:
        axis_summaries = [s for s in summaries if s["axis"] == axis]
        if not axis_summaries:
            continue
        p = AXIS_PARAMS[axis]
        lines.append(f"## {axis} 轴（pitch {p['pitch_mm']}mm × {p['microstepping']}μstep × {p['fs_per_rev']} FS/rev = {p['microstepping']*p['fs_per_rev']} μstep/rev，测试范围 {p['test_range_um'][0]}-{p['test_range_um'][1]}μm）")
        lines.append("")
        lines.append("| 距离 | 方向 | n | mean (ms) | std (ms) | min (ms) | max (ms) |")
        lines.append("|------|------|---|-----------|----------|----------|----------|")
        for s in axis_summaries:
            for dirname, key in [("+", "pos"), ("−", "neg")]:
                st = s[key]
                if st is None:
                    lines.append(f"| {fmt_dist(s['distance_um'])} | {dirname} | 0 | N/A | N/A | N/A | N/A |")
                else:
                    lines.append(f"| {fmt_dist(s['distance_um'])} | {dirname} | {st['n']} | "
                                 f"{st['mean']:.2f} | {st['stdev']:.2f} | {st['min']:.2f} | {st['max']:.2f} |")
        lines.append("")

    lines.append("## 跳过的档位")
    lines.append("")
    skipped = [s for s in summaries if s.get("skipped")]
    if skipped:
        for s in skipped:
            lines.append(f"- **{s['axis']}** 距离 {fmt_dist(s['distance_um'])}：行程不足（{s['reason']}）")
    else:
        lines.append("- 无")
    lines.append("")

    with open(path, "w") as f:
        f.write("\n".join(lines))


def get_firmware_commit():
    """读 git 当前 HEAD 短哈希。"""
    try:
        import subprocess
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=os.path.join(os.path.dirname(__file__), "..", ".."),
        ).decode().strip()
    except Exception:
        return "unknown"


# ============================================================================
# main
# ============================================================================

def main():
    global TRIALS_PER_DIR  # 允许 --trials 覆盖
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=None, help="串口设备（默认自动查找）")
    parser.add_argument("--axes", default="X,Y,Z", help="测哪些轴，逗号分隔（默认 X,Y,Z）")
    parser.add_argument("--output-dir", default=None,
                        help="输出目录（默认 software/tests/results）")
    parser.add_argument("--skip-home", action="store_true",
                        help="跳过 HOME 步骤（如果你已手动 home 过，不推荐）")
    parser.add_argument("--no-move-to-center", action="store_true",
                        help="不要先把轴 MOVETO 到中心位置（如果你已手动定位）")
    parser.add_argument("--setup-only", action="store_true",
                        help="只跑步骤 0/1/2（放宽限位 + HOME + MOVETO 中心），不做 benchmark")
    parser.add_argument("--yes", action="store_true",
                        help="跳过 step 2 完成后的人工确认（默认会暂停等待）")
    parser.add_argument("--trials", type=int, default=TRIALS_PER_DIR,
                        help=f"每方向 trial 次数（默认 {TRIALS_PER_DIR}）")
    parser.add_argument("--distances", default=None,
                        help=f"距离档位（μm，逗号分隔），默认 {','.join(str(d) for d in DISTANCES_UM)}")
    args = parser.parse_args()

    port = args.port or find_teensy_port()
    if not port:
        print("❌ 找不到 Teensy 串口（用 --port 指定）")
        return 1
    print(f"✓ Teensy: {port}")

    axes = [a.strip().upper() for a in args.axes.split(",") if a.strip()]
    for a in axes:
        if a not in AXIS_PARAMS:
            print(f"❌ 未知轴: {a}")
            return 1

    # 覆盖默认参数
    TRIALS_PER_DIR = args.trials
    distances = (
        [int(s.strip()) for s in args.distances.split(",") if s.strip()]
        if args.distances else list(DISTANCES_UM)
    )

    out_dir = args.output_dir or os.path.join(os.path.dirname(__file__), "results")
    os.makedirs(out_dir, exist_ok=True)
    ts = time.strftime("%Y%m%d_%H%M%S")
    csv_path = os.path.join(out_dir, f"benchmark_xyz_{ts}.csv")
    md_path = os.path.join(out_dir, f"benchmark_xyz_{ts}.md")

    ser = serial.Serial(port, 115200, timeout=0.1)
    time.sleep(0.5)
    ser.reset_input_buffer()
    reader = ResponseReader(ser)

    print("\n⏳ 等待 1 秒收集位置上报...")
    time.sleep(1.0)
    init_pos = get_current_position(reader, ser)
    if not init_pos:
        print("❌ 没收到位置上报（固件是否运行？GUI 是否占用了串口？）")
        return 1
    print(f"✓ 初始位置: X={init_pos['x']}μstep ({usteps_to_um('X', init_pos['x']):+.1f}μm), "
          f"Y={init_pos['y']}μstep ({usteps_to_um('Y', init_pos['y']):+.1f}μm), "
          f"Z={init_pos['z']}μstep ({usteps_to_um('Z', init_pos['z']):+.1f}μm)")

    cmd_id = 1

    # 0a. 配置 actuator（对齐 GUI 启动序列，防止 firmware 残留异常微步配置）
    cmd_id = configure_actuators(ser, reader, axes, cmd_id)

    # 0b. 放宽 chip 软限位（消除 GUI 残留 SET_LIM 影响）
    cmd_id = widen_soft_limits(ser, reader, cmd_id)

    # 1. HOME XYZ
    if not args.skip_home:
        cmd_id = home_all_axes(ser, reader, axes, cmd_id)

    # 2. 把各轴 MOVETO 到测试范围中心
    #
    # 顺序强制为 Y → X → Z：真机上有上下料装置位于 X 路径上，HOME 完 Y=0 时
    # X 直接走到 61mm 会撞到装置。必须先把 Y 移出装置遮挡区（41mm 中心），
    # 然后 X 才能安全移到 61mm。Z 是垂直轴，不在 XY 平面，顺序无影响。
    MOVE_TO_CENTER_ORDER = ["Y", "X", "Z"]
    if not args.no_move_to_center:
        print(f"\n[2] 把各轴移动到测试范围中心（顺序 Y → X → Z，避开上下料装置）")
        for axis in MOVE_TO_CENTER_ORDER:
            if axis not in axes:
                continue
            lo, hi = AXIS_PARAMS[axis]["test_range_um"]
            print(f"  [{axis}] 测试范围 {lo/1000:g}-{hi/1000:g}mm, 中心 {(lo+hi)/2000:g}mm")
            cmd_id = move_to_center(ser, reader, axis, cmd_id)

    if args.setup_only:
        print("\n✓ --setup-only：步骤 0/1/2 完成，跳过 benchmark。")
        ser.close()
        return 0

    # 步骤 2.5：人工确认 XYZ 是否真的到了中心位置
    if not args.yes:
        print("\n──────────────────────────────────────────────────────────")
        print("⏸  请人工确认各轴是否真到了中心位置：")
        final_pos = get_current_position(reader, ser, timeout_s=2.0)
        if final_pos:
            for axis in axes:
                lo, hi = AXIS_PARAMS[axis]["test_range_um"]
                center = (lo + hi) / 2
                sign = AXIS_PARAMS[axis]["movement_sign"]
                actual = usteps_to_um(axis, final_pos[axis.lower()]) * sign
                mark = "✓" if abs(actual - center) <= 500 else "⚠"
                print(f"  {mark} {axis}: 实际 {actual:+.1f}μm / 中心 {center:+.1f}μm "
                      f"(差 {actual - center:+.1f}μm)")
        ans = input("继续 benchmark? [y/N] ").strip().lower()
        if ans != "y":
            print("✗ 用户中止，退出。")
            ser.close()
            return 0
        print("──────────────────────────────────────────────────────────")

    # 3. 逐轴 × 逐档位测试
    print(f"\n[3] 开始测试，距离档位 {[fmt_dist(d) for d in distances]}，每方向 {TRIALS_PER_DIR} trial")
    all_rows = []
    summaries = []
    for axis in axes:
        print(f"\n--- {axis} 轴 ---")
        for dist_um in distances:
            if not fits_in_travel(axis, dist_um):
                lo, hi = AXIS_PARAMS[axis]["test_range_um"]
                half = min(hi - axis_center_um(axis), axis_center_um(axis) - lo)
                print(f"  [{axis}] dist={fmt_dist(dist_um)} > 半行程-{SAFETY_MARGIN_UM}μm={half-SAFETY_MARGIN_UM:.0f}μm，跳过")
                summaries.append({
                    "axis": axis, "distance_um": dist_um,
                    "pos": None, "neg": None,
                    "skipped": True,
                    "reason": f"测试范围半行程 {half:.0f}μm 减 1mm 缓冲不够",
                })
                continue
            t_block_start = time.perf_counter()
            rows, summary, cmd_id = benchmark_axis_distance(ser, reader, axis, dist_um, cmd_id)
            t_block = time.perf_counter() - t_block_start
            all_rows.extend(rows)
            summaries.append(summary)
            pos_mean = summary["pos"]["mean"] if summary["pos"] else 0
            neg_mean = summary["neg"]["mean"] if summary["neg"] else 0
            print(f"  [{axis}] dist={fmt_dist(dist_um)} 完成 "
                  f"+方向 mean={pos_mean:.1f}ms / −方向 mean={neg_mean:.1f}ms "
                  f"(块耗时 {t_block:.1f}s)")

    # 4. 写报告
    print(f"\n[4] 写出报告")
    write_csv(csv_path, all_rows)
    meta = {"timestamp": ts, "firmware_commit": get_firmware_commit()}
    write_markdown(md_path, summaries, meta)
    print(f"  ✓ CSV: {csv_path}")
    print(f"  ✓ MD:  {md_path}")

    ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
