import time
import datetime
import os
from typing import Optional

from define import CMD_SET, AXIS, AXIS_MOVE_CMD_MAP, AXIS_MOVETO_CMD_MAP, AXIS_LIMIT_CODE_MAP

CMDS = CMD_SET
from define import OBJECTIVE_RATIO, SCREW_PITCH_W_MM, OBJECTIVE_HOLES
from define import SQUID_FILTERWHEEL_OFFSET
from utils.helpers import int_to_payload

from PyQt5.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QLineEdit,
    QLabel,
    QTextEdit,
    QGroupBox,
    QComboBox,
    QCheckBox,
    QGridLayout,
    QFileDialog,
    QTabWidget,
)
from PyQt5.QtCore import QTimer, Qt, QMutex
from PyQt5.QtGui import QFont

from hardware.serial_thread import SerialThread
from gui.widgets import AxisStatusDisplay, ControlPanel, LogDisplay, IlluminationPanel
from gui.test_panel import IntegrationTestPanel
from hardware.axis_manager import AxisManager
from utils.constants import AXIS_CONFIG, AXIS_MM_PER_STEP
from utils.helpers import format_command, find_teensy_port


class TeensyControlGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.serial_thread: Optional[SerialThread] = None
        self.axis_manager = AxisManager()
        self._query_mutex = QMutex()
        self._query_timer_active = False

        # 日志文件相关
        self.log_file = None
        self.log_file_path = os.getcwd()  # 默认当前路径

        # 添加轴使能状态字典（从 AXIS_CONFIG 动态生成，跟随 profile）
        self.axis_enabled_states = {axis: True for axis in AXIS_CONFIG.keys()}

        self.init_ui()
        self.setup_timers()
        self.find_and_connect_teensy()

    def init_ui(self):
        self.setWindowTitle("Teensy Motor Control")
        self.setGeometry(100, 100, 1600, 1000)

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        # 顶部状态栏（常驻）
        main_layout.addLayout(self.create_top_bar())

        # 标签页
        self.tab_widget = QTabWidget()
        self.tab_widget.setStyleSheet("""
            QTabBar::tab {
                font-size: 18px;
                font-weight: bold;
                padding: 8px 28px;
                color: #555;
                border: 1px solid #ccc;
                border-bottom: none;
                border-top-left-radius: 6px;
                border-top-right-radius: 6px;
                margin-right: 2px;
            }
            QTabBar::tab:selected {
                color: white;
                background-color: #2980b9;
                border-color: #2980b9;
            }
            QTabBar::tab:hover:!selected {
                background-color: #d5e8f0;
                color: #2471a3;
            }
        """)

        # Tab 1: Motion — 轴控制 + 全轴状态
        motion_tab = self.create_motion_tab()
        self.tab_widget.addTab(motion_tab, "Motion")

        # Tab 2: Illumination — 照明控制
        illumination_tab = self.create_illumination_tab()
        self.tab_widget.addTab(illumination_tab, "Illumination")

        # Tab 3: Log — 日志
        log_tab = self.create_log_tab()
        self.tab_widget.addTab(log_tab, "Log")

        # Tab 4: Integration Test — 集成测试
        test_tab = self.create_integration_test_tab()
        self.tab_widget.addTab(test_tab, "Integration Test")

        main_layout.addWidget(self.tab_widget)

    def create_top_bar(self):
        layout = QHBoxLayout()

        # 连接状态
        self.status_label = QLabel("Disconnected")
        self.status_label.setStyleSheet("color: red; font-weight: bold;")
        layout.addWidget(self.status_label)

        layout.addStretch()

        # 固件版本
        self.version_label = QLabel("Firmware Version: Unknown")
        self.version_label.setStyleSheet("color: darkorange; font-weight: bold;")
        layout.addWidget(self.version_label)

        return layout

    def create_left_panel(self):
        panel = QWidget()
        layout = QVBoxLayout(panel)

        # 轴选择
        layout.addLayout(self.create_axis_selector())

        # 位置显示
        layout.addLayout(self.create_position_display())

        # 当前轴状态
        layout.addWidget(self.create_current_axis_status())

        # 控制面板
        self.control_panel = ControlPanel()
        self.control_panel.homing_clicked.connect(self.send_homing)
        self.control_panel.reset_clicked.connect(self.send_reset)
        self.control_panel.limits_set_clicked.connect(self.set_limits)
        self.control_panel.forward_clicked.connect(self.move_forward)
        self.control_panel.backward_clicked.connect(self.move_backward)
        self.control_panel.previous_clicked.connect(self.previous_position)
        self.control_panel.next_clicked.connect(self.next_position)
        self.control_panel.test_clicked.connect(self.run_w_test)
        self.control_panel.enable_toggled.connect(self.toggle_axis_enable)
        self.control_panel.axis_changed.connect(self.on_axis_changed)
        self.control_panel.move_absolute_clicked.connect(self.moveto_axis)
        self.control_panel.velocity_accel_set.connect(self.send_velocity_acceleration)

        layout.addWidget(self.control_panel)

        panel.setMaximumWidth(600)
        return panel

    def create_axis_selector(self):
        layout = QHBoxLayout()
        layout.addWidget(QLabel("Select Axis:"))
        self.axis_combo = QComboBox()
        for axis_id, config in AXIS_CONFIG.items():
            self.axis_combo.addItem(config["display_name"], axis_id)
        self.axis_combo.currentIndexChanged.connect(self.on_axis_changed)
        layout.addWidget(self.axis_combo)
        layout.addStretch()
        return layout

    def create_position_display(self):
        layout = QVBoxLayout()

        font_pos = QFont("Arial", 15)
        font_pos.setBold(True)

        self.pos_label = QLabel("Current Position:  —  mm")
        self.pos_label.setFont(font_pos)
        self.pos_label.setStyleSheet("color: blue;")
        layout.addWidget(self.pos_label)

        self.steps_label = QLabel("Current Position:  —  steps")
        self.steps_label.setFont(font_pos)
        self.steps_label.setStyleSheet("color: blue;")
        layout.addWidget(self.steps_label)

        layout.addStretch()

        return layout

    def create_current_axis_status(self):
        group = QGroupBox("Current Axis Status")
        layout = QVBoxLayout(group)

        grid = QGridLayout()

        self.axis_state_label = QLabel("State: Unknown")
        grid.addWidget(QLabel("State:"), 0, 0)
        grid.addWidget(self.axis_state_label, 0, 1)

        self.axis_moving_label = QLabel("Moving: NO")
        grid.addWidget(QLabel("Moving:"), 1, 0)
        grid.addWidget(self.axis_moving_label, 1, 1)

        self.axis_enabled_label = QLabel("Enabled: YES")
        grid.addWidget(QLabel("Enabled:"), 2, 0)
        grid.addWidget(self.axis_enabled_label, 2, 1)

        self.axis_limits_label = QLabel("Limits: 0x0")
        grid.addWidget(QLabel("Limits:"), 3, 0)
        grid.addWidget(self.axis_limits_label, 3, 1)

        layout.addLayout(grid)

        btn_layout = QHBoxLayout()
        self.status_query_btn = QPushButton("Query Axis Status")
        self.status_query_btn.clicked.connect(self.query_axis_status)
        btn_layout.addWidget(self.status_query_btn)

        btn_layout.addStretch()
        layout.addLayout(btn_layout)

        return group

    def create_motion_tab(self):
        """Motion 标签页：左侧轴控制 + 右侧全轴状态表"""
        tab = QWidget()
        layout = QHBoxLayout(tab)

        # 左侧控制面板
        left_panel = self.create_left_panel()
        layout.addWidget(left_panel)

        # 右侧状态显示
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)

        self.axis_status_display = AxisStatusDisplay()
        self.axis_status_display.refresh_clicked.connect(self.refresh_all_axes_status)
        right_layout.addWidget(self.axis_status_display)
        right_layout.addStretch()

        layout.addWidget(right_panel)
        return tab

    def create_illumination_tab(self):
        """Illumination 标签页：照明控制面板"""
        tab = QWidget()
        layout = QVBoxLayout(tab)

        self.illumination_panel = IlluminationPanel()
        self.illumination_panel.port_cmd.connect(self._send_illu_port)
        self.illumination_panel.turn_off_all.connect(self._send_illu_turn_off_all)
        self.illumination_panel.led_matrix_cmd.connect(self._send_illu_led_matrix)
        self.illumination_panel.intensity_factor_cmd.connect(self._send_illu_intensity_factor)
        layout.addWidget(self.illumination_panel)

        return tab

    def create_integration_test_tab(self):
        """Integration Test 标签页：连通性 / 协议自检"""
        tab = QWidget()
        layout = QVBoxLayout(tab)
        self.test_panel = IntegrationTestPanel()
        self.test_panel.request_send_command.connect(
            lambda cmd: self.send_command(cmd, "Test"))
        self.test_panel.log_message.connect(self.log)
        layout.addWidget(self.test_panel)
        return tab

    def create_log_tab(self):
        """Log 标签页：已发送命令 + 日志"""
        tab = QWidget()
        layout = QVBoxLayout(tab)

        # 调试命令输入
        cmd_layout = QHBoxLayout()
        cmd_layout.addWidget(QLabel("Debug Command:"))
        self.debug_cmd_edit = QLineEdit()
        self.debug_cmd_edit.setPlaceholderText("e.g. S:ENCPOS, S:HWINFO, S:VERSION")
        self.debug_cmd_edit.returnPressed.connect(self.send_debug_command)
        cmd_layout.addWidget(self.debug_cmd_edit)
        send_btn = QPushButton("Send")
        send_btn.clicked.connect(self.send_debug_command)
        cmd_layout.addWidget(send_btn)
        layout.addLayout(cmd_layout)

        # 发送命令显示
        layout.addWidget(self.create_sent_commands_group())

        # 日志显示
        layout.addWidget(self.create_log_group())

        # 重连按钮
        self.connect_btn = QPushButton("Reconnect")
        self.connect_btn.clicked.connect(self.find_and_connect_teensy)
        layout.addWidget(self.connect_btn)

        return tab

    def create_sent_commands_group(self):
        self.sent_display = LogDisplay("Sent Commands")
        self.sent_display.clear_clicked.connect(self.clear_sent_commands)
        return self.sent_display

    def create_log_group(self):
        group = QGroupBox("Log")
        layout = QVBoxLayout(group)

        # 标题栏
        title_layout = QHBoxLayout()
        title_layout.addWidget(QLabel("Log:"))
        title_layout.addStretch()

        clear_btn = QPushButton("Clear Log")
        clear_btn.clicked.connect(self.clear_log)
        title_layout.addWidget(clear_btn)

        layout.addLayout(title_layout)

        # 日志保存选项
        save_layout = QHBoxLayout()

        # 保存到文件复选框
        self.save_log_checkbox = QCheckBox("Save to file")
        self.save_log_checkbox.stateChanged.connect(self.toggle_log_save)
        save_layout.addWidget(self.save_log_checkbox)

        # 路径标签
        save_layout.addWidget(QLabel("Path:"))

        # 路径输入框
        self.log_path_edit = QLineEdit(self.log_file_path)
        self.log_path_edit.setMinimumWidth(300)
        self.log_path_edit.textChanged.connect(self.update_log_path)
        save_layout.addWidget(self.log_path_edit)

        # 浏览按钮
        browse_btn = QPushButton("Browse...")
        browse_btn.clicked.connect(self.browse_log_path)
        save_layout.addWidget(browse_btn)

        save_layout.addStretch()
        layout.addLayout(save_layout)

        # 文本显示
        self.log_text_edit = QTextEdit()
        self.log_text_edit.setReadOnly(True)
        layout.addWidget(self.log_text_edit)

        return group

    def browse_log_path(self):
        """浏览并选择日志保存目录"""
        directory = QFileDialog.getExistingDirectory(
            self, "Select Log Directory", self.log_path_edit.text()
        )
        if directory:
            self.log_path_edit.setText(directory)

    def update_log_path(self):
        """更新日志保存路径"""
        new_path = self.log_path_edit.text().strip()
        if os.path.isdir(new_path):
            self.log_file_path = new_path
            # 如果日志保存已启用，重新打开日志文件
            if self.save_log_checkbox.isChecked():
                self.close_log_file()
                self.open_log_file()
                self.log(f"Log file path changed to: {new_path}")

    def toggle_log_save(self, state):
        """切换日志保存到文件功能"""
        if state == Qt.Checked:
            self.open_log_file()
            self.log("Log saving to file enabled")
        else:
            self.close_log_file()
            self.log("Log saving to file disabled")

    def open_log_file(self):
        """打开日志文件"""
        try:
            # 确保目录存在
            os.makedirs(self.log_file_path, exist_ok=True)

            # 生成带时间戳的文件名
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            log_filename = f"motor_control_log_{timestamp}.txt"
            log_file_path = os.path.join(self.log_file_path, log_filename)

            # 打开文件
            self.log_file = open(log_file_path, "a", encoding="utf-8")

            # 写入文件头
            self.log_file.write(f"=== Motor Control Log ===\n")
            self.log_file.write(
                f"Started at: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n"
            )
            self.log_file.write(f"Log file: {log_filename}\n")
            self.log_file.write("=" * 40 + "\n\n")

            self.log(f"Log file created: {log_filename}")

        except Exception as e:
            self.log_text_edit.append(f"[ERROR] Failed to create log file: {str(e)}")
            self.save_log_checkbox.setChecked(False)

    def close_log_file(self):
        """关闭日志文件"""
        if self.log_file:
            try:
                self.log_file.write(f"\n" + "=" * 40 + "\n")
                self.log_file.write(
                    f"Log ended at: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n"
                )
                self.log_file.close()
                self.log("Log file closed")
            except Exception as e:
                self.log_text_edit.append(f"[ERROR] Failed to close log file: {str(e)}")
            finally:
                self.log_file = None

    def toggle_axis_enable(self, enable_state):
        """切换轴使能状态"""
        current_axis = self.get_current_axis()

        # 更新本地状态
        self.axis_enabled_states[current_axis] = enable_state

        # 发送使能/禁用命令
        if enable_state:
            self.enable_axis()
        else:
            self.disable_axis()

        # 更新按钮显示
        self.control_panel.set_enable_state(enable_state)

    def enable_axis(self):
        """使能轴"""
        current_axis = self.get_current_axis()
        if self._set_axis_enable(current_axis, True):
            self.log(f"Axis {current_axis} enabled")
            self.axis_manager.update_axis_status(current_axis, {"enabled": "YES"})
            self.update_current_axis_display(current_axis)

    def disable_axis(self):
        """禁用轴"""
        current_axis = self.get_current_axis()
        if self._set_axis_enable(current_axis, False):
            self.log(f"Axis {current_axis} disabled")
            self.axis_manager.update_axis_status(current_axis, {"enabled": "NO"})
            self.update_current_axis_display(current_axis)

    def setup_timers(self):
        # 启动定时器
        self.startup_timer = QTimer(self)
        self.startup_timer.setInterval(500)
        self.startup_timer.timeout.connect(self.startup_launch)

        # 状态刷新定时器
        self.status_timer = QTimer(self)
        self.status_timer.setInterval(2000)
        self.status_timer.timeout.connect(self.safe_refresh_all_axes)
        self.status_timer.start()

    def safe_refresh_all_axes(self):
        """安全刷新所有轴状态"""
        if self.axis_status_display.auto_poll_check.isChecked():
            self.refresh_all_axes_status()

    # ====== 业务函数 ======
    def on_axis_changed(self):
        """当下拉菜单选择改变时更新控件显示状态"""
        current_axis = self.get_current_axis()

        # 更新控件显示（通过ControlPanel自动处理）
        self.control_panel.set_current_axis(current_axis)

        # 更新limits的状态
        self.set_limits()

        # 更新当前轴的使能状态显示
        enabled = self.axis_enabled_states.get(current_axis, True)
        self.control_panel.set_enable_state(enabled)

        # 更新当前轴状态显示
        self.update_current_axis_display(current_axis)

        # 发送查询命令更新当前轴状态
        if self.is_connected():
            self.query_axis_status()

    def update_current_axis_display(self, axis):
        """更新当前轴状态显示"""
        status = self.axis_manager.get_axis_status(axis)
        if not status:
            return

        self.axis_state_label.setText(f"State: {status['state']}")
        self.axis_moving_label.setText(f"Moving: {status['moving']}")
        self.axis_enabled_label.setText(f"Enabled: {status['enabled']}")
        self.axis_limits_label.setText(f"Limits: {status['limits']}")

        # 设置颜色
        color_map = {"IDLE": "green", "MOVING": "blue", "ERROR": "red"}
        state_color = color_map.get(status["state"], "orange")
        self.axis_state_label.setStyleSheet(f"color: {state_color}; font-weight: bold;")

        self.axis_moving_label.setStyleSheet(
            "color: blue; font-weight: bold;"
            if status["moving"] == "YES"
            else "color: black;"
        )

        self.axis_enabled_label.setStyleSheet(
            "color: green; font-weight: bold;"
            if status["enabled"] == "YES"
            else "color: red; font-weight: bold;"
        )

    def get_current_axis(self):
        return self.axis_combo.currentData()

    def is_connected(self):
        """检查连接状态"""
        if not self.serial_thread:
            return False

        # 使用新的状态检查方法
        return self.serial_thread.is_connected()

    def find_and_connect_teensy(self):
        """查找并连接Teensy"""
        # 停止现有连接
        if self.serial_thread:
            self.serial_thread.stop()
            self.serial_thread.wait()
            self.serial_thread = None

        # 查找端口
        teensy_port = find_teensy_port()
        if teensy_port is None:
            self.update_connection_status(False, "Teensy not found")
            self.log("Teensy not found. Please check your connection.")
            return

        # 创建新连接
        try:
            self.serial_thread = SerialThread(teensy_port)
            self.serial_thread.data_received.connect(self.handle_received_data)
            self.serial_thread.binary_response.connect(self.handle_binary_response)
            self.serial_thread.error_occurred.connect(self.handle_serial_error)
            self.serial_thread.debug_info.connect(self.handle_debug_info)
            self.serial_thread.start()

            self.update_connection_status(True, f"Connecting to {teensy_port}...")
            self.log(f"Connecting to Teensy on {teensy_port}")

            # 延迟检查连接状态
            QTimer.singleShot(2000, self.check_connection_status)

        except Exception as e:
            error_msg = f"Failed to start serial thread: {str(e)}"
            self.update_connection_status(False, error_msg)
            self.log(error_msg)

    def check_connection_status(self):
        """检查连接状态"""
        if self.is_connected() and self.serial_thread is not None:
            self.update_connection_status(
                True, f"Connected to {self.serial_thread.port}"
            )

            # 延迟执行初始化命令
            QTimer.singleShot(500, self.query_firmware_version)
            QTimer.singleShot(800, self.query_hardware_info)
            QTimer.singleShot(1000, self.refresh_all_axes_status)
        else:
            self.update_connection_status(False, "Connection failed")
            self.log("Connection to Teensy failed")

    def handle_serial_error(self, error_message):
        """处理串口错误"""
        self.log(f"Serial Error: {error_message}")

        # 如果错误表明连接断开，更新状态
        if any(
            keyword in error_message.lower()
            for keyword in ["failed", "closed", "not open", "disconnected", "error"]
        ):
            self.update_connection_status(False, "Connection Error")

    def update_connection_status(self, connected, message):
        self.status_label.setText(message)
        if connected:
            self.status_label.setStyleSheet("color: green; font-weight: bold;")
            self.startup_timer.start()
        else:
            self.status_label.setStyleSheet("color: red; font-weight: bold;")
            self.version_label.setText("Firmware Version: Unknown")

    def handle_debug_info(self, debug_message):
        """处理调试信息"""
        # 可选：记录到调试日志
        if hasattr(self, "log_display"):
            self.log(f"[DEBUG] {debug_message}")

    def handle_error(self, error_message):
        """处理错误信号"""
        self.log(f"Error: {error_message}")

        # 如果是连接错误，更新状态
        if "not open" in error_message or "Failed to connect" in error_message:
            self.update_connection_status(False, "Connection Error")

    # ====== 命令发送相关 ======
    def send_debug_command(self):
        """发送调试命令"""
        cmd = self.debug_cmd_edit.text().strip()
        if cmd:
            self.send_command(cmd, "Debug")
            self.debug_cmd_edit.clear()

    def send_command(self, command, log_prefix="Sent"):
        """发送命令的通用方法"""
        if not self.is_connected():
            self.log("Not connected to Teensy")
            return False

        # 使用互斥锁防止并发发送
        if not self._query_mutex.tryLock():
            self.log("Warning: Command skipped due to busy state")
            return False

        try:
            success = self.serial_thread.send_command(command)
            if success:
                self.sent_display.append(command)
                self.log(f"{log_prefix} {command} command")
                return True
            else:
                self.log(f"Failed to send: {command}")
                return False
        finally:
            self._query_mutex.unlock()

    def wait_until_idle(self, timeout: float = 10) -> bool:
        """等待轴回到 IDLE 状态（线程安全）"""
        axis = self.get_current_axis()
        start = time.time()

        while time.time() - start < timeout:
            # 使用 get_axis_status 获取状态副本，避免竞态条件
            status = self.axis_manager.get_axis_status(axis)
            if status and status.get("state") == "IDLE":
                return True

            QApplication.processEvents()
            time.sleep(0.05)

        self.log(f"Axis {axis} wait-until-idle timed out after {timeout} s.")
        return False

    def send_homing(self):
        """发送 Homing 命令到当前轴"""
        axis = self.get_current_axis()

        # 使用协议轴值（与旧 Squid AXIS 类一致），不是固件内部数组索引
        _AXIS_PROTOCOL = {"X": AXIS.X, "Y": AXIS.Y, "Z": AXIS.Z, "W": AXIS.W}
        protocol_axis = _AXIS_PROTOCOL.get(axis)
        if protocol_axis is None:
            self.log(f"Axis {axis} does not support homing")
            return

        # 使用二进制命令发送 Homing
        # 2026-05-11：按 movement_sign 派生 data[3]（与老 Squid microcontroller.py:88 一致）
        # firmware 收 data[3] 后用它覆盖 _config.homing_direct
        #   X/Y sign=+1 → data[3]=1 (HOME_NEGATIVE=朝-方向)
        #   Z   sign=-1 → data[3]=0 (HOME_POSITIVE=朝+方向)
        sign = AXIS_CONFIG.get(axis, {}).get("movement_sign", 1)
        home_dir = 1 if sign == 1 else 0
        self._home_or_zero(protocol_axis, home_dir)

        # 更新状态
        self.axis_manager.axis_status[axis]["state"] = "HOMING_INIT"

        # 等待轴回到 IDLE（成功或超时）
        if not self.wait_until_idle(15):
            self.log(f"Axis {axis} homing timeout")
            return

        # Homing 完成后重新设置软限位（homing 过程中固件会禁用虚拟限位）
        if axis not in ("W", "E4"):
            self.set_limits()
        else:
            # 滤光轮 homing 完成后需要移动 offset
            offset_um = int(SQUID_FILTERWHEEL_OFFSET * 1000)  # mm -> μm
            self._move_step_axis_relative_position(axis, offset_um)
            self.log(f"Filter wheel {axis} moving offset: {offset_um} μm")

    def send_reset(self):
        command = format_command(self.get_current_axis(), "RESET")
        self.send_command(command)

    def move_forward(self):
        self.move_axis(True)

    def move_backward(self):
        self.move_axis(False)

    def previous_position(self):
        """移动到上一个位置（滤光轮/物镜）"""
        axis = self.get_current_axis()
        if axis in ("W", "E4"):  # W 和 E4 都是滤光轮
            self.move_filterwheel(False)
        elif axis == "E1":
            self.move_objective(False)

    def next_position(self):
        """移动到下一个位置（滤光轮/物镜）"""
        axis = self.get_current_axis()
        if axis in ("W", "E4"):  # W 和 E4 都是滤光轮
            self.move_filterwheel(True)
        elif axis == "E1":
            self.move_objective(True)

    def run_w_test(self):
        """W轴自动测试: homing → (next×7 → previous×7) × N 回合"""
        import threading

        rounds = self.control_panel.test_rounds_spin.value()

        def _test_worker():
            self.log(f"=== W Test Start ({rounds} rounds) ===")

            # 1. Homing
            self.log("W Test: Homing...")
            self.send_homing()

            # 等待 homing + offset 完成
            time.sleep(1.0)
            if not self.wait_until_idle(15):
                self.log("W Test: Homing timeout, abort.")
                return

            # 2. 正转+反转 × N 回合
            for r in range(rounds):
                self.log(f"W Test: Round {r+1}/{rounds}")

                for i in range(7):
                    time.sleep(0.5)
                    self.log(f"W Test: R{r+1} Next {i+1}/7")
                    self.move_filterwheel(True)
                    if not self.wait_until_idle(5):
                        self.log(f"W Test: R{r+1} Next {i+1} timeout, abort.")
                        return

                for i in range(7):
                    time.sleep(0.5)
                    self.log(f"W Test: R{r+1} Previous {i+1}/7")
                    self.move_filterwheel(False)
                    if not self.wait_until_idle(5):
                        self.log(f"W Test: R{r+1} Previous {i+1} timeout, abort.")
                        return

            self.log(f"=== W Test Done ({rounds} rounds) ===")

        threading.Thread(target=_test_worker, daemon=True).start()

    # ====== 数据处理相关 ======
    def handle_received_data(self, data):
        if data is None:
            return

        # 转发给集成测试面板，让 pending 的测试有机会捕获响应
        if hasattr(self, "test_panel") and self.test_panel is not None:
            self.test_panel.on_response(data)

        if data.startswith("S:VERSION:"):
            version = data.split(":")[-1].strip()
            self.version_label.setText(f"Firmware Version: {version}")
            self.log(f"Firmware version: {version}")
            return

        # 处理硬件信息响应: S:HWINFO:<axis>:TMC4361A+<driver>
        if data.startswith("S:HWINFO:") and "END" not in data:
            parts = data.split(":")
            if len(parts) >= 4:
                axis = parts[2]
                driver = parts[3].replace("TMC4361A+", "")
                self.axis_manager.update_axis_status(axis, {"driver": driver})
                self.axis_status_display.update_axis_status(
                    axis, self.axis_manager.get_axis_status(axis))
            return

        # 处理轴数据（parse_axis_data 只调用一次，用 parsed 缓存结果）
        parsed = self.axis_manager.parse_axis_data(data)
        if parsed:
            axis = self.axis_manager.last_parsed_axis
            status = self.axis_manager.get_axis_status(axis)

            self.axis_status_display.update_axis_status(axis, status)

            if axis == self.get_current_axis():
                self.update_current_axis_display(axis)

                if "position_mm" in status:
                    sign = AXIS_CONFIG[axis]["movement_sign"]
                    mm = float(status["position_mm"]) * sign
                    self.pos_label.setText(f"Current Position: {mm:.4f} mm")
                if "position_steps" in status:
                    sign = AXIS_CONFIG[axis]["movement_sign"]
                    s = int(status['position_steps']) * sign
                    if AXIS_CONFIG[axis].get("has_encoder"):
                        enc = int(status.get("encoder_steps", 0)) * sign
                        self.steps_label.setText(f"Encoder: {enc} | Microsteps: {s}")
                    else:
                        self.steps_label.setText(f"Microsteps: {s}")
                if "enabled" in status:
                    enabled = status["enabled"] == "YES"
                    self.axis_enabled_states[axis] = enabled
                    self.control_panel.set_enable_state(enabled)

        # 未识别的固件 ASCII 响应显示到日志
        self.log(data)

    def handle_binary_response(self, data: bytes):
        """处理固件 24 字节二进制位置上报包（不写入日志，10ms 周期调用）。

        响应包格式：
          byte[0]     : cmd_id
          byte[1]     : 状态 (0=完成, 1=运动中, 2=CRC错误)
          byte[2-5]   : X 轴位置（int32 大端序；编码器使能时为 ENC_POS，否则为 XACTUAL）
          byte[6-9]   : Y 轴位置
          byte[10-13] : Z 轴位置
          byte[14-17] : W 轴位置
          byte[18]    : 状态位（bit0=摇杆按钮）
          byte[19-21] : 保留
          byte[22]    : 固件版本（高半字节=主版本，低半字节=次版本）
          byte[23]    : CRC-8-CCITT
        """
        if len(data) < 24:
            return

        import struct
        steps = {
            "X": struct.unpack('>i', data[2:6])[0],
            "Y": struct.unpack('>i', data[6:10])[0],
            "Z": struct.unpack('>i', data[10:14])[0],
            "W": struct.unpack('>i', data[14:18])[0],
        }
        fw_status = data[1]   # 0=COMPLETED, 1=IN_PROGRESS, 2=CRC_ERROR
        moving_str = "YES" if fw_status == 1 else "NO"
        state_str  = "MOVING" if fw_status == 1 else "IDLE"

        # 更新 axis_manager 中所有轴的位置 + 运动状态
        # 位置值来源由固件决定：编码器使能返回 ENC_POS，否则返回 XACTUAL
        for axis, s in steps.items():
            mm_per_step = AXIS_MM_PER_STEP.get(axis, 0.0)
            position_mm = s * mm_per_step
            status_data = {
                "position_steps": s,
                "position_mm":    round(position_mm, 4),
                "moving":  moving_str,
                "state":   state_str,
            }
            self.axis_manager.update_axis_status(axis, status_data)
            self.axis_status_display.update_axis_status(
                axis, self.axis_manager.get_axis_status(axis)
            )

        # 更新当前轴详细显示（pos_label / steps_label / state labels）
        current_axis = self.get_current_axis()
        if current_axis in steps:
            s    = steps[current_axis]
            sign = AXIS_CONFIG[current_axis]["movement_sign"]
            mm_per_step = AXIS_MM_PER_STEP.get(current_axis, 0.0)
            um   = s * mm_per_step * 1000 * sign
            self.pos_label.setText(f"Current Position: {um:.2f} μm")
            if AXIS_CONFIG[current_axis].get("has_encoder"):
                self.steps_label.setText(f"Position (encoder): {um:.2f} μm")
            else:
                self.steps_label.setText(f"Position (steps): {um:.2f} μm")
            self.update_current_axis_display(current_axis)

    def log(self, message):
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        log_message = f"[{timestamp}] {message}"

        # 输出到GUI界面
        self.log_text_edit.append(log_message)

        # 如果保存到文件功能已启用，写入文件
        if self.save_log_checkbox.isChecked() and self.log_file:
            try:
                self.log_file.write(log_message + "\n")
                self.log_file.flush()  # 确保数据写入磁盘
            except Exception as e:
                self.log_text_edit.append(
                    f"[ERROR] Failed to write to log file: {str(e)}"
                )

    # ====== 查询相关 ======
    def query_firmware_version(self):
        self.send_command("S:VERSION", "Sent firmware version query")

    def query_hardware_info(self):
        self.send_command("S:HWINFO", "Sent hardware info query")

    def query_axis_status(self):
        # 生产固件（ENABLE_DEBUG 关闭）不发 ASCII 回包，直接用 axis_manager 缓存刷新 UI
        # 调试固件（ENABLE_DEBUG 打开）同时发 ASCII 查询命令，以获取更多状态
        current_axis = self.get_current_axis()
        self.update_current_axis_display(current_axis)
        status = self.axis_manager.get_axis_status(current_axis)
        if status:
            sign = AXIS_CONFIG[current_axis]["movement_sign"]
            s    = int(status.get("position_steps", 0))
            mm_per_step = AXIS_MM_PER_STEP.get(current_axis, 0.0)
            um   = s * mm_per_step * 1000 * sign
            self.pos_label.setText(f"Current Position: {um:.2f} μm")
            if AXIS_CONFIG[current_axis].get("has_encoder"):
                self.steps_label.setText(f"Position (encoder): {um:.2f} μm")
            else:
                self.steps_label.setText(f"Position (steps): {um:.2f} μm")
        # 调试构建时额外发 ASCII 命令（生产构建该命令无响应，但不影响功能）
        if self.is_connected():
            command = format_command(current_axis, "GET_DATA")
            self.serial_thread.send_command(command)

    def refresh_all_axes_status(self):
        if self.is_connected() and self.serial_thread is not None:
            # 使用互斥锁防止并发
            if not self._query_mutex.tryLock():
                return
            try:
                for axis in AXIS_CONFIG.keys():
                    command = format_command(axis, "GET_DATA")
                    self.serial_thread.send_command(command)
                    time.sleep(0.05)  # 稍微延迟，避免发送过快
            finally:
                self._query_mutex.unlock()

    # ====== 其他功能 ======
    def set_limits(self):
        if not self.is_connected() or self.serial_thread is None:
            self.log("Not connected to Teensy")
            return

        limits = self.control_panel.get_limits()
        if limits is None:
            self.log("Invalid limit values")
            return

        low, high = limits  # μm
        axis_name = self.get_current_axis()

        codes = AXIS_LIMIT_CODE_MAP.get(axis_name)
        if codes is None:
            self.log(f"Axis {axis_name} does not support soft limits")
            return

        # μm → microsteps: um / 1000 = mm, mm / mm_per_step = steps
        mm_per_step = AXIS_MM_PER_STEP.get(axis_name, 0.0)
        if mm_per_step == 0:
            self.log(f"Axis {axis_name}: mm_per_step not configured")
            return

        sign = AXIS_CONFIG[axis_name]["movement_sign"]
        low_usteps = int((low / 1000.0) / mm_per_step) * sign
        high_usteps = int((high / 1000.0) / mm_per_step) * sign

        # sign < 0 时正负方向翻转，确保 positive > negative
        if low_usteps > high_usteps:
            low_usteps, high_usteps = high_usteps, low_usteps

        pos_code, neg_code = codes
        for limit_code, usteps in [(pos_code, high_usteps), (neg_code, low_usteps)]:
            cmd = bytearray(8)
            cmd[1] = CMD_SET.SET_LIM
            cmd[2] = limit_code
            payload = int_to_payload(usteps, 4)
            cmd[3] = (payload >> 24) & 0xFF
            cmd[4] = (payload >> 16) & 0xFF
            cmd[5] = (payload >> 8) & 0xFF
            cmd[6] = payload & 0xFF
            self.serial_thread.send_binary_command(cmd)

        self.log(f"Set limits [{axis_name}]: Low={low} μm, High={high} μm")

    def _move_step_axis_relative_position(self, axis_name: str, distance: int) -> bool:
        """发送相对移动命令

        Args:
            axis_name: 轴名称 (X, Y, Z, W, E1, E3, E4)
            distance: 移动距离，单位 μm

        Returns:
            是否发送成功
        """
        if self.serial_thread is None:
            return False

        # 使用命令映射表获取正确的命令码
        move_cmd = AXIS_MOVE_CMD_MAP.get(axis_name)
        if move_cmd is None:
            self.log(f"Unknown axis for move command: {axis_name}")
            return False

        # μm → microsteps: distance_um / 1000 → mm, mm / mm_per_step → microsteps
        mm_per_step = AXIS_MM_PER_STEP.get(axis_name)
        if mm_per_step is None or mm_per_step == 0:
            self.log(f"No mm_per_step for axis: {axis_name}")
            return False
        microsteps = int(distance / 1000.0 / mm_per_step)

        cmd = bytearray(8)
        payload = int_to_payload(microsteps, 4)
        cmd[1] = move_cmd
        cmd[2] = payload >> 24
        cmd[3] = (payload >> 16) & 0xFF
        cmd[4] = (payload >> 8) & 0xFF
        cmd[5] = payload & 0xFF

        return self.serial_thread.send_binary_command(cmd)

    def _move_step_axis_absolute_position(self, axis_name: str, position: int) -> bool:
        """发送绝对移动命令

        Args:
            axis_name: 轴名称 (X, Y, Z, W, E1, E3, E4)
            position: 目标位置，单位 μm

        Returns:
            是否发送成功
        """
        if self.serial_thread is None:
            return False

        # 使用命令映射表获取正确的命令码
        moveto_cmd = AXIS_MOVETO_CMD_MAP.get(axis_name)
        if moveto_cmd is None:
            self.log(f"Unknown axis for moveto command: {axis_name}")
            return False

        # μm → microsteps: position_um / 1000 → mm, mm / mm_per_step → microsteps
        mm_per_step = AXIS_MM_PER_STEP.get(axis_name)
        if mm_per_step is None or mm_per_step == 0:
            self.log(f"No mm_per_step for axis: {axis_name}")
            return False
        microsteps = int(position / 1000.0 / mm_per_step)

        cmd = bytearray(8)
        payload = int_to_payload(microsteps, 4)
        cmd[1] = moveto_cmd
        cmd[2] = payload >> 24
        cmd[3] = (payload >> 16) & 0xFF
        cmd[4] = (payload >> 8) & 0xFF
        cmd[5] = payload & 0xFF

        return self.serial_thread.send_binary_command(cmd)

    def _home_or_zero(self, axis_index, home_dir=1):
        """发 HOME_OR_ZERO 命令。home_dir：0=HOME_POSITIVE(朝+方向), 1=HOME_NEGATIVE(朝-方向)."""
        if self.serial_thread is None:
            return
        cmd = bytearray(8)
        cmd[1] = CMD_SET.HOME_OR_ZERO
        cmd[2] = axis_index
        cmd[3] = home_dir
        self.serial_thread.send_binary_command(cmd)

    def _set_axis_enable(self, axis_name, enable):
        """发送 SET_AXIS_DISABLE_ENABLE 二进制命令，返回是否成功"""
        if self.serial_thread is None:
            return False
        _AXIS_PROTOCOL = {"X": AXIS.X, "Y": AXIS.Y, "Z": AXIS.Z, "W": AXIS.W}
        protocol_axis = _AXIS_PROTOCOL.get(axis_name)
        if protocol_axis is None:
            self.log(f"Axis {axis_name} does not support enable/disable via binary protocol")
            return False
        cmd = bytearray(8)
        cmd[1] = CMD_SET.SET_AXIS_DISABLE_ENABLE
        cmd[2] = protocol_axis
        cmd[3] = 1 if enable else 0
        self.serial_thread.send_binary_command(cmd)
        return True

    def send_velocity_acceleration(self, vel_mm_s: float, acc_mm_s2: float):
        """处理速度/加速度设置信号"""
        axis = self.get_current_axis()
        if self._set_max_velocity_acceleration(axis, vel_mm_s, acc_mm_s2):
            self.log(f"Axis {axis}: vel={vel_mm_s:.2f} mm/s, acc={acc_mm_s2:.1f} mm/s²")

    def _set_max_velocity_acceleration(self, axis_name, vel_mm_s, acc_mm_s2):
        """发送 SET_MAX_VELOCITY_ACCELERATION 二进制命令，返回是否成功"""
        if self.serial_thread is None:
            return False
        _AXIS_PROTOCOL = {"X": AXIS.X, "Y": AXIS.Y, "Z": AXIS.Z, "W": AXIS.W}
        protocol_axis = _AXIS_PROTOCOL.get(axis_name)
        if protocol_axis is None:
            self.log(f"Axis {axis_name} does not support velocity/acceleration setting")
            return False
        # 编码：vel × 100 → uint16，acc × 10 → uint16
        vel_uint16 = min(max(int(vel_mm_s * 100), 1), 65535)
        acc_uint16 = min(max(int(acc_mm_s2 * 10), 1), 65535)
        cmd = bytearray(8)
        cmd[1] = CMD_SET.SET_MAX_VELOCITY_ACCELERATION
        cmd[2] = protocol_axis
        cmd[3] = (vel_uint16 >> 8) & 0xFF
        cmd[4] = vel_uint16 & 0xFF
        cmd[5] = (acc_uint16 >> 8) & 0xFF
        cmd[6] = acc_uint16 & 0xFF
        self.serial_thread.send_binary_command(cmd)
        return True

    def move_axis(self, is_forward: bool) -> None:
        """相对位移移动

        Args:
            is_forward: True 表示前进方向，False 表示后退方向
        """
        if not self.is_connected():
            self.log("Not connected to Teensy")
            return

        distance = self.control_panel.get_move_distance()  # 单位: μm
        if distance is None:
            self.log("Invalid move distance")
            return

        # 确定移动方向
        value = distance if is_forward else -distance

        # 获取当前轴配置并应用移动符号
        axis = self.get_current_axis()
        value = int(AXIS_CONFIG[axis]["movement_sign"]) * value

        # 使用二进制命令发送相对移动（传入轴名称而非索引）
        self._move_step_axis_relative_position(axis, value)

    def moveto_axis(self, pos_um: float) -> None:
        """绝对位置移动

        Args:
            pos_um: 目标位置，单位 μm
        """
        if not self.is_connected():
            self.log("Not connected to Teensy")
            return

        axis = self.get_current_axis()
        value = int(AXIS_CONFIG[axis]["movement_sign"]) * int(pos_um)

        # 使用二进制命令发送绝对移动（传入轴名称而非索引）
        self._move_step_axis_absolute_position(axis, value)

    def move_filterwheel(self, is_next: bool) -> None:
        """移动滤光轮到下一个/上一个位置

        Args:
            is_next: True 移动到下一个位置，False 移动到上一个位置
        """
        if not self.is_connected():
            self.log("Not connected to Teensy")
            return

        from utils.constants import FILTERWHEEL_DISTANCE

        # 计算移动距离（单位：μm）
        distance_um = int(1000 * FILTERWHEEL_DISTANCE)
        value = distance_um if is_next else -distance_um

        # 获取当前轴
        axis = self.get_current_axis()

        # 使用二进制命令发送相对移动
        self._move_step_axis_relative_position(axis, value)

        direction = "Next" if is_next else "Previous"
        self.log(f"Filter wheel {axis} move ({direction}): {value} μm")

    def move_objective(self, is_next):
        distance_mm = OBJECTIVE_RATIO * SCREW_PITCH_W_MM / OBJECTIVE_HOLES
        distance_um = -1 * int(1000 * distance_mm)
        value = distance_um if is_next else -distance_um
        axis = self.get_current_axis()
        self._move_step_axis_relative_position(axis, value)
        direction = "Next" if is_next else "Previous"
        self.log(f"Sent objective move ({direction}): {value} μm")

    # ====== 照明命令发送 ======

    def _send_illu_port(self, port: int, intensity: int, on: bool):
        """SET_PORT_ILLUMINATION (cmd 37)：原子设置强度 + 开关"""
        if self.serial_thread is None:
            return
        cmd = bytearray(8)
        cmd[1] = CMDS.SET_PORT_ILLUMINATION
        cmd[2] = port
        cmd[3] = (intensity >> 8) & 0xFF
        cmd[4] = intensity & 0xFF
        cmd[5] = 1 if on else 0
        self.serial_thread.send_binary_command(cmd)
        state = "ON" if on else "OFF"
        self.log(f"Illumination port {port} {state}, intensity={intensity}")

    def _send_illu_turn_off_all(self):
        """TURN_OFF_ALL_PORTS (cmd 39)"""
        if self.serial_thread is None:
            return
        cmd = bytearray(8)
        cmd[1] = CMDS.TURN_OFF_ALL_PORTS
        self.serial_thread.send_binary_command(cmd)
        self.log("Illumination: Turn Off All Ports")

    def _send_illu_led_matrix(self, pattern: int, r: int, g: int, b: int):
        """SET_ILLUMINATION_LED_MATRIX (cmd 13)"""
        if self.serial_thread is None:
            return
        cmd = bytearray(8)
        cmd[1] = CMDS.SET_ILLUMINATION_LED_MATRIX
        cmd[2] = pattern
        cmd[3] = r
        cmd[4] = g
        cmd[5] = b
        self.serial_thread.send_binary_command(cmd)
        self.log(f"Illumination LED matrix: pattern={pattern} R={r} G={g} B={b}")

    def _send_illu_intensity_factor(self, pct: int):
        """SET_ILLUMINATION_INTENSITY_FACTOR (cmd 17)"""
        if self.serial_thread is None:
            return
        cmd = bytearray(8)
        cmd[1] = CMDS.SET_ILLUMINATION_INTENSITY_FACTOR
        cmd[2] = pct
        self.serial_thread.send_binary_command(cmd)
        self.log(f"Illumination intensity factor: {pct}%")

    def clear_sent_commands(self):
        self.sent_display.clear()

    def clear_log(self):
        self.log_text_edit.clear()

    def startup_launch(self):
        axis = self.get_current_axis()
        if axis not in ["E4", "W"]:
            self.set_limits()
        # 先下发 SET_LEAD_SCREW_PITCH + CONFIGURE_STEPPER_DRIVER，
        # 把固件 screwPitch/microstepping 拉回 Octoaxes 默认值
        # （防止此前旧 Squid 上位机把固件切到 32 细分残留）
        self._configure_actuators()
        # 再为有编码器的轴下发 CONFIGURE_STAGE_PID，使能编码器
        self._configure_encoders()
        self.startup_timer.stop()

    def _configure_actuators(self):
        """启动时下发 SET_LEAD_SCREW_PITCH + CONFIGURE_STEPPER_DRIVER
        使固件 screwPitch 与 microstepping 与 constants.py 的 actuator_* 字段一致。
        """
        if self.serial_thread is None:
            return
        _AXIS_PROTOCOL = {"X": AXIS.X, "Y": AXIS.Y, "Z": AXIS.Z}
        for axis_name, config in AXIS_CONFIG.items():
            protocol_axis = _AXIS_PROTOCOL.get(axis_name)
            if protocol_axis is None:
                continue
            pitch_mm = config.get("actuator_screw_pitch_mm")
            microstepping = config.get("actuator_microstepping")
            current_ma = config.get("actuator_motor_current_ma")
            hold_ratio = config.get("actuator_motor_hold_ratio")
            if None in (pitch_mm, microstepping, current_ma, hold_ratio):
                continue

            # SET_LEAD_SCREW_PITCH (cmd 23): data[2]=axis, data[3..4]=pitch*1000 (uint16 大端)
            pitch_x1000 = int(round(pitch_mm * 1000))
            cmd = bytearray(8)
            cmd[1] = CMD_SET.SET_LEAD_SCREW_PITCH
            cmd[2] = protocol_axis
            cmd[3] = (pitch_x1000 >> 8) & 0xFF
            cmd[4] = pitch_x1000 & 0xFF
            self.serial_thread.send_binary_command(cmd)

            # CONFIGURE_STEPPER_DRIVER (cmd 21):
            #   data[2]=axis, data[3]=microstepping 编码,
            #   data[4..5]=current_mA (uint16 大端), data[6]=hold*255
            # 微步编码（与旧 Squid 一致）: 1→0, 256→255, 其他→原值
            if microstepping == 1:
                ms_byte = 0
            elif microstepping >= 256:
                ms_byte = 255
            else:
                ms_byte = int(microstepping) & 0xFF
            current_int = int(round(current_ma)) & 0xFFFF
            hold_byte = max(0, min(255, int(round(hold_ratio * 255))))
            cmd = bytearray(8)
            cmd[1] = CMD_SET.CONFIGURE_STEPPER_DRIVER
            cmd[2] = protocol_axis
            cmd[3] = ms_byte
            cmd[4] = (current_int >> 8) & 0xFF
            cmd[5] = current_int & 0xFF
            cmd[6] = hold_byte
            self.serial_thread.send_binary_command(cmd)

            self.log(
                f"Actuator configured: {axis_name} pitch={pitch_mm}mm "
                f"microsteps={microstepping} current={current_ma}mA hold={hold_ratio}"
            )

    def _configure_encoders(self):
        """启动时为编码器轴下发 CONFIGURE_STAGE_PID"""
        if self.serial_thread is None:
            return
        _AXIS_PROTOCOL = {"X": AXIS.X, "Y": AXIS.Y, "Z": AXIS.Z, "W": AXIS.W}
        for axis_name, config in AXIS_CONFIG.items():
            if not config.get("has_encoder"):
                continue
            tpr = config.get("encoder_transitions_per_rev", 0)
            flip = config.get("encoder_flip_direction", False)
            protocol_axis = _AXIS_PROTOCOL.get(axis_name)
            if protocol_axis is None or tpr == 0:
                continue
            cmd = bytearray(8)
            cmd[1] = CMD_SET.CONFIGURE_STAGE_PID
            cmd[2] = protocol_axis
            cmd[3] = 1 if flip else 0
            cmd[4] = (tpr >> 8) & 0xFF
            cmd[5] = tpr & 0xFF
            self.serial_thread.send_binary_command(cmd)
            self.log(f"Encoder configured: {axis_name} tpr={tpr} flip={flip}")

    def closeEvent(self, event):
        # 关闭日志文件
        if self.save_log_checkbox.isChecked():
            self.close_log_file()

        # 停止所有定时器
        self.startup_timer.stop()
        self.status_timer.stop()

        # 关闭串口连接
        if self.serial_thread:
            self.serial_thread.stop()
            self.serial_thread.wait()

        event.accept()
