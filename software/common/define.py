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
# Note: the objective turret was migrated in A5 to "encoder + PID closed loop + slot-calibrated
# absolute positioning" (see the main_window _objective_* method family). Motor current/hold/
# velocity/acceleration are now driven by each axis's actuator_* / default_velocity /
# default_acceleration fields in constants.py (plan A: software defaults = firmware defaults).
# The old open-loop move_objective's NEXT_SIGN / backlash compensation / RMS current constants
# were removed along with it.


def objective_slot_angle(microsteps, microstepping, fullsteps_per_rev=200):
    """物镜转换器：电机 microstep → (工位号, 物镜盘角度°)。融合 new-W-axis A5（152d50e）。

    含齿轮比 OBJECTIVE_RATIO(=2.75)：物镜盘 1 圈 = 电机 OBJECTIVE_RATIO 圈。
      物镜盘 1 圈对应电机 µstep = OBJECTIVE_RATIO × fullsteps_per_rev × microstepping
      一个工位 = 物镜盘 1 圈 / OBJECTIVE_HOLES
    返回 (slot 0..OBJECTIVE_HOLES-1, 物镜盘角度 deg)。
    """
    usteps_per_obj_rev = OBJECTIVE_RATIO * fullsteps_per_rev * microstepping
    if usteps_per_obj_rev == 0:
        return 0, 0.0
    usteps_per_slot = usteps_per_obj_rev / OBJECTIVE_HOLES
    deg = (microsteps / usteps_per_obj_rev) * 360.0
    slot = int(round(microsteps / usteps_per_slot)) % OBJECTIVE_HOLES
    return slot, deg


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
    # octoaxes mainline (6 axes: X/Y/Z/W/W2/Turret)
    "X": CMD_SET.MOVE_X,      # 0
    "Y": CMD_SET.MOVE_Y,      # 1
    "Z": CMD_SET.MOVE_Z,      # 2
    "W": CMD_SET.MOVE_W,      # 4
    "W2": CMD_SET.MOVE_W2,    # 19 (filter wheel 2 dedicated command; firmware handleMoveW2)
    "Turret": CMD_SET.MOVE_TURRET,    # 44 (objective turret dedicated command, firmware handleMoveTurret -> findAxisByName("Turret"))
    # octoaxesplus dual-camera (W1/W2 = filter wheels)
    "W1": CMD_SET.MOVE_W,     # 4 (reuses the W command; firmware handleMoveW pending a W->W1 fallback)
}

# mapping from axis name to absolute-move command (shared by both profiles)
AXIS_MOVETO_CMD_MAP = {
    "X": CMD_SET.MOVETO_X,    # 6
    "Y": CMD_SET.MOVETO_Y,    # 7
    "Z": CMD_SET.MOVETO_Z,    # 8
    "W": CMD_SET.MOVETO_W,    # 18
    "Turret": CMD_SET.MOVETO_TURRET,  # 45 (2026-05-29 objective turret dedicated command)
    "W2": CMD_SET.MOVETO_W2,  # 43 (filter wheel 2 dedicated cmd; firmware handleMoveToW2 implemented)
    # octoaxesplus (W1 reuses the W command)
    "W1": CMD_SET.MOVETO_W,   # 18 (reuses the W command; firmware handleMoveToW has a W->W1 fallback)
}
