"""集成测试面板 (Integration Test Tab)

两类按钮:
- 总测试 (Run All Tests): 依次跑完所有分项
- 分项测试 (Run): 表格内每行一个

结果以表格展示 (Test / Status / Details), 底部一行综合结论.

新增分项测试: 在 TESTS 列表加一项, 实现 _test_<id>() 方法即可.
测试通过 self.request_send_command 发命令, 通过 on_response() 接收 ASCII 响应,
内部用 QTimer 做超时 — 不阻塞 UI 线程.
"""

import time

from PyQt5.QtCore import QThread, QTimer, pyqtSignal
from PyQt5.QtGui import QColor
from PyQt5.QtWidgets import (
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QSpinBox,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from define import AXIS, CMD_SET
from utils.constants import AXIS_CONFIG, AXIS_MM_PER_STEP


class ZAgingWorker(QThread):
    """Z 轴老化测试后台线程（不阻塞 UI）。

    单圈：HOME → 等 dwell → 正向点动 ×fwd（每步 step_um，停 dwell）
                → 反向点动 ×bwd（每步 step_um，停 dwell）。循环 rounds 圈。

    完成判定镜像 z_aging_test.py：发命令 → 等单步预期耗时让 chip 走完 →
    读 axis_manager 的 Z position_steps 判位置稳定（连续相同=停）；并校验单步实际
    位移 ≈ 标称（不足=失步）。位置/状态来自 GUI 持续接收的 24 字节广播包，命令走
    serial_thread.send_binary_command（线程安全）。固件配置已由 GUI 启动时下发，无需重发。
    """

    progress = pyqtSignal(str)          # 实时进度文本
    finished_ok = pyqtSignal(bool, str)  # (success, summary)

    def __init__(self, main_window, rounds, step_um=1000.0,
                 fwd=26, bwd=25, dwell=0.5, parent=None):
        super().__init__(parent)
        self.mw = main_window
        self.rounds = rounds
        self.step_um = float(step_um)
        self.fwd = int(fwd)
        self.bwd = int(bwd)
        self.dwell = float(dwell)
        self.home_timeout = 70.0
        self._stop = False

        zc = AXIS_CONFIG["Z"]
        self.sign = zc["movement_sign"]
        self.mm_per_step = AXIS_MM_PER_STEP.get("Z")
        vel = zc.get("default_velocity", 3.0)
        acc = zc.get("default_acceleration", 20.0)
        step_mm = self.step_um / 1000.0
        self.expected = step_mm / vel + vel / acc           # 单步预期耗时
        self.deadline = self.expected * 3.0 + 0.5           # 稳定确认 deadline
        self.nominal = abs(int(self.sign * self.step_um / 1000.0 / self.mm_per_step))
        self.fwd_delta = int(self.sign * (+self.step_um) / 1000.0 / self.mm_per_step)
        self.bwd_delta = int(self.sign * (-self.step_um) / 1000.0 / self.mm_per_step)
        self.tol = max(2000, int(self.nominal * 0.3))

    def stop(self):
        self._stop = True

    # ── 底层 ─────────────────────────────────────────────────────────────
    def _send(self, b1, b2=0, b3=0, b4=0, b5=0, b6=0):
        st = getattr(self.mw, "serial_thread", None)
        if st is None:
            return False
        cmd = bytearray(8)
        cmd[1] = b1
        cmd[2] = b2
        cmd[3] = b3
        cmd[4] = b4
        cmd[5] = b5
        cmd[6] = b6
        return st.send_binary_command(cmd)

    def _z_steps(self):
        st = self.mw.axis_manager.get_axis_status("Z")
        return st.get("position_steps") if st else None

    def _send_move(self, delta):
        p = delta & 0xFFFFFFFF
        self._send(CMD_SET.MOVE_Z, (p >> 24) & 0xFF, (p >> 16) & 0xFF,
                   (p >> 8) & 0xFF, p & 0xFF)

    def _wait_stable(self, deadline):
        """位置连续两次相同 = 停车。返回 True=已稳定 / False=deadline 内仍在动。"""
        t0 = time.time()
        prev = self._z_steps()
        while time.time() - t0 < deadline:
            if self._stop:
                return True
            time.sleep(0.05)
            cur = self._z_steps()
            if cur is not None and cur == prev:
                return True
            prev = cur if cur is not None else prev
        return False

    def _home(self):
        # Z movement_sign=-1 → home_dir=0（HOME_POSITIVE），与 send_homing 一致
        home_dir = 1 if self.sign == 1 else 0
        self._send(CMD_SET.HOME_OR_ZERO, AXIS.Z, home_dir)
        time.sleep(0.3)
        t0 = time.time()
        prev = None
        stable = 0
        while time.time() - t0 < self.home_timeout:
            if self._stop:
                return False
            cur = self._z_steps()
            if cur is not None and cur == prev and abs(cur) < 8000:
                stable += 1
                if stable >= 3:     # 位置稳定且近 0 = homing 完成
                    return True
            else:
                stable = 0
            prev = cur
            time.sleep(0.1)
        return False

    def _run_dir(self, label, c, n, delta):
        for i in range(1, n + 1):
            if self._stop:
                return True, f"用户停止于第 {c} 圈 {label}{i}"
            prev = self._z_steps()
            self._send_move(delta)
            time.sleep(self.expected)            # 等预期耗时让 chip 走完
            settled = self._wait_stable(self.deadline)
            cur = self._z_steps()
            moved = abs(cur - prev) if (cur is not None and prev is not None) else 0
            self.progress.emit(f"第 {c}/{self.rounds} 圈 {label}{i}/{n}  Z={cur}  Δ={moved}")
            if not settled:
                return False, f"第 {c} 圈 {label}{i} 未到位(卡/失步)  Z={cur}"
            if moved < self.tol:
                return False, f"第 {c} 圈 {label}{i} 位移不足 {moved}<{self.tol}(失步)  Z={cur}"
            time.sleep(self.dwell)
        return True, ""

    def run(self):
        done = 0
        try:
            if getattr(self.mw, "serial_thread", None) is None or \
                    not self.mw.serial_thread.is_connected():
                self.finished_ok.emit(False, "未连接串口")
                return
            for c in range(1, self.rounds + 1):
                if self._stop:
                    break
                self.progress.emit(f"第 {c}/{self.rounds} 圈：HOME…")
                if not self._home():
                    self.finished_ok.emit(
                        False, f"第 {c} 圈 HOME 超时/中止（已完成 {done} 圈）")
                    return
                time.sleep(self.dwell)
                ok, msg = self._run_dir("正", c, self.fwd, self.fwd_delta)
                if not ok:
                    if self._stop:
                        break
                    self.finished_ok.emit(False, msg + f"（已完成 {done} 圈）")
                    return
                if self._stop:
                    break
                ok, msg = self._run_dir("反", c, self.bwd, self.bwd_delta)
                if not ok:
                    if self._stop:
                        break
                    self.finished_ok.emit(False, msg + f"（已完成 {done} 圈）")
                    return
                done += 1
            if self._stop:
                self.finished_ok.emit(True, f"已停止：完成 {done}/{self.rounds} 圈")
            else:
                self.finished_ok.emit(True, f"全部通过：{done}/{self.rounds} 圈")
        except Exception as e:  # noqa: BLE001
            self.finished_ok.emit(False, f"异常：{e}（已完成 {done} 圈）")


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
    AGING_TEST_ID = "z_aging"   # 长时序后台测试，不纳入 Run All 批量

    TESTS = [
        ("firmware_version", "Firmware Version (S:VERSION)", "_test_firmware_version"),
        ("hardware_info",    "Hardware Info (S:HWINFO)",     "_test_hardware_info"),
        (AGING_TEST_ID,      "Z 轴老化测试 (HOME→正26→反25/圈)", "_test_z_aging"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.main_window = None   # 由 main_window 创建后注入（发命令 + 读 axis 状态）
        self._pending = {}   # test_id -> {"timer": QTimer, "handler": callable}
        self._results = {tid: {"status": STATUS_PENDING, "details": ""}
                         for tid, _, _ in self.TESTS}
        self._batch_queue = []
        self._batch_active = False
        self._aging_worker = None   # ZAgingWorker 运行中实例

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

        self._row_buttons = {}
        self.rounds_spin = None
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
            self._row_buttons[tid] = btn

            if tid == self.AGING_TEST_ID:
                # 老化测试行：Action 单元格 = 轮数输入框 + Run/Stop（轮数是本测试专属参数）
                cell = QWidget()
                hb = QHBoxLayout(cell)
                hb.setContentsMargins(4, 2, 4, 2)
                hb.setSpacing(4)
                hb.addWidget(QLabel("轮数:"))
                self.rounds_spin = QSpinBox()
                self.rounds_spin.setRange(1, 1000000)
                self.rounds_spin.setValue(200)
                self.rounds_spin.setSingleStep(50)
                self.rounds_spin.setToolTip("Z 轴老化测试循环圈数（默认 200）")
                hb.addWidget(self.rounds_spin)
                hb.addWidget(btn)
                self.table.setCellWidget(row, 3, cell)
            else:
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
        # 老化测试是长时序后台任务，不纳入 Run All 批量
        self._batch_queue = [tid for tid, _, _ in self.TESTS
                             if tid != self.AGING_TEST_ID]
        self._batch_active = True
        self.run_all_btn.setEnabled(False)
        self._run_next_in_batch()

    def run_single_test(self, test_id):
        # 老化测试：运行中点同一按钮 = 停止
        if test_id == self.AGING_TEST_ID and self._aging_worker is not None:
            self._stop_aging()
            return
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
        if self._aging_worker is not None:
            self._aging_worker.stop()
        if self._batch_active:
            self._batch_active = False
            self.run_all_btn.setEnabled(True)

    # ====== Z 轴老化测试（后台 QThread） ======

    def _test_z_aging(self):
        """启动 Z 老化测试后台线程（不阻塞 UI）。轮数取自 rounds_spin。"""
        if self._aging_worker is not None:
            return
        if self.main_window is None or \
                getattr(self.main_window, "serial_thread", None) is None or \
                not self.main_window.serial_thread.is_connected():
            self._finalize_aging(False, "未连接串口")
            return
        rounds = self.rounds_spin.value()
        worker = ZAgingWorker(self.main_window, rounds)
        worker.progress.connect(self._on_aging_progress)
        worker.finished_ok.connect(self._on_aging_finished)
        self._aging_worker = worker
        self.rounds_spin.setEnabled(False)
        self._set_aging_running(True)
        self._results[self.AGING_TEST_ID] = {
            "status": STATUS_RUNNING, "details": f"启动：共 {rounds} 圈…"}
        self._refresh_table()
        self._refresh_summary()
        self.log_message.emit(f"[Test:{self.AGING_TEST_ID}] 启动 Z 老化测试 {rounds} 圈")
        worker.start()

    def _stop_aging(self):
        if self._aging_worker is not None:
            self.log_message.emit(f"[Test:{self.AGING_TEST_ID}] 请求停止…")
            self._aging_worker.stop()
            btn = self._row_buttons.get(self.AGING_TEST_ID)
            if btn is not None:
                btn.setText("Stopping…")
                btn.setEnabled(False)   # 停止中禁用，worker 结束后恢复

    _STOP_STYLE = (
        "QPushButton { background-color: #d62728; color: white;"
        " font-weight: bold; border-radius: 3px; padding: 3px 10px; }"
        "QPushButton:hover { background-color: #b71c1c; }"
    )

    def _set_aging_running(self, running):
        """切换老化行按钮外观：运行中=红色 Stop，空闲=默认 Run。"""
        btn = self._row_buttons.get(self.AGING_TEST_ID)
        if btn is None:
            return
        btn.setEnabled(True)
        if running:
            btn.setText("Stop")
            btn.setStyleSheet(self._STOP_STYLE)
        else:
            btn.setText("Run")
            btn.setStyleSheet("")

    def _on_aging_progress(self, text):
        self._results[self.AGING_TEST_ID] = {"status": STATUS_RUNNING, "details": text}
        self._refresh_table()
        self._refresh_summary()

    def _on_aging_finished(self, ok, summary):
        worker = self._aging_worker
        self._aging_worker = None
        if worker is not None:
            worker.wait(2000)
        self.rounds_spin.setEnabled(True)
        self._set_aging_running(False)
        self._finalize_aging(ok, summary)

    def _finalize_aging(self, passed, details):
        self._results[self.AGING_TEST_ID] = {
            "status": STATUS_PASS if passed else STATUS_FAIL, "details": details}
        self._refresh_table()
        self._refresh_summary()
        prefix = "PASS" if passed else "FAIL"
        self.log_message.emit(f"[Test:{self.AGING_TEST_ID}] {prefix} - {details}")

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
