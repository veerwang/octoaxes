#!/usr/bin/env python3
"""
测试 11: 简单移动测试（不涉及 homing）
在当前位置小范围移动，验证电机基本功能
"""

import serial
import time
import sys

PORT = "/dev/ttyACM0"
BAUDRATE = 115200
AXIS = "Z"

def send_command(ser, cmd, wait=0.3):
    """发送命令并读取响应（带协议头）"""
    print(f"[TX] {cmd}")
    # 添加协议头 0x55 0xAA
    data = b'\x55\xAA' + cmd.encode() + b'\n'
    ser.write(data)
    time.sleep(wait)
    response = []
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            response.append(line)
            print(f"  [RX] {line}")
    return response

def get_position(ser):
    """获取当前位置"""
    for line in send_command(ser, f"{AXIS}:GET_DATA", 0.3):
        if "STATE:" in line:
            parts = line.split(",")
            if len(parts) > 1:
                return float(parts[1].split(":")[1])
    return 0.0

def wait_for_idle(ser, timeout=10):
    """等待电机停止"""
    start = time.time()
    while time.time() - start < timeout:
        for line in send_command(ser, f"{AXIS}:GET_DATA", 0.2):
            if "STATE:" in line:
                state = line.split(",")[0].split(":")[1]
                if state == "IDLE":
                    return True
                if state == "ERROR":
                    return False
        time.sleep(0.3)
    return False

def main():
    print("=" * 60)
    print("测试 11: 简单移动测试（不涉及 homing）")
    print("=" * 60)
    print("此测试在当前位置小范围移动，不触发限位开关")
    print()

    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()
        print(f"连接 {PORT}...\n")

        # Engine Start
        print("[1] Engine Start")
        send_command(ser, "S:Engine Start", 3)

        # 获取当前位置
        print("\n[2] 读取当前位置")
        current_pos = get_position(ser)
        print(f"当前位置: {current_pos:.3f}mm")

        # 读取寄存器状态
        print("\n[3] 读取关键寄存器")
        for line in send_command(ser, f"{AXIS}:DEBUG_REG", 1):
            if any(x in line for x in ["RAMPMODE", "STATUS", "XACTUAL", "VMAX"]):
                if "SCALE" not in line:
                    print(f"  {line}")

        # 小距离相对移动测试
        print("\n[4] 相对移动测试")
        move_distance = 0.05  # mm (降低一半)

        resp = input(f"准备向正方向移动 {move_distance}mm，按 Enter 继续，'q' 退出: ")
        if resp.lower() == 'q':
            print("用户中止")
            ser.close()
            return

        # 计算目标位置 (微米)
        target_um = int((current_pos + move_distance) * 1000)
        print(f"\n发送 MOVETO 命令: 目标 {target_um}um ({target_um/1000:.3f}mm)")
        send_command(ser, f"{AXIS}:MOVETO_AXIS INT32 {target_um:08X}", 0.5)

        # 监控移动
        print("\n监控移动过程...")
        for i in range(20):
            time.sleep(0.3)
            pos = get_position(ser)
            print(f"  [{i*0.3:.1f}s] 位置: {pos:.4f}mm")

            # 检查是否完成
            for line in send_command(ser, f"{AXIS}:GET_DATA", 0.1):
                if "STATE:" in line:
                    state = line.split(",")[0].split(":")[1]
                    if state == "IDLE":
                        print(f"\n移动完成! 最终位置: {pos:.4f}mm")
                        break
                    elif state == "ERROR":
                        print(f"\n移动出错!")
                        break
            else:
                continue
            break

        # 反向移动
        print("\n[5] 反向移动测试")
        resp = input(f"准备向负方向移动 {move_distance}mm (返回原位)，按 Enter 继续，'q' 退出: ")
        if resp.lower() == 'q':
            print("用户中止")
            ser.close()
            return

        target_um = int(current_pos * 1000)
        print(f"\n发送 MOVETO 命令: 目标 {target_um}um ({target_um/1000:.3f}mm)")
        send_command(ser, f"{AXIS}:MOVETO_AXIS INT32 {target_um:08X}", 0.5)

        # 监控移动
        print("\n监控移动过程...")
        for i in range(20):
            time.sleep(0.3)
            pos = get_position(ser)
            print(f"  [{i*0.3:.1f}s] 位置: {pos:.4f}mm")

            for line in send_command(ser, f"{AXIS}:GET_DATA", 0.1):
                if "STATE:" in line:
                    state = line.split(",")[0].split(":")[1]
                    if state == "IDLE":
                        print(f"\n移动完成! 最终位置: {pos:.4f}mm")
                        break
                    elif state == "ERROR":
                        print(f"\n移动出错!")
                        break
            else:
                continue
            break

        # 最终状态
        print("\n" + "=" * 40)
        print("测试结束 - 最终状态")
        print("=" * 40)
        for line in send_command(ser, f"{AXIS}:GET_DATA", 0.5):
            print(f"  {line}")

    except Exception as e:
        print(f"错误: {e}")
        import traceback
        traceback.print_exc()
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    main()
