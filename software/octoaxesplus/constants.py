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
        "actuator_screw_pitch_mm": 1.0,    # 2026-05-21 对齐旧 Squid SCREW_PITCH_W_MM=1
        "actuator_microstepping": 64,      # 2026-05-21 对齐旧 Squid MICROSTEPPING_DEFAULT_W=64
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
        "actuator_screw_pitch_mm": 1.0,    # 2026-05-21 对齐旧 Squid SCREW_PITCH_W_MM=1
        "actuator_microstepping": 64,
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
FILTERWHEEL_DISTANCE = 0.125  # mm  2026-05-21 W 量纲对齐旧 Squid，去掉 ×100 补偿

# 命令前缀（GUI 解析用户输入命令时识别的轴名称）
COMMAND_PREFIXES = list(AXIS_CONFIG.keys())

# 默认值
DEFAULT_LOW_LIMIT = -6000  # μm
DEFAULT_HIGH_LIMIT = 6000  # μm
DEFAULT_MOVE_DISTANCE = 500  # μm

# ─── 照明端口配置（squid++ 双相机：8 路 TTL + 8 通道 DAC 直控） ────────
#
# ILLUMINATION_PORTS:
#   每行 (port_index, display_name, pin_number)。port_index 与 firmware
#   octoaxesplus/illumination.cpp::port_index_to_pin 严格对齐。
#   TTL 引脚由 SET_PORT_ILLUMINATION (cmd 37) 二进制协议驱动。
#
# ILLUMINATION_DAC_CHANNELS:
#   每行 (dac_ch, full_scale_volt)。ch7 (D8) 默认 gain=2 → 满量程 5V，
#   其余 gain=1 → 2.5V。可通过 GAIN 切换按钮在 5V / 2.5V 间切换 D8。
#   DAC 滑条走 ASCII 命令 S:DAC_SET，直控写 raw 值（绕过 illumination_intensity_factor），
#   bring-up 时所见即所得；不影响生产 TTL 按钮路径。
#
# ILLUMINATION_HAS_GAIN_SWITCH:
#   True → 渲染 "D8 max: 5V↔2.5V" 切换按钮（发 S:DAC_GAIN）。
# ILLUMINATION_HAS_DAC_READBACK:
#   True → 渲染 "Read DAC Regs" 按钮（发 S:DAC_READ_ALL）。
ILLUMINATION_PORTS = [
    (0, "D1 (pin 32)", 32),
    (1, "D2 (pin 31)", 31),
    (2, "D3 (pin 30)", 30),
    (3, "D4 (pin 29)", 29),
    (4, "D5 (pin 28)", 28),
    (5, "D6 (pin 25)", 25),
    (6, "D7 (pin 24)", 24),
    (7, "D8 (pin 10)", 10),
]
ILLUMINATION_DAC_CHANNELS = [
    (0, 2.5),  # D1 ch0  gain=1
    (1, 2.5),  # D2 ch1  gain=1
    (2, 2.5),  # D3 ch2  gain=1
    (3, 2.5),  # D4 ch3  gain=1
    (4, 2.5),  # D5 ch4  gain=1
    (5, 2.5),  # D6 ch5  gain=1
    (6, 2.5),  # D7 ch6  gain=1
    (7, 5.0),  # D8 ch7  gain=2（GAIN 切换后可降到 2.5V）
]
ILLUMINATION_HAS_GAIN_SWITCH  = True
ILLUMINATION_HAS_DAC_READBACK = True
