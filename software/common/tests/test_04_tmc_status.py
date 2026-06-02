#!/usr/bin/env python3
"""
测试 04: TMC 芯片通信状态测试
查询各轴的状态数据，验证 TMC4361A/TMC2660 通信是否正常
"""

import serial
import sys
import time

# 调试协议头
DEBUG_HEADER = bytes([0x55, 0xAA])

# 可用的轴名称
AXIS_NAMES = ['X', 'Y', 'Z', 'W', 'Turret', 'E3', 'E4']

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
                        responses.append(f"[binary: {line.hex()}]")
        time.sleep(0.01)

    if buffer:
        try:
            responses.append(buffer.decode('utf-8', errors='replace').strip())
        except:
            pass

    return responses

def test_axis_status(ser, axis_name):
    """测试单个轴的状态"""
    print(f"\n--- 测试轴: {axis_name} ---")

    # 清空缓冲区
    ser.reset_input_buffer()

    # 发送 GET_DATA 命令
    send_debug_command(ser, f"{axis_name}:GET_DATA")

    # 读取响应
    responses = read_all_responses(ser, timeout=1.0)

    if not responses:
        print(f"  [WARN] 无响应")
        return False, "no_response"

    # 分析响应
    has_position = False
    has_limit = False
    has_state = False
    position_value = None
    limit_value = None
    state_value = None

    for resp in responses:
        print(f"  [RX] {resp}")

        if f"{axis_name}:Current Position" in resp:
            has_position = True
            if "microsteps" in resp:
                try:
                    position_value = int(resp.split(":")[-1])
                except:
                    pass

        if f"{axis_name}:LIMIT_SWITCHES" in resp:
            has_limit = True
            try:
                limit_value = resp.split(":")[-1]
            except:
                pass

        if f"{axis_name}:STATE" in resp:
            has_state = True
            try:
                state_value = resp.split(":")[-1]
            except:
                pass

    # 判断通信状态
    if has_position and has_limit:
        # 检查是否是异常值 (例如全 0xFF 表示通信失败)
        if position_value is not None and abs(position_value) > 0x7FFFFFFF:
            print(f"  [FAIL] 位置值异常: {position_value}")
            return False, "bad_position"

        print(f"  [OK] 轴 {axis_name} 通信正常")
        return True, "ok"
    else:
        print(f"  [WARN] 响应不完整 (position={has_position}, limit={has_limit}, state={has_state})")
        return False, "incomplete"

def test_tmc_communication(port_name, baudrate=2000000):
    """测试 TMC 芯片通信"""
    print(f"连接 {port_name}...")

    try:
        ser = serial.Serial(
            port=port_name,
            baudrate=baudrate,
            timeout=1
        )

        time.sleep(0.5)
        ser.reset_input_buffer()

        # 先发送 Engine Start (如果需要)
        print("\n发送 Engine Start...")
        send_debug_command(ser, "S:Engine Start")
        time.sleep(1.0)
        responses = read_all_responses(ser, timeout=1.0)
        for resp in responses:
            print(f"  [RX] {resp}")

        # 测试各轴
        results = {}
        print("\n" + "=" * 50)
        print("测试各轴 TMC 通信状态")
        print("=" * 50)

        for axis in AXIS_NAMES:
            success, status = test_axis_status(ser, axis)
            results[axis] = (success, status)
            time.sleep(0.2)

        ser.close()

        # 汇总结果
        print("\n" + "=" * 50)
        print("测试结果汇总")
        print("=" * 50)

        all_passed = True
        for axis, (success, status) in results.items():
            status_str = "PASS" if success else f"FAIL ({status})"
            print(f"  轴 {axis}: {status_str}")
            if not success:
                all_passed = False

        return all_passed

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return False

def main():
    print("=" * 60)
    print("测试 04: TMC 芯片通信状态测试")
    print("=" * 60)
    print()

    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = '/dev/ttyACM0'
        print(f"[INFO] 使用默认端口: {port}")

    print()
    success = test_tmc_communication(port)

    print()
    print("=" * 60)
    if success:
        print("结果: PASS - 所有轴 TMC 通信正常")
        print(f"下一步: 运行 test_05_axis_move.py {port}")
    else:
        print("结果: FAIL - 部分轴通信异常")
        print()
        print("可能原因:")
        print("  1. SPI 硬件连接问题")
        print("  2. TMC4361A 初始化失败")
        print("  3. TMC2660 驱动器未响应")
        print()
        print("调试建议:")
        print("  - 检查 SPI 接线 (MOSI, MISO, SCK, CS)")
        print("  - 检查芯片供电")
        print("  - 使用示波器观察 SPI 信号")
    print("=" * 60)

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
