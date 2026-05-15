#!/usr/bin/env python3
"""
运行所有测试脚本
"""

import subprocess
import sys
import os

def run_test(script_name, args=None):
    """运行单个测试脚本"""
    script_path = os.path.join(os.path.dirname(__file__), script_name)
    cmd = [sys.executable, script_path]
    if args:
        cmd.extend(args)

    print(f"\n{'=' * 60}")
    print(f"运行: {script_name}")
    print('=' * 60)

    result = subprocess.run(cmd)
    return result.returncode == 0

def main():
    print("=" * 60)
    print("Octoaxes 硬件测试套件")
    print("=" * 60)

    # 获取串口参数
    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = '/dev/ttyACM0'
        print(f"使用默认端口: {port}")
        print("用法: python run_all_tests.py <port>")

    results = {}

    # 测试 1: 串口连接
    results['01_connection'] = run_test('test_01_serial_connection.py')
    if not results['01_connection']:
        print("\n[STOP] 串口连接失败，无法继续测试")
        return 1

    # 测试 2: 版本查询
    results['02_version'] = run_test('test_02_version_query.py', [port])
    if not results['02_version']:
        print("\n[STOP] 固件通信失败，无法继续测试")
        return 1

    # 测试 3: Engine Start
    results['03_engine_start'] = run_test('test_03_engine_start.py', [port])

    # 测试 4: TMC 状态
    results['04_tmc_status'] = run_test('test_04_tmc_status.py', [port])

    # 测试 5: 轴运动 (可选，需要确认安全)
    print("\n" + "-" * 60)
    response = input("是否进行轴运动测试? (需要确保机械安全) [y/N]: ")
    if response.lower() == 'y':
        results['05_axis_move'] = run_test('test_05_axis_move.py', [port, '-a', 'X', '-d', '1000'])
    else:
        results['05_axis_move'] = None
        print("跳过轴运动测试")

    # 汇总结果
    print("\n" + "=" * 60)
    print("测试结果汇总")
    print("=" * 60)

    for name, result in results.items():
        if result is None:
            status = "SKIP"
        elif result:
            status = "PASS"
        else:
            status = "FAIL"
        print(f"  {name}: {status}")

    # 检查是否有失败
    failed = [k for k, v in results.items() if v is False]
    if failed:
        print(f"\n失败的测试: {', '.join(failed)}")
        return 1
    else:
        print("\n所有测试通过!")
        return 0

if __name__ == "__main__":
    sys.exit(main())
