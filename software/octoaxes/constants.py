"""
常量定义
"""

# ─────────────────────────────────────────────────────────────────────────────
# Z 轴硬件变体开关（2026-06-03 newz 分支）
# ─────────────────────────────────────────────────────────────────────────────
# 改这一行即可在新旧 Z 电机间切换，重启 GUI 生效，无需重烧固件：
#   GUI 启动 _configure_actuators() 把所选变体的 pitch/电流/hold 下发给固件覆盖默认。
# 固件侧 currentRange=1 对新旧 Z 驱动板都安全（旧 Z=TMC2660 忽略，新 Z=TMC2240 用它），
# 一个固件通吃；DRIVER_AUTO 上电自动识别在位的驱动板。
#   "old" = 旧 Z（丝杠导程 0.3mm / TMC2660 板 / 0.47A）
#   "new" = MOONS' LE143S-W0601-100-AR1-S-150（导程 1mm / TMC2240 ICS 板 / 1.5A 峰值）
# 注意：此开关仅 octoaxes GUI 有效。旧 Squid software 会下发它自己写死的旧 Z 参数，
#       配新 Z 硬件会有 3.33× 位置错位（旧 Squid 不可改）。
Z_AXIS_VARIANT = "old"

_Z_VARIANTS = {
    "old": {
        "actuator_screw_pitch_mm": 0.3,
        "actuator_motor_current_ma": 500,    # 峰值电流
        "actuator_motor_hold_ratio": 0.5,
        "encoder_transitions_per_rev": 3000,  # 0.3mm pitch / 0.1μm resolution
        "has_encoder": False,                  # 旧 Z 编码器禁用（与旧 Squid USE_ENCODER_Z=False 一致）
        "encoder_flip_direction": True,
        "switch_polarity": 0,                  # 旧 Z 限位极性=0（GUI 启动 cmd 20 下发，固件无需重烧切换）
    },
    "new": {
        "actuator_screw_pitch_mm": 1.0,       # W0601 导程 1mm
        "actuator_motor_current_ma": 1500,    # LE143S 额定 1.5A（TMC2240 路径=峰值，IRUN=23）
        "actuator_motor_hold_ratio": 0.75,    # 竖直 Z 防重力下坠
        "encoder_transitions_per_rev": 10000,  # 1.0mm pitch / 0.1μm resolution
        "has_encoder": True,                   # 2026-06-08 新 Z 编码器实测验证通过(ratio≈1/dev有界)，默认启用
        "encoder_flip_direction": True,        # flip=1：实测 enc 与 xactual 同向需翻转
        "switch_polarity": 1,                  # 新 Z 限位极性=1（与旧 Z 相反，06-09 传感器对调后唯一固件差异，现走 cmd 20 下发）
    },
}

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
        "actuator_microstepping": 256,  # 新旧 Z 共用
        # has_encoder / flip / pitch / 电流 / hold / encoder_transitions 由 Z_AXIS_VARIANT 决定（见文件顶部开关）
        **_Z_VARIANTS[Z_AXIS_VARIANT],
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
    "Turret": {
        # 2026-05-29 本电路板 icID=5 槽位接物镜转换器（4 物镜），CS=pin 19/CLK=pin 28。
        # 协议走专属 MOVE_TURRET(44)/MOVETO_TURRET(45) + HOME_OR_ZERO axis=7（不复用 W 命令）。
        # GUI widgets.py 渲染物镜控制页；main_window.previous/next → move_objective()，
        # 齿轮减速比 OBJECTIVE_RATIO=132/48 × SCREW_PITCH_W_MM=1 / OBJECTIVE_HOLES=4 = 0.6875 mm/位。
        "display_name": "Objectives - expand1_axis",
        "type": "objective",
        "has_limits": False,
        "limits": (0, 3),       # 4 物镜 slot 0..3，与 define.py OBJECTIVE_SWITCH_MAX_INDEX=3 一致
        # 物镜位置显示符号：move_objective() 硬编码 -1（Next=负方向），但 GUI 期望 Next 显正值。
        # movement_sign=-1 翻转显示（pos/steps/状态表乘 sign），且让 homing home_dir=0→HOME_POSITIVE
        # →new_direct=+1 与 EXPAND1_AXIS.homing_direct=1 一致。不影响 move_objective 物理方向（它不用 sign）。
        "movement_sign": -1,
        "index": 5,             # firmware icID（octoaxes.ino: new Objectives(...,5,"Turret",4)）
        "actuator_screw_pitch_mm": 1.0,    # 对齐 config.h SCREW_PITCH_OBJECTIVES_MM=1
        "actuator_microstepping": 64,      # 对齐 config.h MICROSTEPPING_OBJECTIVES=64
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
