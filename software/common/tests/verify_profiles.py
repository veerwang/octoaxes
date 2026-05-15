"""验证两 profile（octoaxes / octoaxesplus）的 import + AXIS_CONFIG 一致性。

修改 software/common/ 任意文件后跑一遍：
    python3 software/common/tests/verify_profiles.py

输出 OK + 退出码 0 表示两 profile 都能正常加载且 AXIS_CONFIG / 命令映射齐全。
任一 profile 出问题 → 退出码 1，定位是哪条 common/ 改动破坏了 profile 隔离。
"""
import importlib
import os
import sys


# verify_profiles.py 位于 software/common/tests/，往上 3 层得到 software/
SOFTWARE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
COMMON = os.path.join(SOFTWARE_DIR, "common")


def check_profile(profile: str, expected_axes: set) -> bool:
    """加载 profile 模拟 main.py 的 sys.path/桥接，检查 AXIS_CONFIG 等。"""
    profile_dir = os.path.join(SOFTWARE_DIR, profile)
    print(f"\n--- {profile} ---")

    # 清掉之前的 cached import 让两 profile 独立加载
    for mod in list(sys.modules):
        if mod in ("constants", "utils", "utils.constants", "utils.helpers",
                   "define", "hardware.axis_manager"):
            del sys.modules[mod]

    # 模拟 profile main.py 的 sys.path 桥接
    for p in (profile_dir, COMMON):
        if p in sys.path:
            sys.path.remove(p)
    sys.path.insert(0, COMMON)
    sys.path.insert(0, profile_dir)

    import constants
    import utils
    sys.modules["utils.constants"] = constants
    utils.constants = constants

    from utils.constants import AXIS_CONFIG
    from define import AXIS_MOVE_CMD_MAP, AXIS_MOVETO_CMD_MAP
    from hardware.axis_manager import AxisManager

    ok = True

    # AXIS_CONFIG 检查
    actual = set(AXIS_CONFIG.keys())
    if actual != expected_axes:
        print(f"  ❌ AXIS_CONFIG keys 不匹配: 实际={sorted(actual)}, 期望={sorted(expected_axes)}")
        ok = False
    else:
        print(f"  ✓ AXIS_CONFIG keys = {sorted(actual)}")

    # AxisManager 初始化检查
    am = AxisManager()
    am_keys = set(am.axis_status.keys())
    if am_keys != expected_axes:
        print(f"  ❌ AxisManager keys 不匹配: 实际={sorted(am_keys)}")
        ok = False
    else:
        print(f"  ✓ AxisManager init keys 与 AXIS_CONFIG 一致")

    # 命令映射齐全检查
    missing_move = [a for a in actual if a not in AXIS_MOVE_CMD_MAP]
    missing_moveto = [a for a in actual if a not in AXIS_MOVETO_CMD_MAP]
    if missing_move:
        print(f"  ❌ AXIS_MOVE_CMD_MAP 缺失: {missing_move}")
        ok = False
    else:
        print(f"  ✓ AXIS_MOVE_CMD_MAP 覆盖全部 {len(actual)} 轴")
    if missing_moveto:
        print(f"  ❌ AXIS_MOVETO_CMD_MAP 缺失: {missing_moveto}")
        ok = False
    else:
        print(f"  ✓ AXIS_MOVETO_CMD_MAP 覆盖全部 {len(actual)} 轴")

    return ok


def main():
    print("=== Profile 隔离 + 完整性验证 ===")
    print(f"SOFTWARE_DIR: {SOFTWARE_DIR}")

    octoaxes_ok = check_profile("octoaxes", {"X", "Y", "Z", "W", "E1", "E3", "E4"})
    octoaxesplus_ok = check_profile("octoaxesplus", {"X", "Y", "Z", "W1", "W2"})

    print()
    if octoaxes_ok and octoaxesplus_ok:
        print("✅ 两 profile 全部通过验证")
        sys.exit(0)
    else:
        print("❌ 至少一个 profile 验证失败 — 检查 common/ 改动")
        sys.exit(1)


if __name__ == "__main__":
    main()
