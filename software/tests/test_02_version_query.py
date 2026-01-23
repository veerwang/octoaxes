#!/usr/bin/env python3
"""
测试 02: 版本查询测试
使用调试协议发送 S:VERSION 命令，验证固件响应
"""

import serial
import sys
import time

# 调试协议头
DEBUG_HEADER = bytes([0x55, 0xAA])

def send_debug_command(ser, command):
    """发送调试协议命令"""
    # 格式: 0x55 0xAA + 命令 + 换行
    data = DEBUG_HEADER + command.encode('utf-8') + b'\n'
    print(f"[TX] {data.hex()} ({command})")
    ser.write(data)

def read_response(ser, timeout=2.0):
    """读取响应"""
    start_time = time.time()
    response = b''

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            chunk = ser.read(ser.in_waiting)
            response += chunk
            # 检查是否收到换行符
            if b'\n' in response:
                break
        time.sleep(0.01)

    return response

def test_version_query(port_name, baudrate=2000000):
    """测试版本查询"""
    print(f"连接 {port_name}...")

    try:
        ser = serial.Serial(
            port=port_name,
            baudrate=baudrate,
            timeout=1
        )

        # 等待初始化
        time.sleep(0.5)
        ser.reset_input_buffer()

        print()
        print("-" * 40)
        print("发送版本查询命令...")
        print("-" * 40)

        # 发送版本查询
        send_debug_command(ser, "S:VERSION")

        # 读取响应
        response = read_response(ser, timeout=2.0)

        if response:
            print(f"[RX] {response.hex()}")
            try:
                text = response.decode('utf-8', errors='replace').strip()
                print(f"[RX] 文本: {text}")

                if "VERSION" in text:
                    print()
                    print("[OK] 版本查询成功!")
                    ser.close()
                    return True
                else:
                    print()
                    print("[WARN] 收到响应但格式不符")
            except:
                print("[WARN] 无法解码响应")
        else:
            print("[RX] (无响应)")

        ser.close()
        print()
        print("[FAIL] 版本查询失败")
        return False

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return False

def main():
    print("=" * 60)
    print("测试 02: 版本查询测试")
    print("=" * 60)
    print()

    # 从命令行获取端口，或使用默认值
    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = '/dev/ttyACM0'
        print(f"[INFO] 使用默认端口: {port}")
        print("       用法: python test_02_version_query.py <port>")

    print()
    success = test_version_query(port)

    print()
    print("=" * 60)
    if success:
        print("结果: PASS - 固件通信正常")
        print(f"下一步: 运行 test_03_engine_start.py {port}")
    else:
        print("结果: FAIL")
        print()
        print("可能原因:")
        print("  1. 固件未正确烧写")
        print("  2. 固件初始化失败")
        print("  3. SPI 通信问题导致固件卡住")
        print()
        print("调试建议:")
        print("  - 检查 Teensy 上的 LED 状态")
        print("  - 使用 pio device monitor 查看调试输出")
    print("=" * 60)

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
