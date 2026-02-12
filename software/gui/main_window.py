import time
import datetime
import os
from typing import Optional

from define import CMD_SET, AXIS_MOVE_CMD_MAP, AXIS_MOVETO_CMD_MAP
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
)
from PyQt5.QtCore import QTimer, Qt, QMutex
from PyQt5.QtGui import QFont

from hardware.serial_thread import SerialThread
from gui.widgets import AxisStatusDisplay, ControlPanel, LogDisplay
from hardware.axis_manager import AxisManager
from utils.constants import AXIS_CONFIG
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

        # 添加轴使能状态字典
        self.axis_enabled_states = {
            "X": True,
            "Y": True,
            "Z": True,
            "W": True,
            "E1": True,
            "E3": True,
            "E4": True,
        }

        self.init_ui()
        self.find_and_connect_teensy()
        self.setup_timers()

    def init_ui(self):
        self.setWindowTitle("Teensy Motor Control")
        self.setGeometry(100, 100, 1600, 1000)

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        # 顶部状态栏
        main_layout.addLayout(self.create_top_bar())

        # Engine Start 按钮
        main_layout.addLayout(self.create_engine_start_bar())

        # 主内容区域
        content_layout = QHBoxLayout()

        # 左侧控制面板
        left_panel = self.create_left_panel()
        content_layout.addWidget(left_panel)

        # 右侧状态显示
        right_panel = self.create_right_panel()
        content_layout.addWidget(right_panel)

        main_layout.addLayout(content_layout)

        # 发送命令显示
        main_layout.addWidget(self.create_sent_commands_group())

        # 日志显示
        main_layout.addWidget(self.create_log_group())

        # 重连按钮
        self.connect_btn = QPushButton("Reconnect")
        self.connect_btn.clicked.connect(self.find_and_connect_teensy)
        main_layout.addWidget(self.connect_btn)

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

    def create_engine_start_bar(self):
        layout = QHBoxLayout()
        self.engine_start_btn = QPushButton("Engine Start")
        self.engine_start_btn.clicked.connect(self.send_engine_start)
        self.engine_start_btn.setStyleSheet(
            "background-color: green; color: white; font-weight: bold; font-size: 16px;"
        )
        self.engine_start_btn.setMinimumHeight(40)
        self.engine_start_btn.setEnabled(False)
        layout.addWidget(self.engine_start_btn)
        layout.addStretch()
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
        self.control_panel.enable_toggled.connect(self.toggle_axis_enable)  # 新增
        self.control_panel.axis_changed.connect(self.on_axis_changed)
        self.control_panel.move_absolute_clicked.connect(self.moveto_axis)

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

    def create_right_panel(self):
        panel = QWidget()
        layout = QVBoxLayout(panel)

        # 所有轴状态显示
        self.axis_status_display = AxisStatusDisplay()
        self.axis_status_display.refresh_clicked.connect(self.refresh_all_axes_status)
        layout.addWidget(self.axis_status_display)

        return panel

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
        command = format_command(current_axis, "ENABLE")
        if self.send_command(command, "Sent enable command"):
            self.log(f"Axis {current_axis} enabled")
            # 更新轴状态显示
            self.axis_manager.update_axis_status(current_axis, {"enabled": "YES"})
            self.update_current_axis_display(current_axis)

    def disable_axis(self):
        """禁用轴"""
        current_axis = self.get_current_axis()
        command = format_command(current_axis, "DISABLE")
        if self.send_command(command, "Sent disable command"):
            self.log(f"Axis {current_axis} disabled")
            # 更新轴状态显示
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
            self.engine_start_btn.setEnabled(True)
        else:
            self.status_label.setStyleSheet("color: red; font-weight: bold;")
            self.engine_start_btn.setEnabled(False)
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

    def send_engine_start(self):
        command = "S:Engine Start"
        if self.send_command(command):
            self.engine_start_btn.setEnabled(False)
            self.engine_start_btn.setText("Engine Started")
            self.engine_start_btn.setStyleSheet(
                "background-color: gray; color: white; font-weight: bold; font-size: 16px;"
            )
            self.startup_timer.start()

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
        axis_index = int(AXIS_CONFIG[axis]["index"])

        # 使用二进制命令发送 Homing
        self._home_or_zero(axis_index)

        # 更新状态
        self.axis_manager.axis_status[axis]["state"] = "HOMING_INIT"

        # 等待轴回到 IDLE（成功或超时）
        if not self.wait_until_idle(15):
            self.log(f"Axis {axis} homing timeout")
            return

        # 滤光轮 homing 完成后需要移动 offset
        if axis in ("W", "E4"):
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
        """W轴自动测试: homing → (next×7 → previous×7) ×2"""
        import threading

        def _test_worker():
            self.log("=== W Test Start ===")

            # 1. Homing
            self.log("W Test: Homing...")
            self.send_homing()

            # 等待 homing + offset 完成
            time.sleep(1.0)
            if not self.wait_until_idle(15):
                self.log("W Test: Homing timeout, abort.")
                return

            # 2. 正转+反转 × 2 回合
            for r in range(2):
                self.log(f"W Test: Round {r+1}/2")

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

            self.log("=== W Test Done ===")

        threading.Thread(target=_test_worker, daemon=True).start()

    # ====== 数据处理相关 ======
    def handle_received_data(self, data):
        if data is None:
            return

        if data.startswith("S:VERSION:"):
            version = data.split(":")[-1].strip()
            self.version_label.setText(f"Firmware Version: {version}")
            self.log(f"Firmware version: {version}")
            return

        # 处理轴数据
        if self.axis_manager.parse_axis_data(data):
            axis = self.axis_manager.last_parsed_axis
            status = self.axis_manager.get_axis_status(axis)

            # 更新状态显示
            self.axis_status_display.update_axis_status(axis, status)

            # 如果是当前轴，更新详细显示
            if axis == self.get_current_axis():
                self.update_current_axis_display(axis)

                # 更新位置显示
                if "position_mm" in status:
                    value = (
                        float(status["position_mm"])
                        * AXIS_CONFIG[axis]["movement_sign"]
                    )
                    self.pos_label.setText(f"Current Position: {value} mm")
                if "position_steps" in status:
                    value = status["position_steps"] * AXIS_CONFIG[axis]["movement_sign"]
                    self.steps_label.setText(f"Current Position: {value} microsteps")

                # 更新使能状态（如果从轴状态中获取）
                if "enabled" in status:
                    enabled = status["enabled"] == "YES"
                    self.axis_enabled_states[axis] = enabled
                    self.control_panel.set_enable_state(enabled)

        # 记录到日志
        if data:
            self.log(f"Received: {data}")

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
                # 尝试重新打开文件
                self.close_log_file()
                self.open_log_file()

    # ====== 查询相关 ======
    def query_firmware_version(self):
        self.send_command("S:VERSION", "Sent firmware version query")

    def query_axis_status(self):
        command = format_command(self.get_current_axis(), "GET_DATA")
        self.send_command(command, "Sent axis status query")

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
        if not self.is_connected():
            self.log("Not connected to Teensy")
            return

        limits = self.control_panel.get_limits()
        if limits is None:
            self.log("Invalid limit values")
            return

        low, high = limits
        from utils.helpers import pack_limit_command

        command = format_command(self.get_current_axis(), pack_limit_command(low, high))
        self.send_command(command, f"Sent limits: Low={low} μm, High={high} μm")

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

        cmd = bytearray(8)
        payload = int_to_payload(distance, 4)
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

        cmd = bytearray(8)
        payload = int_to_payload(position, 4)
        cmd[1] = moveto_cmd
        cmd[2] = payload >> 24
        cmd[3] = (payload >> 16) & 0xFF
        cmd[4] = (payload >> 8) & 0xFF
        cmd[5] = payload & 0xFF

        return self.serial_thread.send_binary_command(cmd)

    def _home_or_zero(self, axis_index):
        if self.serial_thread is None:
            return
        cmd = bytearray(8)
        cmd[1] = CMD_SET.HOME_OR_ZERO
        cmd[2] = axis_index
        cmd[3] = 0
        self.serial_thread.send_binary_command(cmd)

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
        distance = -1 * int(1000 * distance_mm)
        value = distance if is_next else -distance
        from utils.helpers import pack_move_command

        base_command = pack_move_command(value)
        command = format_command(self.get_current_axis(), base_command)
        direction = "Next" if is_next else "Previous"
        self.send_command(command, f"Sent objective move ({direction})")

    def clear_sent_commands(self):
        self.sent_display.clear()

    def clear_log(self):
        self.log_text_edit.clear()

    def startup_launch(self):
        axis = self.get_current_axis()
        if axis not in ["E4", "W"]:
            self.set_limits()
        self.startup_timer.stop()

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
