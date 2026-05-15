"""
常量定义
"""

# ============================================================================
# 轴配置
# ============================================================================
#
# 固件型号 vs 轴映射：
#   - octoaxes      (4 轴主线): X(1), Y(0), Z(2), W(3) + 历史 E1/E3/E4 占位
#   - octoaxesplus  (squid++ 双相机 8 轴): X(1), Y(0), Z1(2), F1(3), Z2(4),
#     F2(5), R(6), T(7)
#
# enabled_for 字段标注此条目适用于哪些固件型号。GUI 启动时根据 S:HWINFO
# 查询的固件型号筛选实际渲染的轴控件（Phase 3.2 待落地，当前所有条目都会
# 被 GUI 遍历显示）。
#
# octoaxes 主线（7 轴）和 octoaxesplus 双相机（5 轴 XYZW1W2）共享 firmware
# axisIndex 空间。X / Y / Z 在两者间 byte-identical；W / W1 / W2 在 firmware
# 是 FilterWheel 类（W_AXIS 模板），不同 axisName 由 axesmrg.cpp::beginAll 派发到
# 同/不同的 AxisConfig 模板。
#
# actuator_* 字段对应 firmware/config.h AxisConstDefinition 默认值，
# 上位机启动时通过 SET_LEAD_SCREW_PITCH / CONFIGURE_STEPPER_DRIVER 下发覆盖
# （避免固件被旧 Squid 上位机配成 32 细分后 Octoaxes GUI 显示错位）。
# ============================================================================

AXIS_CONFIG = {
    # ────────────────────────────────────────────────────────────────────────
    # 共享轴（octoaxes + octoaxesplus 都有）
    # ────────────────────────────────────────────────────────────────────────
    "X": {
        "display_name": "Step Motor - x_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-10, 115000),
        "movement_sign": 1,
        "index": 1,  # X 轴 index=1
        "default_velocity": 25.0,      # mm/s
        "default_acceleration": 500.0, # mm/s²
        "has_encoder": False,
        "encoder_transitions_per_rev": 50800,  # 2.54mm pitch / 0.05μm resolution
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 2.54,
        "actuator_microstepping": 256,
        "actuator_motor_current_ma": 1000,   # 峰值电流
        "actuator_motor_hold_ratio": 0.25,
        "enabled_for": ["octoaxes", "octoaxesplus"],
    },
    "Y": {
        "display_name": "Step Motor - y_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-10, 76000),
        "movement_sign": 1,
        "index": 0,  # Y 轴 index=0
        "default_velocity": 25.0,
        "default_acceleration": 500.0,
        "has_encoder": False,
        "encoder_transitions_per_rev": 50800,  # 2.54mm pitch / 0.05μm resolution
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 2.54,
        "actuator_microstepping": 256,
        "actuator_motor_current_ma": 1000,
        "actuator_motor_hold_ratio": 0.25,
        "enabled_for": ["octoaxes", "octoaxesplus"],
    },

    # ────────────────────────────────────────────────────────────────────────
    # octoaxes 主线轴（4 轴 + 3 历史 EXPAND 占位）
    # ────────────────────────────────────────────────────────────────────────
    "Z": {
        "display_name": "Step Motor - z_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-6000, 6000),
        "movement_sign": -1,
        "index": 2,
        "default_velocity": 3.0,
        "default_acceleration": 20.0,
        "has_encoder": False,
        "encoder_transitions_per_rev": 3000,  # 0.3mm pitch / 0.1μm resolution
        "encoder_flip_direction": True,
        "actuator_screw_pitch_mm": 0.3,
        "actuator_microstepping": 256,
        "actuator_motor_current_ma": 500,
        "actuator_motor_hold_ratio": 0.5,
        "enabled_for": ["octoaxes", "octoaxesplus"],
    },
    "W": {
        "display_name": "Filter Wheel 1 - w_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 3,
        "has_encoder": False,
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 100.0,
        "actuator_microstepping": 8,
        "enabled_for": ["octoaxes"],
    },
    "E1": {
        "display_name": "Objectives - expand1_axis",
        "type": "objective",
        "has_limits": False,
        "limits": (0, 4),
        "movement_sign": 1,
        "index": 4,
        "actuator_screw_pitch_mm": 1.0,
        "actuator_microstepping": 64,
        "enabled_for": ["octoaxes"],
    },
    "E3": {
        "display_name": "Step Motor - expand3_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-6000, 6000),
        "movement_sign": -1,
        "index": 5,
        "actuator_screw_pitch_mm": 0.3,
        "actuator_microstepping": 256,
        "enabled_for": ["octoaxes"],
    },
    "E4": {
        "display_name": "Filter Wheel 2 - expand4_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 6,
        "actuator_screw_pitch_mm": 100.0,
        "actuator_microstepping": 8,
        "enabled_for": ["octoaxes"],
    },

    # ────────────────────────────────────────────────────────────────────────
    # octoaxesplus 双相机轴（squid++ XYZW1W2 五轴方案，2026-05-15 起）
    # W1 / W2 = 滤光转盘，firmware 用 FilterWheel 类
    # W1 占用原 8 轴方案 Z2 的 HC154 CS（通道 6），W2 占用原 T 的 CS（通道 4）
    # ────────────────────────────────────────────────────────────────────────
    "W1": {
        "display_name": "Filter Wheel 1 - w1_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 3,  # firmware icID=3
        "has_encoder": False,
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 100.0,
        "actuator_microstepping": 8,
        "enabled_for": ["octoaxesplus"],
    },
    "W2": {
        "display_name": "Filter Wheel 2 - w2_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 4,  # firmware icID=4
        "has_encoder": False,
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 100.0,
        "actuator_microstepping": 8,
        "enabled_for": ["octoaxesplus"],
    },
}

# 微步 → mm 换算系数（从 AXIS_CONFIG 派生，单一数据源）
# 公式：mm_per_step = screwPitchMM / (fullStepsPerRev * microstepping)
# 修改 actuator_microstepping / actuator_screw_pitch_mm 时此表自动跟随，
# 避免与 _configure_actuators() 下发的微步值不一致
FULLSTEPS_PER_REV = 200
AXIS_MM_PER_STEP = {
    name: cfg["actuator_screw_pitch_mm"]
        / (FULLSTEPS_PER_REV * cfg["actuator_microstepping"])
    for name, cfg in AXIS_CONFIG.items()
}

# 移动距离
FILTERWHEEL_DISTANCE = 0.125 * 100  # mm

# 命令前缀（GUI 解析用户输入命令时识别的轴名称）
# 注意：Phase 3.1 完成后包含全部 13 个候选名称（含 octoaxesplus 的 6 个新名称）。
# Phase 3.2 落地后 GUI 会按固件型号过滤，但 COMMAND_PREFIXES 维持全集
# 以便手控/调试输入任意轴名都被识别。
COMMAND_PREFIXES = list(AXIS_CONFIG.keys())


# ============================================================================
# 固件型号 profile 工具函数（Phase 3.2 GUI 过滤用）
# ============================================================================

# 已知固件型号
FIRMWARE_MODELS = ("octoaxes", "octoaxesplus")

# 默认 profile：未识别固件时假定 octoaxes（向后兼容老机型）
DEFAULT_FIRMWARE_MODEL = "octoaxes"


def axes_for_model(model: str) -> dict:
    """返回指定固件型号下激活的 AXIS_CONFIG 子集。

    用法：
        from utils.constants import AXIS_CONFIG, axes_for_model
        active = axes_for_model("octoaxesplus")  # 返回 {"X","Y","Z","W1","W2"}

    GUI 启动时根据 S:HWINFO 查询的固件型号筛选实际渲染的轴。
    若 model 不在 FIRMWARE_MODELS 内，回退到 DEFAULT_FIRMWARE_MODEL。
    """
    if model not in FIRMWARE_MODELS:
        model = DEFAULT_FIRMWARE_MODEL
    return {
        name: cfg
        for name, cfg in AXIS_CONFIG.items()
        if model in cfg.get("enabled_for", [DEFAULT_FIRMWARE_MODEL])
    }

# 默认值
DEFAULT_LOW_LIMIT = -6000  # μm
DEFAULT_HIGH_LIMIT = 6000  # μm
DEFAULT_MOVE_DISTANCE = 500  # μm
