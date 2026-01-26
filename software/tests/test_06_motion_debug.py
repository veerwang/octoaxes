#!/usr/bin/env python3
"""
测试 06: 运动调试脚本
详细排查电机不动的原因，读取关键寄存器
"""

import serial
import sys
import time
import argparse

# 调试协议头
DEBUG_HEADER = bytes([0x55, 0xAA])

# TMC4361A 寄存器地址 (关键的)
TMC4361A_REGS = {
    0x00: "GENERAL_CONF",
    0x01: "REFERENCE_CONF",
    0x0E: "STATUS",
    0x0F: "EVENTS",
    0x20: "RAMPMODE",
    0x21: "XACTUAL",
    0x22: "VACTUAL",
    0x2D: "XTARGET",
    0x24: "VMAX",
    0x28: "AMAX",
    0x29: "DMAX",
    0x23: "AACTUAL",
    0x05: "SPI_OUT_CONF",
    0x1F: "CLK_FREQ",
}

# STATUS 寄存器位定义
STATUS_BITS = {
    0: "TARGET_REACHED",
    1: "POS_COMP_REACHED",
    2: "VEL_REACHED",
    3: "VEL_STATE_00",
    4: "VEL_STATE_01",
    5: "RAMP_STATE_00",
    6: "RAMP_STATE_01",
    7: "STOPL_ACTIVE",
    8: "STOPR_ACTIVE",
    9: "VSTOPL_ACTIVE",
    10: "VSTOPR_ACTIVE",
    11: "ACTIVE_STALL",
    12: "HOME_ERROR",
    13: "XLATCH_DONE",
    14: "FS_ACTIVE",
    15: "ENC_FAIL",
    16: "N_ACTIVE",
    17: "ENC_DONE",
    24: "SG",           # StallGuard status from TMC2660
    25: "OT",           # Over temperature
    26: "OTPW",         # Temperature pre-warning
    27: "S2GA",         # Short to ground A
    28: "S2GB",         # Short to ground B
    29: "OLA",          # Open load A
    30: "OLB",          # Open load B
    31: "STST",         # Standstill
}

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

def parse_status_register(value):
    """解析 STATUS 寄存器"""
    active_bits = []
    for bit, name in STATUS_BITS.items():
        if value & (1 << bit):
            active_bits.append(f"{name}(bit{bit})")
    return active_bits

def read_register(ser, axis_name, reg_addr):
    """读取单个寄存器 (通过 READ_REG 命令)"""
    ser.reset_input_buffer()
    send_debug_command(ser, f"{axis_name}:READ_REG {reg_addr:02X}")
    responses = read_all_responses(ser, timeout=0.5)

    for resp in responses:
        if "READ_REG" in resp and "=" in resp:
            try:
                # 格式: X:READ_REG 0x0E = 0x00000001
                val_str = resp.split("=")[-1].strip()
                return int(val_str, 16)
            except:
                pass
    return None

def get_axis_data(ser, axis_name):
    """获取轴基本数据"""
    ser.reset_input_buffer()
    send_debug_command(ser, f"{axis_name}:GET_DATA")
    return read_all_responses(ser, timeout=1.0)

def print_section(title):
    """打印分节标题"""
    print()
    print("=" * 60)
    print(f"  {title}")
    print("=" * 60)

def run_motion_debug(port_name, axis_name, distance_um=1000, baudrate=2000000):
    """运动调试主函数

    注意: MOVETO_AXIS 命令接受的是微米 (mm × 1000) 的十六进制表示
    """
    print(f"连接 {port_name}...")

    try:
        ser = serial.Serial(
            port=port_name,
            baudrate=baudrate,
            timeout=1
        )
        time.sleep(0.5)
        ser.reset_input_buffer()

        # ============================================================
        print_section("Step 1: Engine Start")
        # ============================================================
        send_debug_command(ser, "S:Engine Start")
        time.sleep(1.5)
        responses = read_all_responses(ser, timeout=1.0)
        for resp in responses:
            print(f"  [RX] {resp}")

        # ============================================================
        print_section("Step 1.5: 复位轴状态")
        # ============================================================
        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:RESET")
        time.sleep(0.5)
        responses = read_all_responses(ser, timeout=0.5)
        for resp in responses:
            print(f"  [RX] {resp}")

        # ============================================================
        print_section(f"Step 2: 检查轴 {axis_name} 当前状态")
        # ============================================================
        responses = get_axis_data(ser, axis_name)
        for resp in responses:
            print(f"  {resp}")

        # ============================================================
        print_section("Step 3: 读取关键寄存器")
        # ============================================================

        # 方法1: 尝试 READ_REG 命令
        print("\n尝试 READ_REG 命令...")
        status_val = read_register(ser, axis_name, 0x0E)  # STATUS

        if status_val is None:
            print("  [INFO] READ_REG 命令不可用，使用 GET_DATA 结果")
        else:
            print(f"  STATUS (0x0E) = 0x{status_val:08X}")
            active = parse_status_register(status_val)
            print(f"    活跃位: {', '.join(active) if active else '无'}")

        # 方法2: DEBUG_REG 命令 - 读取关键寄存器
        print("\n发送 DEBUG_REG 命令，读取关键 TMC4361A 寄存器...")
        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:DEBUG_REG")
        responses = read_all_responses(ser, timeout=2.0)

        vmax_val = None
        amax_val = None
        rampmode_val = None

        for resp in responses:
            print(f"  {resp}")
            # 解析关键值
            if "VMAX" in resp and "=" in resp:
                try:
                    vmax_val = int(resp.split("=")[-1].strip())
                except:
                    pass
            if "AMAX" in resp and "=" in resp:
                try:
                    amax_val = int(resp.split("=")[-1].strip())
                except:
                    pass
            if "RAMPMODE" in resp and "=" in resp:
                try:
                    rampmode_val = resp.split("=")[-1].strip()
                except:
                    pass

        # 检查关键参数
        print("\n关键参数检查:")
        if vmax_val is not None:
            if vmax_val == 0:
                print(f"  [WARN] VMAX = 0，电机无法移动！")
            else:
                print(f"  [OK] VMAX = {vmax_val}")
        if amax_val is not None:
            if amax_val == 0:
                print(f"  [WARN] AMAX = 0，电机无法加速！")
            else:
                print(f"  [OK] AMAX = {amax_val}")
        if rampmode_val is not None:
            print(f"  [INFO] RAMPMODE = {rampmode_val}")

        # ============================================================
        print_section("Step 4: 发送运动命令")
        # ============================================================

        # 获取当前位置
        print("\n获取当前位置...")
        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:GET_POSITION")
        responses = read_all_responses(ser, timeout=0.5)
        current_pos_mm = 0.0
        for resp in responses:
            print(f"  {resp}")
            if "(mm):" in resp:
                try:
                    current_pos_mm = float(resp.split(":")[-1])
                except:
                    pass

        # MOVETO_AXIS 命令接受绝对位置的微米值 (mm × 1000)
        current_pos_um = int(current_pos_mm * 1000)
        target_pos_um = current_pos_um + distance_um
        print(f"\n当前位置: {current_pos_mm:.3f} mm = {current_pos_um} 微米")
        print(f"移动距离: {distance_um} 微米 = {distance_um/1000:.3f} mm")
        print(f"目标位置: {target_pos_um} 微米 = {target_pos_um/1000:.3f} mm")

        # 发送 MOVETO_AXIS 命令 (绝对位置，单位：微米)
        print(f"\n发送 MOVETO_AXIS 命令...")

        # 转换为 hex 格式 (32位有符号整数，单位：微米)
        if target_pos_um < 0:
            hex_val = (1 << 32) + target_pos_um
        else:
            hex_val = target_pos_um
        hex_str = f"{hex_val:08X}"

        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:MOVETO_AXIS INT32 {hex_str}")

        # 读取立即响应
        time.sleep(0.2)
        responses = read_all_responses(ser, timeout=0.5)
        for resp in responses:
            print(f"  [RX] {resp}")

        # ============================================================
        print_section("Step 4.5: 运动后立即读取寄存器")
        # ============================================================
        time.sleep(0.1)
        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:DEBUG_REG")
        responses = read_all_responses(ser, timeout=2.0)
        for resp in responses:
            print(f"  {resp}")

        # ============================================================
        print_section("Step 5: 监控运动状态")
        # ============================================================

        print("\n每 0.5 秒查询一次状态，最多 10 秒...")
        print()

        start_time = time.time()
        prev_pos_mm = current_pos_mm
        movement_detected = False

        for i in range(20):  # 最多 20 次，每次 0.5 秒
            time.sleep(0.5)

            # 获取当前状态
            ser.reset_input_buffer()
            send_debug_command(ser, f"{axis_name}:GET_DATA")
            responses = read_all_responses(ser, timeout=0.5)

            # 解析响应
            pos_mm = None
            state = None
            is_moving = None
            limit_sw = None

            for resp in responses:
                if "STATE:" in resp:
                    state = resp.split("STATE:")[-1]
                if "(mm):" in resp:
                    try:
                        pos_mm = float(resp.split(":")[-1])
                    except:
                        pass
                if "IS_MOVING:" in resp:
                    is_moving = "YES" in resp
                if "LIMIT_SWITCHES:" in resp:
                    limit_sw = resp.split(":")[-1]

            elapsed = time.time() - start_time

            # 检测是否有移动
            if pos_mm is not None and abs(pos_mm - prev_pos_mm) > 0.0001:
                movement_detected = True

            # 打印状态
            pos_str = f"{pos_mm:.3f}" if pos_mm is not None else "?"
            delta_str = f"Δ={((pos_mm - prev_pos_mm)*1000):+.0f}um" if pos_mm is not None else ""
            print(f"  [{elapsed:5.1f}s] POS={pos_str:>10}mm {delta_str:>12}  STATE={state}  MOVING={is_moving}  LIM={limit_sw}")

            if pos_mm is not None:
                prev_pos_mm = pos_mm

            # 如果到达目标或进入 IDLE/ERROR 状态，停止监控
            if state == "IDLE" or state == "ERROR":
                if i > 0:  # 第一次不算
                    break

        # ============================================================
        print_section("Step 6: 分析结果")
        # ============================================================

        final_pos_mm = prev_pos_mm
        pos_delta_um = int((final_pos_mm - current_pos_mm) * 1000)

        print(f"\n初始位置:   {current_pos_mm:.3f} mm ({current_pos_um} 微米)")
        print(f"目标位置:   {target_pos_um/1000:.3f} mm ({target_pos_um} 微米)")
        print(f"最终位置:   {final_pos_mm:.3f} mm ({int(final_pos_mm*1000)} 微米)")
        print(f"实际移动:   {pos_delta_um} 微米 ({pos_delta_um/1000:.3f} mm)")
        print(f"期望移动:   {distance_um} 微米 ({distance_um/1000:.3f} mm)")

        print()
        if movement_detected:
            error_um = abs(pos_delta_um - distance_um)
            if error_um < 10:  # 10微米以内误差认为正常
                print("[OK] 运动正常完成")
            else:
                print(f"[WARN] 运动完成但位置不准确 (误差: {error_um} 微米)")
        else:
            print("[FAIL] 电机未移动")
            print()
            print("可能原因:")
            print("  1. VMAX/AMAX/DMAX 参数为 0 或太小")
            print("  2. RAMPMODE 未设置为 position 模式")
            print("  3. TMC2660 驱动器未正确初始化")
            print("  4. 电机电流设置过低")
            print("  5. 机械问题 (电机未连接、卡住等)")
            print("  6. STEP_CONF 未配置 (微步/每转步数)")
            print()
            print("建议:")
            print("  - 检查 STEP_CONF, SCALE_VALUES, CURRENT_CONF 配置")
            print("  - 检查 TMC2660 CHOPCONF/DRVCTRL 配置")
            print("  - 使用示波器检查 SPI 通信")

        ser.close()
        return movement_detected

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='运动调试脚本')
    parser.add_argument('port', nargs='?', default='/dev/ttyACM0', help='串口端口')
    parser.add_argument('-a', '--axis', default='Z', help='测试轴名称 (默认: Z)')
    parser.add_argument('-d', '--distance', type=int, default=1000, help='移动距离 (微米, 默认: 1000 = 1mm)')

    args = parser.parse_args()

    print("=" * 60)
    print("测试 06: 运动调试脚本")
    print("=" * 60)
    print()
    print(f"端口: {args.port}")
    print(f"测试轴: {args.axis}")
    print(f"移动距离: {args.distance} 微米 = {args.distance/1000:.3f} mm")

    success = run_motion_debug(args.port, args.axis, args.distance)

    print()
    print("=" * 60)
    if success:
        print("结果: PASS")
    else:
        print("结果: FAIL - 需要进一步调试")
    print("=" * 60)

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
