#! /usr/bin/env python3
# coding=utf-8

"""
description: Teensy Control GUI using PyQt5
author: kevin.wang
create date: 2024-07-17
version: 1.1.0
"""

import sys
from PyQt5.QtWidgets import QApplication
from PyQt5.QtGui import QFont

# 导入调试工具
from utils.debug import setup_exception_handler
from gui.main_window import TeensyControlGUI


def main():
    # 设置异常处理器
    setup_exception_handler()

    app = QApplication(sys.argv)

    # 设置全局字体
    font = QFont("Arial", 14)
    app.setFont(font)

    window = TeensyControlGUI()
    window.show()

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
