import serial
import traceback
import time
import threading

from collections import deque
from crc import CrcCalculator, Crc8
from PyQt5.QtCore import QThread, pyqtSignal, QMutex, QTimer, QMutexLocker

try:
    from utils.debug import debug_decorator, RecursionMonitor

    DEBUG_ENABLED = True
except ImportError:
    DEBUG_ENABLED = False

    def debug_decorator(func):
        return func


class SerialThread(QThread):
    """串口通信线程（支持字符串和二进制命令）"""

    data_received = pyqtSignal(str)
    # firmware 24-byte binary position-report packet (byte-level, not logged)
    binary_response = pyqtSignal(bytes)
    error_occurred = pyqtSignal(str)
    debug_info = pyqtSignal(str)
    # add a timer signal, used to start the timer on the main thread
    start_timer_signal = pyqtSignal()

    # firmware response-packet length (consistent with the firmware MSG_LENGTH)
    RESPONSE_LENGTH = 24                  # command response / octoaxes mainline broadcast length
    EXTENDED_RESPONSE_LENGTH = 40         # octoaxesplus extended position broadcast length
    EXTENDED_POS_CMD_ID = 0xFD            # extended position broadcast cmd_id marker

    def __init__(self, port, baudrate=115200):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.running = True
        self._send_lock = QMutex()
        self._read_lock = QMutex()
        self._command_count = 0
        self._error_count = 0
        self._connection_retries = 0
        self._max_retries = 3

        self.crc_calculator = CrcCalculator(Crc8.CCITT, table_based=True)

        # FIFO command buffer, stores tuples (command_type, command_data)
        # command_type: 'S' means a string command, 'B' means a binary command
        self._command_buffer = deque()
        self._buffer_lock = QMutex()
        self._buffer_size = 100  # maximum buffer capacity

        # send timer and interval
        self._send_timer = None
        self._send_interval_ms = 50  # 50ms send interval
        self._last_send_time = 0

        # debug info
        self._debug_buffer = []
        self._max_debug_buffer = 100

        # status flags
        self._port_open = False

        # connect the signal to ensure the timer is created and started on the main thread
        self.start_timer_signal.connect(self._init_timer)

    def run(self):
        """主运行循环"""
        self._log_debug(f"Starting serial thread for port: {self.port}")

        # emit the signal to create the timer on the main thread
        self.start_timer_signal.emit()

        while self.running and self._connection_retries < self._max_retries:
            try:
                self._open_serial_port()
                if self._port_open:
                    self._read_data_loop()
                else:
                    break

            except Exception as e:
                self._log_debug(f"Connection/read error: {e}")
                self._handle_connection_error()

                if self._connection_retries >= self._max_retries:
                    self.error_occurred.emit(
                        f"Failed to connect after {self._max_retries} retries"
                    )
                    break

                # wait and retry
                time.sleep(2)

        # stop the timer
        self._stop_timer()

    def _init_timer(self):
        """在主线程初始化定时器"""
        if not self._send_timer:
            self._send_timer = QTimer()
            self._send_timer.timeout.connect(self._process_command_buffer)
            self._send_timer.start(self._send_interval_ms)
            self._log_debug("Command buffer timer started")

    def _stop_timer(self):
        """停止定时器"""
        if self._send_timer:
            self._send_timer.stop()
            self._send_timer = None
            self._log_debug("Command buffer timer stopped")

    def _open_serial_port(self):
        """打开串口"""
        try:
            self._log_debug(f"Attempting to open {self.port} at {self.baudrate} baud")
            self.ser = serial.Serial(
                port=self.port, baudrate=self.baudrate, timeout=0.1, write_timeout=0.5
            )

            # wait for the serial port to stabilize
            time.sleep(0.5)

            if self.ser and self.ser.is_open:
                self._port_open = True
                self._connection_retries = 0
                self.data_received.emit(f"Connected to {self.port}")
                self._log_debug("Serial port opened successfully")
                return True
            else:
                self._port_open = False
                return False

        except Exception as e:
            self._port_open = False
            self._log_debug(f"Failed to open serial port: {e}")
            self.error_occurred.emit(f"Failed to open {self.port}: {str(e)}")
            return False

    def _read_data_loop(self):
        """数据读取循环（字节级解析，区分二进制响应包与 ASCII 调试行）"""
        byte_buf = bytearray()
        error_count = 0
        max_errors = 10

        while self.running and self._port_open:
            try:
                if not self._read_lock.tryLock():
                    time.sleep(0.01)
                    continue

                try:
                    bytes_waiting = self._get_bytes_waiting()
                    if bytes_waiting > 0:
                        data = self.ser.read(bytes_waiting)
                        if data:
                            byte_buf.extend(data)
                            byte_buf = self._parse_incoming(byte_buf)
                        error_count = 0
                    else:
                        time.sleep(0.01)

                finally:
                    self._read_lock.unlock()

            except serial.SerialException as e:
                error_count += 1
                self._log_debug(f"Serial error #{error_count}: {e}")
                if error_count > max_errors:
                    self._log_debug("Too many serial errors, closing port")
                    break
                time.sleep(0.1)

            except Exception as e:
                self._log_debug(f"Unexpected read error: {e}")
                break

    def _parse_incoming(self, buf: bytearray) -> bytearray:
        """从字节缓冲区中识别并消费二进制响应包和 ASCII 文本行。

        二进制响应包：
          - 24 字节标准包（旧 Squid / octoaxes 主线 / octoaxesplus 命令响应）
          - 40 字节扩展位置广播（octoaxesplus 周期性上报，首字节 cmd_id=0xFD）
        ASCII 调试行：以可打印字符开头，\\n 结尾。
        三者交织出现，逐字节判断。优先级：40 字节 (cmd_id=0xFD) > 24 字节 > ASCII。
        """
        RL = self.RESPONSE_LENGTH
        ERL = self.EXTENDED_RESPONSE_LENGTH
        EXT_CMD = self.EXTENDED_POS_CMD_ID
        while len(buf) > 0:
            # first try to match the 40-byte extended position packet (first byte cmd_id=0xFD)
            if len(buf) >= ERL and buf[0] == EXT_CMD:
                candidate = bytes(buf[:ERL])
                if self._validate_response_crc(candidate):
                    self.binary_response.emit(candidate)
                    del buf[:ERL]
                    continue

            # then try to match the 24-byte packet
            if len(buf) >= RL:
                candidate = bytes(buf[:RL])
                if self._validate_response_crc(candidate):
                    self.binary_response.emit(candidate)
                    del buf[:RL]
                    continue

            # try to match an ASCII text line (ending with a newline)
            nl = buf.find(b'\n')
            if nl >= 0:
                line = buf[:nl].decode('utf-8', errors='ignore').strip()
                del buf[:nl + 1]
                if line:
                    self.data_received.emit(line)
                    self._log_debug(f"Received: {line}")
                continue

            # not enough data, wait for more bytes
            # buffer cap: if it exceeds 4xRL and cannot be consumed, drop the oldest byte (prevents permanent blocking)
            if len(buf) > 4 * RL:
                self._log_debug(f"Parse stall, dropping byte: 0x{buf[0]:02x}")
                del buf[0]
                continue

            break

        return buf

    def _validate_response_crc(self, data: bytes) -> bool:
        """验证响应包 CRC-8-CCITT（最后一字节是校验，对前面所有字节计算）。

        支持两种长度：
          - 24 字节（标准）
          - 40 字节（octoaxesplus 扩展位置广播）
        """
        if len(data) not in (self.RESPONSE_LENGTH, self.EXTENDED_RESPONSE_LENGTH):
            return False
        expected = self.crc_calculator.calculate_checksum(data[:-1])
        return data[-1] == expected

    def _get_bytes_waiting(self):
        """安全获取等待字节数"""
        try:
            if self.ser and hasattr(self.ser, "in_waiting") and self.ser.is_open:
                result = self.ser.in_waiting
                # ensure an integer is returned
                return int(result) if result is not None else 0
            return 0

        except (AttributeError, ValueError, TypeError) as e:
            self._log_debug(f"Error getting bytes waiting: {e}")
            return 0

        except Exception as e:
            self._log_debug(f"Unexpected error in _get_bytes_waiting: {e}")
            return 0

    def _handle_connection_error(self):
        """处理连接错误"""
        self._connection_retries += 1
        self._port_open = False

        # clear the command buffer - using QMutexLocker
        locker = QMutexLocker(self._buffer_lock)
        self._command_buffer.clear()

        # try to close the serial port
        try:
            if self.ser and hasattr(self.ser, "close"):
                self.ser.close()
        except Exception as e:
            self._log_debug(f"Failed to close serial port during error handling: {e}")

        self._log_debug(
            f"Connection error, retry {self._connection_retries}/{self._max_retries}"
        )

    def send_command(self, command):
        """发送字符串命令到串口（使用FIFO缓冲区）"""
        if not self.running or not command:
            self._log_debug(f"Cannot send command, thread not running: {command}")
            return False

        # check whether the port is open
        if not self._port_open:
            self._log_debug(f"Cannot send command, port not open: {command}")
            return False

        # try to send directly (if the lock is available)
        if self._send_lock.tryLock():
            success = self._send_string_command_direct(command)
            self._send_lock.unlock()
            return success
        else:
            # the lock is held, put the command into the buffer
            return self._buffer_command("S", command)

    def send_binary_command(self, binary_data):
        """发送二进制命令到串口（使用FIFO缓冲区）"""
        if not self.running or not binary_data:
            self._log_debug(f"Cannot send binary command, thread not running")
            return False

        # check whether the port is open
        if not self._port_open:
            self._log_debug(f"Cannot send binary command, port not open")
            return False

        # try to send directly (if the lock is available)
        if self._send_lock.tryLock():
            success = self._send_binary_command_direct(binary_data)
            self._send_lock.unlock()
            return success
        else:
            # the lock is held, put the command into the buffer
            return self._buffer_command("B", binary_data)

    def _send_string_command_direct(self, command):
        """直接发送字符串命令（内部方法）"""
        self._command_count += 1
        success = False

        try:
            if self.ser and self._port_open:
                self._log_debug(
                    f"Sending string command #{self._command_count}: {command}"
                )

                # append a newline and encode
                encoded = b"\x55\xaa" + (command + "\n").encode("utf-8")
                bytes_written = self.ser.write(encoded)

                if bytes_written == len(encoded):
                    success = True
                    self._last_send_time = (
                        time.time() * 1000
                    )  # record the last send time (ms)
                    self._log_debug(f"String command sent successfully: {command}")
                else:
                    self._log_debug(
                        f"Only {bytes_written}/{len(encoded)} bytes written"
                    )
                    success = False

            else:
                self._log_debug(f"Cannot send, port not ready: {command}")

        except serial.SerialTimeoutException:
            self._log_debug(f"Timeout sending string command: {command}")
            success = False

        except serial.SerialException as e:
            self._error_count += 1
            error_msg = f"Serial error sending string '{command}': {str(e)}"
            self._log_debug(error_msg)
            self.error_occurred.emit(error_msg)

            # mark the port as closed
            self._port_open = False
            success = False

        except Exception as e:
            self._error_count += 1
            error_msg = f"Unexpected error sending string '{command}': {str(e)}"
            self._log_debug(error_msg)
            success = False

        return success

    def _send_binary_command_direct(self, binary_data):
        """直接发送二进制命令（内部方法）"""
        self._command_count += 1
        success = False

        # create a copy of the data to avoid modifying the original array
        data_to_send = bytearray(binary_data)
        data_to_send[0] = 0x02  # binary-command prefix
        data_to_send[-1] = self.crc_calculator.calculate_checksum(data_to_send[:-1])

        try:
            if self.ser and self._port_open:
                # record the binary data length (for debugging)
                data_length = len(data_to_send)
                self._log_debug(
                    f"Sending binary command #{self._command_count}, length: {data_length}"
                )

                # send the binary data copy
                bytes_written = self.ser.write(data_to_send)

                if bytes_written == len(data_to_send):
                    success = True
                    self._last_send_time = (
                        time.time() * 1000
                    )  # record the last send time (ms)
                    self._log_debug(
                        f"Binary command sent successfully, length: {data_length}"
                    )
                else:
                    self._log_debug(
                        f"Only {bytes_written}/{len(data_to_send)} bytes written for binary command"
                    )
                    success = False

            else:
                self._log_debug(f"Cannot send binary command, port not ready")

        except serial.SerialTimeoutException:
            self._log_debug(f"Timeout sending binary command")
            success = False

        except serial.SerialException as e:
            self._error_count += 1
            error_msg = f"Serial error sending binary command: {str(e)}"
            self._log_debug(error_msg)
            self.error_occurred.emit(error_msg)

            # mark the port as closed
            self._port_open = False
            success = False

        except Exception as e:
            self._error_count += 1
            error_msg = f"Unexpected error sending binary command: {str(e)}"
            self._log_debug(error_msg)
            success = False

        return success

    def _buffer_command(self, command_type, command_data):
        """将命令放入缓冲区"""
        locker = QMutexLocker(self._buffer_lock)
        if len(self._command_buffer) >= self._buffer_size:
            if command_type == "S":
                self._log_debug(f"Command buffer full, dropping string: {command_data}")
            else:
                self._log_debug(
                    f"Command buffer full, dropping binary command (length: {len(command_data)})"
                )
            return False

        self._command_buffer.append((command_type, command_data))
        self._log_debug(
            f"Command buffered (#{len(self._command_buffer)}), type: {command_type}"
        )
        return True

    def _process_command_buffer(self):
        """处理缓冲区中的命令（定时器调用）"""
        if not self.running or not self._port_open:
            return

        # check the send interval
        current_time = time.time() * 1000  # current time (ms)
        if current_time - self._last_send_time < self._send_interval_ms:
            return

        # check whether the buffer has commands
        with_buffer_lock = False
        command_type = None
        command_data = None

        # use tryLock instead of a blocking lock
        if self._buffer_lock.tryLock():
            try:
                if not self._command_buffer:
                    return

                # take one command out of the buffer
                command_type, command_data = self._command_buffer.popleft()
                self._log_debug(f"Processing buffered command, type: {command_type}")
                with_buffer_lock = True
            finally:
                self._buffer_lock.unlock()
        else:
            return

        # try to acquire the send lock
        if not self._send_lock.tryLock():
            # if acquiring the send lock fails, put the command back into the buffer
            if command_type is not None and command_data is not None:
                locker = QMutexLocker(self._buffer_lock)
                self._command_buffer.appendleft((command_type, command_data))
                self._log_debug(
                    f"Send lock busy, command requeued, type: {command_type}"
                )
            return

        try:
            # call different send functions based on the command type
            if command_type == "S":
                self._send_string_command_direct(command_data)
            elif command_type == "B":
                self._send_binary_command_direct(command_data)
            else:
                self._log_debug(f"Unknown command type in buffer: {command_type}")
        finally:
            self._send_lock.unlock()

    def _log_debug(self, message):
        """记录调试信息"""
        self._debug_buffer.append(message)
        if len(self._debug_buffer) > self._max_debug_buffer:
            self._debug_buffer.pop(0)

        if DEBUG_ENABLED:
            timestamp = time.strftime("%H:%M:%S")
            print(f"[{timestamp}] [SerialThread] {message}")

        # emit the debug signal (optional)
        try:
            self.debug_info.emit(message)
        except RuntimeError:
            # the signal may be invalid when the thread is shutting down
            pass

    def stop(self):
        """停止线程"""
        self._log_debug("Stopping serial thread")
        self.running = False
        self._port_open = False

        # stop the timer
        self._stop_timer()

        # clear the command buffer - using QMutexLocker
        locker = QMutexLocker(self._buffer_lock)
        self._command_buffer.clear()

        # wait a short while
        time.sleep(0.2)

        # close the serial port
        try:
            if self.ser and hasattr(self.ser, "close"):
                self.ser.close()
                self._log_debug("Serial port closed")
        except Exception as e:
            self._log_debug(f"Error closing port: {e}")

    def is_connected(self):
        """检查是否连接"""
        return self.running and self._port_open

    def get_status(self):
        """获取状态信息"""
        locker = QMutexLocker(self._buffer_lock)
        buffer_size = len(self._command_buffer)

        return {
            "running": self.running,
            "port_open": self._port_open,
            "port": self.port,
            "baudrate": self.baudrate,
            "command_count": self._command_count,
            "error_count": self._error_count,
            "connection_retries": self._connection_retries,
            "buffer_size": buffer_size,
            "max_buffer_size": self._buffer_size,
            "send_interval_ms": self._send_interval_ms,
        }

    def set_send_interval(self, interval_ms):
        """设置发送间隔（毫秒）"""
        if interval_ms > 0:
            self._send_interval_ms = interval_ms
            if self._send_timer:
                self._send_timer.setInterval(interval_ms)
            self._log_debug(f"Send interval set to {interval_ms}ms")

    def clear_command_buffer(self):
        """清空命令缓冲区"""
        locker = QMutexLocker(self._buffer_lock)
        count = len(self._command_buffer)
        self._command_buffer.clear()
        self._log_debug(f"Command buffer cleared, {count} commands removed")
        return count
