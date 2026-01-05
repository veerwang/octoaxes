"""
辅助函数
"""
import serial.tools.list_ports
import struct
from hardware.serial_thread import SerialThread


def find_teensy_port():
    """查找Teensy设备端口"""
    try:
        ports = serial.tools.list_ports.comports()
        for port in ports:
            if "USB Serial" in port.description or "Teensy" in port.description:
                return port.device
    except Exception as e:
        print(f"Error finding Teensy port: {e}")
    return None

def format_command(axis, command):
    """格式化命令"""
    return f"{axis}:{command}"

def pack_move_command(distance_um):
    """打包移动命令"""
    packed_data = struct.pack('!i', distance_um)
    return f"MOVE_AXIS int {packed_data.hex()}"

def pack_moveto_command(distance_um):
    """打包移动命令"""
    packed_data = struct.pack('!i', distance_um)
    return f"MOVETO_AXIS int {packed_data.hex()}"

def pack_limit_command(low_limit, high_limit):
    """打包限位设置命令"""
    packed_low = struct.pack('!i', low_limit)
    packed_high = struct.pack('!i', high_limit)
    return f"SET_LIMITS int {packed_low.hex()} {packed_high.hex()}"

def int_to_payload(signed_int, number_of_bytes) -> int:
    actually_signed_int = int(round(signed_int))
    if actually_signed_int >= 0:
        payload = actually_signed_int
    else:
        payload = 2 ** (8 * number_of_bytes) + actually_signed_int  # find two's complement
    return int(payload)

def payload_to_int(payload, number_of_bytes) -> int:
    signed = 0
    for i in range(number_of_bytes):
        signed = signed + int(payload[i]) * (256 ** (number_of_bytes - 1 - i))
    if signed >= 256**number_of_bytes / 2:
        signed = signed - 256**number_of_bytes
    return int(signed)

