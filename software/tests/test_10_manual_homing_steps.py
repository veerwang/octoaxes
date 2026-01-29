#!/usr/bin/env python3
"""
测试 10: 手动单步 Homing 调试
每一步都需要用户确认，可随时中止
"""

import serial
import time
import sys

PORT = "/dev/ttyACM0"
BAUDRATE = 115200
AXIS = "Z"

# Z 轴参数
STEPS_PER_MM = 170666.67  # 根据实际配置调整

def send_command(ser, cmd, wait=0.3, show=True):
    """发送命令并读取响应（带协议头）"""
    if show:
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
            if show:
                print(f"  [RX] {line}")
    return response

def get_status(ser):
    """获取当前状态"""
    for line in send_command(ser, f"{AXIS}:GET_DATA", 0.3, show=False):
        if "STATE:" in line:
            parts = line.split(",")
            state = parts[0].split(":")[1] if ":" in parts[0] else "UNKNOWN"
            pos = float(parts[1].split(":")[1]) if len(parts) > 1 else 0
            lim = int(parts[3].split(":")[1], 16) if len(parts) > 3 else 0
            return state, pos, lim
    return "UNKNOWN", 0, 0

def wait_for_key(prompt="按 Enter 继续，输入 'q' 退出: "):
    """等待用户输入"""
    resp = input(prompt)
    if resp.lower() == 'q':
        print("用户中止")
        sys.exit(0)
    return resp

def main():
    print("=" * 60)
    print("测试 10: 手动单步 Homing 调试")
    print("=" * 60)
    print("每一步都会暂停等待确认，输入 'q' 随时退出")
    print()

    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()
        print(f"连接 {PORT}...\n")

        # Step 1: Engine Start
        print("=" * 40)
        print("Step 1: Engine Start")
        print("=" * 40)
        wait_for_key()
        send_command(ser, "S:Engine Start", 3)
        state, pos, lim = get_status(ser)
        print(f"\n当前状态: {state}, 位置: {pos:.3f}mm, 限位: 0x{lim:X}")

        # Step 2: Reset
        print("\n" + "=" * 40)
        print("Step 2: Reset 轴状态")
        print("=" * 40)
        wait_for_key()
        send_command(ser, f"{AXIS}:RESET", 1)
        state, pos, lim = get_status(ser)
        print(f"\n当前状态: {state}, 位置: {pos:.3f}mm, 限位: 0x{lim:X}")

        # Step 3: 检查是否在限位
        print("\n" + "=" * 40)
        print("Step 3: 检查限位状态")
        print("=" * 40)
        state, pos, lim = get_status(ser)
        if lim & 0x2:
            print("!! 右限位已激活 - 电机可能在限位位置")
            resp = wait_for_key("是否尝试手动移动离开限位? (y/n): ")
            if resp.lower() == 'y':
                # 尝试小距离负方向移动
                print("尝试向负方向移动 0.5mm...")
                target_um = int((pos - 0.5) * 1000)
                send_command(ser, f"{AXIS}:MOVETO_AXIS INT32 {target_um:08X}", 2)
                state, pos, lim = get_status(ser)
                print(f"移动后状态: {state}, 位置: {pos:.3f}mm, 限位: 0x{lim:X}")
        elif lim & 0x1:
            print("!! 左限位已激活 - 电机可能在限位位置")
        else:
            print("限位未激活，可以安全进行 homing")

        # Step 4: 开始 Homing
        print("\n" + "=" * 40)
        print("Step 4: 开始 Homing (可随时按 Ctrl+C 中止)")
        print("=" * 40)
        print("注意: Homing 会向限位方向移动电机")
        wait_for_key()

        send_command(ser, f"{AXIS}:HOMING", 0.5)

        # 监控 homing 过程
        print("\n监控 Homing 过程 (按 Ctrl+C 发送 RESET 停止)...")
        try:
            for i in range(60):
                time.sleep(0.5)
                state, pos, lim = get_status(ser)
                print(f"  [{i*0.5:4.1f}s] STATE={state:15s} POS={pos:8.3f}mm LIM=0x{lim:X}")

                # 读取串口输出
                while ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line and ("limit" in line.lower() or "homing" in line.lower() or
                                "stop" in line.lower() or "safe" in line.lower()):
                        print(f"    >> {line}")

                if state == "IDLE":
                    print("\n  [OK] Homing 完成!")
                    break
                elif state == "ERROR":
                    print("\n  [ERROR] Homing 失败!")
                    break
        except KeyboardInterrupt:
            print("\n\n用户中断 - 发送 RESET...")
            send_command(ser, f"{AXIS}:RESET", 1)

        # 最终状态
        print("\n" + "=" * 40)
        print("最终状态")
        print("=" * 40)
        state, pos, lim = get_status(ser)
        print(f"状态: {state}")
        print(f"位置: {pos:.3f}mm")
        print(f"限位: 0x{lim:X}")

        # 读取寄存器
        print("\n关键寄存器:")
        for line in send_command(ser, f"{AXIS}:DEBUG_REG", 1, show=False):
            if any(x in line for x in ["RAMPMODE", "STATUS", "EVENTS", "XACTUAL", "XTARGET", "VMAX"]):
                if "SCALE" not in line:
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
