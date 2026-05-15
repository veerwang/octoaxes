#! /usr/bin/env python3
# coding=utf-8

"""
octoaxesplus 上位机入口（squid++ 双相机 5 轴方案：X/Y/Z/W1/W2）。

运行方式：
    cd software/octoaxesplus
    python main.py
或：
    python software/octoaxesplus/main.py

启动逻辑：
1) 把 ../common 加入 sys.path，让通用 GUI / hardware / utils 模块可被 import
2) 把本目录加入 sys.path（让 constants.py 能作为顶级模块直接 import）
3) 把本 profile 的 constants 注册为 utils.constants 别名，使共享代码里的
   `from utils.constants import AXIS_CONFIG` 能找到 octoaxesplus 专属配置
4) 启动现有 PyQt GUI
"""
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SOFTWARE_ROOT = os.path.dirname(HERE)
COMMON = os.path.join(SOFTWARE_ROOT, "common")

# 共享模块（gui/, hardware/, utils/, define.py 等）走 common
sys.path.insert(0, COMMON)
# 本 profile 目录优先（含 constants.py）
sys.path.insert(0, HERE)

# 把 profile 的 constants 注册为 utils.constants 别名，共享代码无需改 import
import constants as _profile_constants  # noqa: E402
import utils  # noqa: E402
sys.modules["utils.constants"] = _profile_constants
utils.constants = _profile_constants

from PyQt5.QtWidgets import QApplication  # noqa: E402
from PyQt5.QtGui import QFont  # noqa: E402

from utils.debug import setup_exception_handler  # noqa: E402
from gui.main_window import TeensyControlGUI  # noqa: E402


def main():
    setup_exception_handler()
    app = QApplication(sys.argv)
    font = QFont("Arial", 14)
    app.setFont(font)
    window = TeensyControlGUI()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
