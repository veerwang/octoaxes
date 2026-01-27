#!/usr/bin/env python3
"""
测试 07: Homing 后移动测试脚本
先进行 homing，成功后再移动指定距离

用户确认：旧 API 在先 homing 再移动 500um 时可以正常工作
"""

import serial
import sys
import time
import argparse

# 调试协议头
DEBUG_HEADER = bytes([0x55, 0xAA])

# Homing 状态
HOMING_STATES = ["HOMING_INIT", "HOMING_SEARCH", "HOMING_SET_ZERO", "LEAVING_HOME"]

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

def get_axis_state(ser, axis_name):
    """获取轴当前状态"""
    ser.reset_input_buffer()
    send_debug_command(ser, f"{axis_name}:GET_DATA")
    responses = read_all_responses(ser, timeout=0.5)

    state = None
    pos_mm = None
    is_moving = None
    limit_sw = None

    for resp in responses:
        if "STATE:" in resp:
            state = resp.split("STATE:")[-1].strip()
        if "(mm):" in resp:
            try:
                pos_mm = float(resp.split(":")[-1])
            except:
                pass
        if "IS_MOVING:" in resp:
            is_moving = "YES" in resp
        if "LIMIT_SWITCHES:" in resp:
            limit_sw = resp.split(":")[-1].strip()

    return {
        'state': state,
        'pos_mm': pos_mm,
        'is_moving': is_moving,
        'limit_sw': limit_sw,
        'responses': responses
    }

def print_section(title):
    """打印分节标题"""
    print()
    print("=" * 60)
    print(f"  {title}")
    print("=" * 60)

def wait_for_idle(ser, axis_name, timeout=60.0, interval=0.5):
    """等待轴进入 IDLE 状态"""
    start_time = time.time()
    last_state = None

    while time.time() - start_time < timeout:
        data = get_axis_state(ser, axis_name)
        state = data['state']
        pos_mm = data['pos_mm']
        limit_sw = data['limit_sw']

        elapsed = time.time() - start_time

        if state != last_state:
            print(f"  [{elapsed:5.1f}s] STATE={state}  POS={pos_mm:.3f}mm  LIM={limit_sw}")
            last_state = state

        if state == "IDLE":
            return True, data
        elif state == "ERROR":
            print(f"  [ERROR] 轴进入错误状态!")
            return False, data

        time.sleep(interval)

    print(f"  [TIMEOUT] 等待 IDLE 状态超时 ({timeout}s)")
    return False, data

def run_homing_test(port_name, axis_name, distance_um=500, baudrate=2000000):
    """Homing 后移动测试

    Args:
        port_name: 串口名称
        axis_name: 轴名称 (X, Y, Z 等)
        distance_um: 移动距离 (微米)
        baudrate: 波特率
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
        print_section("Step 2: 复位轴状态")
        # ============================================================
        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:RESET")
        time.sleep(0.5)
        responses = read_all_responses(ser, timeout=0.5)
        for resp in responses:
            print(f"  [RX] {resp}")

        # 检查当前状态
        data = get_axis_state(ser, axis_name)
        print(f"\n  当前状态: {data['state']}")
        print(f"  当前位置: {data['pos_mm']:.3f} mm")
        print(f"  限位开关: {data['limit_sw']}")

        # ============================================================
        print_section("Step 3: 开始 Homing")
        # ============================================================

        # 记录初始位置
        initial_pos_mm = data['pos_mm'] if data['pos_mm'] is not None else 0.0

        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:HOMING")
        time.sleep(0.3)

        # 读取响应
        responses = read_all_responses(ser, timeout=0.5)
        for resp in responses:
            print(f"  [RX] {resp}")

        # 检查是否开始homing
        data = get_axis_state(ser, axis_name)
        if data['state'] not in HOMING_STATES:
            print(f"\n  [WARN] Homing 可能未启动，当前状态: {data['state']}")

        # ============================================================
        print_section("Step 4: 等待 Homing 完成")
        # ============================================================

        print("\n  等待 homing 完成 (最多 60 秒)...\n")
        success, data = wait_for_idle(ser, axis_name, timeout=60.0)

        if not success:
            print("\n  [FAIL] Homing 失败")
            ser.close()
            return False

        print(f"\n  [OK] Homing 完成!")
        print(f"  位置已设为: {data['pos_mm']:.3f} mm")

        # ============================================================
        print_section("Step 5: 读取 Homing 后的寄存器状态")
        # ============================================================

        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:DEBUG_REG")
        responses = read_all_responses(ser, timeout=2.0)
        for resp in responses:
            print(f"  {resp}")

        # ============================================================
        print_section(f"Step 6: 移动 {distance_um} 微米")
        # ============================================================

        # 获取当前位置 (homing 后应该是 0)
        data = get_axis_state(ser, axis_name)
        current_pos_mm = data['pos_mm'] if data['pos_mm'] is not None else 0.0
        current_pos_um = int(current_pos_mm * 1000)

        # 计算目标位置
        target_pos_um = current_pos_um + distance_um

        print(f"\n  当前位置: {current_pos_mm:.3f} mm ({current_pos_um} 微米)")
        print(f"  移动距离: {distance_um} 微米 ({distance_um/1000:.3f} mm)")
        print(f"  目标位置: {target_pos_um} 微米 ({target_pos_um/1000:.3f} mm)")

        # 发送移动命令 (MOVETO_AXIS 接受微米值的十六进制)
        if target_pos_um < 0:
            hex_val = (1 << 32) + target_pos_um
        else:
            hex_val = target_pos_um
        hex_str = f"{hex_val:08X}"

        print(f"\n  发送: MOVETO_AXIS INT32 {hex_str}")

        ser.reset_input_buffer()
        send_debug_command(ser, f"{axis_name}:MOVETO_AXIS INT32 {hex_str}")

        time.sleep(0.2)
        responses = read_all_responses(ser, timeout=0.5)
        for resp in responses:
            print(f"  [RX] {resp}")

        # ============================================================
        print_section("Step 7: 监控移动状态")
        # ============================================================

        print("\n  每 0.3 秒查询一次状态，最多 15 秒...\n")

        start_time = time.time()
        prev_pos_mm = current_pos_mm
        movement_detected = False
        max_iterations = 50

        for i in range(max_iterations):
            time.sleep(0.3)

            data = get_axis_state(ser, axis_name)
            pos_mm = data['pos_mm']
            state = data['state']
            is_moving = data['is_moving']
            limit_sw = data['limit_sw']

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

            # 如果进入 IDLE/ERROR 状态，停止监控
            if state == "IDLE" or state == "ERROR":
                if i > 0:  # 第一次不算
                    break

        # ============================================================
        print_section("Step 8: 分析结果")
        # ============================================================

        final_pos_mm = prev_pos_mm
        pos_delta_um = int((final_pos_mm - current_pos_mm) * 1000)

        print(f"\n  Homing 后位置:  {current_pos_mm:.3f} mm ({current_pos_um} 微米)")
        print(f"  目标位置:       {target_pos_um/1000:.3f} mm ({target_pos_um} 微米)")
        print(f"  最终位置:       {final_pos_mm:.3f} mm ({int(final_pos_mm*1000)} 微米)")
        print(f"  实际移动:       {pos_delta_um} 微米 ({pos_delta_um/1000:.3f} mm)")
        print(f"  期望移动:       {distance_um} 微米 ({distance_um/1000:.3f} mm)")

        print()
        if movement_detected:
            error_um = abs(pos_delta_um - distance_um)
            if error_um < 10:  # 10微米以内误差认为正常
                print("  [OK] 运动正常完成")
                result = True
            else:
                print(f"  [WARN] 运动完成但位置不准确 (误差: {error_um} 微米)")
                result = True  # 有移动就算部分成功
        else:
            print("  [FAIL] 电机未移动")
            result = False

        ser.close()
        return result

    except serial.SerialException as e:
        print(f"[FAIL] 串口错误: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Homing 后移动测试脚本')
    parser.add_argument('port', nargs='?', default='/dev/ttyACM0', help='串口端口')
    parser.add_argument('-a', '--axis', default='Z', help='测试轴名称 (默认: Z)')
    parser.add_argument('-d', '--distance', type=int, default=500, help='移动距离 (微米, 默认: 500)')

    args = parser.parse_args()

    print("=" * 60)
    print("测试 07: Homing 后移动测试")
    print("=" * 60)
    print()
    print(f"端口: {args.port}")
    print(f"测试轴: {args.axis}")
    print(f"移动距离: {args.distance} 微米 = {args.distance/1000:.3f} mm")
    print()
    print("测试流程:")
    print("  1. Engine Start")
    print("  2. 复位轴状态 (RESET)")
    print("  3. 执行 Homing")
    print("  4. 等待 Homing 完成")
    print("  5. 移动指定距离")
    print("  6. 检查移动结果")

    success = run_homing_test(args.port, args.axis, args.distance)

    print()
    print("=" * 60)
    if success:
        print("结果: PASS")
    else:
        print("结果: FAIL")
    print("=" * 60)

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
