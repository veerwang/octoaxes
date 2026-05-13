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

**最后更新**: 2026-05-13（merge develop → maxpro）
**当前硬件**: X/Y/Z 硬件单元在 TMC2660 ↔ TMC2240 间切换过；2026-05-12 末次切回 TMC2660；W 长期 TMC2240
**当前进度（octoaxes 主线，来自 develop）**: Z 编码器启用 + XYZ 速度基线 + Homing 调试打印清理 + `Axis::moveRelativeMicrosteps` 静默 reject 修复 + 旧 Squid X 卡死 root cause + 协议层下降沿立即发包优化 + **Y homing 异响实测确定方案（256 微步 + 30 mm/s）+ 调试基础设施（S:SET_HOMING_VEL 命令 + diag_y_homing_noise.py 交互扫描脚本）**；响应包保持 24 字节与旧 Squid 兼容
**当前进度（octoaxesplus 双相机变体，来自 maxpro）**: 8 轴 config.h 主体重构完成 — SPI HAL 走 74HC154 片选 + 删除 DEPRECATED_PIN + TTL 扩 8 端口 + DAC CS 走 HC154 + MCP23S17_1 扩展 IO 驱动（轮询模式）+ pin 28 时钟/TTL5 冲突修复 + IIC/Serial2 占位补齐；两工程编译均 SUCCESS
**下一步**:
- octoaxes 主线：提交 Y homing 修复 + 工具脚本；TMC2240 SG4 调优（如再切回 TMC2240）；W 轴 60ms 优化；继续框架效率优化（Target Pipeline / 多轴并行 home）
- octoaxesplus 双相机：8 轴 AxisConfig 扩展（Z2/F1/F2/R/T）+ Axis 实例化（StepAxis/FilterWheel/Objectives 映射）；CAM_TRI_READY1/2 双相机握手；TRIGGER_IN/OUT1-2 联动

### octoaxesplus 工程（squid++ 双相机变体）

`firmware/octoaxesplus/` 是面向 squid++ 双相机硬件的 maxpro 分支独立固件工程，与 `firmware/octoaxes/` 并存。硬件资源差异通过 PCB 级片选/扩展 IO 抽象实现。

**工程结构 (2026-04-14)**:
- **platformio.ini** 复用 octoaxes 配置（teensy41/debug/dev/fast 四环境），common `build_flags` 增加 `-D USE_HC154_CS` 启用 HC154 片选分支
- **tmc 目录共享** 符号链接 `octoaxesplus/tmc → ../octoaxes/tmc`，避免代码重复；`TMC_SPI.cpp` 用 `#ifdef USE_HC154_CS` 在两套 CS 实现间切换
- **硬件配置文档** `documents/squid++（双相机）配置.md` 由 xlsx 转换，含 3 表：Teensy 4.1 引脚定义（48 项）/ 74HC154 16 路 SPI1 片选映射 / MCP23S17_1 扩展 IO（8 轴 INTR/TARGET）

**74HC154 4→16 片选译码器 (2026-04-14)**:
- A0-A3 地址引脚（pin 33/34/35/36），`HC154_Channel` 枚举 16 通道（按 squid++ §2 命名：MCP23S17_1 / DAC80508_1/2 / 轴 R/T/F2/Z2/F1/Z1/Y/X / EXPAND_NSCS1 等）
- `Pins::hc154_init()` + `Pins::hc154_select(channel)` 内联函数，事务前选通、事务后归零到 EXPAND_NSCS1 占位
- 轴 CS 不再是 GPIO 引脚号，改为 HC154 通道号（X=10, Y=9, Z1=8, F1=7）
- DAC8050x_CS=2（DAC80508_1 通道）、MCP23S17_1=0 通道

**CAMERA_TRIGGER 4→8 路扩展 (2026-04-14)**:
- 旧 4 路 (pin 29-32) 替换为 squid++ 8 路（pin 9/8/23/22/15/41/40/39），CAMERA_TRIGGER_1/2 即 CAM_TRI_OUT1/OUT2 双相机
- `trigger.h::NUM_TRIGGER_CHANNELS` 4→8，下游零修改自动适配

**MCP23S17_1 扩展 IO 驱动 (2026-04-17)**:
- 用于读取 8 轴 TMC4361A 的 INTR/TARGET_REACHED_OUT 信号（运动完成判定的硬件级路径，目前仍走 XACTUAL 轮询）
- `mcp23s17.h/cpp`：寄存器/opcode 定义 + SPI1 事务封装；CS 走 HC154 通道 0
- init 配置：IOCON=0x00（BANK=0/HAEN=0/顺序寻址）+ IODIRA/B=0xFF（16 路全输入）+ GPPUA/B=0xFF（100kΩ 上拉容错）+ GPINTENA/B=0x00（关硬件中断，轮询模式）
- API：`readReg/writeReg/readPortA/B/readGPIO`（一次事务连读 A+B 利用 SEQOP 地址自增）

**Pin 28 时钟/TTL5 冲突修复 (2026-04-17)**:
- 删除 `TMC4361_EXPAND_CLK = 28`（与 `ILLUMINATION_D5 = 28` 共用 pin，2 MHz PWM 干扰激光 TTL）；squid++ 单套时钟已够，仅 octoaxesplus 侧修复
- 补充占位：`IIC_WP=14, IIC_SDA=18, IIC_SCL=19`（Wire1 占位）、`RX2=16, TX2=17`（Serial2 占位）

**照明/DAC 适配 (2026-04-16)**:
- TTL 端口 D1-D5 重映射至 squid++ 分配（pin 32/31/30/29/28），新增 D6/D7/D8（pin 25/24/10），DAC 通道扩到 0-7
- DAC8050x SPI 事务 CS 走 HC154（hc154_select(DAC8050x_CS) → hc154_select(EXPAND_NSCS1)）
- `ILLUMINATION_INTERLOCK` 从 pin 2 改为 pin 38，`LED_DRIVER_SYNC` 占位 pin 255（squid++ 无独立 SYNC，待核实）

### 位置上报协议 (2026-04-17 修正，回到 24 字节)

响应包与旧 Squid 固件完全兼容（MSG_LENGTH = 24）：

- **响应包格式（24 字节，与旧 Squid 一致）**:
  - byte[0]: cmd_id
  - byte[1]: 执行状态（0=COMPLETED, 1=IN_PROGRESS, 2=CRC_ERROR）
  - bytes[2-5]: X 轴位置（int32 大端，微步）
  - bytes[6-9]: Y 轴位置
  - bytes[10-13]: Z 轴位置
  - bytes[14-17]: W 轴位置（旧 Squid 不填，Octoaxes 新增，对纯 XYZ 上位机兼容）
  - byte[18]: 状态位（bit0 = 摇杆按钮）
  - bytes[19-21]: 保留
  - byte[22]: 固件版本（高半字节=major，低半字节=minor）
  - byte[23]: CRC-8-CCITT of byte[0..22]
- **位置来源**：`getCurrentPositionMicrosteps()` — 按 `enableEncoder` 返回 XACTUAL 或 ENC_POS（字节布局不变，透明切换）
- **当前配置**：`software/utils/constants.py` 中 XYZW `has_encoder = False`，GUI 不下发 CONFIGURE_STAGE_PID，固件返回 XACTUAL

### 编码器基础设施（保留备用，当前关闭）

- **编码器分辨率常量**：`config.h` 中 `ENCODER_RESOLUTION_UM_X/Y/Z`（μm/pulse: 0.05/0.05/0.1）
- **encoderLinesPerRev 公式**：`screwPitchMM * 1000 / ENCODER_RESOLUTION_UM`
- **invertEncoderDir**：编码器方向反转配置项（Z 轴 = true，W 轴按硬件决定）
- **启用路径**：上位机 `CONFIGURE_STAGE_PID` (cmd 25) → `Axis::configureStagePID()` 运行时设 `_config.enableEncoder = true` → 调用 `motor_initABNEncoder()`

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
