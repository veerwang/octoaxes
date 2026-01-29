#!/usr/bin/env python3
"""
W 轴（滤光轮）Homing 功能测试脚本

测试内容：
1. 连接 Teensy 设备
2. 发送 Engine Start 命令
3. 发送 W 轴 homing 命令
4. 监控 homing 过程状态
5. 验证 homing 完成
"""

import sys
import time
import serial
import serial.tools.list_ports
from crc import CrcCalculator, Crc8

# 命令定义
CMD_HOME_OR_ZERO = 5
W_AXIS_INDEX = 3

# 串口配置
BAUDRATE = 115200
TIMEOUT = 0.1


class WAxisHomingTest:
    """W 轴 Homing 测试类"""

    def __init__(self):
        self.ser = None
        self.crc_calculator = CrcCalculator(Crc8.CCITT, table_based=True)
        self.port = None

    def find_teensy_port(self):
        """查找 Teensy 设备端口"""
        ports = serial.tools.list_ports.comports()
        for port in ports:
            desc = port.description.lower()
            if "usb serial" in desc or "teensy" in desc:
                print(f"[INFO] 找到 Teensy 设备: {port.device} ({port.description})")
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
            time.sleep(0.5)  # 等待串口稳定
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

    def send_string_command(self, command):
        """发送字符串命令"""
        if not self.ser or not self.ser.is_open:
            print("[ERROR] 串口未连接")
            return False

        try:
            # 添加前缀和换行符
            data = b"\x55\xaa" + (command + "\n").encode("utf-8")
            self.ser.write(data)
            print(f"[TX] {command}")
            return True
        except Exception as e:
            print(f"[ERROR] 发送失败: {e}")
            return False

    def send_binary_command(self, cmd, axis_index, params=None):
        """发送二进制命令"""
        if not self.ser or not self.ser.is_open:
            print("[ERROR] 串口未连接")
            return False

        try:
            # 构建二进制数据包
            # 格式: [前缀(1), CMD(1), 轴索引(1), 参数(3), CRC(1)]
            data = bytearray(8)
            data[0] = 0x02  # 二进制命令前缀
            data[1] = cmd
            data[2] = axis_index

            if params:
                for i, p in enumerate(params[:5]):
                    data[3 + i] = p

            # 计算 CRC
            data[7] = self.crc_calculator.calculate_checksum(data[:-1])

            self.ser.write(data)
            print(f"[TX] Binary: CMD={cmd}, Axis={axis_index}, Data={data.hex()}")
            return True
        except Exception as e:
            print(f"[ERROR] 发送失败: {e}")
            return False

    def read_response(self, timeout=2.0):
        """读取响应"""
        if not self.ser or not self.ser.is_open:
            return None

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
        print("步骤 1: 发送 Engine Start 命令")
        print("=" * 50)

        self.send_string_command("S:Engine Start")
        time.sleep(0.5)

        # 等待系统初始化
        print("[INFO] 等待系统初始化...")
        responses = self.read_response(timeout=5.0)

        # 检查是否初始化成功
        for line in responses:
            if "initialized" in line.lower() or "success" in line.lower():
                print("[OK] 系统初始化成功")
                return True

        print("[WARN] 未收到明确的初始化成功消息，继续测试...")
        return True

    def get_w_axis_status(self):
        """获取 W 轴状态"""
        self.send_string_command("W:GET_DATA")
        time.sleep(0.1)
        return self.read_response(timeout=1.0)

    def start_w_axis_homing(self):
        """启动 W 轴 homing"""
        print("\n" + "=" * 50)
        print("步骤 2: 启动 W 轴 Homing")
        print("=" * 50)

        # 方法 1: 使用字符串命令
        print("[INFO] 发送 W 轴 homing 命令（字符串方式）...")
        self.send_string_command("W:HOMING")

        # 或者使用二进制命令（备选）
        # print("[INFO] 发送 W 轴 homing 命令（二进制方式）...")
        # self.send_binary_command(CMD_HOME_OR_ZERO, W_AXIS_INDEX)

        return True

    def monitor_homing_progress(self, timeout=30.0):
        """监控 homing 进度"""
        print("\n" + "=" * 50)
        print("步骤 3: 监控 Homing 进度")
        print("=" * 50)

        start_time = time.time()
        last_status_time = 0
        homing_complete = False
        homing_error = False

        while time.time() - start_time < timeout:
            # 每秒查询一次状态
            current_time = time.time()
            if current_time - last_status_time >= 1.0:
                self.send_string_command("W:GET_DATA")
                last_status_time = current_time

            # 读取响应
            responses = self.read_response(timeout=0.5)

            for line in responses:
                line_lower = line.lower()

                # 检查状态
                if "w:state:" in line_lower:
                    if "idle" in line_lower:
                        print("[OK] W 轴状态: IDLE - Homing 完成")
                        homing_complete = True
                    elif "homing" in line_lower:
                        print(f"[INFO] W 轴状态: Homing 进行中...")
                    elif "error" in line_lower:
                        print(f"[ERROR] W 轴状态: 错误")
                        homing_error = True

                # 检查位置
                if "w:current position" in line_lower or "pos:" in line_lower:
                    print(f"[INFO] {line}")

                # 检查完成标志
                if "homing complete" in line_lower or "home found" in line_lower:
                    print("[OK] 收到 Homing 完成信号")
                    homing_complete = True

            if homing_complete or homing_error:
                break

            time.sleep(0.1)

        return homing_complete, homing_error

    def verify_homing_result(self):
        """验证 homing 结果"""
        print("\n" + "=" * 50)
        print("步骤 4: 验证 Homing 结果")
        print("=" * 50)

        # 获取最终状态
        time.sleep(0.5)
        self.send_string_command("W:GET_DATA")
        time.sleep(0.2)
        responses = self.read_response(timeout=1.0)

        # 分析结果
        is_idle = False
        position = None

        for line in responses:
            if "W:STATE:" in line and "IDLE" in line.upper():
                is_idle = True
            if "Position" in line or "Pos:" in line:
                position = line

        if is_idle:
            print("[OK] W 轴已回到 IDLE 状态")
        else:
            print("[WARN] W 轴状态未知")

        if position:
            print(f"[INFO] 当前位置: {position}")

        return is_idle

    def run_test(self, port=None):
        """运行完整测试"""
        print("\n" + "=" * 60)
        print("    W 轴（滤光轮）Homing 功能测试")
        print("=" * 60)

        # 连接
        if not self.connect(port):
            return False

        try:
            # 发送 Engine Start
            if not self.send_engine_start():
                print("[ERROR] Engine Start 失败")
                return False

            # 等待系统完全初始化
            print("[INFO] 等待系统完全初始化...")
            time.sleep(2.0)

            # 获取初始状态
            print("\n[INFO] 获取 W 轴初始状态...")
            self.get_w_axis_status()
            time.sleep(0.5)

            # 启动 homing
            if not self.start_w_axis_homing():
                print("[ERROR] 启动 Homing 失败")
                return False

            # 监控进度
            homing_complete, homing_error = self.monitor_homing_progress(timeout=30.0)

            if homing_error:
                print("\n[FAIL] Homing 过程出现错误")
                return False

            if not homing_complete:
                print("\n[FAIL] Homing 超时")
                return False

            # 验证结果
            success = self.verify_homing_result()

            # 总结
            print("\n" + "=" * 60)
            if success:
                print("    测试结果: 通过")
            else:
                print("    测试结果: 未能完全验证")
            print("=" * 60)

            return success

        finally:
            self.disconnect()


def main():
    """主函数"""
    import argparse

    parser = argparse.ArgumentParser(description="W 轴 Homing 功能测试")
    parser.add_argument("-p", "--port", help="串口端口（如 /dev/ttyACM0）", default=None)
    parser.add_argument(
        "-l", "--list", action="store_true", help="列出可用串口"
    )
    args = parser.parse_args()

    if args.list:
        print("可用串口:")
        ports = serial.tools.list_ports.comports()
        for port in ports:
            print(f"  {port.device}: {port.description}")
        return 0

    test = WAxisHomingTest()
    success = test.run_test(port=args.port)

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
