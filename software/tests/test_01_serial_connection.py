#!/usr/bin/env python3
"""
测试 01: 串口连接测试
验证能否打开串口并与 Teensy 建立连接
"""

import serial
import serial.tools.list_ports
import sys
import time

def find_teensy_port():
    """查找 Teensy 设备端口"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        # Teensy 4.1 的 VID:PID 是 16C0:0483
        if port.vid == 0x16C0 and port.pid == 0x0483:
            return port.device
        # 也检查描述信息
        if 'teensy' in port.description.lower():
            return port.device
    return None

def list_all_ports():
    """列出所有可用串口"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("未发现任何串口设备")
        return

    print("可用串口设备:")
    print("-" * 60)
    for port in ports:
        print(f"  端口: {port.device}")
        print(f"    描述: {port.description}")
        print(f"    VID:PID: {port.vid:04X}:{port.pid:04X}" if port.vid else "    VID:PID: N/A")
        print(f"    序列号: {port.serial_number or 'N/A'}")
        print()

def test_connection(port_name, baudrate=2000000):
    """测试串口连接"""
    print(f"尝试连接 {port_name} @ {baudrate} bps...")

    try:
        ser = serial.Serial(
            port=port_name,
            baudrate=baudrate,
            timeout=1
        )
        print(f"[OK] 串口已打开: {ser.name}")

        # 等待 Teensy 初始化
        time.sleep(0.5)

        # 清空缓冲区
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        print(f"[OK] 串口配置:")
        print(f"     波特率: {ser.baudrate}")
        print(f"     数据位: {ser.bytesize}")
        print(f"     停止位: {ser.stopbits}")
        print(f"     校验位: {ser.parity}")

        ser.close()
        print("[OK] 串口连接测试通过")
        return True

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return False

def main():
    print("=" * 60)
    print("测试 01: 串口连接测试")
    print("=" * 60)
    print()

    # 列出所有端口
    list_all_ports()

    # 查找 Teensy
    port = find_teensy_port()

    if port:
        print(f"[INFO] 自动检测到 Teensy: {port}")
    else:
        print("[WARN] 未自动检测到 Teensy 设备")
        # 尝试常见端口
        common_ports = ['/dev/ttyACM0', '/dev/ttyACM1', '/dev/ttyUSB0']
        for p in common_ports:
            try:
                ser = serial.Serial(p, timeout=0.1)
                ser.close()
                port = p
                print(f"[INFO] 找到可用端口: {port}")
                break
            except:
                pass

    if not port:
        print("[FAIL] 未找到可用串口")
        print("\n请检查:")
        print("  1. Teensy 是否通过 USB 连接")
        print("  2. 是否有权限访问串口 (可能需要 sudo 或加入 dialout 组)")
        print("  3. 固件是否已烧写")
        sys.exit(1)

    print()
    print("-" * 60)

    # 测试连接
    success = test_connection(port)

    print()
    print("=" * 60)
    if success:
        print("结果: PASS")
        print(f"下一步: 运行 test_02_version_query.py {port}")
    else:
        print("结果: FAIL")
    print("=" * 60)

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
