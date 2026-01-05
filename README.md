# Octoaxes

多轴显微镜运动控制系统，基于 SQUID 显微镜平台开发。支持 7 轴精密运动控制，适用于自动化显微成像应用。

## 功能特性

- **7 轴运动控制**：X、Y、Z 位移台，滤光轮（W、E4），物镜切换器（E1），扩展 Z 轴（E3）
- **精密定位**：支持 256 倍微步进，实现亚微米级定位精度
- **自动回原点**：每个轴支持独立的 Homing 功能，配合限位开关使用
- **实时状态监控**：通过 PyQt5 图形界面实时显示各轴位置和状态
- **硬件触发**：支持外部触发信号和照明系统控制

## 系统架构

```
┌─────────────────┐     USB/Serial     ┌─────────────────┐
│   PC 控制软件   │ ◄───────────────► │   Teensy 4.1    │
│    (PyQt5)      │     115200 baud    │    固件         │
└─────────────────┘                    └────────┬────────┘
                                                │ SPI
                                       ┌────────┴────────┐
                                       │   TMC4361A x7   │
                                       │   运动控制器    │
                                       └────────┬────────┘
                                                │
                                       ┌────────┴────────┐
                                       │   TMC2660 x7    │
                                       │   步进驱动器    │
                                       └─────────────────┘
```

## 目录结构

```
octoaxes/
├── firmware/                 # 嵌入式固件
│   └── octoaxes/
│       ├── octoaxes.ino     # Arduino 主程序
│       ├── config.h         # 系统和轴配置
│       ├── axis.h/.cpp      # 轴基类
│       ├── stepaxis.h/.cpp  # 步进电机轴实现
│       ├── filterwheel.h/.cpp   # 滤光轮控制
│       ├── objectives.h/.cpp    # 物镜切换器
│       ├── axesmrg.h/.cpp   # 轴管理器
│       ├── commandprocessor.h/.cpp  # 命令处理
│       ├── serial.h/.cpp    # 串口通信
│       └── TMC4361A*.h/.cpp # TMC4361A 驱动库
│
├── software/                 # PC 控制软件
│   ├── main.py              # 程序入口
│   ├── define.py            # 命令定义
│   ├── gui/                 # 图形界面
│   │   ├── main_window.py   # 主窗口
│   │   └── widgets.py       # UI 组件
│   ├── hardware/            # 硬件通信
│   │   ├── serial_thread.py # 串口线程
│   │   └── axis_manager.py  # 轴状态管理
│   └── utils/               # 工具模块
│       ├── constants.py     # 常数定义
│       └── helpers.py       # 辅助函数
│
└── documents/               # 文档资料
```

## 硬件要求

- **控制器**：Teensy 4.1 开发板
- **电机驱动**：TMC4361A + TMC2660 组合（每轴一组）
- **步进电机**：适配各轴的步进电机
- **限位开关**：每轴配备限位开关用于回原点

## 软件依赖

### 固件开发
- PlatformIO IDE
- Teensyduino

### PC 软件
- Python 3.8+
- PyQt5
- pyserial
- crc

## 安装与使用

### 1. 固件编译上传

使用 PlatformIO 打开 `firmware/octoaxes` 目录，编译并上传到 Teensy 4.1：

```bash
cd firmware/octoaxes
pio run --target upload
```

### 2. 安装 Python 依赖

```bash
cd software
pip install PyQt5 pyserial crc
```

### 3. 运行控制软件

```bash
cd software
python main.py
```

### 4. 基本操作流程

1. 连接 Teensy 4.1 到 PC
2. 启动软件，选择正确的串口
3. 点击 **Connect** 建立连接
4. 点击 **Engine Start** 初始化电机驱动
5. 对需要的轴执行 **Homing** 操作
6. 使用控制面板进行位置移动

## 轴配置说明

| 轴 | 名称 | 用途 | 最大速度 | 螺距 |
|---|------|------|---------|------|
| X | X 轴 | 水平位移 | 25 mm/s | 2.54 mm |
| Y | Y 轴 | 水平位移 | 25 mm/s | 2.54 mm |
| Z | Z 轴 | 垂直位移 | 3 mm/s | 0.3 mm |
| W | 滤光轮 1 | 滤光片切换 | - | 100 mm |
| E1 | 物镜切换 | 物镜选择 | - | 1 mm |
| E3 | 扩展 Z 轴 | 辅助 Z 轴 | 3 mm/s | 0.3 mm |
| E4 | 滤光轮 2 | 滤光片切换 | - | 100 mm |

## 主要命令

| 命令 | 说明 |
|------|------|
| `MOVE_X/Y/Z` | 相对位置移动 |
| `MOVETO_X/Y/Z` | 绝对位置移动 |
| `HOME_X/Y/Z` | 回原点操作 |
| `STOP` | 停止所有运动 |
| `GET_DATA` | 获取轴状态数据 |
| `VERSION` | 获取固件版本 |

## 许可证

MIT License

## 作者

kevin.wang
