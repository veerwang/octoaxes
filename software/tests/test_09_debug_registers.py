#!/usr/bin/env python3
"""
测试 09: 寄存器状态调试（不移动电机）
只读取和显示寄存器状态，不执行任何移动操作
"""

import serial
import time
import sys

PORT = "/dev/ttyACM0"
BAUDRATE = 115200
AXIS = "Z"

def send_command(ser, cmd, wait=0.3):
    """发送命令并读取响应"""
    ser.write(f"{cmd}\n".encode())
    time.sleep(wait)
    response = []
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            response.append(line)
    return response

def main():
    print("=" * 60)
    print("测试 09: 寄存器状态调试（只读，不移动）")
    print("=" * 60)

    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()
        print(f"连接 {PORT}...")

        # Engine Start
        print("\n[1] Engine Start")
        for line in send_command(ser, "S:Engine Start", 3):
            if "motor_adjustBows" in line or "initialized" in line:
                print(f"  {line}")

        # 读取寄存器状态
        print(f"\n[2] 读取 {AXIS} 轴寄存器状态")
        for line in send_command(ser, f"{AXIS}:DEBUG_REG", 1):
            if "REG:" in line:
                print(f"  {line}")

        # 读取当前数据
        print(f"\n[3] 读取 {AXIS} 轴当前数据")
        for line in send_command(ser, f"{AXIS}:GET_DATA", 0.5):
            print(f"  {line}")

        # 解析关键寄存器
        print("\n[4] 关键状态分析")
        for line in send_command(ser, f"{AXIS}:DEBUG_REG", 1):
            if "RAMPMODE" in line:
                val = int(line.split("=")[1], 16)
                mode_str = {0: "HOLD", 1: "TRAPEZOID_VEL", 2: "S-SHAPE_VEL",
                           4: "TRAPEZOID_POS", 5: "TRAPEZOID_POS", 6: "S-SHAPE_POS"}.get(val, "UNKNOWN")
                print(f"  RAMPMODE = 0x{val:X} ({mode_str})")
            elif "STATUS" in line:
                val = int(line.split("=")[1], 16)
                print(f"  STATUS = 0x{val:X}")
                if val & 0x01: print("    - TARGET_REACHED")
                if val & 0x80: print("    - STOPL_ACTIVE (左限位激活)")
                if val & 0x100: print("    - STOPR_ACTIVE (右限位激活)")
            elif "EVENTS" in line:
                val = int(line.split("=")[1], 16)
                print(f"  EVENTS = 0x{val:X}")
                if val & 0x800: print("    - STOPL_EVENT")
                if val & 0x1000: print("    - STOPR_EVENT")
                if val & 0x2000000: print("    - STALLGUARD EVENT!")
            elif "VMAX" in line and "SCALE" not in line:
                val = int(line.split("=")[1], 0)
                print(f"  VMAX = {val}")
            elif "XACTUAL" in line:
                val = int(line.split("=")[1], 0)
                print(f"  XACTUAL = {val} ({val/170666.67:.3f} mm)")
            elif "XTARGET" in line:
                val = int(line.split("=")[1], 0)
                print(f"  XTARGET = {val} ({val/170666.67:.3f} mm)")

        print("\n" + "=" * 60)
        print("调试完成 - 未执行任何移动操作")
        print("=" * 60)

    except Exception as e:
        print(f"错误: {e}")
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    main()
