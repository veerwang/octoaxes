SQUID_FILTERWHEEL_MAX_INDEX = 8
SQUID_FILTERWHEEL_MIN_INDEX = 1
SQUID_FILTERWHEEL_OFFSET = 0.008   # fully consistent with legacy Squid (the hardware direction inversion is handled at the firmware level, see W_AXIS .invert_direction)

OBJECTIVE_SWITCH_MAX_INDEX = 3
OBJECTIVE_SWITCH_MIN_INDEX = 0
OBJECTIVE_HOLES = 4
OBJECTIVE_GEAR_LARGE = 132.0
OBJECTIVE_GEAR_SMALL = 48.0
OBJECTIVE_RATIO = OBJECTIVE_GEAR_LARGE / OBJECTIVE_GEAR_SMALL
SCREW_PITCH_W_MM = 1
# the motor microstep direction for the Next slot (advance one slot in the negative direction, consistent with the historical move_objective)
OBJECTIVE_NEXT_SIGN = -1
# gear-backlash compensation empirical factor (mm): when reversing, first move a little to take up the backlash, otherwise the first
# switch after reversing under-rotates and the objective drifts off the optical axis. Compensation = OBJECTIVE_RATIO * this factor / OBJECTIVE_GEAR_LARGE
# (empirical value from the veerwang objectswitch reference)
OBJECTIVE_BACKLASH_FACTOR_MM = 0.2

# objective W-axis motor parameters (explicitly sent by the host before the first switch, overriding firmware defaults for gentler motion).
# values consistent with the reference software_20260601 objective turret (the same author notes "gentler/quieter"):
# current is RMS (the CONFIGURE_STEPPER_DRIVER protocol expects RMS, not peak)
OBJECTIVE_MOTOR_CURRENT_RMS_MA = 1000
OBJECTIVE_MOTOR_I_HOLD = 0.5
OBJECTIVE_MAX_VELOCITY_MM = 0.5
OBJECTIVE_MAX_ACCELERATION_MM = 10.0


class AXIS:
    """协议轴值（与旧 Squid AXIS 类一致）"""
    X  = 0
    Y  = 1
    Z  = 2
    XY = 4   # for combined homing only
    W  = 5
    W2 = 6
    TURRET = 7   # objective turret (octoaxes extension, firmware protocolAxisToName case 7)


class LIMIT_CODE:
    """SET_LIM 命令的限位码（与固件 commandprocessor.cpp LIM_CODE_* 一致）"""
    X_POSITIVE = 0
    X_NEGATIVE = 1
    Y_POSITIVE = 2
    Y_NEGATIVE = 3
    Z_POSITIVE = 4
    Z_NEGATIVE = 5


# axis name -> (positive limit code, negative limit code) mapping
AXIS_LIMIT_CODE_MAP = {
    "X": (LIMIT_CODE.X_POSITIVE, LIMIT_CODE.X_NEGATIVE),
    "Y": (LIMIT_CODE.Y_POSITIVE, LIMIT_CODE.Y_NEGATIVE),
    "Z": (LIMIT_CODE.Z_POSITIVE, LIMIT_CODE.Z_NEGATIVE),
}


class CMD_SET:
    # relative-move commands
    MOVE_X = 0
    MOVE_Y = 1
    MOVE_Z = 2
    MOVE_THETA = 3
    MOVE_W = 4
    MOVE_W2 = 19   # new in octoaxesplus (firmware config.h:MOVE_W2 is defined, handler pending)
    MOVE_TURRET = 44   # objective turret (Turret) dedicated relative-move command (firmware handleMoveTurret)

    # absolute-move commands
    MOVETO_X = 6
    MOVETO_Y = 7
    MOVETO_Z = 8
    MOVETO_W = 18
    MOVETO_W2 = 43   # octoaxesplus W2 absolute move (firmware config.h:MOVETO_W2 added)
    MOVETO_TURRET = 45   # objective turret (Turret) dedicated absolute-move command (firmware handleMoveToTurret)

    # homing commands
    HOME_OR_ZERO = 5

    # limit settings
    SET_LIM = 9
    SET_LIM_SWITCH_POLARITY = 20

    # illumination control
    TURN_ON_ILLUMINATION = 10
    TURN_OFF_ILLUMINATION = 11
    SET_ILLUMINATION = 12
    SET_ILLUMINATION_LED_MATRIX = 13
    SET_ILLUMINATION_INTENSITY_FACTOR = 17

    # hardware configuration
    ACK_JOYSTICK_BUTTON_PRESSED = 14
    ANALOG_WRITE_ONBOARD_DAC = 15
    SET_DAC80508_REFDIV_GAIN = 16
    CONFIGURE_STEPPER_DRIVER = 21
    SET_MAX_VELOCITY_ACCELERATION = 22
    SET_LEAD_SCREW_PITCH = 23
    SET_OFFSET_VELOCITY = 24

    # PID control
    CONFIGURE_STAGE_PID = 25
    ENABLE_STAGE_PID = 26
    DISABLE_STAGE_PID = 27
    SET_PID_ARGUMENTS = 29

    # misc
    SET_HOME_SAFETY_MERGIN = 28
    SEND_HARDWARE_TRIGGER = 30
    SET_STROBE_DELAY = 31
    SET_AXIS_DISABLE_ENABLE = 32
    SET_TRIGGER_MODE = 33

    # multi-port illumination commands (v1.0+)
    SET_PORT_INTENSITY = 34
    TURN_ON_PORT = 35
    TURN_OFF_PORT = 36
    SET_PORT_ILLUMINATION = 37
    SET_MULTI_PORT_MASK = 38
    TURN_OFF_ALL_PORTS = 39

    SET_PIN_LEVEL = 41

    # initialization and reset
    INITFILTERWHEEL = 253
    INITIALIZE = 254
    RESET = 255


# mapping from axis name to relative-move command (shared by both profiles: the union of octoaxes 7 axes + octoaxesplus 5 axes)
# note: the command code and axis index are not a simple additive relationship
AXIS_MOVE_CMD_MAP = {
    # octoaxes mainline (7 axes)
    "X": CMD_SET.MOVE_X,      # 0
    "Y": CMD_SET.MOVE_Y,      # 1
    "Z": CMD_SET.MOVE_Z,      # 2
    "W": CMD_SET.MOVE_W,      # 4
    "Turret": CMD_SET.MOVE_TURRET,    # 44 (objective turret dedicated command, firmware handleMoveTurret -> findAxisByName("Turret"))
    "E3": CMD_SET.MOVE_Z,     # 2 (E3 uses the same command as Z, distinguished by axis index)
    "E4": CMD_SET.MOVE_W,     # 4 (E4 uses the same command as W, distinguished by axis index)
    # octoaxesplus dual-camera (W1/W2 = filter wheels)
    "W1": CMD_SET.MOVE_W,     # 4 (reuses the W command; firmware handleMoveW pending a W->W1 fallback)
    "W2": CMD_SET.MOVE_W2,    # 19 (dedicated command; firmware handleMoveW2 currently NOT_IMPLEMENTED, pending)
}

# mapping from axis name to absolute-move command (shared by both profiles)
AXIS_MOVETO_CMD_MAP = {
    "X": CMD_SET.MOVETO_X,    # 6
    "Y": CMD_SET.MOVETO_Y,    # 7
    "Z": CMD_SET.MOVETO_Z,    # 8
    "W": CMD_SET.MOVETO_W,    # 18
    "Turret": CMD_SET.MOVETO_TURRET,  # 45 (2026-05-29 objective turret dedicated command)
    "E3": CMD_SET.MOVETO_Z,   # 8
    "E4": CMD_SET.MOVETO_W,   # 18
    # octoaxesplus (W1/W2 temporarily reuse the W command; change once firmware provides a separate MOVETO_W2 cmd)
    "W1": CMD_SET.MOVETO_W,   # 18 (reuses the W command; firmware handleMoveToW has a W->W1 fallback)
    "W2": CMD_SET.MOVETO_W2,  # 43 (dedicated cmd; firmware handleMoveToW2 implemented)
}
