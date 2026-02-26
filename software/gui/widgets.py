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

        filter_btn_layout.addStretch()

        rounds_label = QLabel("Rounds:")
        filter_btn_layout.addWidget(rounds_label)

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


class IlluminationPanel(QGroupBox):
    """照明控制面板

    Signals:
        port_cmd(port_index, intensity_0_65535, on_off)
            → SET_PORT_ILLUMINATION
        turn_off_all()
            → TURN_OFF_ALL_PORTS
        led_matrix_cmd(pattern, r, g, b)
            → SET_ILLUMINATION_LED_MATRIX
        intensity_factor_cmd(pct_0_100)
            → SET_ILLUMINATION_INTENSITY_FACTOR
    """

    port_cmd            = pyqtSignal(int, int, bool)   # port, intensity, on
    turn_off_all        = pyqtSignal()
    led_matrix_cmd      = pyqtSignal(int, int, int, int)  # pattern, r, g, b
    intensity_factor_cmd = pyqtSignal(int)             # 0-100

    # D3/D4 源码非连续（历史遗留）：port_index → old source code
    PORT_SOURCES = [11, 12, 14, 13, 15]   # D1-D5
    PORT_PINS    = [5,  4,  22, 3,  23]   # 对应引脚
    PORT_NAMES   = ["D1 (pin 5)", "D2 (pin 4)", "D3 (pin 22)", "D4 (pin 3)", "D5 (pin 23)"]

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
        # 只影响标题字体，不传播给子控件
        self.setStyleSheet(
            "QGroupBox::title { font-weight: bold; font-size: 13px; }"
        )
        self._port_intensity_pct = [50] * 5
        self._port_on = [False] * 5
        self._init_ui()

    def _init_ui(self):
        from PyQt5.QtWidgets import QSizePolicy
        root = QVBoxLayout(self)
        root.setSpacing(4)
        root.setContentsMargins(6, 14, 6, 6)

        # ── 全局控制行 ───────────────────────────────────────
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

        # 全部关闭
        off_all_btn = QPushButton("All OFF")
        off_all_btn.setStyleSheet(
            "background-color: #c0392b; color: white; font-weight: bold; font-size: 11px;"
        )
        off_all_btn.setMinimumWidth(62)
        off_all_btn.clicked.connect(self._on_turn_off_all)
        global_layout.addWidget(off_all_btn)

        root.addLayout(global_layout)
        root.addWidget(self._make_divider())

        # ── 五个 TTL 端口行 ────────────────────────────────────
        self._port_btns = []
        self._port_sliders = []
        self._port_pct_labels = []

        ports_grid = QGridLayout()
        ports_grid.setVerticalSpacing(3)
        ports_grid.setHorizontalSpacing(4)
        ports_grid.setColumnStretch(1, 1)   # 滑条列自动拉伸

        for i, name in enumerate(self.PORT_NAMES):
            lbl = QLabel(name)
            lbl.setMinimumWidth(85)
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

        # ── LED 矩阵控制 ──────────────────────────────────────
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

        # R/G/B 滑条（共用 QGridLayout 对齐）
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

    # ── 内部辅助 ──────────────────────────────────────────────

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
        # 重置所有按钮状态（不触发 toggled 信号）
        for i, btn in enumerate(self._port_btns):
            btn.blockSignals(True)
            btn.setChecked(False)
            self._set_port_btn_style(btn, False)
            self._port_on[i] = False
            btn.blockSignals(False)
        self.turn_off_all.emit()

    def _on_port_slider(self, port_idx, value):
        self._port_intensity_pct[port_idx] = value
        self._port_pct_labels[port_idx].setText(f"{value}%")
        # 如果端口当前开启，实时更新强度
        if self._port_on[port_idx]:
            intensity = int(value / 100.0 * 65535)
            self.port_cmd.emit(port_idx, intensity, True)

    def _on_port_toggle(self, port_idx, checked):
        self._port_on[port_idx] = checked
        self._set_port_btn_style(self._port_btns[port_idx], checked)
        pct = self._port_intensity_pct[port_idx]
        intensity = int(pct / 100.0 * 65535)
        self.port_cmd.emit(port_idx, intensity, checked)

    def _update_color_preview(self):
        r = self._rgb_sliders[0].value()
        g = self._rgb_sliders[1].value()
        b = self._rgb_sliders[2].value()
        self._color_preview.setStyleSheet(
            f"background-color: rgb({r},{g},{b}); border: 1px solid #888;"
        )

    def _send_led_matrix(self):
        pattern = self._pattern_combo.currentData()
        r = self._rgb_sliders[0].value()
        g = self._rgb_sliders[1].value()
        b = self._rgb_sliders[2].value()
        self.led_matrix_cmd.emit(pattern, r, g, b)

    def _clear_led_matrix(self):
        # 发送 pattern=FULL, RGB=0,0,0 相当于熄灭
        self.led_matrix_cmd.emit(0, 0, 0, 0)


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
