#!/usr/bin/env python3
"""
测试 08: Homing 详细调试脚本
逐步检查 homing 过程中的每个环节
"""

import serial
import sys
import time
import argparse

DEBUG_HEADER = bytes([0x55, 0xAA])

def send_cmd(ser, command):
    """发送调试协议命令"""
    data = DEBUG_HEADER + command.encode('utf-8') + b'\n'
    print(f"[TX] {command}")
    ser.write(data)

def read_responses(ser, timeout=1.0):
    """读取响应"""
    responses = []
    start_time = time.time()
    buffer = b''

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            chunk = ser.read(ser.in_waiting)
            buffer += chunk
            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                if line:
                    try:
                        responses.append(line.decode('utf-8', errors='replace').strip())
                    except:
                        pass
        time.sleep(0.01)

    return responses

def print_section(title):
    print()
    print("=" * 70)
    print(f"  {title}")
    print("=" * 70)

def run_homing_debug(port_name, axis_name, baudrate=2000000):
    """Homing 详细调试"""
    print(f"连接 {port_name}...")

    try:
        ser = serial.Serial(port=port_name, baudrate=baudrate, timeout=1)
        time.sleep(0.5)
        ser.reset_input_buffer()

        # ============================================================
        print_section("Step 1: Engine Start")
        # ============================================================
        send_cmd(ser, "S:Engine Start")
        time.sleep(1.5)
        for resp in read_responses(ser, timeout=1.0):
            print(f"  {resp}")

        # ============================================================
        print_section("Step 2: 初始状态")
        # ============================================================
        ser.reset_input_buffer()
        send_cmd(ser, f"{axis_name}:GET_DATA")
        for resp in read_responses(ser, timeout=0.5):
            print(f"  {resp}")

        # ============================================================
        print_section("Step 3: 读取寄存器状态 (发送 HOMING 前)")
        # ============================================================
        ser.reset_input_buffer()
        send_cmd(ser, f"{axis_name}:DEBUG_REG")
        for resp in read_responses(ser, timeout=2.0):
            print(f"  {resp}")

        # ============================================================
        print_section("Step 4: 发送 HOMING 命令")
        # ============================================================
        ser.reset_input_buffer()
        send_cmd(ser, f"{axis_name}:HOMING")
        time.sleep(0.3)
        for resp in read_responses(ser, timeout=1.0):
            print(f"  [RX] {resp}")

        # ============================================================
        print_section("Step 5: HOMING 后立即检查状态")
        # ============================================================
        ser.reset_input_buffer()
        send_cmd(ser, f"{axis_name}:GET_DATA")
        for resp in read_responses(ser, timeout=0.5):
            print(f"  {resp}")

        # ============================================================
        print_section("Step 6: HOMING 后立即读取寄存器")
        # ============================================================
        time.sleep(0.2)
        ser.reset_input_buffer()
        send_cmd(ser, f"{axis_name}:DEBUG_REG")
        for resp in read_responses(ser, timeout=2.0):
            print(f"  {resp}")

        # ============================================================
        print_section("Step 7: 每秒监控状态 (15秒)")
        # ============================================================
        start_time = time.time()
        last_state = None

        for i in range(15):
            time.sleep(1.0)
            ser.reset_input_buffer()
            send_cmd(ser, f"{axis_name}:GET_DATA")
            responses = read_responses(ser, timeout=0.5)

            state = None
            pos_mm = None
            is_moving = None
            for resp in responses:
                if "STATE:" in resp:
                    state = resp.split("STATE:")[-1].strip()
                if "(mm):" in resp:
                    try:
                        pos_mm = float(resp.split(":")[-1])
                    except:
                        pass
                if "IS_MOVING:" in resp:
                    is_moving = "YES" in resp

            elapsed = time.time() - start_time
            pos_str = f"{pos_mm:.3f}" if pos_mm is not None else "?"
            print(f"  [{elapsed:5.1f}s] STATE={state}  POS={pos_str}mm  MOVING={is_moving}")

            if state != last_state:
                if state == "IDLE" and i > 0:
                    print(f"\n  => 进入 IDLE 状态")
                    break
                elif state == "ERROR":
                    print(f"\n  => 进入 ERROR 状态!")
                    break
                last_state = state

        # ============================================================
        print_section("Step 8: 最终寄存器状态")
        # ============================================================
        ser.reset_input_buffer()
        send_cmd(ser, f"{axis_name}:DEBUG_REG")
        for resp in read_responses(ser, timeout=2.0):
            print(f"  {resp}")

        ser.close()

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Homing 详细调试脚本')
    parser.add_argument('port', nargs='?', default='/dev/ttyACM0', help='串口端口')
    parser.add_argument('-a', '--axis', default='Z', help='测试轴名称')

    args = parser.parse_args()

    print("=" * 70)
    print("测试 08: Homing 详细调试")
    print("=" * 70)
    print()
    print(f"端口: {args.port}")
    print(f"测试轴: {args.axis}")

    run_homing_debug(args.port, args.axis)

if __name__ == "__main__":
    main()
