# CLAUDE.md

此文件为 Claude Code 在本仓库中工作时提供指导。

## 重要：开始工作前

**请先阅读以下文件了解项目当前状态：**
1. `TODO.md` - 查看待办任务
2. `SESSION.md` - 查看上次会话进度

## 项目概述

Octoaxes 是一个多轴显微镜运动控制系统，基于 SQUID 显微镜平台开发。支持 7 轴精密运动控制，适用于自动化显微成像应用。

### 技术栈

- **固件**: C++ (Arduino/PlatformIO), Teensy 4.1
- **上位机**: Python, PyQt5
- **硬件**: TMC4361A 运动控制器 + TMC2660/TMC2240 步进驱动器

### 目录结构

- `firmware/` - Teensy 4.1 嵌入式固件
- `software/` - PyQt5 PC 控制软件
- `documents/` - 文档资料

## 项目管理说明

本项目采用文件驱动的项目管理方式：

- **CLAUDE.md** - 项目指导和架构说明（本文件）
- **TODO.md** - 任务跟踪和待办事项
- **SESSION.md** - 会话记录，用于跨会话延续上下文

### 工作流程

1. **开始新会话时**：先阅读 TODO.md 和 SESSION.md 了解当前状态
2. **工作过程中**：及时更新 TODO.md 中的任务状态
3. **结束会话前**：更新 SESSION.md 记录进度和下一步计划

## 当前状态

**最后更新**: 2026-04-14
**当前进度**: octoaxesplus 空工程搭建 + squid++（双相机）硬件配置文档
**下一步**: 编码器硬件验证 + StealthChop 调优 + octoaxesplus 功能开发

### octoaxesplus 工程初始化 (2026-04-14)

为 squid++ 双相机版本创建独立固件工程，`firmware/octoaxesplus/`：

- **platformio.ini** 复用 octoaxes 配置（teensy41/debug/dev/fast 四环境）
- **tmc 目录共享** 符号链接 `octoaxesplus/tmc → ../octoaxes/tmc`，避免代码重复
- **空工程** `octoaxesplus.ino` 仅含空 setup/loop，编译通过（1.51s）
- **硬件配置** `documents/squid++（双相机）配置.md` 由 xlsx 转换，含 3 表：
  - Teensy 4.1 引脚定义（48 项）
  - 74HC154 16 路 SPI1 片选映射
  - MCP23S17_1 扩展 IO（8 轴 INTR/TARGET）

### 编码器与位置上报协议 (2026-04-13)

Z 轴编码器使能，响应包扩展至 32 字节，GUI 显示编码器/微步 μm 对比：

- **编码器分辨率常量**: `config.h` 中 `ENCODER_RESOLUTION_UM_X/Y/Z`（μm/pulse）
- **encoderLinesPerRev 公式**: `screwPitchMM * 1000 / ENCODER_RESOLUTION_UM`
- **invertEncoderDir**: 编码器方向反转配置，Z 轴 = true
- **响应包格式 (32 字节)**:
  - bytes[0-22]: 不变（cmd, status, X/Y/Z/W pos, btn, W encoder）
  - bytes[23-26]: Z 轴编码器位置（新增）
  - bytes[27-29]: 保留
  - byte[30]: 固件版本
  - byte[31]: CRC-8-CCITT
- **GUI 显示**: 有编码器的轴显示 `Encoder: xx.xx μm | Steps: xx.xx μm | Δ: xx.xx μm`

### 串口看门狗 (2026-04-10)

通信中断后自动关闭所有照明，防止激光/LED 无人值守：

- **命令 40 (SET_WATCHDOG_TIMEOUT)**: `[timeout_MSB..LSB]` 设置超时（ms）并使能，0=默认5s，上限1h
- **命令 42 (HEARTBEAT)**: 空操作心跳，看门狗靠收包（CRC 通过）重置，不靠此命令
- **主循环检查**: `watchdog_check()` 超时后调用 `turn_off_all_ports()` 单次触发
- **代码位置**: 变量和函数在 `illumination.h/cpp`，handler 在 `commandprocessor.cpp`

### 驱动芯片自动检测 (2026-03-25)

初始化时自动检测每轴驱动芯片类型，无需修改 config.h：

- **配置方式**: `config.h` 中所有轴 `.driverType = DRIVER_AUTO`，初始化时自动检测
- **检测方法**: TMC4361A reset 后用 format=0x0A + CDL=40 读取 TMC2240 IOIN.VERSION (0x40)
- **通信差异**: TMC2660 用 20-bit Cover (format 0x0A)，TMC2240 用 40-bit Cover (format 0x0D)
- **已知限制**: TMC2240 Cover READ 在 format=0x0D 下不可靠（auto SPI 干扰），写入用 shadow register 规避
- **查询命令**: `S:HWINFO` 返回各轴芯片型号，配套脚本 `software/tests/test_hwinfo.py`

### TMC4361A 编程文档完成 (2026-01-22)

完整的中文编程指南：`documents/TMC4361A_Programming_Guide.md`

- **版本**: v1.5（完整版）
- **内容**: 涵盖 TMC4361 数据手册全部 224 页
- **章节**: 29 章 + 2 个附录
- **主题**:
  - 基础配置（引脚、SPI 通信、斜坡配置）
  - 高级功能（目标管线、同步、电流缩放）
  - 编码器接口（增量式 ABN、串行 SSI/SPI）
  - 闭环控制（校准、PID、反电动势补偿）
  - 完整寄存器定义和电气特性

### 重构完成 (2026-01-21)

固件架构已按照官方 TMC-API 设计模式重构，采用 4 层架构：

```
┌─────────────────────────────────────────────────┐
│          应用层 (Axis, StepAxis, etc.)           │
├─────────────────────────────────────────────────┤
│          运动控制层 (MotorControl)               │
├─────────────────────────────────────────────────┤
│   驱动层 (TMC4361A, TMC2660/TMC2240 - icID API)   │
├─────────────────────────────────────────────────┤
│          硬件抽象层 (TMC_SPI)                    │
└─────────────────────────────────────────────────┘
```

**新增文件 (tmc/ 目录):**
- `tmc/hal/` - SPI 硬件抽象层
- `tmc/ic/TMC4361A/` - 运动控制器驱动
- `tmc/ic/TMC2660/` - TMC2660 步进驱动器驱动
- `tmc/ic/TMC2240/` - TMC2240 步进驱动器驱动
- `tmc/motion/` - 高级运动控制 API
- `tmc/helpers/` - 官方 API 辅助函数

**兼容性:**
- 旧 API (TMC4361ATypeDef*) 继续可用
- 新 API 使用 icID 标识符 (0-6)
- Axis 类自动链接两套 API

## 开发指南

### 固件编译

```bash
cd firmware/octoaxes
pio run                         # 编译生产版本
pio run -e teensy41_debug       # 编译调试版本
pio run --target upload         # 上传固件
```

### 运行上位机

```bash
cd software
python main.py
```

### 代码规范

- 固件代码遵循 Arduino/C++ 风格
- Python 代码遵循 PEP 8 规范
- 提交信息使用中文描述
