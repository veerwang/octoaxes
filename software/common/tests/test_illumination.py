#!/usr/bin/env python3
"""
照明系统测试脚本

测试所有照明命令：
  - 旧版 API：TURN_ON/OFF, SET_ILLUMINATION, LED_MATRIX, INTENSITY_FACTOR, DAC_GAIN
  - 新版多端口 API：SET_PORT_INTENSITY, TURN_ON/OFF_PORT, SET_PORT_ILLUMINATION,
                    SET_MULTI_PORT_MASK, TURN_OFF_ALL_PORTS

用法：
  python test_illumination.py [PORT]
  默认 PORT = /dev/ttyACM0
"""

import serial
import time
import sys
import struct

# ─────────────────────────────────────────────
# 配置
# ─────────────────────────────────────────────
PORT     = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUDRATE = 115200

# ─────────────────────────────────────────────
# 命令码（与 config.h 保持一致）
# ─────────────────────────────────────────────
CMD_TURN_ON_ILLUMINATION         = 10
CMD_TURN_OFF_ILLUMINATION        = 11
CMD_SET_ILLUMINATION             = 12
CMD_SET_ILLUMINATION_LED_MATRIX  = 13
CMD_SET_DAC80508_REFDIV_GAIN     = 16
CMD_SET_ILLUMINATION_INTENSITY_FACTOR = 17
CMD_SET_PORT_INTENSITY           = 34
CMD_TURN_ON_PORT                 = 35
CMD_TURN_OFF_PORT                = 36
CMD_SET_PORT_ILLUMINATION        = 37
CMD_SET_MULTI_PORT_MASK          = 38
CMD_TURN_OFF_ALL_PORTS           = 39

# 光源码（旧版 API 用）
SRC_LED_FULL    = 0    # LED 矩阵全亮
SRC_LED_LEFT    = 1
SRC_LED_RIGHT   = 2
SRC_LED_LB_RR   = 3    # 左蓝右红
SRC_LED_LOW_NA  = 4
SRC_LED_LEFT_DOT  = 5
SRC_LED_RIGHT_DOT = 6
SRC_LED_TOP     = 7
SRC_LED_BOTTOM  = 8
SRC_D1 = 11   # TTL 端口 D1（pin 5）
SRC_D2 = 12   # TTL 端口 D2（pin 4）
SRC_D3 = 14   # TTL 端口 D3（pin 22）注意非连续
SRC_D4 = 13   # TTL 端口 D4（pin 3） 注意非连续
SRC_D5 = 15   # TTL 端口 D5（pin 23）

# ─────────────────────────────────────────────
# CRC8-CCITT（与固件 serial.cpp 一致）
# ─────────────────────────────────────────────
CRC_TABLE = [
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,
    0x24,0x23,0x2A,0x2D,0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,
    0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,0xE0,0xE7,0xEE,0xE9,
    0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,
    0xB4,0xB3,0xBA,0xBD,0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,
    0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,0xB7,0xB0,0xB9,0xBE,
    0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,
    0x03,0x04,0x0D,0x0A,0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,
    0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,0x89,0x8E,0x87,0x80,
    0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,
    0xDD,0xDA,0xD3,0xD4,0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,
    0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,0x19,0x1E,0x17,0x10,
    0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,
    0x6A,0x6D,0x64,0x63,0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,
    0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,0xAE,0xA9,0xA0,0xA7,
    0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,
    0xFA,0xFD,0xF4,0xF3,
]

def crc8(data: bytes) -> int:
    val = 0
    for b in data:
        val = CRC_TABLE[val ^ b]
    return val

# ─────────────────────────────────────────────
# 协议辅助
# ─────────────────────────────────────────────
_seq = 0

def make_packet(cmd: int, b2=0, b3=0, b4=0, b5=0, b6=0) -> bytes:
    """构建 8 字节标准命令包（自动附加 CRC）"""
    global _seq
    _seq = (_seq + 1) & 0xFF
    payload = bytes([_seq, cmd, b2, b3, b4, b5, b6])
    return payload + bytes([crc8(payload)])

def engine_start(ser: serial.Serial):
    """发送 Engine Start（调试协议）"""
    pkt = b'\x55\xAA' + b'S:Engine Start\n'
    ser.write(pkt)
    time.sleep(3)
    drain(ser)

def send(ser: serial.Serial, pkt: bytes, delay=0.1, label=""):
    """发送二进制命令包并打印"""
    hex_str = " ".join(f"{b:02X}" for b in pkt)
    print(f"  TX [{label}]: {hex_str}")
    ser.write(pkt)
    time.sleep(delay)
    drain(ser)

def drain(ser: serial.Serial):
    """读取并打印所有待接收数据"""
    while ser.in_waiting:
        try:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                print(f"  RX: {line}")
        except Exception:
            ser.read(ser.in_waiting)
            break

def pause(msg="按 Enter 继续，'q' 跳过本步骤: "):
    resp = input(f"  >>> {msg}")
    return resp.strip().lower() != 'q'

# ─────────────────────────────────────────────
# 测试步骤
# ─────────────────────────────────────────────

def test_legacy_api(ser):
    """旧版单光源 API 测试（命令 10-17）"""
    print("\n" + "=" * 55)
    print("  旧版照明 API（命令 10-17）")
    print("=" * 55)

    # --- SET_ILLUMINATION_INTENSITY_FACTOR ---
    print("\n[A] SET_ILLUMINATION_INTENSITY_FACTOR — 设置强度缩放因子为 60%")
    pkt = make_packet(CMD_SET_ILLUMINATION_INTENSITY_FACTOR, 60)  # 60/100 = 0.6
    send(ser, pkt, label="CMD=17 factor=60%")
    print("  预期：illumination_intensity_factor = 0.6（固件内部状态）")
    pause("观察调试输出后按 Enter，")

    # --- SET_DAC80508_REFDIV_GAIN ---
    print("\n[B] SET_DAC80508_REFDIV_GAIN — 设置默认增益")
    pkt = make_packet(CMD_SET_DAC80508_REFDIV_GAIN, 0x00, 0x80)
    send(ser, pkt, label="CMD=16 div=0 gains=0x80")
    print("  预期：DAC 增益寄存器写入成功（用示波器/万用表验证 Vref）")
    pause()

    # --- TURN_ON_ILLUMINATION (D1, TTL) ---
    print("\n[C1] TURN_ON_ILLUMINATION — 光源 D1（source=11, pin=5）")
    pkt = make_packet(CMD_SET_ILLUMINATION, SRC_D1, 0xFF, 0xFF)   # 先设强度最大
    send(ser, pkt, label="CMD=12 src=D1 intensity=65535")
    pkt = make_packet(CMD_TURN_ON_ILLUMINATION, SRC_D1)
    send(ser, pkt, label="CMD=10 src=D1")
    print("  预期：pin5（D1）变 HIGH，DAC ch0 输出约 Vref*0.6")
    if not pause(): pass

    pkt = make_packet(CMD_TURN_OFF_ILLUMINATION)
    send(ser, pkt, label="CMD=11 (off)")
    print("  预期：pin5 变 LOW")
    pause()

    # --- TURN_ON_ILLUMINATION (D3, non-sequential) ---
    print("\n[C2] TURN_ON_ILLUMINATION — 光源 D3（source=14, pin=22，非连续码）")
    pkt = make_packet(CMD_SET_ILLUMINATION, SRC_D3, 0x80, 0x00)   # 强度 0x8000 = 32768
    send(ser, pkt, label="CMD=12 src=D3 intensity=32768")
    pkt = make_packet(CMD_TURN_ON_ILLUMINATION, SRC_D3)
    send(ser, pkt, label="CMD=10 src=D3")
    print("  预期：pin22（D3）变 HIGH，DAC ch2 输出约 Vref*0.6*(32768/65535)≈0.3Vref")
    pause()
    send(ser, make_packet(CMD_TURN_OFF_ILLUMINATION), label="CMD=11 (off)")
    pause()

    # --- SET_ILLUMINATION_LED_MATRIX (full pattern) ---
    print("\n[D] SET_ILLUMINATION_LED_MATRIX — LED 矩阵全亮（白光）")
    pkt = make_packet(CMD_SET_ILLUMINATION_LED_MATRIX, SRC_LED_FULL, 255, 255, 255)
    send(ser, pkt, label="CMD=13 src=0(FULL) R=255 G=255 B=255")
    print("  预期：LED 矩阵 128 颗全部白光点亮")
    pause()

    print("\n[D2] SET_ILLUMINATION_LED_MATRIX — 左半蓝右半红图案")
    pkt = make_packet(CMD_SET_ILLUMINATION_LED_MATRIX, SRC_LED_LB_RR, 128, 0, 128)
    send(ser, pkt, label="CMD=13 src=3(LB_RR) r=128 g=0 b=128")
    print("  预期：左半蓝色，右半红色")
    pause()

    print("\n[D3] 关闭 LED 矩阵（source=0, intensity=0）")
    pkt = make_packet(CMD_SET_ILLUMINATION, SRC_LED_FULL, 0, 0)
    send(ser, pkt, label="CMD=12 src=0 intensity=0")
    pkt = make_packet(CMD_TURN_ON_ILLUMINATION, SRC_LED_FULL)
    send(ser, pkt, label="CMD=10 src=0")
    time.sleep(0.2)
    send(ser, make_packet(CMD_TURN_OFF_ILLUMINATION), label="CMD=11 (off)")
    print("  预期：LED 矩阵熄灭")
    pause()


def test_multiport_api(ser):
    """新版多端口 API 测试（命令 34-39）"""
    print("\n" + "=" * 55)
    print("  新版多端口 API（命令 34-39）")
    print("=" * 55)

    # --- TURN_OFF_ALL_PORTS ---
    print("\n[E] TURN_OFF_ALL_PORTS — 关闭所有端口")
    send(ser, make_packet(CMD_TURN_OFF_ALL_PORTS), label="CMD=39")
    print("  预期：D1-D5 全部 LOW，LED 矩阵熄灭，DAC 输出 0")
    pause()

    # --- SET_PORT_INTENSITY + TURN_ON_PORT ---
    print("\n[F] SET_PORT_INTENSITY + TURN_ON_PORT — 端口 0（D1）半亮")
    intensity = 32768   # 0x8000
    hi = (intensity >> 8) & 0xFF
    lo = intensity & 0xFF
    send(ser, make_packet(CMD_SET_PORT_INTENSITY, 0, hi, lo), label="CMD=34 port=0 intensity=32768")
    send(ser, make_packet(CMD_TURN_ON_PORT, 0), label="CMD=35 port=0")
    print("  预期：pin5（D1）HIGH，DAC ch0 ≈ 0.3Vref（强度因子0.6 × 0.5）")
    pause()
    send(ser, make_packet(CMD_TURN_OFF_PORT, 0), label="CMD=36 port=0")
    print("  预期：pin5 LOW")
    pause()

    # --- SET_PORT_ILLUMINATION (原子操作) ---
    print("\n[G] SET_PORT_ILLUMINATION — 端口 2（D3）原子设置强度+开启")
    intensity = 65535   # 最大
    hi = (intensity >> 8) & 0xFF
    lo = intensity & 0xFF
    pkt = make_packet(CMD_SET_PORT_ILLUMINATION, 2, hi, lo, 1)  # data[5]=1 → turn on
    send(ser, pkt, label="CMD=37 port=2 intensity=65535 on=1")
    print("  预期：pin22（D3）HIGH，DAC ch2 输出满幅×0.6")
    pause()
    pkt = make_packet(CMD_SET_PORT_ILLUMINATION, 2, 0, 0, 0)   # data[5]=0 → turn off
    send(ser, pkt, label="CMD=37 port=2 intensity=0 on=0")
    print("  预期：pin22 LOW，DAC ch2 = 0")
    pause()

    # --- SET_MULTI_PORT_MASK — 同时开 D1 和 D2 ---
    print("\n[H] SET_MULTI_PORT_MASK — 同时开启 D1(port0) 和 D2(port1)")
    # port_mask = 0b11 = 0x0003（选中 port0, port1）
    # on_mask   = 0b11 = 0x0003（全部置 ON）
    pkt = make_packet(CMD_SET_MULTI_PORT_MASK, 0x00, 0x03, 0x00, 0x03)
    send(ser, pkt, label="CMD=38 port_mask=0x0003 on_mask=0x0003")
    print("  预期：pin5（D1）和 pin4（D2）同时变 HIGH")
    pause()

    print("\n[H2] SET_MULTI_PORT_MASK — 关闭 D1，保持 D2")
    # port_mask = 0b11（选中 0,1），on_mask = 0b10（只开 port1）
    pkt = make_packet(CMD_SET_MULTI_PORT_MASK, 0x00, 0x03, 0x00, 0x02)
    send(ser, pkt, label="CMD=38 port_mask=0x0003 on_mask=0x0002")
    print("  预期：pin5（D1）LOW，pin4（D2）仍 HIGH")
    pause()

    # --- TURN_OFF_ALL_PORTS 收尾 ---
    print("\n[I] TURN_OFF_ALL_PORTS — 全部关闭")
    send(ser, make_packet(CMD_TURN_OFF_ALL_PORTS), label="CMD=39")
    print("  预期：所有端口 LOW，LED 矩阵熄灭")
    pause()


def test_interlock(ser):
    """联锁功能说明（无法自动测试，需手动操作）"""
    print("\n" + "=" * 55)
    print("  联锁检查（手动验证）")
    print("=" * 55)
    print()
    print("  联锁引脚：pin2（INPUT_PULLUP）")
    print("  LOW（接地）= 安全 → 允许开灯")
    print("  HIGH（悬空/断开）= 危险 → loop() 强制调用 turn_off_all_ports()")
    print()
    print("  手动测试方法：")
    print("    1. 先开启某个端口（例如上面步骤 F）")
    print("    2. 拔开 pin2 接地（使其悬空，变 HIGH）")
    print("    3. 观察对应 TTL 输出是否在下一个 loop() 周期变 LOW")
    print("    4. 重新将 pin2 接地，再发命令开灯确认恢复正常")
    print()
    print("  如用 -DDISABLE_LASER_INTERLOCK 编译，interlock 检查被跳过（无激光系统）")
    input("  >>> 了解后按 Enter 继续...")


# ─────────────────────────────────────────────
# 主程序
# ─────────────────────────────────────────────
def main():
    print("=" * 55)
    print("  Octoaxes 照明系统测试")
    print(f"  端口: {PORT}  波特率: {BAUDRATE}")
    print("=" * 55)

    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()

        # Engine Start
        print("\n[0] 发送 Engine Start（等待 3 秒系统初始化）...")
        engine_start(ser)
        print("    Engine Start 完成，开始照明测试")

        test_legacy_api(ser)
        test_multiport_api(ser)
        test_interlock(ser)

        print("\n" + "=" * 55)
        print("  所有测试步骤完成")
        print("=" * 55)

    except serial.SerialException as e:
        print(f"\n串口错误: {e}")
        print(f"请检查 {PORT} 是否正确，或尝试：python test_illumination.py /dev/ttyACM1")
    except KeyboardInterrupt:
        print("\n\n用户中断测试")
    finally:
        if 'ser' in locals() and ser.is_open:
            # 安全关闭所有照明
            ser.write(make_packet(CMD_TURN_OFF_ALL_PORTS))
            time.sleep(0.1)
            ser.close()
            print("串口已关闭，所有照明已关闭")


if __name__ == "__main__":
    main()
