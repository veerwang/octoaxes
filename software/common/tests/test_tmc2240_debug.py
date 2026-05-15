#!/usr/bin/env python3
"""
TMC2240 调试脚本

1. 连接串口，收集启动阶段 TMC2240 DEBUG 输出
2. 发送 W 轴移动命令，观察是否有响应
3. 读取 W 轴状态
"""

import sys
import time
import struct
import serial
import serial.tools.list_ports

BAUDRATE = 115200
TIMEOUT = 0.1
HEADER = b"\x55\xaa"


def find_teensy_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if port.vid == 0x16C0 and port.pid == 0x0483:
            return port.device
        if "teensy" in port.description.lower() or "usb serial" in port.description.lower():
            return port.device
    # fallback
    for p in ["/dev/ttyACM0", "/dev/ttyACM1"]:
        try:
            s = serial.Serial(p, timeout=0.1)
            s.close()
            return p
        except:
            pass
    return None


def send_cmd(ser, cmd):
    data = HEADER + (cmd + "\n").encode("utf-8")
    ser.write(data)
    print(f"  >>> {cmd}")


def read_all(ser, timeout=2.0):
    """读取所有串口输出"""
    start = time.time()
    buf = ""
    lines = []
    while time.time() - start < timeout:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buf += data.decode("utf-8", errors="replace")
            while "\n" in buf:
                idx = buf.find("\n")
                line = buf[:idx].strip()
                buf = buf[idx + 1:]
                if line:
                    lines.append(line)
                    print(f"  <<< {line}")
        time.sleep(0.01)
    return lines


def int_to_hex(value):
    return struct.pack("!i", value).hex()


def main():
    print("=" * 60)
    print("  TMC2240 调试脚本")
    print("=" * 60)

    port = find_teensy_port()
    if not port:
        print("[ERROR] 未找到 Teensy")
        return 1

    print(f"[INFO] 连接 {port}")
    ser = serial.Serial(port, BAUDRATE, timeout=TIMEOUT, write_timeout=0.5)
    time.sleep(0.3)
    ser.reset_input_buffer()

    try:
        # ============================================================
        # 1. 发送 Engine Start，收集启动输出（含 TMC2240 DEBUG）
        # ============================================================
        print("\n" + "=" * 60)
        print("  [1] Engine Start - 收集 TMC2240 初始化调试输出")
        print("=" * 60)
        send_cmd(ser, "S:Engine Start")
        lines = read_all(ser, timeout=5.0)

        # 检查是否有 TMC2240 DEBUG 输出
        debug_lines = [l for l in lines if "TMC2240" in l or "GCONF" in l
                       or "CHOPCONF" in l or "DRVSTATUS" in l or "IOIN" in l
                       or "SPI_OUT_CONF" in l or "SCALE_VALUES" in l
                       or "CURRENT_CONF" in l or "GSTAT" in l]
        if debug_lines:
            print("\n  --- TMC2240 调试信息汇总 ---")
            for l in debug_lines:
                print(f"  {l}")
        else:
            print("\n  [WARN] 未捕获到 TMC2240 DEBUG 输出")
            print("  可能原因: W 轴不是 TMC2240 类型, 或 debug 打印被禁用")

        # ============================================================
        # 2. 查询 W 轴当前位置
        # ============================================================
        print("\n" + "=" * 60)
        print("  [2] 查询 W 轴状态")
        print("=" * 60)
        time.sleep(0.5)
        send_cmd(ser, "W:GET_DATA")
        read_all(ser, timeout=1.0)

        # ============================================================
        # 3. 发送 W 轴相对移动 (MOVE +10000 微步)
        # ============================================================
        print("\n" + "=" * 60)
        print("  [3] W 轴相对移动 +10000 微步")
        print("=" * 60)
        usteps = 10000
        hex_val = int_to_hex(usteps)
        send_cmd(ser, f"W:MOVE_AXIS int {hex_val}")
        lines = read_all(ser, timeout=3.0)

        # ============================================================
        # 4. 等待并检查位置变化
        # ============================================================
        print("\n" + "=" * 60)
        print("  [4] 检查移动结果")
        print("=" * 60)
        time.sleep(1.0)
        send_cmd(ser, "W:GET_DATA")
        read_all(ser, timeout=1.0)

        # ============================================================
        # 5. 反向移动回去
        # ============================================================
        print("\n" + "=" * 60)
        print("  [5] W 轴相对移动 -10000 微步（回位）")
        print("=" * 60)
        hex_val = int_to_hex(-usteps)
        send_cmd(ser, f"W:MOVE_AXIS int {hex_val}")
        read_all(ser, timeout=3.0)

        time.sleep(1.0)
        send_cmd(ser, "W:GET_DATA")
        read_all(ser, timeout=1.0)

        print("\n" + "=" * 60)
        print("  调试完成")
        print("=" * 60)

    finally:
        ser.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
