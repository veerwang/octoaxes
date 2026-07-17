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
        "display_name": "Step Motor - old z_axis",
        "limits": (-100, 6000),
        "actuator_screw_pitch_mm": 0.3,
        "actuator_motor_current_ma": 500,    # 峰值电流
        "actuator_motor_hold_ratio": 0.5,
        "encoder_transitions_per_rev": 3000,  # 0.3mm pitch / 0.1μm resolution
        "has_encoder": False,                  # 旧 Z 编码器禁用（与旧 Squid USE_ENCODER_Z=False 一致）
        "encoder_flip_direction": True,
        "switch_polarity": 0,                  # 旧 Z 限位极性=0（GUI 启动 cmd 20 下发，固件无需重烧切换）
        "homing_velocity_mm": 1.0,             # 旧 Z homing 速度（GUI 启动 S:SET_HOMING_VEL 下发；固件开机默认也是 1）
    },
    "new": {
        "display_name": "Step Motor - new z_axis",
        "limits": (-100, 30000),   # 上限 30mm（保守余量，实测行程上限位≈34.5mm STOPR 开关）；下限 -100um。⚠️octoaxes 主线板未实测
        "actuator_screw_pitch_mm": 1.0,       # W0601 导程 1mm
        "actuator_motor_current_ma": 1500,    # LE143S 额定 1.5A（TMC2240 路径=峰值，IRUN=23）
        "actuator_motor_hold_ratio": 0.75,    # 竖直 Z 防重力下坠
        "encoder_transitions_per_rev": 10000,  # 1.0mm pitch / 0.1μm resolution
        "has_encoder": True,                   # 2026-06-08 新 Z 编码器实测验证通过(ratio≈1/dev有界)，默认启用
        "encoder_flip_direction": True,        # flip=1：实测 enc 与 xactual 同向需翻转
        "switch_polarity": 1,                  # 新 Z 限位极性=1（与旧 Z 相反，06-09 传感器对调后唯一固件差异，现走 cmd 20 下发）
        "homing_velocity_mm": 2.0,             # 新 Z homing 速度（GUI 启动 S:SET_HOMING_VEL 下发提速，避免长行程 ~34.5mm 回零超时；固件开机默认 1）
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
        "type": "step_motor",
        "has_limits": True,
        "movement_sign": -1,
        "index": 2,
        "default_velocity": 3.0,
        "default_acceleration": 20.0,
        "actuator_microstepping": 256,  # 新旧 Z 共用
        # display_name / limits / has_encoder / flip / pitch / 电流 / hold / encoder_transitions 由 Z_AXIS_VARIANT 决定（见文件顶部开关）
        **_Z_VARIANTS[Z_AXIS_VARIANT],
    },
    "W": {
        "display_name": "Filter Wheel 1 - w_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,    # 与旧 Squid 一致。滤光轮 homing 搜索方向 = movement_sign（固件对
                               # W 段字节反向映射：sign=1 → data[3]=NEGATIVE → homing_direct=-1
                               # → 搜索 +，见 firmware filterwheel.cpp 2026-07-17 注释）
        "index": 3,
        "has_encoder": True,    # 2026-05-21 启用 ABN 编码器，GUI 通过 ENC_POS 反映 chip 真实位置
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 1.0,    # 2026-05-21 对齐旧 Squid SCREW_PITCH_W_MM=1
        "actuator_microstepping": 64,      # 2026-05-21 对齐旧 Squid MICROSTEPPING_DEFAULT_W=64
        # 2026-07-10 补齐电流/hold，让 GUI 启动 _configure_actuators 也给滤光轮下发 cmd21/23。
        # 值与固件 config.h FILTERWHEEL 常量一致 → 下发=显式同步、不改变行为；旧 Squid 不发则
        # 用固件默认（三方一致）。firmware begin() 与 cmd21 同走 motor_initDriver，3100 逐位等价。
        "actuator_motor_current_ma": 3100,   # = FILTERWHEEL_MOTOR_PEAK_CURRENT_mA
        "actuator_motor_hold_ratio": 0.5,    # = FILTERWHEEL_MOTOR_I_HOLD
    },
    # 2026-07-17 移除 W2（Filter Wheel 2）条目：本硬件只有一个滤光轮，GUI 轴列表由
    # AXIS_CONFIG.keys() 数据驱动，删条目即不再生成。固件 W2 轴（icID=4）保留不动
    # （缺板由死轴容错静默处理，装板也不影响其他轴）。恢复：照 W 条目补回 index=4
    # 的 W2（参数与 W 一致），见 git 历史 2026-07-09 版本。
    "Turret": {
        # 2026-05-29 本电路板 icID=5 槽位接物镜转换器（4 物镜），CS=pin 19/CLK=pin 28。
        # 协议走专属 MOVE_TURRET(44)/MOVETO_TURRET(45) + HOME_OR_ZERO axis=7（不复用 W 命令）。
        # GUI widgets.py 渲染物镜控制页；main_window.previous/next → _objective_goto()（融合 A5 闭环：
        # 绝对定位到工位标定角度，跨槽逐格拆步）。齿轮减速比 OBJECTIVE_RATIO=132/48 ×
        # SCREW_PITCH_W_MM=1 / OBJECTIVE_HOLES=4 = 0.6875 mm/位（物镜盘 1 圈 = 电机 2.75 圈 = 4 物镜）。
        "display_name": "Turret - expand1_axis",
        "type": "objective",
        "has_limits": False,
        "limits": (0, 3),       # 4 物镜 slot 0..3，与 define.py OBJECTIVE_SWITCH_MAX_INDEX=3 一致
        # 物镜位置显示 + 角度→位置换算符号：movement_sign=-1 既翻转显示（pos/steps/状态表 + slot/角度
        # 乘 sign），也参与 _objective_angle_to_position_um（角度×sign→电机 µstep），Next 显正值。
        # 且让 homing home_dir=0→HOME_POSITIVE→与 EXPAND1_AXIS.homing_direct=1 一致。
        "movement_sign": -1,
        "index": 5,             # firmware icID（octoaxes.ino: new Objectives(...,5,"Turret",4)）
        "actuator_screw_pitch_mm": 1.0,    # 对齐 config.h SCREW_PITCH_OBJECTIVES_MM=1
        "actuator_microstepping": 64,      # 对齐 config.h MICROSTEPPING_OBJECTIVES=64
        # 2026-07-10 A5a：补 objective GUI/下发字段，值对齐本项目固件 config.h（方案A 同款：
        # software 默认=firmware 默认 → 下发即同步、逐位等价；旧 Squid 不发则用固件默认）。
        "actuator_motor_current_ma": 1800,  # = OBJECTIVES_MOTOR_PEAK_CURRENT_mA（EXPAND1_AXIS currentRange=1）
        "actuator_motor_hold_ratio": 0.5,   # = OBJECTIVES_MOTOR_I_HOLD
        # GUI 物镜页速度/加速度输入框预填值（Apply 时下发 SET_MAX_VELOCITY_ACCELERATION，不在启动下发）
        "default_velocity": 0.5,        # = MAX_VELOCITY_OBJECTIVES_mm（0.5×pitch）
        "default_acceleration": 80.0,   # = MAX_ACCELERATION_OBJECTIVES_mm（80×pitch）
        # 弹片自定位（融合 A1b GUI 主导时序，见 A5c）：到位后 GUI 延迟发 cmd32 断电流让弹片归中。
        # 断流→弹片凹坑机械归中，精度优于 PID 死区，且修得了编码器（电机尾轴）看不到的齿轮空程。
        "auto_disable_at_rest": True,
        "rest_disable_delay_ms": 100,
        # === 编码器 + PID 闭环（用户确认：物镜转换器一定带编码器）===
        # GUI 启动 _configure_encoders 下发 CONFIGURE_STAGE_PID(Turret, tpr, flip) 运行时启用编码器，
        # 再 SET_PID_ARGUMENTS + ENABLE_STAGE_PID 开闭环（运行时下发，固件 config.h enableEncoder=false
        # 只是开机默认，被运行时覆盖）。
        "has_encoder": True,
        "encoder_transitions_per_rev": 4000,   # 1000 线(PPR) ×4 倍频 = 4000 counts/转（对齐 Mega W；本项目硬件须实测确认）
        "encoder_flip_direction": True,        # 编码器计数方向 vs 电机命令（Mega W=True；本项目须实测确认）
        # PID 值取 Mega W 起点，⚠️ 本项目 Turret 硬件须用 tune_w_pid.py 重新整定（稳定性由加速度主导，
        # 改 velocity/accel/物镜数须重校验）。运行时下发 SET_PID_ARGUMENTS，改此重启 GUI 即生效。
        "pid_enabled": True,
        "pid_p": 1536,
        "pid_i": 2,
        "pid_d": 16,
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
