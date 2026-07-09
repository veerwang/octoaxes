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
    QSpinBox,
    QSlider,
    QFrame,
)
from PyQt5.QtCore import pyqtSignal, Qt, QTimer
from PyQt5.QtGui import QIntValidator, QDoubleValidator, QFont, QColor

from utils.constants import AXIS_CONFIG

# illumination port metadata (the profile decides the number of ports/DAC/GAIN etc.). Provide safe defaults when the profile does not define them
# to keep backward compatibility (before the old octoaxes constants.py is upgraded)
try:
    from utils.constants import (
        ILLUMINATION_PORTS,
        ILLUMINATION_DAC_CHANNELS,
        ILLUMINATION_HAS_GAIN_SWITCH,
        ILLUMINATION_HAS_DAC_READBACK,
    )
except ImportError:
    ILLUMINATION_PORTS = [
        (0, "D1 (pin 5)",   5),
        (1, "D2 (pin 4)",   4),
        (2, "D3 (pin 22)", 22),
        (3, "D4 (pin 3)",   3),
        (4, "D5 (pin 23)", 23),
    ]
    ILLUMINATION_DAC_CHANNELS = []
    ILLUMINATION_HAS_GAIN_SWITCH  = False
    ILLUMINATION_HAS_DAC_READBACK = False


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

        # status grid
        grid_widget = QWidget()
        self.grid = QGridLayout(grid_widget)
        self.create_header()
        self.create_axis_rows()

        layout.addWidget(grid_widget)

        # refresh button
        refresh_layout = QHBoxLayout()
        refresh_btn = QPushButton("Refresh All Axes")
        refresh_btn.clicked.connect(self.refresh_clicked.emit)

        self.auto_poll_check = QCheckBox("Enable auto-poll")
        self.auto_poll_check.setChecked(False)  # unchecked by default

        refresh_layout.addWidget(refresh_btn)
        refresh_layout.addWidget(self.auto_poll_check)

        refresh_layout.addStretch()
        layout.addLayout(refresh_layout)

    def create_header(self):
        headers = ["Axis", "Driver", "State", "Position (mm)", "Moving", "Enabled", "Limits"]
        for col, header in enumerate(headers):
            label = QLabel(header)
            label.setStyleSheet(
                "font-weight: bold; background-color: #e0e0e0; padding: 5px;"
            )
            label.setAlignment(Qt.AlignCenter)
            self.grid.addWidget(label, 0, col)

    def create_axis_rows(self):
        for row, (axis_id, config) in enumerate(AXIS_CONFIG.items(), start=1):
            # axis name
            name_label = QLabel(config["display_name"])
            name_label.setStyleSheet("font-weight: bold; padding: 5px;")
            self.grid.addWidget(name_label, row, 0)

            # status label
            self.axis_labels[axis_id] = {
                "driver": self.create_status_label("-"),
                "state": self.create_status_label(),
                "position": self.create_status_label("0.000"),
                "moving": self.create_status_label("NO"),
                "enabled": self.create_status_label("YES"),
                "limits": self.create_status_label("0x0"),
            }

            # add to the grid
            for col, key in enumerate(
                ["driver", "state", "position", "moving", "enabled", "limits"], start=1
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

        # update the status
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

        if "driver" in status:
            labels["driver"].setText(status["driver"])

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
    move_absolute_clicked = pyqtSignal(float)  # unit um; unified to um regardless of axis type

    enable_toggled = pyqtSignal(bool)  # added: enable-state toggle signal
    axis_changed = pyqtSignal(str)
    velocity_accel_set = pyqtSignal(float, float)  # vel_mm_s, acc_mm_s2

    def __init__(self):
        super().__init__("Motor Control")
        self.current_axis = "X"
        self.is_switching = False
        self.axis_enabled = True  # default enable state is enabled

        # store the move distance per axis type
        self.um_distance_values = {}  # um values for the Z and E3 axes
        self.mm_distance_values = {}  # mm values for the X and Y axes
        # store the absolute position per axis type (um)
        self.abs_um_values = {}  # um absolute positions for the Z and E3 axes
        self.abs_mm_values = {}  # mm absolute positions for the X and Y axes
        # store velocity/acceleration per axis
        self.vel_values = {}
        self.acc_values = {}

        self.init_ui()
        # initial setup, deferred to ensure the UI is fully loaded
        QTimer.singleShot(100, lambda: self.set_current_axis(self.current_axis))

    def init_ui(self):
        layout = QVBoxLayout(self)

        # use a stacked widget
        self.stacked_widget = QStackedWidget()

        # create two control pages
        self.normal_control_page = self.create_normal_control_page()
        self.filter_control_page = self.create_filter_control_page()

        # add to the stacked widget
        self.stacked_widget.addWidget(self.normal_control_page)
        self.stacked_widget.addWidget(self.filter_control_page)

        layout.addWidget(self.stacked_widget)

        # connect the toggle signal
        self.stacked_widget.currentChanged.connect(self.on_page_changed)

    def create_normal_control_page(self):
        """创建普通步进电机控制页面"""
        page = QWidget()
        layout = QVBoxLayout(page)

        # enable/disable button - normal-axis page
        self.enable_btn_normal = QPushButton("Disable Axis")
        self.enable_btn_normal.setStyleSheet(
            "background-color: orange; font-weight: bold;"
        )
        self.enable_btn_normal.clicked.connect(self.toggle_enable)
        layout.addWidget(self.enable_btn_normal)

        # limit settings
        limit_widget = self.create_limit_widget()
        layout.addWidget(limit_widget)

        # function buttons
        self.home_btn_normal = QPushButton("Homing")
        self.home_btn_normal.clicked.connect(self.emit_homing)
        layout.addWidget(self.home_btn_normal)

        self.reset_btn_normal = QPushButton("Reset")
        self.reset_btn_normal.clicked.connect(self.emit_reset)
        layout.addWidget(self.reset_btn_normal)

        # ====== move-distance settings - different inputs per axis type ======
        # 1. Z and E3 axes use: Move Distance (um, 1-1000)
        self.um_distance_widget = QWidget()
        um_layout = QHBoxLayout(self.um_distance_widget)
        um_layout.addWidget(QLabel("Move Distance (um, 1-1000):"))
        self.distance_input_um = QLineEdit("500")
        self.distance_input_um.setValidator(QIntValidator(1, 1000))
        self.distance_input_um.setMaximumWidth(80)
        um_layout.addWidget(self.distance_input_um)
        um_layout.addStretch()
        layout.addWidget(self.um_distance_widget)

        # 2. X and Y axes use: Move Relative Distance (mm, 1-120)
        self.mm_distance_widget = QWidget()
        mm_layout = QHBoxLayout(self.mm_distance_widget)
        mm_layout.addWidget(QLabel("Move Relative Distance (mm, 0.1-120):"))
        self.distance_input_mm = QLineEdit("1.0")
        # set a validator: allow decimals, range 0.1-120, at most 1 decimal place
        validator = QDoubleValidator(0.1, 120.0, 1)
        validator.setNotation(QDoubleValidator.StandardNotation)
        self.distance_input_mm.setValidator(validator)
        self.distance_input_mm.setMaximumWidth(80)
        # set the font to ensure digits display clearly
        font = QFont("Arial", 14)
        self.distance_input_mm.setFont(font)
        mm_layout.addWidget(self.distance_input_mm)
        mm_layout.addStretch()
        layout.addWidget(self.mm_distance_widget)

        # hidden initially; shown by axis type in set_current_axis
        self.um_distance_widget.setVisible(False)
        self.mm_distance_widget.setVisible(False)

        # move buttons
        btn_layout = QHBoxLayout()
        self.forward_btn = QPushButton("Forward")
        self.forward_btn.clicked.connect(self.emit_forward)
        btn_layout.addWidget(self.forward_btn)

        self.backward_btn = QPushButton("Backward")
        self.backward_btn.clicked.connect(self.emit_backward)
        btn_layout.addWidget(self.backward_btn)
        layout.addLayout(btn_layout)

        # >>> absolute-position widgets
        self.abs_widget = QWidget()
        abs_layout = QHBoxLayout(self.abs_widget)
        # the label and input field are set by axis type in set_current_axis
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

        # velocity / acceleration settings
        vel_acc_layout = QHBoxLayout()
        vel_acc_layout.addWidget(QLabel("Vel (mm/s):"))
        self.vel_input = QLineEdit("25.0")
        self.vel_input.setMaximumWidth(65)
        v = QDoubleValidator(0.01, 655.0, 2)
        v.setNotation(QDoubleValidator.StandardNotation)
        self.vel_input.setValidator(v)
        vel_acc_layout.addWidget(self.vel_input)

        vel_acc_layout.addWidget(QLabel("Acc (mm/s²):"))
        self.acc_input = QLineEdit("500.0")
        self.acc_input.setMaximumWidth(70)
        a = QDoubleValidator(0.1, 6553.0, 1)
        a.setNotation(QDoubleValidator.StandardNotation)
        self.acc_input.setValidator(a)
        vel_acc_layout.addWidget(self.acc_input)

        self.vel_acc_apply_btn = QPushButton("Apply")
        self.vel_acc_apply_btn.setMaximumWidth(55)
        self.vel_acc_apply_btn.clicked.connect(self.emit_vel_acc)
        vel_acc_layout.addWidget(self.vel_acc_apply_btn)
        vel_acc_layout.addStretch()
        layout.addLayout(vel_acc_layout)

        layout.addStretch()
        return page

    def create_filter_control_page(self):
        """创建FilterWheel控制页面"""
        page = QWidget()
        layout = QVBoxLayout(page)

        # enable/disable button - FilterWheel page
        self.enable_btn_filter = QPushButton("Disable Axis")
        self.enable_btn_filter.setStyleSheet(
            "background-color: orange; font-weight: bold;"
        )
        self.enable_btn_filter.clicked.connect(self.toggle_enable)
        layout.addWidget(self.enable_btn_filter)

        # spacer
        placeholder = QLabel("Filter Wheel / Objective Control")
        placeholder.setAlignment(Qt.AlignCenter)
        placeholder.setStyleSheet("color: gray; font-style: italic; padding: 10px;")
        layout.addWidget(placeholder)

        layout.addStretch()

        # function buttons
        self.home_btn_filter = QPushButton("Homing")
        self.home_btn_filter.clicked.connect(self.emit_homing)
        layout.addWidget(self.home_btn_filter)

        self.reset_btn_filter = QPushButton("Reset")
        self.reset_btn_filter.clicked.connect(self.emit_reset)
        layout.addWidget(self.reset_btn_filter)

        layout.addStretch()

        # FilterWheel-specific buttons
        filter_btn_layout = QHBoxLayout()
        self.previous_btn = QPushButton("Previous Position")
        self.previous_btn.clicked.connect(self.emit_previous)
        filter_btn_layout.addWidget(self.previous_btn)

        self.next_btn = QPushButton("Next Position")
        self.next_btn.clicked.connect(self.emit_next)
        filter_btn_layout.addWidget(self.next_btn)

        filter_btn_layout.addStretch()

        self.rounds_label = QLabel("Rounds:")
        filter_btn_layout.addWidget(self.rounds_label)

        self.test_rounds_spin = QSpinBox()
        self.test_rounds_spin.setRange(1, 10)
        self.test_rounds_spin.setValue(2)
        self.test_rounds_spin.setMinimumWidth(50)
        filter_btn_layout.addWidget(self.test_rounds_spin)

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
            # toggle the state
            self.axis_enabled = not self.axis_enabled

            # update the button text and color
            if self.axis_enabled:
                btn_text = "Disable Axis"
                btn_color = "orange"
            else:
                btn_text = "Enable Axis"
                btn_color = "green"

            # update the buttons on both pages
            self.enable_btn_normal.setText(btn_text)
            self.enable_btn_normal.setStyleSheet(
                f"background-color: {btn_color}; font-weight: bold;"
            )

            self.enable_btn_filter.setText(btn_text)
            self.enable_btn_filter.setStyleSheet(
                f"background-color: {btn_color}; font-weight: bold;"
            )

            # emit the signal
            self.enable_toggled.emit(self.axis_enabled)

    def set_enable_state(self, enabled):
        """设置使能状态"""
        self.axis_enabled = enabled

        # update the button text and color
        if self.axis_enabled:
            btn_text = "Disable Axis"
            btn_color = "orange"
        else:
            btn_text = "Enable Axis"
            btn_color = "green"

        # update the button
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
            # read the text value
            text = self.abs_pos_edit.text()

            # convert units based on the current axis type
            if self.current_axis in ["Z", "E3"]:
                # Z and E3 axes: unit is um, convert directly to an integer
                pos_um = int(float(text))
            elif self.current_axis in ["X", "Y"]:
                # X and Y axes: unit is mm, needs conversion to um
                pos_mm = float(text)
                pos_um = int(pos_mm * 1000)
            else:
                # other axes use default handling
                pos_um = int(float(text))

            # emit the signal (unit um)
            self.move_absolute_clicked.emit(pos_um)
        except ValueError:
            pass

    def emit_vel_acc(self):
        """发射速度/加速度设置信号"""
        try:
            vel = float(self.vel_input.text())
            acc = float(self.acc_input.text())
            if vel > 0 and acc > 0:
                self.vel_values[self.current_axis] = vel
                self.acc_values[self.current_axis] = acc
                self.velocity_accel_set.emit(vel, acc)
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

        # read the limits from AXIS_CONFIG
        low, high = AXIS_CONFIG[self.current_axis]["limits"]
        self.set_axis_limits(low, high)

        try:
            # decide which control page to show based on axis type
            # decide dynamically using AXIS_CONFIG[axis]["type"], follows the profile (octoaxes/octoaxesplus)
            axis_type = AXIS_CONFIG.get(axis, {}).get("type", "step_motor")
            if axis_type in ("filter_wheel", "objective"):
                # FilterWheel / Objectives axes - show page 1 (filter wheel / objective control)
                target_index = 1
                # the Rounds + Test buttons were originally for the filter-wheel auto-test (Next x7 -> Previous x7) x N rounds
                # (see main_window.run_w_test). An objective has 4 lenses, not 8 slots, so it does not apply; shown only for filter_wheel.
                is_filter_wheel = (axis_type == "filter_wheel")
                self.test_btn.setVisible(is_filter_wheel)
                self.test_rounds_spin.setVisible(is_filter_wheel)
                self.rounds_label.setVisible(is_filter_wheel)
            else:
                # normal stepper-motor axes - show page 0
                target_index = 0

            # only run when the page actually needs to change
            if self.stacked_widget.currentIndex() != target_index:
                self.stacked_widget.setCurrentIndex(target_index)
                # give the UI time to update
                QTimer.singleShot(10, lambda: self.axis_changed.emit(axis))
            else:
                self.axis_changed.emit(axis)

            # show the matching distance-input widget by axis type (only on the normal control page)
            if target_index == 0:  # normal control page
                if axis in ["Z", "E3"]:
                    # Z and E3 axes: show the um input, hide the mm input
                    self.um_distance_widget.setVisible(True)
                    self.mm_distance_widget.setVisible(False)

                    # restore the previously saved value or use the default
                    if axis in self.um_distance_values:
                        self.distance_input_um.setText(
                            str(self.um_distance_values[axis])
                        )
                    else:
                        self.distance_input_um.setText("500")

                    # set the absolute-position widget to um units
                    self.abs_pos_label.setText("Absolute Position (μm):")
                    self.abs_pos_edit.setValidator(QIntValidator(-120000, 120000))
                    if axis in self.abs_um_values:
                        self.abs_pos_edit.setText(str(self.abs_um_values[axis]))
                    else:
                        self.abs_pos_edit.setText("0")

                elif axis in ["X", "Y"]:
                    # X and Y axes: show the mm input, hide the um input
                    self.um_distance_widget.setVisible(False)
                    self.mm_distance_widget.setVisible(True)

                    # restore the previously saved value or use the default
                    if axis in self.mm_distance_values:
                        self.distance_input_mm.setText(
                            str(self.mm_distance_values[axis])
                        )
                    else:
                        self.distance_input_mm.setText("1.0")

                    # set the absolute-position widget to mm units
                    self.abs_pos_label.setText("Absolute Position (mm):")
                    validator = QDoubleValidator(-120.0, 120.0, 3)
                    validator.setNotation(QDoubleValidator.StandardNotation)
                    self.abs_pos_edit.setValidator(validator)
                    if axis in self.abs_mm_values:
                        self.abs_pos_edit.setText(str(self.abs_mm_values[axis]))
                    else:
                        self.abs_pos_edit.setText("0.000")

                # load the axis's velocity/acceleration (from cache or the AXIS_CONFIG default)
                default_vel = AXIS_CONFIG.get(axis, {}).get("default_velocity", 5.0)
                default_acc = AXIS_CONFIG.get(axis, {}).get("default_acceleration", 100.0)
                self.vel_input.setText(str(self.vel_values.get(axis, default_vel)))
                self.acc_input.setText(str(self.acc_values.get(axis, default_acc)))

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
            # select the matching input widget based on the current axis type
            if self.current_axis in ["Z", "E3"]:
                # Z and E3 axes: input is um, return directly
                distance = int(self.distance_input_um.text())
                if 1 <= distance <= 1000:
                    # save the current value
                    self.um_distance_values[self.current_axis] = distance
                    return distance  # already in um
                return None
            elif self.current_axis in ["X", "Y"]:
                # X and Y axes: input is mm, needs conversion to um (multiply by 1000)
                distance_mm = float(self.distance_input_mm.text())
                if 0 < distance_mm <= 120.0:
                    # save the current value
                    self.mm_distance_values[self.current_axis] = distance_mm
                    # convert to um (multiply by 1000 and round)
                    distance_um = int(distance_mm * 1000)
                    return distance_um
                return None
            else:
                # other axis types use the default um input
                distance = int(self.distance_input_um.text())
                if 1 <= distance <= 1000:
                    return distance
                return None
        except (ValueError, AttributeError):
            return None


class IlluminationPanel(QGroupBox):
    """照明控制面板（数据驱动 — 按 constants.ILLUMINATION_PORTS 等元数据动态渲染）

    profile 差异：
      * octoaxes：5 路 TTL (D1-D5) + 无 DAC 直控 + 无 GAIN 切换 + 无 read-back
      * octoaxesplus (squid++)：8 路 TTL (D1-D8) + 8 通道 DAC 直控 + D8 5V↔2.5V GAIN
        切换 + DAC 寄存器读回

    协议拆分：
      * TTL 按钮 → port_cmd 二进制 (SET_PORT_ILLUMINATION, cmd 37)，走 firmware
        intensity_intensity_factor 缩放路径
      * DAC 滑条 → dac_channel_cmd 走 ASCII (S:DAC_SET <ch> <raw>)，直控 raw，
        bring-up 时所见即所得（与 ttl_test 工具一致）
      * GAIN 切换 / Read 按钮 → ASCII (S:DAC_GAIN, S:DAC_READ_ALL)

    Signals:
        port_cmd(port_index, intensity_0_65535, on_off)
            → SET_PORT_ILLUMINATION (二进制)
        turn_off_all()
            → TURN_OFF_ALL_PORTS
        led_matrix_cmd(pattern, r, g, b)
            → SET_ILLUMINATION_LED_MATRIX (cmd 13 缓存) + TURN_ON_ILLUMINATION (cmd 10 点亮)
        led_matrix_off_cmd()
            → TURN_OFF_ILLUMINATION (cmd 11)
        intensity_factor_cmd(pct_0_100)
            → SET_ILLUMINATION_INTENSITY_FACTOR
        dac_channel_cmd(channel, raw_0_65535)
            → ASCII "S:DAC_SET <ch> <raw>" — 直控 DAC raw（绕过 factor）
        dac_gain_cmd(gain_hex)
            → ASCII "S:DAC_GAIN <hex>" — 切 D8 满量程
        dac_readback_cmd()
            → ASCII "S:DAC_READ_ALL" — 触发寄存器读回（回包打到 Log）
    """

    port_cmd            = pyqtSignal(int, int, bool)   # port, intensity, on
    turn_off_all        = pyqtSignal()
    led_matrix_cmd      = pyqtSignal(int, int, int, int)  # pattern, r, g, b (Set Matrix: cmd13+cmd10)
    led_matrix_update_cmd = pyqtSignal(int, int, int, int)  # pattern, r, g, b (real-time update: cmd13 only, firmware auto-refreshes when already lit)
    led_matrix_off_cmd  = pyqtSignal()                  # Clear -> actually extinguish the matrix
    intensity_factor_cmd = pyqtSignal(int)             # 0-100
    dac_channel_cmd     = pyqtSignal(int, int)         # ch, raw 0-65535
    dac_gain_cmd        = pyqtSignal(int)              # gain hex 0x00..0xFF
    dac_readback_cmd    = pyqtSignal()

    LED_PATTERNS = [
        (0, "Full — 全亮"),
        (1, "Left Half — 左半"),
        (2, "Right Half — 右半"),
        (3, "Left Blue / Right Red"),
        (4, "Low NA — 低数值孔"),
        (5, "Left Dot — 左点"),
        (6, "Right Dot — 右点"),
        (7, "Top Half — 上半"),
        (8, "Bottom Half — 下半"),
    ]

    def __init__(self):
        super().__init__("Illumination")
        # only affects the title font, not propagated to child widgets
        self.setStyleSheet(
            "QGroupBox::title { font-weight: bold; font-size: 13px; }"
        )
        # profile metadata snapshot
        self._ports         = list(ILLUMINATION_PORTS)
        self._dac_channels  = list(ILLUMINATION_DAC_CHANNELS)
        self._has_gain      = bool(ILLUMINATION_HAS_GAIN_SWITCH)
        self._has_readback  = bool(ILLUMINATION_HAS_DAC_READBACK)
        n_ports = len(self._ports)
        self._port_intensity_pct = [50] * n_ports
        self._port_on            = [False] * n_ports
        # DAC raw state mirror (squid++ only)
        self._dac_raw            = [0] * len(self._dac_channels)
        # D8 (ch7) gain state: True=2 (full 5V), False=1 (full 2.5V); initial value matches the firmware default 0x0080
        self._d8_gain2 = True
        self._init_ui()

    def _init_ui(self):
        from PyQt5.QtWidgets import QSizePolicy
        root = QVBoxLayout(self)
        root.setSpacing(4)
        root.setContentsMargins(6, 14, 6, 6)

        # -- global control row --------------------------------
        global_layout = QHBoxLayout()
        global_layout.setSpacing(4)

        global_layout.addWidget(QLabel("Global Factor:"))

        self._factor_slider = QSlider(Qt.Horizontal)
        self._factor_slider.setRange(0, 100)
        self._factor_slider.setValue(60)
        self._factor_slider.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self._factor_slider.setMinimumWidth(80)
        self._factor_slider.valueChanged.connect(self._on_factor_changed)
        global_layout.addWidget(self._factor_slider, stretch=2)

        self._factor_label = QLabel("60%")
        self._factor_label.setFixedWidth(44)
        global_layout.addWidget(self._factor_label)

        apply_factor_btn = QPushButton("Apply")
        apply_factor_btn.setMinimumWidth(56)
        apply_factor_btn.clicked.connect(self._send_factor)
        global_layout.addWidget(apply_factor_btn)

        # turn all off
        off_all_btn = QPushButton("All OFF")
        off_all_btn.setStyleSheet(
            "background-color: #c0392b; color: white; font-weight: bold; font-size: 11px;"
        )
        off_all_btn.setMinimumWidth(62)
        off_all_btn.clicked.connect(self._on_turn_off_all)
        global_layout.addWidget(off_all_btn)

        root.addLayout(global_layout)
        root.addWidget(self._make_divider())

        # -- TTL port rows (generated dynamically from ILLUMINATION_PORTS) --------
        self._port_btns = []
        self._port_sliders = []
        self._port_pct_labels = []

        ports_grid = QGridLayout()
        ports_grid.setVerticalSpacing(3)
        ports_grid.setHorizontalSpacing(4)
        ports_grid.setColumnStretch(1, 1)   # the slider column stretches automatically

        for i, (port_idx, name, _pin) in enumerate(self._ports):
            lbl = QLabel(name)
            lbl.setMinimumWidth(95)
            ports_grid.addWidget(lbl, i, 0)

            slider = QSlider(Qt.Horizontal)
            slider.setRange(0, 100)
            slider.setValue(self._port_intensity_pct[i])
            slider.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
            slider.setMinimumWidth(60)
            slider.valueChanged.connect(lambda v, idx=i: self._on_port_slider(idx, v))
            ports_grid.addWidget(slider, i, 1)
            self._port_sliders.append(slider)

            pct_lbl = QLabel(f"{self._port_intensity_pct[i]}%")
            pct_lbl.setFixedWidth(44)
            pct_lbl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            ports_grid.addWidget(pct_lbl, i, 2)
            self._port_pct_labels.append(pct_lbl)

            btn = QPushButton("OFF")
            btn.setCheckable(True)
            btn.setChecked(False)
            btn.setFixedWidth(46)
            self._set_port_btn_style(btn, False)
            btn.toggled.connect(lambda checked, idx=i: self._on_port_toggle(idx, checked))
            ports_grid.addWidget(btn, i, 3)
            self._port_btns.append(btn)

        root.addLayout(ports_grid)
        root.addWidget(self._make_divider())

        # -- DAC direct-control area (only squid++ has ILLUMINATION_DAC_CHANNELS) --
        # the slider uses ASCII S:DAC_SET to directly control raw (bypassing the firmware intensity_factor),
        # what-you-see-is-what-you-get during bring-up; independent of the TTL buttons
        self._dac_sliders   = []
        self._dac_val_labels = []
        self._d8_gain_btn   = None
        if self._dac_channels:
            dac_title = QLabel("DAC raw direct (bring-up，绕过 factor)")
            dac_title.setStyleSheet("color: #555; font-style: italic;")
            root.addWidget(dac_title)

            dac_grid = QGridLayout()
            dac_grid.setVerticalSpacing(3)
            dac_grid.setHorizontalSpacing(4)
            dac_grid.setColumnStretch(1, 1)
            for row, (ch, full_v) in enumerate(self._dac_channels):
                lbl = QLabel(f"DAC{ch} → D{ch+1}")
                lbl.setMinimumWidth(95)
                dac_grid.addWidget(lbl, row, 0)

                sl = QSlider(Qt.Horizontal)
                sl.setRange(0, 100)
                sl.setValue(0)
                sl.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
                sl.setMinimumWidth(60)
                sl.valueChanged.connect(
                    lambda pct, c=ch, idx=row: self._on_dac_slider(c, idx, pct)
                )
                dac_grid.addWidget(sl, row, 1)
                self._dac_sliders.append(sl)

                v_lbl = QLabel(f"0% (0.00V / {full_v:.1f}V)")
                v_lbl.setFixedWidth(120)
                v_lbl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
                dac_grid.addWidget(v_lbl, row, 2)
                self._dac_val_labels.append(v_lbl)
            root.addLayout(dac_grid)

            # GAIN + Read action row
            dac_ops = QHBoxLayout()
            dac_ops.setSpacing(4)
            if self._has_gain:
                self._d8_gain_btn = QPushButton("D8 max: 5V")
                self._d8_gain_btn.setStyleSheet(
                    "background-color: #2c3e50; color: white; font-weight: bold;"
                )
                self._d8_gain_btn.setToolTip(
                    "点击切换 D8 满量程：5V (gain=2) ↔ 2.5V (gain=1)\n"
                    "发 S:DAC_GAIN 0x80 / 0x00"
                )
                self._d8_gain_btn.clicked.connect(self._on_d8_gain_toggle)
                dac_ops.addWidget(self._d8_gain_btn)
            if self._has_readback:
                read_btn = QPushButton("Read DAC Regs")
                read_btn.setStyleSheet(
                    "background-color: #8e44ad; color: white; font-weight: bold;"
                )
                read_btn.setToolTip("发 S:DAC_READ_ALL，回包打到 Log 区")
                read_btn.clicked.connect(self.dac_readback_cmd.emit)
                dac_ops.addWidget(read_btn)
            dac_ops.addStretch()
            root.addLayout(dac_ops)
            root.addWidget(self._make_divider())

        # -- LED matrix control --------------------------------
        matrix_layout = QVBoxLayout()
        matrix_layout.setSpacing(3)

        pat_row = QHBoxLayout()
        pat_row.setSpacing(4)
        pat_row.addWidget(QLabel("Pattern:"))
        self._pattern_combo = QComboBox()
        for code, label in self.LED_PATTERNS:
            self._pattern_combo.addItem(label, code)
        self._pattern_combo.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        pat_row.addWidget(self._pattern_combo, stretch=1)
        matrix_layout.addLayout(pat_row)

        # R/G/B sliders (aligned in a shared QGridLayout)
        rgb_grid = QGridLayout()
        rgb_grid.setVerticalSpacing(3)
        rgb_grid.setHorizontalSpacing(4)
        rgb_grid.setColumnStretch(1, 1)
        self._rgb_sliders = []
        self._rgb_labels  = []
        for row_i, (ch_name, default) in enumerate([("R", 255), ("G", 255), ("B", 255)]):
            rgb_grid.addWidget(QLabel(f"{ch_name}:"), row_i, 0)
            sl = QSlider(Qt.Horizontal)
            sl.setRange(0, 255)
            sl.setValue(default)
            sl.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
            sl.setMinimumWidth(60)
            sl.valueChanged.connect(self._update_color_preview)
            rgb_grid.addWidget(sl, row_i, 1)
            val_lbl = QLabel(str(default))
            val_lbl.setFixedWidth(30)
            val_lbl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            sl.valueChanged.connect(lambda v, lbl=val_lbl: lbl.setText(str(v)))
            rgb_grid.addWidget(val_lbl, row_i, 2)
            self._rgb_sliders.append(sl)
            self._rgb_labels.append(val_lbl)
        matrix_layout.addLayout(rgb_grid)

        # Merged from new-W-axis be20270: brightfield brightness (0-100%), scales R/G/B as a whole
        # when sending (color is set by the RGB sliders above; brightness is adjustable independently).
        bright_row = QHBoxLayout()
        bright_row.setSpacing(4)
        bright_row.addWidget(QLabel("亮度:"))
        self._brightness_slider = QSlider(Qt.Horizontal)
        self._brightness_slider.setRange(0, 100)
        self._brightness_slider.setValue(100)
        self._brightness_slider.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self._brightness_slider.setMinimumWidth(60)
        self._brightness_slider.valueChanged.connect(self._update_color_preview)
        # Real-time adjust: dragging sends cmd13 (cache + refresh only, no cmd10). Firmware
        # set_illumination_led_matrix auto-refreshes the current source when illumination_is_on;
        # when not lit it only caches, so it won't accidentally turn the matrix on.
        self._brightness_slider.valueChanged.connect(self._on_brightness_live)
        bright_row.addWidget(self._brightness_slider, stretch=1)
        self._brightness_label = QLabel("100%")
        self._brightness_label.setFixedWidth(48)
        self._brightness_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self._brightness_slider.valueChanged.connect(
            lambda v: self._brightness_label.setText(f"{v}%")
        )
        bright_row.addWidget(self._brightness_label)
        matrix_layout.addLayout(bright_row)

        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(4)
        self._color_preview = QLabel()
        self._color_preview.setFixedSize(28, 22)
        self._color_preview.setStyleSheet(
            "background-color: rgb(255,255,255); border: 1px solid #888;"
        )
        ctrl_row.addWidget(self._color_preview)

        set_matrix_btn = QPushButton("Set Matrix")
        set_matrix_btn.setStyleSheet(
            "background-color: #2980b9; color: white; font-weight: bold;"
        )
        set_matrix_btn.clicked.connect(self._send_led_matrix)
        ctrl_row.addWidget(set_matrix_btn)

        clear_matrix_btn = QPushButton("Clear")
        clear_matrix_btn.setMinimumWidth(46)
        clear_matrix_btn.clicked.connect(self._clear_led_matrix)
        ctrl_row.addWidget(clear_matrix_btn)

        ctrl_row.addStretch()
        matrix_layout.addLayout(ctrl_row)

        root.addLayout(matrix_layout)
        root.addStretch()

    # -- internal helpers --------------------------------

    @staticmethod
    def _make_divider(text=""):
        frame = QFrame()
        frame.setFrameShape(QFrame.HLine)
        frame.setFrameShadow(QFrame.Sunken)
        frame.setStyleSheet("color: #aaa;")
        return frame

    @staticmethod
    def _set_port_btn_style(btn: QPushButton, on: bool):
        if on:
            btn.setText("ON")
            btn.setStyleSheet(
                "background-color: #27ae60; color: white; font-weight: bold;"
            )
        else:
            btn.setText("OFF")
            btn.setStyleSheet(
                "background-color: #7f8c8d; color: white;"
            )

    def _on_factor_changed(self, v):
        self._factor_label.setText(f"{v}%")

    def _send_factor(self):
        self.intensity_factor_cmd.emit(self._factor_slider.value())

    def _on_turn_off_all(self):
        # reset all button states (without firing the toggled signal)
        for i, btn in enumerate(self._port_btns):
            btn.blockSignals(True)
            btn.setChecked(False)
            self._set_port_btn_style(btn, False)
            self._port_on[i] = False
            btn.blockSignals(False)
        self.turn_off_all.emit()

    def _on_port_slider(self, ui_row, value):
        self._port_intensity_pct[ui_row] = value
        self._port_pct_labels[ui_row].setText(f"{value}%")
        # if the port is currently on, update the intensity in real time
        if self._port_on[ui_row]:
            intensity = int(value / 100.0 * 65535)
            port_idx = self._ports[ui_row][0]
            self.port_cmd.emit(port_idx, intensity, True)

    def _on_port_toggle(self, ui_row, checked):
        self._port_on[ui_row] = checked
        self._set_port_btn_style(self._port_btns[ui_row], checked)
        pct = self._port_intensity_pct[ui_row]
        intensity = int(pct / 100.0 * 65535)
        port_idx = self._ports[ui_row][0]
        self.port_cmd.emit(port_idx, intensity, checked)

    # -- DAC direct control (squid++ only) --------------------------------

    def _on_dac_slider(self, dac_ch, ui_row, pct):
        # the current full-scale voltage (the GAIN switch can change D8/ch7)
        full_v = self._dac_channels[ui_row][1]
        raw = int(round(pct * 65535 / 100))
        if raw > 65535:
            raw = 65535
        self._dac_raw[ui_row] = raw
        voltage = pct / 100.0 * full_v
        self._dac_val_labels[ui_row].setText(
            f"{pct}% ({voltage:.2f}V / {full_v:.1f}V)"
        )
        self.dac_channel_cmd.emit(dac_ch, raw)

    def _on_d8_gain_toggle(self):
        # toggle D8 (ch7) gain: True=gain2/5V, False=gain1/2.5V
        self._d8_gain2 = not self._d8_gain2
        gain_hex = 0x80 if self._d8_gain2 else 0x00
        self.dac_gain_cmd.emit(gain_hex)
        # sync the local full-scale mirror (ch7 only) + refresh the display
        for i, (ch, full_v) in enumerate(self._dac_channels):
            if ch == 7:
                new_v = 5.0 if self._d8_gain2 else 2.5
                self._dac_channels[i] = (ch, new_v)
                pct = self._dac_sliders[i].value()
                voltage = pct / 100.0 * new_v
                self._dac_val_labels[i].setText(
                    f"{pct}% ({voltage:.2f}V / {new_v:.1f}V)"
                )
                break
        if self._d8_gain_btn:
            self._d8_gain_btn.setText(
                f"D8 max: {'5V' if self._d8_gain2 else '2.5V'}"
            )

    def _scaled_rgb(self):
        """按亮度滑条(0-100%)整体缩放 R/G/B，返回实际下发的 0-255 三元组。（融合 be20270）"""
        pct = self._brightness_slider.value()
        return tuple(int(sl.value() * pct / 100) for sl in self._rgb_sliders)

    def _update_color_preview(self):
        r, g, b = self._scaled_rgb()
        self._color_preview.setStyleSheet(
            f"background-color: rgb({r},{g},{b}); border: 1px solid #888;"
        )

    def _send_led_matrix(self):
        pattern = self._pattern_combo.currentData()
        r, g, b = self._scaled_rgb()
        self.led_matrix_cmd.emit(pattern, r, g, b)

    def _on_brightness_live(self, _value):
        # Brightness slider real-time adjust: send cmd13 (scaled RGB). When already lit the firmware
        # auto-refreshes brightness; when not lit it only updates the cache, taking effect on the next
        # Set Matrix / turn-on, so it won't accidentally turn the matrix on. (merged from be20270)
        pattern = self._pattern_combo.currentData()
        r, g, b = self._scaled_rgb()
        self.led_matrix_update_cmd.emit(pattern, r, g, b)

    def _clear_led_matrix(self):
        # firmware cmd 13 only caches parameters and does not extinguish (behavior change on 2026-05-10);
        # cmd 11 TURN_OFF_ILLUMINATION must be sent to actually extinguish the matrix
        self.led_matrix_off_cmd.emit()


class LogDisplay(QGroupBox):
    """日志显示组件"""

    clear_clicked = pyqtSignal()

    def __init__(self, title):
        super().__init__(title)
        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout(self)

        # title bar
        title_layout = QHBoxLayout()
        title_layout.addWidget(QLabel(f"{self.title()}:"))
        title_layout.addStretch()

        clear_btn = QPushButton(f"Clear {self.title()}")
        clear_btn.clicked.connect(self.clear_clicked.emit)
        title_layout.addWidget(clear_btn)

        layout.addLayout(title_layout)

        # text display
        self.text_edit = QTextEdit()
        self.text_edit.setReadOnly(True)
        if "Sent" in self.title():
            self.text_edit.setMaximumHeight(120)
        layout.addWidget(self.text_edit)

    def append(self, text):
        self.text_edit.append(text)

    def clear(self):
        self.text_edit.clear()
