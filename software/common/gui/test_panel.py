"""集成测试面板 (Integration Test Tab)

两类按钮:
- 总测试 (Run All Tests): 依次跑完所有分项
- 分项测试 (Run): 表格内每行一个

结果以表格展示 (Test / Status / Details), 底部一行综合结论.

新增分项测试: 在 TESTS 列表加一项, 实现 _test_<id>() 方法即可.
测试通过 self.request_send_command 发命令, 通过 on_response() 接收 ASCII 响应,
内部用 QTimer 做超时 — 不阻塞 UI 线程.
"""

from PyQt5.QtCore import QTimer, pyqtSignal
from PyQt5.QtGui import QColor
from PyQt5.QtWidgets import (
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)


STATUS_PENDING = "PENDING"
STATUS_RUNNING = "RUNNING"
STATUS_PASS = "PASS"
STATUS_FAIL = "FAIL"

_STATUS_COLOR = {
    STATUS_PENDING: QColor("#888888"),
    STATUS_RUNNING: QColor("#1a7fb6"),
    STATUS_PASS: QColor("#2ca02c"),
    STATUS_FAIL: QColor("#d62728"),
}


class IntegrationTestPanel(QWidget):
    """集成测试 Tab"""

    request_send_command = pyqtSignal(str)
    log_message = pyqtSignal(str)

    # (test_id, 显示名, runner 方法名)
    TESTS = [
        ("firmware_version", "Firmware Version (S:VERSION)", "_test_firmware_version"),
        ("hardware_info",    "Hardware Info (S:HWINFO)",     "_test_hardware_info"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self._pending = {}   # test_id -> {"timer": QTimer, "handler": callable}
        self._results = {tid: {"status": STATUS_PENDING, "details": ""}
                         for tid, _, _ in self.TESTS}
        self._batch_queue = []
        self._batch_active = False

        self._build_ui()
        self._refresh_table()
        self._refresh_summary()

    # ====== UI ======

    def _build_ui(self):
        layout = QVBoxLayout(self)

        top = QHBoxLayout()
        self.run_all_btn = QPushButton("Run All Tests")
        self.run_all_btn.setStyleSheet(
            "QPushButton { font-size: 16px; font-weight: bold; padding: 10px 24px;"
            " background-color: #2980b9; color: white; border-radius: 4px; }"
            "QPushButton:hover { background-color: #2471a3; }"
            "QPushButton:disabled { background-color: #999; }"
        )
        self.run_all_btn.clicked.connect(self.run_all_tests)
        top.addWidget(self.run_all_btn)

        self.reset_btn = QPushButton("Reset Results")
        self.reset_btn.clicked.connect(self.reset_results)
        top.addWidget(self.reset_btn)

        top.addStretch()
        layout.addLayout(top)

        self.table = QTableWidget(len(self.TESTS), 4, self)
        self.table.setHorizontalHeaderLabels(["Test", "Status", "Details", "Action"])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionMode(QTableWidget.NoSelection)
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(2, QHeaderView.Stretch)
        header.setSectionResizeMode(3, QHeaderView.ResizeToContents)
        self.table.verticalHeader().setDefaultSectionSize(40)

        for row, (tid, name, _) in enumerate(self.TESTS):
            name_item = QTableWidgetItem(name)
            f = name_item.font()
            f.setBold(True)
            name_item.setFont(f)
            self.table.setItem(row, 0, name_item)
            self.table.setItem(row, 1, QTableWidgetItem(STATUS_PENDING))
            self.table.setItem(row, 2, QTableWidgetItem(""))
            btn = QPushButton("Run")
            btn.clicked.connect(lambda _, t=tid: self.run_single_test(t))
            self.table.setCellWidget(row, 3, btn)

        layout.addWidget(self.table)

        self.summary_label = QLabel()
        self.summary_label.setStyleSheet(
            "font-size: 16px; font-weight: bold; padding: 8px;"
        )
        layout.addWidget(self.summary_label)

    def _refresh_table(self):
        for row, (tid, _, _) in enumerate(self.TESTS):
            r = self._results[tid]
            status = r["status"]
            details = r["details"]
            status_item = QTableWidgetItem(status)
            status_item.setForeground(_STATUS_COLOR.get(status, QColor("black")))
            f = status_item.font()
            f.setBold(True)
            status_item.setFont(f)
            self.table.setItem(row, 1, status_item)
            self.table.setItem(row, 2, QTableWidgetItem(details))

    def _refresh_summary(self):
        total = len(self.TESTS)
        passed = sum(1 for r in self._results.values() if r["status"] == STATUS_PASS)
        failed = sum(1 for r in self._results.values() if r["status"] == STATUS_FAIL)
        running = sum(1 for r in self._results.values() if r["status"] == STATUS_RUNNING)
        if running:
            text = f"Running...  ({passed}/{total} passed, {failed} failed)"
            color = "#1a7fb6"
        elif passed + failed == 0:
            text = f"No tests run yet (0/{total})"
            color = "#888888"
        elif failed == 0:
            text = f"All passed: {passed}/{total}"
            color = "#2ca02c"
        elif passed == 0:
            text = f"All failed: {failed}/{total}"
            color = "#d62728"
        else:
            text = f"Partial: {passed}/{total} passed, {failed} failed"
            color = "#d68910"
        self.summary_label.setText(text)
        self.summary_label.setStyleSheet(
            f"font-size: 16px; font-weight: bold; color: {color}; padding: 8px;"
        )

    # ====== 公共 API ======

    def reset_results(self):
        self._cancel_pending()
        self._results = {tid: {"status": STATUS_PENDING, "details": ""}
                         for tid, _, _ in self.TESTS}
        self._refresh_table()
        self._refresh_summary()

    def run_all_tests(self):
        self.reset_results()
        self._batch_queue = [tid for tid, _, _ in self.TESTS]
        self._batch_active = True
        self.run_all_btn.setEnabled(False)
        self._run_next_in_batch()

    def run_single_test(self, test_id):
        self._batch_active = False
        self._batch_queue = []
        self.run_all_btn.setEnabled(True)
        self._start_test(test_id)

    def on_response(self, line: str):
        """SerialThread 收到的 ASCII 行: 由 main_window 转发"""
        if not line:
            return
        for tid in list(self._pending.keys()):
            self._pending[tid]["handler"](line)

    # ====== 内部 ======

    def _start_test(self, test_id):
        if test_id in self._pending:
            return
        self._results[test_id] = {"status": STATUS_RUNNING, "details": "running..."}
        self._refresh_table()
        self._refresh_summary()
        runner_name = next(m for tid, _, m in self.TESTS if tid == test_id)
        getattr(self, runner_name)()

    def _run_next_in_batch(self):
        if not self._batch_queue:
            self._batch_active = False
            self.run_all_btn.setEnabled(True)
            return
        next_tid = self._batch_queue.pop(0)
        self._start_test(next_tid)

    def _finalize(self, test_id, passed, details):
        info = self._pending.pop(test_id, None)
        if info:
            info["timer"].stop()
        self._results[test_id] = {
            "status": STATUS_PASS if passed else STATUS_FAIL,
            "details": details,
        }
        self._refresh_table()
        self._refresh_summary()
        prefix = "PASS" if passed else "FAIL"
        self.log_message.emit(f"[Test:{test_id}] {prefix} - {details}")

        if self._batch_active:
            QTimer.singleShot(50, self._run_next_in_batch)

    def _cancel_pending(self):
        for info in self._pending.values():
            info["timer"].stop()
        self._pending.clear()
        if self._batch_active:
            self._batch_active = False
            self.run_all_btn.setEnabled(True)

    # ====== 测试实现 ======

    def _test_firmware_version(self):
        """发 S:VERSION, 2s 内验证收到 'S:VERSION:x.y' 形式响应"""
        timer = QTimer(self)
        timer.setSingleShot(True)
        timer.timeout.connect(
            lambda: self._finalize("firmware_version", False,
                                   "Timeout: no S:VERSION response in 2.0s"))

        def handler(line: str):
            if not line.startswith("S:VERSION:"):
                return
            version = line.split(":", 2)[-1].strip()
            if version and "." in version:
                self._finalize("firmware_version", True, f"version = {version}")
            else:
                self._finalize("firmware_version", False, f"bad format: '{version}'")

        self._pending["firmware_version"] = {"timer": timer, "handler": handler}
        self.request_send_command.emit("S:VERSION")
        timer.start(2000)

    def _test_hardware_info(self):
        """发 S:HWINFO, 2.5s 内验证 X/Y/Z/W 都返回了 driver 信息"""
        EXPECTED_AXES = {"X", "Y", "Z", "W"}
        seen = {}

        timer = QTimer(self)
        timer.setSingleShot(True)

        def on_timeout():
            missing = EXPECTED_AXES - set(seen.keys())
            if not missing:
                summary = ", ".join(f"{a}={seen[a]}" for a in sorted(seen.keys()))
                self._finalize("hardware_info", True, summary)
            else:
                got = (", ".join(f"{a}={seen[a]}" for a in sorted(seen.keys()))
                       if seen else "none")
                self._finalize("hardware_info", False,
                               f"missing axes: {sorted(missing)}; got: {got}")

        timer.timeout.connect(on_timeout)

        def handler(line: str):
            if not line.startswith("S:HWINFO:") or "END" in line:
                return
            parts = line.split(":")
            if len(parts) < 4:
                return
            axis = parts[2]
            driver = parts[3].replace("TMC4361A+", "")
            seen[axis] = driver
            if EXPECTED_AXES.issubset(seen.keys()):
                summary = ", ".join(f"{a}={seen[a]}" for a in sorted(seen.keys()))
                self._finalize("hardware_info", True, summary)

        self._pending["hardware_info"] = {"timer": timer, "handler": handler}
        self.request_send_command.emit("S:HWINFO")
        timer.start(2500)
