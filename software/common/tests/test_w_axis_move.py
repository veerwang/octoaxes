#!/usr/bin/env python3
"""
W 轴（滤光轮）移动测试脚本

测试内容：
1. 向前移动一个孔，做两次
2. 向后移动一个孔，做两次
"""

import sys
import time
import struct
import serial
import serial.tools.list_ports

# 滤光轮配置
FILTERWHEEL_HOLE_DISTANCE_MM = 12.5  # 每个孔位的距离 (mm)

# 串口配置
BAUDRATE = 115200
TIMEOUT = 0.1


class WAxisMoveTest:
    """W 轴移动测试类"""

    def __init__(self):
        self.ser = None
        self.port = None

    def find_teensy_port(self):
        """查找 Teensy 设备端口"""
        ports = serial.tools.list_ports.comports()
        for port in ports:
            desc = port.description.lower()
            if "usb serial" in desc or "teensy" in desc:
                print(f"[INFO] 找到 Teensy 设备: {port.device}")
                return port.device
        return None

    def connect(self, port=None):
        """连接到 Teensy"""
        if port is None:
            port = self.find_teensy_port()

        if port is None:
            print("[ERROR] 未找到 Teensy 设备")
            return False

        try:
            self.ser = serial.Serial(
                port=port, baudrate=BAUDRATE, timeout=TIMEOUT, write_timeout=0.5
            )
            self.port = port
            time.sleep(0.5)
            print(f"[OK] 已连接到 {port}")
            return True
        except Exception as e:
            print(f"[ERROR] 连接失败: {e}")
            return False

    def disconnect(self):
        """断开连接"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[OK] 已断开连接")

    def send_command(self, command):
        """发送字符串命令"""
        if not self.ser or not self.ser.is_open:
            return False

        try:
            data = b"\x55\xaa" + (command + "\n").encode("utf-8")
            self.ser.write(data)
            print(f"[TX] {command}")
            return True
        except Exception as e:
            print(f"[ERROR] 发送失败: {e}")
            return False

    def read_response(self, timeout=2.0):
        """读取响应"""
        if not self.ser or not self.ser.is_open:
            return []

        start_time = time.time()
        buffer = ""
        lines = []

        while time.time() - start_time < timeout:
            try:
                if self.ser.in_waiting > 0:
                    data = self.ser.read(self.ser.in_waiting)
                    buffer += data.decode("utf-8", errors="ignore")

                    while "\n" in buffer:
                        line_end = buffer.find("\n")
                        line = buffer[:line_end].strip()
                        buffer = buffer[line_end + 1 :]

                        if line:
                            print(f"[RX] {line}")
                            lines.append(line)

                time.sleep(0.01)
            except Exception as e:
                print(f"[ERROR] 读取错误: {e}")
                break

        return lines

    def send_engine_start(self):
        """发送 Engine Start 命令"""
        print("\n" + "=" * 50)
        print("初始化系统")
        print("=" * 50)

        self.send_command("S:Engine Start")
        time.sleep(0.5)
        responses = self.read_response(timeout=5.0)

        for line in responses:
            if "initialized" in line.lower() or "success" in line.lower():
                print("[OK] 系统初始化成功")
                return True

        return True

    def get_w_axis_status(self):
        """获取 W 轴状态"""
        self.send_command("W:GET_DATA")
        time.sleep(0.1)
        return self.read_response(timeout=1.0)

    def wait_for_move_complete(self, timeout=10.0):
        """等待移动完成"""
        start_time = time.time()

        while time.time() - start_time < timeout:
            self.send_command("W:GET_DATA")
            time.sleep(0.2)
            responses = self.read_response(timeout=0.5)

            for line in responses:
                if "IS_MOVING:NO" in line:
                    return True

            time.sleep(0.3)

        print("[WARN] 等待移动完成超时")
        return False

    def int_to_hex(self, value):
        """将有符号整数转换为十六进制字符串（大端序）"""
        packed = struct.pack("!i", value)  # 大端序有符号 32 位整数
        return packed.hex()

    def move_forward_one_hole(self):
        """向前移动一个孔位（正方向）"""
        distance_um = int(FILTERWHEEL_HOLE_DISTANCE_MM * 1000)  # 转换为微米
        hex_value = self.int_to_hex(distance_um)
        self.send_command(f"W:MOVE_AXIS int {hex_value}")
        time.sleep(0.1)
        self.read_response(timeout=0.5)

    def move_backward_one_hole(self):
        """向后移动一个孔位（负方向）"""
        distance_um = int(-FILTERWHEEL_HOLE_DISTANCE_MM * 1000)  # 转换为微米（负值）
        hex_value = self.int_to_hex(distance_um)
        self.send_command(f"W:MOVE_AXIS int {hex_value}")
        time.sleep(0.1)
        self.read_response(timeout=0.5)

    def run_test(self, port=None):
        """运行移动测试"""
        print("\n" + "=" * 60)
        print("    W 轴（滤光轮）移动测试")
        print("=" * 60)
        print(f"    每个孔位距离: {FILTERWHEEL_HOLE_DISTANCE_MM} mm")
        print("=" * 60)

        if not self.connect(port):
            return False

        try:
            # 初始化系统
            self.send_engine_start()
            time.sleep(2.0)

            # 获取初始状态
            print("\n[INFO] 获取初始状态...")
            self.get_w_axis_status()
            time.sleep(0.5)

            # 测试 1: 向前移动一个孔（第一次）
            print("\n" + "=" * 50)
            print("测试 1: 向前移动一个孔（第一次）")
            print("=" * 50)
            self.move_forward_one_hole()
            self.wait_for_move_complete()
            print("[INFO] 当前状态:")
            self.get_w_axis_status()
            time.sleep(1.0)

            # 测试 2: 向前移动一个孔（第二次）
            print("\n" + "=" * 50)
            print("测试 2: 向前移动一个孔（第二次）")
            print("=" * 50)
            self.move_forward_one_hole()
            self.wait_for_move_complete()
            print("[INFO] 当前状态:")
            self.get_w_axis_status()
            time.sleep(1.0)

            # 测试 3: 向后移动一个孔（第一次）
            print("\n" + "=" * 50)
            print("测试 3: 向后移动一个孔（第一次）")
            print("=" * 50)
            self.move_backward_one_hole()
            self.wait_for_move_complete()
            print("[INFO] 当前状态:")
            self.get_w_axis_status()
            time.sleep(1.0)

            # 测试 4: 向后移动一个孔（第二次）
            print("\n" + "=" * 50)
            print("测试 4: 向后移动一个孔（第二次）")
            print("=" * 50)
            self.move_backward_one_hole()
            self.wait_for_move_complete()
            print("[INFO] 最终状态:")
            self.get_w_axis_status()
            time.sleep(0.5)

            # 总结
            print("\n" + "=" * 60)
            print("    测试完成")
            print("=" * 60)

            return True

        finally:
            self.disconnect()


def main():
    """主函数"""
    import argparse

    parser = argparse.ArgumentParser(description="W 轴移动测试")
    parser.add_argument("-p", "--port", help="串口端口", default=None)
    args = parser.parse_args()

    test = WAxisMoveTest()
    success = test.run_test(port=args.port)

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
