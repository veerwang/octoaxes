#!/usr/bin/env python3
"""
硬件检测脚本：查询各轴驱动芯片型号
自动发送 Engine Start + S:HWINFO，读取固件自动检测结果
"""

import serial
import sys
import time

DEBUG_HEADER = bytes([0x55, 0xAA])


def send_debug_command(ser, command):
    data = DEBUG_HEADER + command.encode('utf-8') + b'\n'
    ser.write(data)


def read_lines(ser, timeout=3.0):
    """读取多行响应直到超时或收到 END 标记"""
    lines = []
    start_time = time.time()
    buf = b''

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            buf += ser.read(ser.in_waiting)
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                text = line.decode('utf-8', errors='replace').strip()
                if text:
                    lines.append(text)
                if 'S:HWINFO:END' in text:
                    return lines
        time.sleep(0.01)

    return lines


def drain(ser, timeout=0.5):
    """排空接收缓冲区"""
    start = time.time()
    while time.time() - start < timeout:
        if ser.in_waiting > 0:
            ser.read(ser.in_waiting)
        time.sleep(0.01)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'

    print("=" * 50)
    print("  Octoaxes 硬件检测")
    print("=" * 50)
    print(f"串口: {port}")
    print()

    try:
        ser = serial.Serial(port=port, baudrate=2000000, timeout=1)
        time.sleep(0.5)
        ser.reset_input_buffer()

        # 1. 发送 Engine Start（轴初始化需要）
        print("发送 Engine Start...")
        send_debug_command(ser, "S:Engine Start")
        # 等待系统初始化完成（TMC SPI 通信 + 驱动检测）
        drain(ser, timeout=3.0)
        print("系统初始化完成")
        print()

        # 2. 查询硬件信息
        send_debug_command(ser, "S:HWINFO")
        lines = read_lines(ser, timeout=3.0)

        # 解析 HWINFO 响应
        axes = []
        for line in lines:
            if line.startswith("S:HWINFO:") and "END" not in line:
                # 格式: S:HWINFO:<axis>:TMC4361A+<driver>
                parts = line.split(":")
                if len(parts) >= 4:
                    axis_name = parts[2]
                    chips = parts[3]
                    axes.append((axis_name, chips))

        if axes:
            print(f"{'轴':<8} {'驱动芯片':<20}")
            print("-" * 30)
            for name, chips in axes:
                print(f"{name:<8} {chips:<20}")
            print()
            print(f"共检测到 {len(axes)} 个轴")
        else:
            print("[FAIL] 未收到 HWINFO 响应")
            print("收到的数据:")
            for line in lines:
                print(f"  {line}")

        ser.close()

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return 1

    print("=" * 50)
    return 0


if __name__ == "__main__":
    sys.exit(main())
