"""
调试工具
"""
import traceback
import sys


def setup_exception_handler():
    """设置全局异常处理器"""
    def exception_handler(exctype, value, tb):
        print("=" * 80)
        print("Unhandled exception occurred:")
        print(f"Type: {exctype}")
        print(f"Value: {value}")
        print("Traceback:")
        traceback.print_tb(tb)
        print("=" * 80)
        
        # 调用默认处理器
        sys.__excepthook__(exctype, value, tb)
    
    sys.excepthook = exception_handler
