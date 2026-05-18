#!/usr/bin/env python3
"""
快速自检：firmware 是否烧入 S:JOYSTICK_STATS 调试命令 + 实时读 joystick 协议帧计数器

用法：
  python3 software/common/tests/check_joystick_stats.py
  python3 software/common/tests/check_joystick_stats.py --port /dev/ttyACM0

退出码：
  0  正常（解析到计数器值）
  1  Firmware 未识别该命令（需重烧 fa625d1 之后的版本）
  2  其他错误（串口未连接、PC GUI 占用、Teensy 没枚举等）

注意：运行前关闭 PC GUI（octoaxes/main.py 或旧 Squid software），否则串口被占用。

期望输出（新 firmware + 新 joystick + 现场工作正常）：
  JOYSTICK_STATS legacy=0 crc_ok=<N> crc_fail=0
  → crc_ok 持续增长，crc_fail=0 ✅

诊断速查（详见 documents/joystick_protocol.md §6）：
  legacy>0, crc_ok=0   → 老 joystick 在发包（兼容路径生效）
  crc_ok=0, crc_fail=0 → joystick 没连上（Serial5 物理层）
  crc_fail>>0           → 报警，链路异常或 byte[9] 被污染
"""
import argparse
import os
import re
import sys
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from utils.helpers import find_teensy_port  # noqa: E402

# firmware/octoaxes/serial.h DEBUG_PROTOCOL_HEADER_1/2
DEBUG_HEADER = bytes([0x55, 0xAA])

# 匹配 firmware joystick_print_stats 输出格式：
#   JOYSTICK_STATS legacy=N crc_ok=N crc_fail=N
STATS_RE = re.compile(
    rb"JOYSTICK_STATS\s+legacy=(\d+)\s+crc_ok=(\d+)\s+crc_fail=(\d+)"
)


def main():
    parser = argparse.ArgumentParser(
        description="检查 firmware 是否支持 S:JOYSTICK_STATS 并读 joystick 帧计数器"
    )
    parser.add_argument("--port", default=None, help="串口（默认自动检测 Teensy）")
    parser.add_argument("--baud", type=int, default=2_000_000)
    parser.add_argument("--wait", type=float, default=1.0,
                        help="发送命令后等待回包的秒数（默认 1.0s）")
    args = parser.parse_args()

    try:
        port = args.port or find_teensy_port()
    except Exception as e:
        print(f"❌ 找不到 Teensy 串口: {e}")
        return 2

    print(f"打开 {port} @ {args.baud}")
    try:
        ser = serial.Serial(port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        msg = str(e)
        if "Resource busy" in msg or "busy" in msg.lower():
            print(f"❌ 串口被占用: {e}")
            print("   提示: 关闭 PC GUI（octoaxes/main.py 或旧 Squid software）后重试")
        else:
            print(f"❌ 串口打开失败: {e}")
        return 2

    try:
        time.sleep(2.0)  # Teensy USB 枚举
        ser.reset_input_buffer()

        cmd = DEBUG_HEADER + b"S:JOYSTICK_STATS\n"
        print(f"发送: {cmd!r}")
        ser.write(cmd)
        ser.flush()
        time.sleep(args.wait)

        buf = ser.read(ser.in_waiting or 1)
        # 多读一轮防止位置广播切断响应
        time.sleep(0.1)
        buf += ser.read(ser.in_waiting or 1)
        print(f"收到 {len(buf)} 字节")

        # 响应是 ASCII 行（DEBUG_PRINTLN → SerialUSB.println），夹在 10ms 周期
        # 24 字节位置广播之间。直接对原始字节做正则匹配。
        m = STATS_RE.search(buf)
        if m:
            legacy = int(m.group(1))
            crc_ok = int(m.group(2))
            crc_fail = int(m.group(3))
            print()
            print(f"✓ 计数器读到：legacy={legacy} crc_ok={crc_ok} crc_fail={crc_fail}")
            print()
            # 诊断解读
            if crc_ok > 0 and crc_fail == 0 and legacy == 0:
                print("  ✅ 正向场景：新 joystick + 新 firmware，CRC 校验全通过")
            elif legacy > 0 and crc_ok == 0 and crc_fail == 0:
                print("  ✅ 回归场景：老 joystick + 新 firmware，legacy 兼容路径生效")
            elif crc_fail > 0:
                fail_rate = crc_fail / max(1, crc_ok + crc_fail + legacy) * 100
                print(f"  ⚠ 警告：crc_fail={crc_fail}（占比 {fail_rate:.2f}%）")
                if crc_ok == 0:
                    print("     joystick 端代码可能写错 CRC，或 byte[9] 被污染")
                else:
                    print("     链路偶有干扰；个位数属正常，大量失败需排查接线/EMI")
            elif legacy == 0 and crc_ok == 0 and crc_fail == 0:
                print("  ⚠ joystick 没在发包：Serial5 物理层问题（接线/电源/joystick 未通电）")
            else:
                print("  ⚠ 混合状态，可能新老 joystick 切换中")
            return 0

        # 没匹配到 → 看是不是命令本身未识别
        if b"S:JOYSTICK_STATS" in buf or b"joystick" in buf.lower():
            print("⚠ Firmware 收到命令但回包格式不对：")
            print(f"   首段 raw bytes: {buf[:200]!r}")
            return 1

        print("❌ Firmware 没识别该命令（可能未烧 fa625d1 之后的固件）")
        print("   修复: cd firmware/octoaxes && pio run -t upload")
        print(f"   首段 raw bytes: {buf[:120]!r}")
        return 1

    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
