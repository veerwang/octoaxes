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
│       ├── platformio.ini   # PlatformIO 配置文件
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

## 开发环境搭建

### 1. 安装 PlatformIO

PlatformIO 是一个跨平台的嵌入式开发工具，支持多种安装方式：

#### 方式一：VS Code 扩展（推荐）

1. 安装 [Visual Studio Code](https://code.visualstudio.com/)
2. 打开 VS Code，进入扩展市场（Ctrl+Shift+X）
3. 搜索 "PlatformIO IDE" 并安装
4. 重启 VS Code，等待 PlatformIO 核心组件自动安装完成

#### 方式二：命令行安装（PlatformIO Core）

```bash
# 使用 pip 安装
pip install platformio

# 或使用安装脚本（Linux/macOS）
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py

# 验证安装
pio --version
```

#### 方式三：使用包管理器

```bash
# macOS (Homebrew)
brew install platformio

# Arch Linux
yay -S platformio

# 其他 Linux 发行版
pip install platformio
```

### 2. 安装 Teensy 支持

首次编译 Teensy 项目时，PlatformIO 会自动下载所需的工具链和库。如需手动安装：

```bash
# 安装 Teensy 平台支持
pio platform install teensy

# 安装 Teensy 上传工具（Linux 需要 udev 规则）
# 下载并安装 Teensy Loader：https://www.pjrc.com/teensy/loader.html
```

**Linux 用户注意**：需要添加 udev 规则以允许非 root 用户访问 Teensy：

```bash
# 下载 udev 规则
curl -fsSL https://www.pjrc.com/teensy/00-teensy.rules | sudo tee /etc/udev/rules.d/00-teensy.rules

# 重新加载规则
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### 3. 安装 Python 环境

```bash
# 创建虚拟环境（推荐）
python -m venv venv

# 激活虚拟环境
# Linux/macOS:
source venv/bin/activate
# Windows:
venv\Scripts\activate

# 安装依赖
pip install PyQt5 pyserial crc
```

## 固件编译与上传

### 编译环境说明

项目提供了多个编译环境，适用于不同场景：

| 环境 | 命令 | 用途 | CPU 频率 | 特点 |
|------|------|------|---------|------|
| `teensy41` | `pio run` | 生产环境 | 600 MHz | 优化体积，移除调试信息 |
| `teensy41_debug` | `pio run -e teensy41_debug` | 调试环境 | 600 MHz | 完整调试符号，日志记录 |
| `teensy41_dev` | `pio run -e teensy41_dev` | 开发环境 | 600 MHz | 严格警告检查 |
| `teensy41_fast` | `pio run -e teensy41_fast` | 高性能 | 720 MHz | 超频运行，最高优化 |

### 编译固件

```bash
# 进入固件目录
cd firmware/octoaxes

# 编译生产版本（默认）
pio run

# 编译指定环境
pio run -e teensy41_debug    # 调试版本
pio run -e teensy41_dev      # 开发版本（严格警告）
pio run -e teensy41_fast     # 高性能版本

# 清理编译产物
pio run --target clean
```

### 上传固件

```bash
# 上传到 Teensy（使用默认环境）
pio run --target upload

# 上传指定环境的固件
pio run -e teensy41_debug --target upload

# 编译并上传（一步完成）
pio run -t upload
```

### 串口监视器

```bash
# 打开串口监视器（波特率 2000000）
pio device monitor

# 指定串口
pio device monitor --port /dev/ttyACM0    # Linux
pio device monitor --port COM3            # Windows

# 带时间戳显示
pio device monitor --filter time

# 同时记录到文件（调试环境默认启用）
pio device monitor --filter log2file
```

### 编译选项

在 `platformio.ini` 中可以启用以下编译选项：

```ini
build_flags =
    -D DISABLE_LASER_INTERLOCK  ; 禁用激光安全联锁（仅限无激光系统）
    -D SPI_DEBUG                ; 启用 SPI 调试输出
    -D MOTION_PROFILE_DEBUG     ; 启用运动曲线调试
```

## 运行控制软件

### 启动软件

```bash
cd software
python main.py
```

### 基本操作流程

1. 连接 Teensy 4.1 到 PC（通过 USB）
2. 启动软件，从下拉列表选择正确的串口
3. 点击 **Connect** 建立连接
4. 点击 **Engine Start** 初始化电机驱动
5. 对需要的轴执行 **Homing** 操作（回原点）
6. 使用控制面板进行位置移动

### 常见问题

**Q: 找不到串口？**
- Linux：检查用户是否在 `dialout` 组：`sudo usermod -aG dialout $USER`
- Windows：安装 Teensy 驱动程序
- 检查 USB 线缆是否支持数据传输

**Q: 上传失败？**
- 按下 Teensy 上的复位按钮后重试
- 检查是否安装了 Teensy Loader
- Linux 用户检查 udev 规则是否正确配置

**Q: 编译报错？**
- 运行 `pio pkg update` 更新依赖
- 检查 PlatformIO 版本：`pio upgrade`

## 轴配置说明

| 轴 | 名称 | 用途 | 最大速度 | 螺距 | 微步 |
|---|------|------|---------|------|------|
| X | X 轴 | 水平位移 | 25 mm/s | 2.54 mm | 256 |
| Y | Y 轴 | 水平位移 | 25 mm/s | 2.54 mm | 256 |
| Z | Z 轴 | 垂直位移 | 3 mm/s | 0.3 mm | 256 |
| W | 滤光轮 1 | 滤光片切换 | - | 100 mm | 64 |
| E1 | 物镜切换 | 物镜选择 | - | 1 mm | 64 |
| E3 | 扩展 Z 轴 | 辅助 Z 轴 | 3 mm/s | 0.3 mm | 256 |
| E4 | 滤光轮 2 | 滤光片切换 | - | 100 mm | 64 |

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
