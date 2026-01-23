#!/usr/bin/env python3
"""
测试 05: 轴运动测试
测试单个轴的运动功能
"""

import serial
import sys
import time
import argparse

# 调试协议头
DEBUG_HEADER = bytes([0x55, 0xAA])

def send_debug_command(ser, command):
    """发送调试协议命令"""
    data = DEBUG_HEADER + command.encode('utf-8') + b'\n'
    print(f"[TX] {command}")
    ser.write(data)

def read_all_responses(ser, timeout=1.0):
    """读取所有可用响应"""
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

def get_axis_position(ser, axis_name):
    """获取轴当前位置"""
    ser.reset_input_buffer()
    send_debug_command(ser, f"{axis_name}:GET_POSITION")
    responses = read_all_responses(ser, timeout=0.5)

    for resp in responses:
        if f"{axis_name}:Current Position (microsteps)" in resp:
            try:
                return int(resp.split(":")[-1])
            except:
                pass
    return None

def wait_for_idle(ser, axis_name, timeout=10.0):
    """等待轴进入空闲状态"""
    start_time = time.time()

    while time.time() - start_time < timeout:
        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:GET_DATA")
        responses = read_all_responses(ser, timeout=0.3)

        for resp in responses:
            if f"{axis_name}:STATE:IDLE" in resp:
                return True
            if f"{axis_name}:IS_MOVING:NO" in resp:
                return True

        time.sleep(0.1)

    return False

def test_axis_move(port_name, axis_name, distance_um=1000, baudrate=2000000):
    """测试轴运动"""
    print(f"连接 {port_name}...")

    try:
        ser = serial.Serial(
            port=port_name,
            baudrate=baudrate,
            timeout=1
        )

        time.sleep(0.5)
        ser.reset_input_buffer()

        # 发送 Engine Start
        print("\n发送 Engine Start...")
        send_debug_command(ser, "S:Engine Start")
        time.sleep(1.0)
        read_all_responses(ser, timeout=0.5)

        # 等待初始化完成
        print(f"\n等待轴 {axis_name} 就绪...")
        if not wait_for_idle(ser, axis_name, timeout=5.0):
            print(f"[WARN] 轴 {axis_name} 未进入空闲状态，继续测试...")

        # 获取初始位置
        print(f"\n获取初始位置...")
        initial_pos = get_axis_position(ser, axis_name)
        if initial_pos is not None:
            print(f"  初始位置: {initial_pos} 微步")
        else:
            print(f"  [WARN] 无法获取初始位置")
            initial_pos = 0

        # 执行相对移动
        # distance_um 单位是微米，命令期望的是毫米 * 1000
        # 所以 distance_um 直接作为参数发送
        print(f"\n执行相对移动: {distance_um} 微米 ({distance_um/1000:.3f} mm)...")

        # 转换为 hex 格式 (32位有符号整数)
        if distance_um < 0:
            hex_val = (1 << 32) + distance_um
        else:
            hex_val = distance_um
        hex_str = f"{hex_val:08X}"

        send_debug_command(ser, f"{axis_name}:MOVE_AXIS INT32 {hex_str}")

        # 读取响应
        time.sleep(0.5)
        responses = read_all_responses(ser, timeout=1.0)
        for resp in responses:
            print(f"  [RX] {resp}")

        # 等待移动完成
        print("\n等待移动完成...")
        move_started = time.time()
        move_completed = wait_for_idle(ser, axis_name, timeout=10.0)

        if move_completed:
            move_duration = time.time() - move_started
            print(f"  移动完成，耗时: {move_duration:.2f} 秒")
        else:
            print(f"  [WARN] 等待超时")

        # 获取最终位置
        print(f"\n获取最终位置...")
        final_pos = get_axis_position(ser, axis_name)
        if final_pos is not None:
            print(f"  最终位置: {final_pos} 微步")
            delta = final_pos - initial_pos
            print(f"  位置变化: {delta} 微步")

            # 计算预期位置变化 (假设 256 微步)
            # 1 mm = fullSteps * microsteps / screwPitch
            # 这个计算需要知道具体配置，这里只做基本验证
            if delta != 0:
                print(f"  [OK] 检测到位置变化")
                success = True
            else:
                print(f"  [WARN] 位置未变化")
                success = False
        else:
            print(f"  [WARN] 无法获取最终位置")
            success = False

        ser.close()
        return success

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='轴运动测试')
    parser.add_argument('port', nargs='?', default='/dev/ttyACM0', help='串口端口')
    parser.add_argument('-a', '--axis', default='X', help='测试轴名称 (默认: X)')
    parser.add_argument('-d', '--distance', type=int, default=1000, help='移动距离 (微米, 默认: 1000)')

    args = parser.parse_args()

    print("=" * 60)
    print("测试 05: 轴运动测试")
    print("=" * 60)
    print()
    print(f"端口: {args.port}")
    print(f"测试轴: {args.axis}")
    print(f"移动距离: {args.distance} 微米")
    print()

    success = test_axis_move(args.port, args.axis, args.distance)

    print()
    print("=" * 60)
    if success:
        print("结果: PASS - 轴运动正常")
    else:
        print("结果: FAIL - 轴运动异常")
        print()
        print("可能原因:")
        print("  1. 驱动器电流配置问题")
        print("  2. 电机连接问题")
        print("  3. 运动控制器配置错误")
        print("  4. 速度/加速度参数异常")
    print("=" * 60)

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
