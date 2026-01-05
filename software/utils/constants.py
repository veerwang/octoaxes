"""
常量定义
"""

# 轴配置
AXIS_CONFIG = {
    "X": {
        "display_name": "Step Motor - x_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-80000, 80000),
        "movement_sign": 1,
        "index": 0,
    },
    "Y": {
        "display_name": "Step Motor - y_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-120000, 120000),
        "movement_sign": 1,
        "index": 1,
    },
    "Z": {
        "display_name": "Step Motor - z_axis",
        "type": "step_motor",
        "has_limits": True,
        "limits": (-6000, 6000),
        "movement_sign": -1,
        "index": 2,
    },
    "W": {
        "display_name": "Filter Wheel 1 - w_axis",
        "type": "filter_wheel",
        "has_limits": False,
        "limits": (0, 7),
        "movement_sign": 1,
        "index": 3,
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

# 移动距离
FILTERWHEEL_DISTANCE = 0.125 * 100  # mm

# 命令前缀
COMMAND_PREFIXES = list(AXIS_CONFIG.keys())

# 默认值
DEFAULT_LOW_LIMIT = -6000  # μm
DEFAULT_HIGH_LIMIT = 6000  # μm
DEFAULT_MOVE_DISTANCE = 500  # μm
