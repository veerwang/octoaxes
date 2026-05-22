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
        "movement_sign": 1,    # 与旧 Squid 一致（sign=1 → HOME_NEGATIVE → chip 朝 - 方向 search）
        "index": 3,
        "has_encoder": True,    # 2026-05-21 启用 ABN 编码器，GUI 通过 ENC_POS 反映 chip 真实位置
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 1.0,    # 2026-05-21 对齐旧 Squid SCREW_PITCH_W_MM=1
        "actuator_microstepping": 64,      # 2026-05-21 对齐旧 Squid MICROSTEPPING_DEFAULT_W=64
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
FILTERWHEEL_DISTANCE = 0.125  # mm  2026-05-21 W 量纲对齐旧 Squid，去掉 ×100 补偿（1 槽 = 1/8 圈 = 0.125 mm）

# 命令前缀
COMMAND_PREFIXES = list(AXIS_CONFIG.keys())

# 默认值
DEFAULT_LOW_LIMIT = -6000  # μm
DEFAULT_HIGH_LIMIT = 6000  # μm
DEFAULT_MOVE_DISTANCE = 500  # μm

# ─── 照明端口配置（按 profile 动态，common/gui 据此渲染） ──────────────────
#
# ILLUMINATION_PORTS:
#   每行 (port_index, display_name, pin_number)。port_index 与 firmware
#   port_index_to_pin / port_index_to_dac_channel 严格对齐。
#
# ILLUMINATION_DAC_CHANNELS:
#   每行 (dac_ch, full_scale_volt)。空列表 = 该 profile 无 DAC 直控滑条。
#   octoaxes 旧硬件无独立 DAC 直控（DAC 强度通过 SET_PORT_ILLUMINATION 耦合），
#   bring-up 工具仅 octoaxesplus 需要。
#
# ILLUMINATION_HAS_GAIN_SWITCH / ILLUMINATION_HAS_DAC_READBACK:
#   控制 D8 5V↔2.5V 切换按钮和 Read DAC Regs 按钮是否渲染。
ILLUMINATION_PORTS = [
    (0, "D1 (pin 5)",   5),
    (1, "D2 (pin 4)",   4),
    (2, "D3 (pin 22)", 22),
    (3, "D4 (pin 3)",   3),
    (4, "D5 (pin 23)", 23),
]
ILLUMINATION_DAC_CHANNELS = []          # 旧硬件不暴露 DAC 直控
ILLUMINATION_HAS_GAIN_SWITCH  = False
ILLUMINATION_HAS_DAC_READBACK = False
