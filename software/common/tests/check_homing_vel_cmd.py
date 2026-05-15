#!/usr/bin/env python3
"""
快速自检：firmware 是否烧入 S:SET_HOMING_VEL 调试命令

用法：
  python3 software/tests/check_homing_vel_cmd.py
  python3 software/tests/check_homing_vel_cmd.py --port /dev/ttyACM0

退出码：
  0  Firmware 已支持
  1  Firmware 未支持（需重烧）
  2  其他错误（串口未连接等）
"""
import argparse
import os
import sys
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from utils.helpers import find_teensy_port  # noqa: E402

# firmware/octoaxes/serial.h DEBUG_PROTOCOL_HEADER_1/2
DEBUG_HEADER = bytes([0x55, 0xAA])


def main():
    parser = argparse.ArgumentParser(description="检查 firmware 是否支持 S:SET_HOMING_VEL")
    parser.add_argument("--port", default=None, help="串口（默认自动检测 Teensy）")
    parser.add_argument("--baud", type=int, default=2_000_000)
    parser.add_argument("--axis", default="Y", help="测试轴名（默认 Y）")
    parser.add_argument("--vel", type=float, default=15.0,
                        help="测试速度 mm/s（默认 15，与老 Squid baseline 一致）")
    args = parser.parse_args()

    try:
        port = args.port or find_teensy_port()
    except Exception as e:
        print(f"❌ 找不到 Teensy 串口: {e}")
        return 2

    print(f"打开 {port} @ {args.baud}")
    try:
        ser = serial.Serial(port, args.baud, timeout=0.1)
    except Exception as e:
        print(f"❌ 串口打开失败: {e}")
        return 2

    try:
        time.sleep(2.0)  # Teensy USB 枚举
        ser.reset_input_buffer()

        cmd = DEBUG_HEADER + f"S:SET_HOMING_VEL {args.axis} {args.vel:.3f}\n".encode("ascii")
        print(f"发送: {cmd!r}")
        ser.write(cmd)
        time.sleep(0.5)

        buf = ser.read(ser.in_waiting)
        print(f"收到 {len(buf)} 字节")

        # firmware 响应是 ASCII 行（"S:SET_HOMING_VEL:OK:Y=15.000\n"），
        # 会混在 10ms 周期的 24 字节二进制位置响应包之间。直接字节子串匹配。
        if b"S:SET_HOMING_VEL:OK" in buf:
            # 提取并打印 OK 行
            start = buf.find(b"S:SET_HOMING_VEL:OK")
            end = buf.find(b"\n", start)
            ok_line = buf[start:end if end > 0 else start + 60]
            print(f"✓ Firmware 已支持新命令")
            print(f"  响应: {ok_line.decode('ascii', errors='replace').strip()}")
            return 0
        elif b"S:SET_HOMING_VEL:ERR" in buf:
            start = buf.find(b"S:SET_HOMING_VEL:ERR")
            end = buf.find(b"\n", start)
            err_line = buf[start:end if end > 0 else start + 60]
            print(f"⚠ Firmware 支持但报错: {err_line.decode('ascii', errors='replace').strip()}")
            return 1
        else:
            print("❌ Firmware 没识别该命令")
            print("   修复: cd firmware/octoaxes && ./download.sh nointerlock")
            print(f"   原始 buffer 前 200 字节: {buf[:200]!r}")
            return 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
