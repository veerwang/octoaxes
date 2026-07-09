"""
常量定义
"""

# ─────────────────────────────────────────────────────────────────────────────
# Z-axis hardware variant switch (2026-06-03 newz branch)
# ─────────────────────────────────────────────────────────────────────────────
# change this one line to switch between the old and new Z motors; takes effect after a GUI restart, no firmware reflash needed:
# at startup the GUI's _configure_actuators() sends the selected variant's pitch/current/hold to the firmware to override the defaults.
# on the firmware side currentRange=1 is safe for both Z driver boards (old Z=TMC2660 ignores it, new Z=TMC2240 uses it),
# so one firmware fits both; DRIVER_AUTO auto-detects the installed driver board at power-on.
# "old" = old Z (screw pitch 0.3mm / TMC2660 board / 0.47A)
# "new" = MOONS' LE143S-W0601-100-AR1-S-150 (pitch 1mm / TMC2240 ICS board / 1.5A peak)
# note: this switch is only effective in the octoaxes GUI. Legacy Squid software sends its own hardcoded old-Z parameters,
# which with new-Z hardware gives a 3.33x position error (legacy Squid cannot be changed).
Z_AXIS_VARIANT = "old"

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
        "limits": (-100, 30000),   # upper limit 30mm (conservative margin; measured travel upper limit ~=34.5mm STOPR switch); lower limit -100um. (WARNING) not tested on the octoaxes mainline board
        "actuator_screw_pitch_mm": 1.0,       # W0601 pitch 1mm
        "actuator_motor_current_ma": 1500,    # LE143S rated 1.5A (TMC2240 path=peak, IRUN=23)
        "actuator_motor_hold_ratio": 0.75,    # vertical Z, resists gravity sag
        "encoder_transitions_per_rev": 10000,  # 1.0mm pitch / 0.1μm resolution
        "has_encoder": True,                   # 2026-06-08 new-Z encoder verified on hardware (ratio~=1 / bounded dev), enabled by default
        "encoder_flip_direction": True,        # flip=1: measured that enc and xactual are same-direction and need flipping
        "switch_polarity": 1,                  # new-Z limit polarity=1 (opposite to old Z; after the 06-09 sensor swap this is the only firmware difference, now sent via cmd 20)
        "homing_velocity_mm": 2.0,             # new-Z homing speed (the GUI sends S:SET_HOMING_VEL at startup to speed it up, avoiding a long-travel ~34.5mm homing timeout; firmware boot default is 1)
    },
}

# axis configuration
# axis index maps to firmware: Y(0), X(1), Z(2), W(3)
# the actuator_* fields correspond to the firmware/config.h AxisConstDefinition defaults,
# the host overrides them at startup via SET_LEAD_SCREW_PITCH / CONFIGURE_STEPPER_DRIVER
# (avoids display misalignment in the Octoaxes GUI after legacy Squid configured the firmware to 32 microsteps)
AXIS_CONFIG = {
    "X": {
        "display_name": "Step Motor - x_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-10, 115000),
        "movement_sign": 1,
        "index": 1,  # X axis index=1
        "default_velocity": 25.0,      # mm/s
        "default_acceleration": 500.0, # mm/s²
        "has_encoder": False,
        "encoder_transitions_per_rev": 50800,  # 2.54mm pitch / 0.05μm resolution
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 2.54,
        "actuator_microstepping": 256,
        "actuator_motor_current_ma": 1000,   # peak current
        "actuator_motor_hold_ratio": 0.25,
    },
    "Y": {
        "display_name": "Step Motor - y_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-10, 76000),
        "movement_sign": 1,
        "index": 0,  # Y axis index=0
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
        "actuator_microstepping": 256,  # shared by old and new Z
        # display_name / limits / has_encoder / flip / pitch / current / hold / encoder_transitions are decided by Z_AXIS_VARIANT (see the switch at the top of the file)
        **_Z_VARIANTS[Z_AXIS_VARIANT],
    },
    "W": {
        "display_name": "Filter Wheel 1 - w_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,    # consistent with legacy Squid (sign=1 -> HOME_NEGATIVE -> the chip searches toward -)
        "index": 3,
        "has_encoder": True,    # 2026-05-21 enable the ABN encoder; the GUI reflects the chip's real position via ENC_POS
        "encoder_transitions_per_rev": 4000,
        "encoder_flip_direction": False,
        "actuator_screw_pitch_mm": 1.0,    # 2026-05-21 matches legacy Squid SCREW_PITCH_W_MM=1
        "actuator_microstepping": 64,      # 2026-05-21 matches legacy Squid MICROSTEPPING_DEFAULT_W=64
    },
    "W2": {
        "display_name": "Filter Wheel 2 - expand4_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 4,
        # 2026-07-09 align against W (filter wheel 1) as the baseline: E4=filter wheel 2, same hardware type so parameters should match.
        # the original pitch=100/ms=8 were stale wrong values (firmware EXPAND4_AXIS has long used the FILTERWHEEL constants pitch=1/ms=64).
        # encoder not added yet (requires E4 hardware ABN installed + added to _configure_encoders + firmware encoderLinesPerRev to take effect).
        "actuator_screw_pitch_mm": 1.0,    # align with W / legacy Squid SCREW_PITCH_W_MM=1
        "actuator_microstepping": 64,      # align with W / legacy Squid MICROSTEPPING_DEFAULT_W=64
    },
    "Turret": {
        # 2026-05-29 on this board the icID=5 slot connects to the objective turret (4 objectives), CS=pin 19/CLK=pin 28.
        # the protocol uses dedicated MOVE_TURRET(44)/MOVETO_TURRET(45) + HOME_OR_ZERO axis=7 (does not reuse the W command).
        # GUI widgets.py renders the objective-control page; main_window.previous/next -> move_objective(),
        # gear reduction OBJECTIVE_RATIO=132/48 * SCREW_PITCH_W_MM=1 / OBJECTIVE_HOLES=4 = 0.6875 mm/slot.
        "display_name": "Turret - expand1_axis",
        "type": "objective",
        "has_limits": False,
        "limits": (0, 3),       # 4 objectives, slots 0..3, consistent with define.py OBJECTIVE_SWITCH_MAX_INDEX=3
        # objective position display sign: move_objective() hardcodes -1 (Next=negative direction), but the GUI expects Next to show a positive value.
        # movement_sign=-1 flips the display (pos/steps/status table multiplied by sign), and makes homing home_dir=0 -> HOME_POSITIVE
        # -> new_direct=+1 consistent with EXPAND1_AXIS.homing_direct=1. Does not affect move_objective's physical direction (it does not use sign).
        "movement_sign": -1,
        "index": 5,             # firmware icID（octoaxes.ino: new Objectives(...,5,"Turret",4)）
        "actuator_screw_pitch_mm": 1.0,    # matches config.h SCREW_PITCH_OBJECTIVES_MM=1
        "actuator_microstepping": 64,      # matches config.h MICROSTEPPING_OBJECTIVES=64
    },
}

# microstep -> mm conversion factor (derived from AXIS_CONFIG, single source of truth)
# formula: mm_per_step = screwPitchMM / (fullStepsPerRev * microstepping)
# this table follows automatically when actuator_microstepping / actuator_screw_pitch_mm change,
# avoiding inconsistency with the microstep value sent by _configure_actuators()
FULLSTEPS_PER_REV = 200
AXIS_MM_PER_STEP = {
    name: cfg["actuator_screw_pitch_mm"]
        / (FULLSTEPS_PER_REV * cfg["actuator_microstepping"])
    for name, cfg in AXIS_CONFIG.items()
}

# move distance
FILTERWHEEL_DISTANCE = 0.125  # mm  2026-05-21 W units aligned with legacy Squid, removed the x100 compensation (1 slot = 1/8 turn = 0.125 mm)

# command prefix
COMMAND_PREFIXES = list(AXIS_CONFIG.keys())

# default value
DEFAULT_LOW_LIMIT = -6000  # μm
DEFAULT_HIGH_LIMIT = 6000  # μm
DEFAULT_MOVE_DISTANCE = 500  # μm

# --- illumination port config (dynamic per profile; common/gui renders from this) ---
#
# ILLUMINATION_PORTS:
# each row (port_index, display_name, pin_number). port_index and the firmware
# port_index_to_pin / port_index_to_dac_channel are strictly aligned.
#
# ILLUMINATION_DAC_CHANNELS:
# each row (dac_ch, full_scale_volt). An empty list = this profile has no DAC direct-control slider.
# octoaxes old hardware has no independent DAC direct control (DAC intensity is coupled via SET_PORT_ILLUMINATION),
# the bring-up tool is only needed for octoaxesplus.
#
# ILLUMINATION_HAS_GAIN_SWITCH / ILLUMINATION_HAS_DAC_READBACK:
# controls whether the D8 5V<->2.5V toggle button and the Read DAC Regs button are rendered.
ILLUMINATION_PORTS = [
    (0, "D1 (pin 5)",   5),
    (1, "D2 (pin 4)",   4),
    (2, "D3 (pin 22)", 22),
    (3, "D4 (pin 3)",   3),
    (4, "D5 (pin 23)", 23),
]
ILLUMINATION_DAC_CHANNELS = []          # old hardware does not expose DAC direct control
ILLUMINATION_HAS_GAIN_SWITCH  = False
ILLUMINATION_HAS_DAC_READBACK = False
