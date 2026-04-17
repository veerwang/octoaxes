"""
常量定义
"""

# 轴配置
# 轴索引与固件对应: Y(0), X(1), Z(2), W(3)
# actuator_* 字段对应 firmware/config.h AxisConstDefinition 默认值，
# 上位机启动时通过 SET_LEAD_SCREW_PITCH / CONFIGURE_STEPPER_DRIVER 下发覆盖
# （避免固件被旧 Squid 上位机配成 32 细分后 Octoaxes GUI 显示错位）
AXIS_CONFIG = {
    "X": {
        "display_name": "Step Motor - x_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-80000, 80000),
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
    },
    "Y": {
        "display_name": "Step Motor - y_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-120000, 120000),
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
        "encoder_transitions_per_rev": 3000,  # 0.3mm pitch / 0.1μm resolution
        "encoder_flip_direction": True,
        "actuator_screw_pitch_mm": 0.3,
        "actuator_microstepping": 256,
        "actuator_motor_current_ma": 500,
        "actuator_motor_hold_ratio": 0.5,
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
    },
    "E1": {
        "display_name": "Objectives - expand1_axis",
        "type": "objective",
        "has_limits": False,
        "limits": (0, 4),
        "movement_sign": 1,
        "index": 4,
    },
    "E3": {
        "display_name": "Step Motor - expand3_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-6000, 6000),
        "movement_sign": -1,
        "index": 5,
    },
    "E4": {
        "display_name": "Filter Wheel 2 - expand4_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 6,
    },
}

# 微步 → mm 换算系数（与 firmware/config.h AxisConstDefinition 保持一致）
# 公式：mm_per_step = screwPitchMM / (fullStepsPerRev * microstepping)
AXIS_MM_PER_STEP = {
    "X":  2.54  / (200 * 256),   # ≈ 4.96e-5 mm/step
    "Y":  2.54  / (200 * 256),
    "Z":  0.3   / (200 * 256),   # ≈ 5.86e-6 mm/step
    "W":  100.0 / (200 * 8),     # 0.0625 mm/step（滤光轮，mm 无实际意义）
    "E1": 1.0   / (200 * 64),
    "E3": 0.3   / (200 * 256),
    "E4": 100.0 / (200 * 8),
}

# 移动距离
FILTERWHEEL_DISTANCE = 0.125 * 100  # mm

# 命令前缀
COMMAND_PREFIXES = list(AXIS_CONFIG.keys())

# 默认值
DEFAULT_LOW_LIMIT = -6000  # μm
DEFAULT_HIGH_LIMIT = 6000  # μm
DEFAULT_MOVE_DISTANCE = 500  # μm
