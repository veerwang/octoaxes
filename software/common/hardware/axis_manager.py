class AxisManager:
    """轴状态管理器"""

    def __init__(self):
        self.axis_status = {}
        self.last_parsed_axis = None

        # 初始化轴状态
        self.init_axes()

    def init_axes(self):
        axes = ["X", "Y", "Z", "W", "E1", "E3", "E4"]
        default_status = {
            "state": "Unknown",
            "position_mm": 0.0,
            "position_steps": 0,  # 改为整数类型
            "moving": "NO",
            "enabled": "YES",
            "limits": "0x0",
        }

        for axis in axes:
            self.axis_status[axis] = default_status.copy()

    def parse_axis_data(self, data):
        """解析轴数据，仅当内容匹配已知状态格式时返回 True"""
        for axis in self.axis_status.keys():
            prefix = f"{axis}:"
            if data.startswith(prefix):
                self.last_parsed_axis = axis
                content = data[len(prefix) :]
                return self.parse_axis_content(axis, content)
        return False

    def parse_axis_content(self, axis, content):
        """解析轴内容，返回是否匹配了已知格式"""
        if content.startswith("STATE:"):
            value = content.split(":", 1)[-1].strip()
            self.axis_status[axis]["state"] = value
            return True

        elif content.startswith("Current Position (mm):"):
            value = content.split(":", 1)[-1].strip()
            try:
                self.axis_status[axis]["position_mm"] = float(value)
            except ValueError:
                pass
            return True

        elif content.startswith("Current Position (microsteps):"):
            value = content.split(":", 1)[-1].strip()
            try:
                self.axis_status[axis]["position_steps"] = int(value)
            except ValueError:
                self.axis_status[axis]["position_steps"] = 0
            return True

        elif content.startswith("IS_MOVING:"):
            value = content.split(":", 1)[-1].strip()
            self.axis_status[axis]["moving"] = value
            return True

        elif content.startswith("IS_ENABLED:"):
            value = content.split(":", 1)[-1].strip()
            self.axis_status[axis]["enabled"] = value
            return True

        elif content.startswith("LIMIT_SWITCHES:"):
            value = content.split(":", 1)[-1].strip()
            self.axis_status[axis]["limits"] = value
            return True

        elif content.startswith("AXIS_STATUS:"):
            self.parse_combined_status(axis, content)
            return True

        elif content.startswith("EMERGENCY:"):
            value = content.split(":", 1)[-1].strip()
            self.axis_status[axis]["state"] = value
            return True

        return False

    def parse_combined_status(self, axis, content):
        """解析综合状态信息"""
        status_info = content.split(":", 1)[-1].strip()
        parts = status_info.split(" | ")

        for part in parts:
            if part.startswith("Pos:"):
                pos_str = part.replace("Pos:", "").replace("mm", "").strip()
                try:
                    self.axis_status[axis]["position_mm"] = float(pos_str)
                except ValueError:
                    pass
            elif part.startswith("Moving:"):
                moving = part.replace("Moving:", "").strip()
                self.axis_status[axis]["moving"] = moving
            elif part.startswith("Limits:"):
                limits = part.replace("Limits:", "").strip()
                self.axis_status[axis]["limits"] = limits

    def get_axis_status(self, axis):
        """获取轴状态"""
        return self.axis_status.get(axis, None)

    def update_axis_status(self, axis, status_dict):
        """更新轴状态"""
        if axis in self.axis_status:
            self.axis_status[axis].update(status_dict)

    def get_all_axes_status(self):
        """获取所有轴状态"""
        return self.axis_status.copy()
