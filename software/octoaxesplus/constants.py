"""
octoaxesplus 上位机常量定义（squid++ 双相机 5 轴方案：X/Y/Z/W1/W2）

轴索引与固件对应：Y(0), X(1), Z(2), W1(3), W2(4)
W1 / W2 = 滤光转盘（FilterWheel），CS 分别占用 HC154 通道 6 / 4
（详见 documents/octoaxesplus_axis_definitions.md）。

actuator_* 字段对应 firmware/octoaxesplus/config.h AxisConstDefinition 默认值，
上位机启动时通过 SET_LEAD_SCREW_PITCH / CONFIGURE_STEPPER_DRIVER 下发覆盖
（避免固件被旧 Squid 上位机配成 32 细分后 Octoaxes GUI 显示错位）。
"""

AXIS_CONFIG = {
    "X": {
        "display_name": "Step Motor - x_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-10, 115000),
        "movement_sign": 1,
        "index": 1,
        "default_velocity": 25.0,
        "default_acceleration": 500.0,
        "has_encoder": False,
        "encoder_transitions_per_rev": 50800,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 2.54,
        "actuator_microstepping": 256,
        "actuator_motor_current_ma": 1000,
        "actuator_motor_hold_ratio": 0.25,
    },
    "Y": {
        "display_name": "Step Motor - y_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-10, 76000),
        "movement_sign": 1,
        "index": 0,
        "default_velocity": 25.0,
        "default_acceleration": 500.0,
        "has_encoder": False,
        "encoder_transitions_per_rev": 50800,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 2.54,
        "actuator_microstepping": 256,
        "actuator_motor_current_ma": 1000,
        "actuator_motor_hold_ratio": 0.25,
    },
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
        "encoder_transitions_per_rev": 3000,
        "encoder_flip_direction": True,
        "actuator_screw_pitch_mm": 0.3,
        "actuator_microstepping": 256,
        "actuator_motor_current_ma": 500,
        "actuator_motor_hold_ratio": 0.5,
    },
    "W1": {
        "display_name": "Filter Wheel 1 - w1_axis",
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
    },
    "W2": {
        "display_name": "Filter Wheel 2 - w2_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 4,
        "has_encoder": False,
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 100.0,
        "actuator_microstepping": 8,
    },
}

# 微步 → mm 换算系数（从 AXIS_CONFIG 派生，单一数据源）
FULLSTEPS_PER_REV = 200
AXIS_MM_PER_STEP = {
    name: cfg["actuator_screw_pitch_mm"]
        / (FULLSTEPS_PER_REV * cfg["actuator_microstepping"])
    for name, cfg in AXIS_CONFIG.items()
}

# 移动距离
FILTERWHEEL_DISTANCE = 0.125 * 100  # mm

# 命令前缀（GUI 解析用户输入命令时识别的轴名称）
COMMAND_PREFIXES = list(AXIS_CONFIG.keys())

# 默认值
DEFAULT_LOW_LIMIT = -6000  # μm
DEFAULT_HIGH_LIMIT = 6000  # μm
DEFAULT_MOVE_DISTANCE = 500  # μm
