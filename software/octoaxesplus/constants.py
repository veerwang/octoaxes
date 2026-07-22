"""
octoaxesplus 上位机常量定义（squid++ 双相机 5 轴方案：X/Y/Z/W1/W2）

轴索引与固件对应：Y(0), X(1), Z(2), W1(3), W2(4)
W1 / W2 = 滤光转盘（FilterWheel），CS 分别占用 HC154 通道 6 / 4
（详见 documents/octoaxesplus_axis_definitions.md）。

actuator_* 字段对应 firmware/octoaxesplus/config.h AxisConstDefinition 默认值，
上位机启动时通过 SET_LEAD_SCREW_PITCH / CONFIGURE_STEPPER_DRIVER 下发覆盖
（避免固件被旧 Squid 上位机配成 32 细分后 Octoaxes GUI 显示错位）。
"""

# old/new Z variant switch (ported from the octoaxes profile, 2026-06-06: no octoaxes board on hand, borrowed the
# squid++ dual-camera board to debug the new Z motor MOONS' LE143S-W0601). New Z = TMC2240, 1.5A,
# pitch 1mm; switching only changes this line, sent at GUI startup via SET_LEAD_SCREW_PITCH/CONFIGURE_STEPPER_DRIVER.
# (WARNING) precondition: firmware/octoaxesplus/config.h Z_AXIS.currentRange is set to 1 (I_FS=2A), otherwise 1.5A is miscalculated.
Z_AXIS_VARIANT = "old"   # "old"=old Z (0.3mm/0.5A) / "new"=LE143S (1.0mm/1.5A)

# -- single source of truth for encoder direction (ENC-2, see documents/audit_octoaxesplus_20260608.md) --
# each axis's encoder_flip_direction is the **runtime authoritative value**: sent by the GUI at startup via CONFIGURE_STAGE_PID,
# the firmware configureStagePID sets ENC_IN_CONF.INVERT_ENC_DIR accordingly, overriding the config.h boot default
# Z_INVERT_ENCODER. The firmware boot default is currently ineffective (Z_AXIS.enableEncoder=false gates the
# encoder init in begin()); the boot value only briefly takes effect when an axis has config.h enableEncoder=true.
# -> to change direction just change here; if you enable boot enableEncoder for an axis, config.h Z_INVERT_ENCODER
# must match this (the firmware configureStagePID already adds a mismatch DEBUG warning as a tripwire).
_Z_VARIANTS = {
    "old": {
        "display_name": "Step Motor - old z_axis",
        "limits": (-100, 6000),
        "actuator_screw_pitch_mm": 0.3,
        "actuator_motor_current_ma": 500,    # peak current
        "actuator_motor_hold_ratio": 0.5,
        "encoder_transitions_per_rev": 3000,  # 0.3mm pitch / 0.1μm resolution
        "has_encoder": False,                  # old-Z encoder disabled (consistent with legacy Squid USE_ENCODER_Z=False)
        "encoder_flip_direction": True,
        "switch_polarity": 0,                  # old-Z limit polarity=0 (sent by the GUI via cmd 20 at startup; no firmware reflash needed to switch)
        "homing_velocity_mm": 1.0,             # old-Z homing speed (sent by the GUI via S:SET_HOMING_VEL at startup; the firmware boot default is also 1)
    },
    "new": {
        "display_name": "Step Motor - new z_axis",
        "limits": (-100, 30000),   # upper limit 30mm (conservative margin; 2026-06-08 measured travel upper limit ~=34.5mm STOPR switch); lower limit -100um
        "actuator_screw_pitch_mm": 1.0,       # W0601 pitch 1mm
        "actuator_motor_current_ma": 1500,    # LE143S rated 1.5A (TMC2240 path=peak, IRUN=23)
        "actuator_motor_hold_ratio": 0.75,    # vertical Z, resists gravity sag
        "encoder_transitions_per_rev": 10000,  # 1.0mm pitch / 0.1μm resolution
        "has_encoder": True,                   # 2026-06-08 new-Z encoder verified on hardware (ratio~=1 / bounded dev), enabled by default
        "encoder_flip_direction": True,        # flip=1: measured that enc and xactual are same-direction and need flipping (runtime authoritative, see the ENC-2 note above)
        "switch_polarity": 1,                  # new-Z limit polarity=1 (opposite to old Z; after the 06-09 sensor swap this is the only firmware difference, now sent via cmd 20)
        "homing_velocity_mm": 2.0,             # new-Z homing speed (the GUI sends S:SET_HOMING_VEL at startup to speed it up, avoiding a long-travel ~34.5mm homing timeout; firmware boot default is 1)
    },
}

AXIS_CONFIG = {
    "X": {
        "display_name": "Step Motor - x_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-10, 115000),
        "movement_sign": 1,
        "index": 0,  # firmware icID (2026-07-20 X/Y swapped, unified with the octoaxes mainline X=0/Y=1)
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
        "index": 1,  # firmware icID (2026-07-20 X/Y swapped, unified with the octoaxes mainline X=0/Y=1)
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
        "type": "step_motor",
        "has_limits": True,
        "movement_sign": -1,
        "index": 2,
        "default_velocity": 3.0,
        "default_acceleration": 20.0,
        "actuator_microstepping": 256,  # shared by old and new Z
        # has_encoder / flip / pitch / current / hold / encoder_transitions are decided by Z_AXIS_VARIANT (see the switch at the top of the file)
        **_Z_VARIANTS[Z_AXIS_VARIANT],
    },
    "W1": {
        "display_name": "Filter Wheel 1 - w1_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 3,
        # 2026-07-17 encoder feedback enabled in unison with W2 (user confirmed both filter wheels
        # share the same configuration). flip/tpr take W2's measured values (same sensor model /
        # W_AXIS template); once the W1 driver board is installed, re-verify with the same
        # w2_encoder_check.py procedure. With the board currently missing, cmd25 falls back W->W1
        # onto a dead axis — silent no-op, no side effects.
        "has_encoder": True,
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 1.0,    # 2026-05-21 matches legacy Squid SCREW_PITCH_W_MM=1
        "actuator_microstepping": 64,      # 2026-05-21 matches legacy Squid MICROSTEPPING_DEFAULT_W=64
        # 2026-07-10 add current/hold so the GUI's _configure_actuators also sends them to the filter wheel (= firmware
        # FILTERWHEEL constants, bit-for-bit equivalent; if not sent, falls back to the firmware default). Same handling as octoaxes W/W2.
        "actuator_motor_current_ma": 3100,   # = FILTERWHEEL_MOTOR_PEAK_CURRENT_mA
        "actuator_motor_hold_ratio": 0.5,    # = FILTERWHEEL_MOTOR_I_HOLD
    },
    "W2": {
        "display_name": "Filter Wheel 2 - w2_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 4,
        # 2026-07-15 verified on machine: pure encoder feedback enabled (CONFIGURE_STAGE_PID only
        # enables the encoder; pid_enabled not sent -> PID loop stays open). tpr/flip pending
        # S:ENCPOS measurement confirmation.
        "has_encoder": True,
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 1.0,    # 2026-05-21 matches legacy Squid SCREW_PITCH_W_MM=1
        "actuator_microstepping": 64,
        "actuator_motor_current_ma": 3100,   # = FILTERWHEEL_MOTOR_PEAK_CURRENT_mA (same as W1/firmware)
        "actuator_motor_hold_ratio": 0.5,    # = FILTERWHEEL_MOTOR_I_HOLD
    },
    "Turret": {
        # 2026-06-02 objective turret (4 objectives): physical R axis (HC154 ch3), firmware icID=5.
        # reuses the octoaxes E1 protocol MOVE_TURRET(44)/MOVETO_TURRET(45) + HOME_OR_ZERO axis=7.
        # GUI widgets.py renders the objective-control page; main_window.previous/next -> _objective_goto()
        # (A5 closed loop: absolute positioning to the slot-calibrated angle, multi-slot moves split slot
        # by slot). The R axis is likewise an [objective turret with encoder], using encoder + PID closed
        # loop, configuration aligned with octoaxes Turret (user confirmed: both profiles' turrets are identical).
        "display_name": "Objectives - r_axis",
        "type": "objective",
        "has_limits": False,
        "limits": (0, 3),       # 4 objectives, slots 0..3, consistent with define.py OBJECTIVE_SWITCH_MAX_INDEX=3
        # movement_sign=1: both the display and the _objective_angle_to_position_um angle->µstep conversion multiply by sign.
        "movement_sign": 1,
        "index": 5,             # firmware icID（octoaxesplus.ino: new Objectives(...,5,"Turret",4)）
        "actuator_screw_pitch_mm": 1.0,    # matches config.h SCREW_PITCH_OBJECTIVES_MM=1
        "actuator_microstepping": 64,      # matches config.h MICROSTEPPING_OBJECTIVES=64
        # A5: added the objective GUI/send-down fields, values aligned with firmware config.h (plan A: software defaults = firmware defaults)
        "actuator_motor_current_ma": 1800,  # = OBJECTIVES_MOTOR_PEAK_CURRENT_mA
        "actuator_motor_hold_ratio": 0.5,   # = OBJECTIVES_MOTOR_I_HOLD
        # prefill for the objective page velocity/acceleration (sent via SET_MAX_VELOCITY_ACCELERATION on Apply, not at startup)
        "default_velocity": 0.5,        # = MAX_VELOCITY_OBJECTIVES_mm (0.5*pitch)
        "default_acceleration": 80.0,   # = MAX_ACCELERATION_OBJECTIVES_mm (80*pitch)
        # spring-plate self-centering: after arrival the GUI sends a delayed cmd32 to cut current so the
        # spring plate re-centers (more precise than the PID deadband, also fixes gear lash)
        "auto_disable_at_rest": True,
        "rest_disable_delay_ms": 100,
        # === encoder + PID closed loop (R axis = objective turret with encoder) ===
        # GUI startup _configure_encoders sends SET_PID_ARGUMENTS -> CONFIGURE_STAGE_PID ->
        # ENABLE_STAGE_PID for Turret to close the loop (runtime send-down, overriding the firmware
        # config.h enableEncoder=false boot default).
        "has_encoder": True,
        "encoder_transitions_per_rev": 4000,   # 1000-line (PPR) *4 quadrature (aligned with Mega W / octoaxes Turret; must be verified on this hardware)
        "encoder_flip_direction": True,        # encoder count direction vs motor command (must be verified on this hardware; polarity determined jointly with movement_sign)
        # PID values taken from the Mega W / octoaxes Turret starting point; ⚠️ this R axis hardware must be re-tuned with tune_w_pid.py.
        "pid_enabled": True,
        "pid_p": 1536,
        "pid_i": 2,
        "pid_d": 16,
    },
}

# microstep -> mm conversion factor (derived from AXIS_CONFIG, single source of truth)
FULLSTEPS_PER_REV = 200
AXIS_MM_PER_STEP = {
    name: cfg["actuator_screw_pitch_mm"]
        / (FULLSTEPS_PER_REV * cfg["actuator_microstepping"])
    for name, cfg in AXIS_CONFIG.items()
}

# move distance
FILTERWHEEL_DISTANCE = 0.125  # mm  2026-05-21 W units aligned with legacy Squid, removed the x100 compensation

# command prefix (the axis name recognized when the GUI parses user-entered commands)
COMMAND_PREFIXES = list(AXIS_CONFIG.keys())

# default value
DEFAULT_LOW_LIMIT = -6000  # μm
DEFAULT_HIGH_LIMIT = 6000  # μm
DEFAULT_MOVE_DISTANCE = 500  # μm

# --- illumination port config (squid++ dual-camera: 8 TTL lines + 8 DAC direct-control channels) ---
#
# ILLUMINATION_PORTS:
# each row (port_index, display_name, pin_number). port_index and the firmware
# octoaxesplus/illumination.cpp::port_index_to_pin are strictly aligned.
# the TTL pins are driven by the SET_PORT_ILLUMINATION (cmd 37) binary protocol.
#
# ILLUMINATION_DAC_CHANNELS:
# each row (dac_ch, full_scale_volt). ch7 (D8) defaults to gain=2 -> full scale 5V,
# the rest gain=1 -> 2.5V. D8 can be switched between 5V / 2.5V via the GAIN toggle button.
# the DAC slider uses the ASCII command S:DAC_SET to directly write raw values (bypassing illumination_intensity_factor),
# what-you-see-is-what-you-get during bring-up; does not affect the production TTL button path.
#
# ILLUMINATION_HAS_GAIN_SWITCH:
# True -> render the "D8 max: 5V<->2.5V" toggle button (sends S:DAC_GAIN).
# ILLUMINATION_HAS_DAC_READBACK:
# True -> render the "Read DAC Regs" button (sends S:DAC_READ_ALL).
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
    (7, 5.0),  # D8 ch7  gain=2 (can drop to 2.5V after the GAIN toggle)
]
ILLUMINATION_HAS_GAIN_SWITCH  = True
ILLUMINATION_HAS_DAC_READBACK = True
