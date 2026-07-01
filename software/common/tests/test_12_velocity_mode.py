#!/usr/bin/env python3
"""
测试 12: 直接测试速度模式
绕过 homing 流程，直接测试 motor_setVelocityInternal
"""

import serial
import time

PORT = "/dev/ttyACM0"
BAUDRATE = 115200

def send_cmd(ser, cmd, wait=0.5):
    """发送命令（带协议头）"""
    data = b'\x55\xAA' + cmd.encode() + b'\n'
    ser.write(data)
    time.sleep(wait)
    lines = []
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line and 'RX_AVAIL' not in line and 'LOOP_ALIVE' not in line:
            lines.append(line)
            print(f"  {line}")
    return lines

def get_reg(ser, reg_name):
    """从 DEBUG_REG 输出中获取特定寄存器值"""
    for line in send_cmd(ser, "Z:DEBUG_REG", 1):
        if reg_name in line:
            val = line.split("=")[1]
            return int(val, 0)
    return None

def main():
    print("=" * 60)
    print("测试 12: 直接测试速度模式")
    print("=" * 60)

    ser = serial.Serial(PORT, BAUDRATE, timeout=1)
    time.sleep(2)
    ser.reset_input_buffer()

    # Engine Start
    print("\n[1] Engine Start")
    send_cmd(ser, "S:Engine Start", 3)

    # read the initial state
    print("\n[2] 初始寄存器状态")
    send_cmd(ser, "Z:DEBUG_REG", 1)

    # send the velocity-mode test command directly
    # we need a firmware command to directly call motor_setVelocityInternal
    # for now use the HOMING command, but check the registers immediately

    print("\n[3] 发送 HOMING 命令（触发速度模式）")
    send_cmd(ser, "Z:HOMING", 0.5)

    # read the registers immediately
    print("\n[4] HOMING 后立刻检查寄存器")
    for line in send_cmd(ser, "Z:DEBUG_REG", 0.5):
        if any(x in line for x in ["RAMPMODE", "VMAX", "XACTUAL", "VACTUAL"]):
            print(f"  >>> {line}")

    # monitor the position change
    print("\n[5] 监控 XACTUAL 变化 (5秒)")
    for i in range(10):
        time.sleep(0.5)
        # read XACTUAL directly
        for line in send_cmd(ser, "Z:DEBUG_REG", 0.3):
            if "XACTUAL" in line:
                print(f"  [{i*0.5:.1f}s] {line}")
                break

    # send RESET to stop
    print("\n[6] 发送 RESET 停止电机")
    send_cmd(ser, "Z:RESET", 1)

    # final state
    print("\n[7] 最终寄存器状态")
    for line in send_cmd(ser, "Z:DEBUG_REG", 1):
        if any(x in line for x in ["RAMPMODE", "VMAX", "XACTUAL", "VACTUAL", "STATUS", "EVENTS"]):
            print(f"  {line}")

    ser.close()
    print("\n测试完成")

if __name__ == "__main__":
    main()
