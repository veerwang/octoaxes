SQUID_FILTERWHEEL_MAX_INDEX = 8
SQUID_FILTERWHEEL_MIN_INDEX = 1
SQUID_FILTERWHEEL_OFFSET = 0.008   # 与旧 Squid 完全一致 (硬件方向反相在 firmware 层处理，见 W_AXIS .invert_direction)

OBJECTIVE_SWITCH_MAX_INDEX = 3
OBJECTIVE_SWITCH_MIN_INDEX = 0
OBJECTIVE_HOLES = 4
OBJECTIVE_GEAR_LARGE = 132.0
OBJECTIVE_GEAR_SMALL = 48.0
OBJECTIVE_RATIO = OBJECTIVE_GEAR_LARGE / OBJECTIVE_GEAR_SMALL
SCREW_PITCH_W_MM = 1
# 注：物镜转换器已于 A5 迁到「编码器+PID 闭环 + 工位标定绝对定位」（见 main_window
# _objective_* 方法族）。电机电流/hold/速度/加速度改由 constants.py 各轴的 actuator_*
# / default_velocity / default_acceleration 字段驱动（方案A：软件默认=固件默认）。
# 旧开环 move_objective 的 NEXT_SIGN / 齿隙补偿 / RMS 电流常量随之移除。


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
    XY = 4   # 联合归位专用
    W  = 5
    W2 = 6
    TURRET = 7   # 物镜转换器（octoaxes 扩展，firmware protocolAxisToName case 7）


class LIMIT_CODE:
    """SET_LIM 命令的限位码（与固件 commandprocessor.cpp LIM_CODE_* 一致）"""
    X_POSITIVE = 0
    X_NEGATIVE = 1
    Y_POSITIVE = 2
    Y_NEGATIVE = 3
    Z_POSITIVE = 4
    Z_NEGATIVE = 5


# 轴名称 → (正向限位码, 负向限位码) 映射
AXIS_LIMIT_CODE_MAP = {
    "X": (LIMIT_CODE.X_POSITIVE, LIMIT_CODE.X_NEGATIVE),
    "Y": (LIMIT_CODE.Y_POSITIVE, LIMIT_CODE.Y_NEGATIVE),
    "Z": (LIMIT_CODE.Z_POSITIVE, LIMIT_CODE.Z_NEGATIVE),
}


class CMD_SET:
    # 相对移动命令
    MOVE_X = 0
    MOVE_Y = 1
    MOVE_Z = 2
    MOVE_THETA = 3
    MOVE_W = 4
    MOVE_W2 = 19   # octoaxesplus 新增（firmware config.h:MOVE_W2 已定义，handler 待实施）
    MOVE_TURRET = 44   # 物镜转换器(Turret)相对运动专属命令（firmware handleMoveTurret）

    # 绝对移动命令
    MOVETO_X = 6
    MOVETO_Y = 7
    MOVETO_Z = 8
    MOVETO_W = 18
    MOVETO_W2 = 43   # octoaxesplus W2 绝对运动（firmware config.h:MOVETO_W2 已加）
    MOVETO_TURRET = 45   # 物镜转换器(Turret)绝对运动专属命令（firmware handleMoveToTurret）

    # 归位命令
    HOME_OR_ZERO = 5

    # 限位设置
    SET_LIM = 9
    SET_LIM_SWITCH_POLARITY = 20

    # 照明控制
    TURN_ON_ILLUMINATION = 10
    TURN_OFF_ILLUMINATION = 11
    SET_ILLUMINATION = 12
    SET_ILLUMINATION_LED_MATRIX = 13
    SET_ILLUMINATION_INTENSITY_FACTOR = 17

    # 硬件配置
    ACK_JOYSTICK_BUTTON_PRESSED = 14
    ANALOG_WRITE_ONBOARD_DAC = 15
    SET_DAC80508_REFDIV_GAIN = 16
    CONFIGURE_STEPPER_DRIVER = 21
    SET_MAX_VELOCITY_ACCELERATION = 22
    SET_LEAD_SCREW_PITCH = 23
    SET_OFFSET_VELOCITY = 24

    # PID 控制
    CONFIGURE_STAGE_PID = 25
    ENABLE_STAGE_PID = 26
    DISABLE_STAGE_PID = 27
    SET_PID_ARGUMENTS = 29

    # 其他
    SET_HOME_SAFETY_MERGIN = 28
    SEND_HARDWARE_TRIGGER = 30
    SET_STROBE_DELAY = 31
    SET_AXIS_DISABLE_ENABLE = 32
    SET_TRIGGER_MODE = 33

    # 多端口照明命令（v1.0+）
    SET_PORT_INTENSITY = 34
    TURN_ON_PORT = 35
    TURN_OFF_PORT = 36
    SET_PORT_ILLUMINATION = 37
    SET_MULTI_PORT_MASK = 38
    TURN_OFF_ALL_PORTS = 39

    SET_PIN_LEVEL = 41

    # 初始化和重置
    INITFILTERWHEEL = 253
    INITIALIZE = 254
    RESET = 255


# 轴名称到相对移动命令的映射（两 profile 共享：octoaxes 7 轴 + octoaxesplus 5 轴的并集）
# 注意：命令码与轴索引不是简单的加法关系
AXIS_MOVE_CMD_MAP = {
    # octoaxes 主线（6 轴：X/Y/Z/W/W2/Turret）
    "X": CMD_SET.MOVE_X,      # 0
    "Y": CMD_SET.MOVE_Y,      # 1
    "Z": CMD_SET.MOVE_Z,      # 2
    "W": CMD_SET.MOVE_W,      # 4
    "W2": CMD_SET.MOVE_W2,    # 19 (滤光轮2 专属命令；firmware handleMoveW2)
    "Turret": CMD_SET.MOVE_TURRET,    # 44 (物镜转换器专属命令，firmware handleMoveTurret→findAxisByName("Turret"))
    # octoaxesplus 双相机（W1/W2 = 滤光转盘）
    "W1": CMD_SET.MOVE_W,     # 4 (复用 W 命令；firmware handleMoveW 待加 W→W1 兜底)
}

# 轴名称到绝对移动命令的映射（两 profile 共享）
AXIS_MOVETO_CMD_MAP = {
    "X": CMD_SET.MOVETO_X,    # 6
    "Y": CMD_SET.MOVETO_Y,    # 7
    "Z": CMD_SET.MOVETO_Z,    # 8
    "W": CMD_SET.MOVETO_W,    # 18
    "Turret": CMD_SET.MOVETO_TURRET,  # 45 (2026-05-29 物镜转换器专属命令)
    "W2": CMD_SET.MOVETO_W2,  # 43 (滤光轮2 专属 cmd；firmware handleMoveToW2 已实施)
    # octoaxesplus（W1 复用 W 命令）
    "W1": CMD_SET.MOVETO_W,   # 18 (复用 W 命令；firmware handleMoveToW 已加 W→W1 兜底)
}
