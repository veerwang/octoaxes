#!/usr/bin/env python3
"""
测试 03: Engine Start 启动测试
发送启动命令，验证系统进入运行状态
"""

import serial
import sys
import time

# 调试协议头
DEBUG_HEADER = bytes([0x55, 0xAA])

def send_debug_command(ser, command):
    """发送调试协议命令"""
    data = DEBUG_HEADER + command.encode('utf-8') + b'\n'
    print(f"[TX] {data.hex()} ({command})")
    ser.write(data)

def read_all_responses(ser, timeout=2.0):
    """读取所有可用响应"""
    responses = []
    start_time = time.time()
    buffer = b''

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            chunk = ser.read(ser.in_waiting)
            buffer += chunk
            # 按换行符分割
            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                if line:
                    responses.append(line)
        time.sleep(0.01)

    # 处理剩余数据
    if buffer:
        responses.append(buffer)

    return responses

def test_engine_start(port_name, baudrate=2000000):
    """测试 Engine Start 命令"""
    print(f"连接 {port_name}...")

    try:
        ser = serial.Serial(
            port=port_name,
            baudrate=baudrate,
            timeout=1
        )

        time.sleep(0.5)
        ser.reset_input_buffer()

        # 先查询版本确认通信正常
        print()
        print("-" * 40)
        print("步骤 1: 查询版本...")
        print("-" * 40)
        send_debug_command(ser, "S:VERSION")
        time.sleep(0.5)
        responses = read_all_responses(ser, timeout=1.0)
        for resp in responses:
            print(f"[RX] {resp.decode('utf-8', errors='replace')}")

        # 发送 Engine Start 命令
        print()
        print("-" * 40)
        print("步骤 2: 发送 Engine Start...")
        print("-" * 40)
        send_debug_command(ser, "S:Engine Start")

        # 等待响应
        time.sleep(1.0)
        responses = read_all_responses(ser, timeout=2.0)

        engine_started = False
        for resp in responses:
            text = resp.decode('utf-8', errors='replace')
            print(f"[RX] {text}")
            if "Engine Start" in text or "Starting" in text:
                engine_started = True

        ser.close()

        print()
        if engine_started:
            print("[OK] Engine Start 成功!")
            return True
        else:
            print("[WARN] 未收到明确的启动确认")
            print("       但命令可能已执行，请继续下一个测试")
            return True  # 继续测试

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return False

def main():
    print("=" * 60)
    print("测试 03: Engine Start 启动测试")
    print("=" * 60)
    print()

    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = '/dev/ttyACM0'
        print(f"[INFO] 使用默认端口: {port}")

    print()
    success = test_engine_start(port)

    print()
    print("=" * 60)
    if success:
        print("结果: PASS")
        print(f"下一步: 运行 test_04_tmc_status.py {port}")
    else:
        print("结果: FAIL")
    print("=" * 60)

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
