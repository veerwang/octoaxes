from PyQt5.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QLabel,
    QLineEdit,
    QTextEdit,
    QGroupBox,
    QGridLayout,
    QComboBox,
    QCheckBox,
    QStackedWidget,
)
from PyQt5.QtCore import pyqtSignal, Qt, QTimer
from PyQt5.QtGui import QIntValidator, QDoubleValidator, QFont

from utils.constants import AXIS_CONFIG


class AxisStatusDisplay(QGroupBox):
    """所有轴状态显示组件"""

    refresh_clicked = pyqtSignal()

    def __init__(self):
        super().__init__("All Axes Status")
        self.setStyleSheet("QGroupBox { font-weight: bold; font-size: 16px; }")

        self.axis_labels = {}
        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout(self)

        # 状态网格
        grid_widget = QWidget()
        self.grid = QGridLayout(grid_widget)
        self.create_header()
        self.create_axis_rows()

        layout.addWidget(grid_widget)

        # 刷新按钮
        refresh_layout = QHBoxLayout()
        refresh_btn = QPushButton("Refresh All Axes")
        refresh_btn.clicked.connect(self.refresh_clicked.emit)

        self.auto_poll_check = QCheckBox("Enable auto-poll")
        self.auto_poll_check.setChecked(False)  # 默认不选

        refresh_layout.addWidget(refresh_btn)
        refresh_layout.addWidget(self.auto_poll_check)

        refresh_layout.addStretch()
        layout.addLayout(refresh_layout)

    def create_header(self):
        headers = ["Axis", "State", "Position (mm)", "Moving", "Enabled", "Limits"]
        for col, header in enumerate(headers):
            label = QLabel(header)
            label.setStyleSheet(
                "font-weight: bold; background-color: #e0e0e0; padding: 5px;"
            )
            label.setAlignment(Qt.AlignCenter)
            self.grid.addWidget(label, 0, col)

    def create_axis_rows(self):
        for row, (axis_id, config) in enumerate(AXIS_CONFIG.items(), start=1):
            # 轴名称
            name_label = QLabel(config["display_name"])
            name_label.setStyleSheet("font-weight: bold; padding: 5px;")
            self.grid.addWidget(name_label, row, 0)

            # 状态标签
            self.axis_labels[axis_id] = {
                "state": self.create_status_label(),
                "position": self.create_status_label("0.000"),
                "moving": self.create_status_label("NO"),
                "enabled": self.create_status_label("YES"),
                "limits": self.create_status_label("0x0"),
            }

            # 添加到网格
            for col, key in enumerate(
                ["state", "position", "moving", "enabled", "limits"], start=1
            ):
                self.grid.addWidget(self.axis_labels[axis_id][key], row, col)

    def create_status_label(self, text="Unknown"):
        label = QLabel(text)
        label.setStyleSheet("padding: 5px; background-color: #f8f8f8;")
        label.setAlignment(Qt.AlignCenter)
        return label

    def update_axis_status(self, axis, status):
        if axis not in self.axis_labels:
            return

        labels = self.axis_labels[axis]

        # 更新状态
        if "state" in status:
            labels["state"].setText(status["state"])
            self.set_state_color(labels["state"], status["state"])

        if "position_mm" in status:
            value = status["position_mm"] * AXIS_CONFIG[axis]["movement_sign"]
            labels["position"].setText(f"{value}")

        if "moving" in status:
            labels["moving"].setText(status["moving"])
            self.set_moving_color(labels["moving"], status["moving"])

        if "enabled" in status:
            labels["enabled"].setText(status["enabled"])
            self.set_enabled_color(labels["enabled"], status["enabled"])

        if "limits" in status:
            labels["limits"].setText(status["limits"])

    def set_state_color(self, label, state):
        colors = {"IDLE": "#d4ffd4", "MOVING": "#d4e8ff", "ERROR": "#ffd4d4"}
        color = colors.get(state, "#fff8d4")
        label.setStyleSheet(f"background-color: {color}; padding: 5px;")

    def set_moving_color(self, label, moving):
        if moving == "YES":
            label.setStyleSheet(
                "background-color: #d4e8ff; color: blue; font-weight: bold; padding: 5px;"
            )
        else:
            label.setStyleSheet(
                "background-color: #f8f8f8; color: black; padding: 5px;"
            )

    def set_enabled_color(self, label, enabled):
        if enabled == "YES":
            label.setStyleSheet(
                "background-color: #d4ffd4; color: green; font-weight: bold; padding: 5px;"
            )
        else:
            label.setStyleSheet(
                "background-color: #ffd4d4; color: red; font-weight: bold; padding: 5px;"
            )


class ControlPanel(QGroupBox):
    """控制面板组件 - 添加使能/禁用按钮"""

    homing_clicked = pyqtSignal()
    reset_clicked = pyqtSignal()
    limits_set_clicked = pyqtSignal()
    forward_clicked = pyqtSignal()
    backward_clicked = pyqtSignal()
    previous_clicked = pyqtSignal()
    next_clicked = pyqtSignal()
    test_clicked = pyqtSignal()
    move_absolute_clicked = pyqtSignal(float)  # 单位 um，无论轴类型都统一为um

    enable_toggled = pyqtSignal(bool)  # 新增：使能状态切换信号
    axis_changed = pyqtSignal(str)

    def __init__(self):
        super().__init__("Motor Control")
        self.current_axis = "X"
        self.is_switching = False
        self.axis_enabled = True  # 默认使能状态为启用

        # 存储不同轴类型的移动距离
        self.um_distance_values = {}  # Z和E3轴的um值
        self.mm_distance_values = {}  # X和Y轴的mm值
        # 存储不同轴类型的绝对位置值（um单位）
        self.abs_um_values = {}  # Z和E3轴的um绝对位置值
        self.abs_mm_values = {}  # X和Y轴的mm绝对位置值

        self.init_ui()
        # 初始设置，延迟执行以确保UI完全加载
        QTimer.singleShot(100, lambda: self.set_current_axis(self.current_axis))

    def init_ui(self):
        layout = QVBoxLayout(self)

        # 使用堆叠控件
        self.stacked_widget = QStackedWidget()

        # 创建两个控制页面
        self.normal_control_page = self.create_normal_control_page()
        self.filter_control_page = self.create_filter_control_page()

        # 添加到堆叠控件
        self.stacked_widget.addWidget(self.normal_control_page)
        self.stacked_widget.addWidget(self.filter_control_page)

        layout.addWidget(self.stacked_widget)

        # 连接切换信号
        self.stacked_widget.currentChanged.connect(self.on_page_changed)

    def create_normal_control_page(self):
        """创建普通步进电机控制页面"""
        page = QWidget()
        layout = QVBoxLayout(page)

        # 使能/禁用按钮 - 普通轴页面
        self.enable_btn_normal = QPushButton("Disable Axis")
        self.enable_btn_normal.setStyleSheet(
            "background-color: orange; font-weight: bold;"
        )
        self.enable_btn_normal.clicked.connect(self.toggle_enable)
        layout.addWidget(self.enable_btn_normal)

        # 限位设置
        limit_widget = self.create_limit_widget()
        layout.addWidget(limit_widget)

        # 功能按钮
        self.home_btn_normal = QPushButton("Homing")
        self.home_btn_normal.clicked.connect(self.emit_homing)
        layout.addWidget(self.home_btn_normal)

        self.reset_btn_normal = QPushButton("Reset")
        self.reset_btn_normal.clicked.connect(self.emit_reset)
        layout.addWidget(self.reset_btn_normal)

        # ====== 移动距离设置 - 为不同轴类型提供不同输入 ======
        # 1. Z和E3轴使用：Move Distance (um, 1-1000)
        self.um_distance_widget = QWidget()
        um_layout = QHBoxLayout(self.um_distance_widget)
        um_layout.addWidget(QLabel("Move Distance (um, 1-1000):"))
        self.distance_input_um = QLineEdit("500")
        self.distance_input_um.setValidator(QIntValidator(1, 1000))
        self.distance_input_um.setMaximumWidth(80)
        um_layout.addWidget(self.distance_input_um)
        um_layout.addStretch()
        layout.addWidget(self.um_distance_widget)

        # 2. X和Y轴使用：Move Relative Distance (mm, 1-120)
        self.mm_distance_widget = QWidget()
        mm_layout = QHBoxLayout(self.mm_distance_widget)
        mm_layout.addWidget(QLabel("Move Relative Distance (mm, 0.1-120):"))
        self.distance_input_mm = QLineEdit("1.0")
        # 设置验证器：允许小数，范围0.1-120，小数点后最多1位
        validator = QDoubleValidator(0.1, 120.0, 1)
        validator.setNotation(QDoubleValidator.StandardNotation)
        self.distance_input_mm.setValidator(validator)
        self.distance_input_mm.setMaximumWidth(80)
        # 设置字体，确保数字清晰显示
        font = QFont("Arial", 14)
        self.distance_input_mm.setFont(font)
        mm_layout.addWidget(self.distance_input_mm)
        mm_layout.addStretch()
        layout.addWidget(self.mm_distance_widget)

        # 初始时都隐藏，在set_current_axis中根据轴类型显示
        self.um_distance_widget.setVisible(False)
        self.mm_distance_widget.setVisible(False)

        # 移动按钮
        btn_layout = QHBoxLayout()
        self.forward_btn = QPushButton("Forward")
        self.forward_btn.clicked.connect(self.emit_forward)
        btn_layout.addWidget(self.forward_btn)

        self.backward_btn = QPushButton("Backward")
        self.backward_btn.clicked.connect(self.emit_backward)
        btn_layout.addWidget(self.backward_btn)
        layout.addLayout(btn_layout)

        # >>> 绝对位置控件
        self.abs_widget = QWidget()
        abs_layout = QHBoxLayout(self.abs_widget)
        # 标签和输入框将在set_current_axis中根据轴类型设置
        self.abs_pos_label = QLabel("Absolute Position:")
        self.abs_pos_edit = QLineEdit("0.0")
        self.abs_pos_edit.setMaximumWidth(100)
        self.abs_move_btn = QPushButton("MoveTo")
        self.abs_move_btn.clicked.connect(self.emit_absolute_move)
        abs_layout.addWidget(self.abs_pos_label)
        abs_layout.addWidget(self.abs_pos_edit)
        abs_layout.addWidget(self.abs_move_btn)
        abs_layout.addStretch()
        layout.addWidget(self.abs_widget)

        self.abs_widget.setVisible(True)

        layout.addStretch()
        return page

    def create_filter_control_page(self):
        """创建FilterWheel控制页面"""
        page = QWidget()
        layout = QVBoxLayout(page)

        # 使能/禁用按钮 - FilterWheel页面
        self.enable_btn_filter = QPushButton("Disable Axis")
        self.enable_btn_filter.setStyleSheet(
            "background-color: orange; font-weight: bold;"
        )
        self.enable_btn_filter.clicked.connect(self.toggle_enable)
        layout.addWidget(self.enable_btn_filter)

        # 占位空间
        placeholder = QLabel("Filter Wheel / Objective Control")
        placeholder.setAlignment(Qt.AlignCenter)
        placeholder.setStyleSheet("color: gray; font-style: italic; padding: 10px;")
        layout.addWidget(placeholder)

        layout.addStretch()

        # 功能按钮
        self.home_btn_filter = QPushButton("Homing")
        self.home_btn_filter.clicked.connect(self.emit_homing)
        layout.addWidget(self.home_btn_filter)

        self.reset_btn_filter = QPushButton("Reset")
        self.reset_btn_filter.clicked.connect(self.emit_reset)
        layout.addWidget(self.reset_btn_filter)

        layout.addStretch()

        # FilterWheel专用按钮
        filter_btn_layout = QHBoxLayout()
        self.previous_btn = QPushButton("Previous Position")
        self.previous_btn.clicked.connect(self.emit_previous)
        filter_btn_layout.addWidget(self.previous_btn)

        self.next_btn = QPushButton("Next Position")
        self.next_btn.clicked.connect(self.emit_next)
        filter_btn_layout.addWidget(self.next_btn)

        self.test_btn = QPushButton("Test")
        self.test_btn.clicked.connect(self.emit_test)
        filter_btn_layout.addWidget(self.test_btn)
        layout.addLayout(filter_btn_layout)

        layout.addStretch()
        return page

    def create_limit_widget(self):
        """创建限位设置控件"""
        widget = QWidget()
        layout = QHBoxLayout(widget)

        layout.addWidget(QLabel("Low Limit (μm):"))
        self.low_limit_edit = QLineEdit("-6000")
        self.low_limit_edit.setValidator(QIntValidator(-120000, 120000))
        self.low_limit_edit.setMaximumWidth(80)
        layout.addWidget(self.low_limit_edit)

        layout.addWidget(QLabel("Up Limit (μm):"))
        self.high_limit_edit = QLineEdit("6000")
        self.high_limit_edit.setValidator(QIntValidator(-120000, 120000))
        self.high_limit_edit.setMaximumWidth(80)
        layout.addWidget(self.high_limit_edit)

        self.set_limits_btn = QPushButton("Set Limit")
        self.set_limits_btn.clicked.connect(self.emit_set_limits)
        layout.addWidget(self.set_limits_btn)
        layout.addStretch()

        return widget

    def set_axis_limits(self, low, high):
        self.low_limit_edit.setText(str(low))
        self.high_limit_edit.setText(str(high))

    def toggle_enable(self):
        """切换使能状态"""
        if not self.is_switching:
            # 切换状态
            self.axis_enabled = not self.axis_enabled

            # 更新按钮文本和颜色
            if self.axis_enabled:
                btn_text = "Disable Axis"
                btn_color = "orange"
            else:
                btn_text = "Enable Axis"
                btn_color = "green"

            # 更新两个页面上的按钮
            self.enable_btn_normal.setText(btn_text)
            self.enable_btn_normal.setStyleSheet(
                f"background-color: {btn_color}; font-weight: bold;"
            )

            self.enable_btn_filter.setText(btn_text)
            self.enable_btn_filter.setStyleSheet(
                f"background-color: {btn_color}; font-weight: bold;"
            )

            # 发射信号
            self.enable_toggled.emit(self.axis_enabled)

    def set_enable_state(self, enabled):
        """设置使能状态"""
        self.axis_enabled = enabled

        # 更新按钮文本和颜色
        if self.axis_enabled:
            btn_text = "Disable Axis"
            btn_color = "orange"
        else:
            btn_text = "Enable Axis"
            btn_color = "green"

        # 更新按钮
        self.enable_btn_normal.setText(btn_text)
        self.enable_btn_normal.setStyleSheet(
            f"background-color: {btn_color}; font-weight: bold;"
        )

        self.enable_btn_filter.setText(btn_text)
        self.enable_btn_filter.setStyleSheet(
            f"background-color: {btn_color}; font-weight: bold;"
        )

    def emit_homing(self):
        """发射归零信号"""
        if not self.is_switching and self.axis_enabled:
            self.homing_clicked.emit()

    def emit_reset(self):
        """发射重置信号"""
        if not self.is_switching:
            self.reset_clicked.emit()

    def emit_set_limits(self):
        """发射设置限位信号"""
        if not self.is_switching:
            self.limits_set_clicked.emit()

    def emit_forward(self):
        """发射前进信号"""
        if not self.is_switching and self.axis_enabled:
            self.forward_clicked.emit()

    def emit_backward(self):
        """发射后退信号"""
        if not self.is_switching and self.axis_enabled:
            self.backward_clicked.emit()

    def emit_previous(self):
        """发射上一个位置信号"""
        if not self.is_switching and self.axis_enabled:
            self.previous_clicked.emit()

    def emit_next(self):
        """发射下一个位置信号"""
        if not self.is_switching and self.axis_enabled:
            self.next_clicked.emit()

    def emit_test(self):
        """发射测试信号"""
        if not self.is_switching and self.axis_enabled:
            self.test_clicked.emit()

    def emit_absolute_move(self):
        """发射绝对位置移动信号（单位 um）"""
        try:
            # 读取文本值
            text = self.abs_pos_edit.text()

            # 根据当前轴类型转换单位
            if self.current_axis in ["Z", "E3"]:
                # Z和E3轴：单位是um，直接转换为整数
                pos_um = int(float(text))
            elif self.current_axis in ["X", "Y"]:
                # X和Y轴：单位是mm，需要转换为um
                pos_mm = float(text)
                pos_um = int(pos_mm * 1000)
            else:
                # 其他轴使用默认处理
                pos_um = int(float(text))

            # 发射信号（单位um）
            self.move_absolute_clicked.emit(pos_um)
        except ValueError:
            pass

    def on_page_changed(self, index):
        """页面切换时的处理"""
        pass

    def set_current_axis(self, axis):
        """设置当前轴"""
        if self.is_switching:
            return

        self.is_switching = True
        self.current_axis = axis

        # 用 AXIS_CONFIG 读取限位
        low, high = AXIS_CONFIG[self.current_axis]["limits"]
        self.set_axis_limits(low, high)

        try:
            # 根据轴类型判断显示哪个控制页面
            if axis in ["E4", "W", "E1"]:
                # FilterWheel轴 - 显示第1页
                target_index = 1
            else:
                # 普通步进电机轴 - 显示第0页
                target_index = 0

            # 只有页面需要切换时才执行
            if self.stacked_widget.currentIndex() != target_index:
                self.stacked_widget.setCurrentIndex(target_index)
                # 给UI时间更新
                QTimer.singleShot(10, lambda: self.axis_changed.emit(axis))
            else:
                self.axis_changed.emit(axis)

            # 根据轴类型显示相应的距离输入控件（仅在普通控制页面）
            if target_index == 0:  # 普通控制页面
                if axis in ["Z", "E3"]:
                    # Z和E3轴：显示um输入，隐藏mm输入
                    self.um_distance_widget.setVisible(True)
                    self.mm_distance_widget.setVisible(False)

                    # 恢复之前保存的值或使用默认值
                    if axis in self.um_distance_values:
                        self.distance_input_um.setText(
                            str(self.um_distance_values[axis])
                        )
                    else:
                        self.distance_input_um.setText("500")

                    # 设置绝对位置控件为um单位
                    self.abs_pos_label.setText("Absolute Position (μm):")
                    self.abs_pos_edit.setValidator(QIntValidator(-120000, 120000))
                    if axis in self.abs_um_values:
                        self.abs_pos_edit.setText(str(self.abs_um_values[axis]))
                    else:
                        self.abs_pos_edit.setText("0")

                elif axis in ["X", "Y"]:
                    # X和Y轴：显示mm输入，隐藏um输入
                    self.um_distance_widget.setVisible(False)
                    self.mm_distance_widget.setVisible(True)

                    # 恢复之前保存的值或使用默认值
                    if axis in self.mm_distance_values:
                        self.distance_input_mm.setText(
                            str(self.mm_distance_values[axis])
                        )
                    else:
                        self.distance_input_mm.setText("1.0")

                    # 设置绝对位置控件为mm单位
                    self.abs_pos_label.setText("Absolute Position (mm):")
                    validator = QDoubleValidator(-120.0, 120.0, 3)
                    validator.setNotation(QDoubleValidator.StandardNotation)
                    self.abs_pos_edit.setValidator(validator)
                    if axis in self.abs_mm_values:
                        self.abs_pos_edit.setText(str(self.abs_mm_values[axis]))
                    else:
                        self.abs_pos_edit.setText("0.000")

        except Exception as e:
            print(f"Error switching control panel: {e}")
        finally:
            QTimer.singleShot(50, lambda: setattr(self, "is_switching", False))

    def get_limits(self):
        """获取限位值"""
        try:
            low = int(self.low_limit_edit.text())
            high = int(self.high_limit_edit.text())
            if low >= high:
                return None
            return low, high
        except ValueError:
            return None

    def get_move_distance(self):
        """获取移动距离（转换为um单位）"""
        try:
            # 根据当前轴类型选择相应的输入控件
            if self.current_axis in ["Z", "E3"]:
                # Z和E3轴：输入的是um，直接返回
                distance = int(self.distance_input_um.text())
                if 1 <= distance <= 1000:
                    # 保存当前值
                    self.um_distance_values[self.current_axis] = distance
                    return distance  # 已经是um单位
                return None
            elif self.current_axis in ["X", "Y"]:
                # X和Y轴：输入的是mm，需要转换为um（乘以1000）
                distance_mm = float(self.distance_input_mm.text())
                if 0 < distance_mm <= 120.0:
                    # 保存当前值
                    self.mm_distance_values[self.current_axis] = distance_mm
                    # 转换为um（乘以1000并取整）
                    distance_um = int(distance_mm * 1000)
                    return distance_um
                return None
            else:
                # 其他轴类型，使用默认的um输入
                distance = int(self.distance_input_um.text())
                if 1 <= distance <= 1000:
                    return distance
                return None
        except (ValueError, AttributeError):
            return None


class LogDisplay(QGroupBox):
    """日志显示组件"""

    clear_clicked = pyqtSignal()

    def __init__(self, title):
        super().__init__(title)
        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout(self)

        # 标题栏
        title_layout = QHBoxLayout()
        title_layout.addWidget(QLabel(f"{self.title()}:"))
        title_layout.addStretch()

        clear_btn = QPushButton(f"Clear {self.title()}")
        clear_btn.clicked.connect(self.clear_clicked.emit)
        title_layout.addWidget(clear_btn)

        layout.addLayout(title_layout)

        # 文本显示
        self.text_edit = QTextEdit()
        self.text_edit.setReadOnly(True)
        if "Sent" in self.title():
            self.text_edit.setMaximumHeight(120)
        layout.addWidget(self.text_edit)

    def append(self, text):
        self.text_edit.append(text)

    def clear(self):
        self.text_edit.clear()
