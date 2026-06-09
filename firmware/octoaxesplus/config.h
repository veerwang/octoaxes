#ifndef CONFIG_H
#define CONFIG_H

#include "axis.h"
#include "def_octopi_80120.h"

namespace Commands {
    const int MOVE_X = 0;
    const int MOVE_Y = 1;
    const int MOVE_Z = 2;
    const int MOVE_THETA = 3;
    const int MOVE_W = 4;
    const int HOME_OR_ZERO = 5;
    const int MOVETO_X = 6;
    const int MOVETO_Y = 7;
    const int MOVETO_Z = 8;
    const int SET_LIM = 9;
    const int TURN_ON_ILLUMINATION = 10;
    const int TURN_OFF_ILLUMINATION = 11;
    const int SET_ILLUMINATION = 12;
    const int SET_ILLUMINATION_LED_MATRIX = 13;
    const int ACK_JOYSTICK_BUTTON_PRESSED = 14;
    const int ANALOG_WRITE_ONBOARD_DAC = 15;
    const int SET_DAC80508_REFDIV_GAIN = 16;
    const int SET_ILLUMINATION_INTENSITY_FACTOR = 17;
    const int MOVETO_W = 18;
    const int MOVE_W2 = 19;
    const int SET_LIM_SWITCH_POLARITY = 20;
    const int CONFIGURE_STEPPER_DRIVER = 21;
    const int SET_MAX_VELOCITY_ACCELERATION = 22;
    const int SET_LEAD_SCREW_PITCH = 23;
    const int SET_OFFSET_VELOCITY = 24;
    const int CONFIGURE_STAGE_PID = 25;
    const int ENABLE_STAGE_PID = 26;
    const int DISABLE_STAGE_PID = 27;
    const int SET_HOME_SAFETY_MERGIN = 28;
    const int SET_PID_ARGUMENTS = 29;
    const int SEND_HARDWARE_TRIGGER = 30;
    const int SET_STROBE_DELAY = 31;
    const int SET_AXIS_DISABLE_ENABLE = 32;
    const int SET_TRIGGER_MODE = 33;
    // 多端口照明命令（v1.0+）
    const int SET_PORT_INTENSITY = 34;
    const int TURN_ON_PORT = 35;
    const int TURN_OFF_PORT = 36;
    const int SET_PORT_ILLUMINATION = 37;
    const int SET_MULTI_PORT_MASK = 38;
    const int TURN_OFF_ALL_PORTS = 39;
    // 安全与心跳
    const int SET_WATCHDOG_TIMEOUT = 40;  // 设置串口看门狗超时（ms），使能后通信中断自动关灯
    const int SET_PIN_LEVEL = 41;
    const int HEARTBEAT = 42;             // 空操作心跳（看门狗靠收包重置，不靠此命令）
    const int MOVETO_W2 = 43;             // octoaxesplus W2 绝对运动命令（MOVE_W2=19 的配套）
    const int MOVE_TURRET   = 44;             // E1 物镜转换器相对运动，data[2..5] = int32 微步大端（复用 octoaxes E1 协议）
    const int MOVETO_TURRET = 45;             // E1 物镜转换器绝对运动
    const int INITFILTERWHEEL_W2 = 252;
    const int INITFILTERWHEEL = 253;
    const int INITIALIZE = 254;
    const int RESET = 255;
}

// 引脚定义
namespace Pins {
    // squid++ 双相机：所有 SPI 片选走 74HC154（见本文件 HC154_Channel 枚举）
    // 下述 X/Y/Z/W_AXIS_CS、DAC8050x_CS 语义不再是 GPIO 引脚号，而是 HC154 通道号(0-15)
    // TMC_SPI HAL 在 USE_HC154_CS 编译分支下按通道号调用 Pins::hc154_select()
    const int POWER_GOOD = 0;
    const int TMC4361_STANDARD_CLK = 37;
    // squid++ 双相机取消 TMC4361 扩展时钟（原 pin 28 被 TTL5 占用，且 8 轴全挂 SPI1
    // 单套时钟已够）。CLOCK_EXPAND 仅作为 tmc_ic_configs 的运行时标识保留

    // 轴 SPI 片选（HC154 通道号，非 GPIO）
    const int X_AXIS_CS  = 10;  // HC154 Y10 = AXIS_X
    const int Y_AXIS_CS  = 9;   // HC154 Y9  = AXIS_Y
    const int Z_AXIS_CS  = 8;   // HC154 Y8  = AXIS_Z（主焦点 Z）
    const int W1_AXIS_CS = 6;   // HC154 Y6  = AXIS_W1（滤光转盘 1，占用原 Z2 CS）
    const int W2_AXIS_CS = 4;   // HC154 Y4  = AXIS_W2（滤光转盘 2，占用原 T  CS）

    // 历史别名（保留方便未来恢复 squid++ 8 轴方案，当前未被代码引用）
    const int W_AXIS_CS  = 7;   // HC154 Y7  = 原 AXIS_F1（octoaxes 主线 W 滤光转盘）
    const int Z2_AXIS_CS = 6;   // [deprecated] 现由 W1_AXIS_CS=6 取代
    const int F2_AXIS_CS = 5;   // HC154 Y5  = 原 AXIS_F2（未使用，预留）
    const int R_AXIS_CS  = 3;   // HC154 Y3  = 原 AXIS_R （未使用，预留）
    const int T_AXIS_CS  = 4;   // [deprecated] 现由 W2_AXIS_CS=4 取代

    // EXPAND1-4_AXIS_CS 旧别名已于 2026-05-13 删除（审计发现完全无引用）
    // squid++ 用 Z2_AXIS_CS=6 / F2_AXIS_CS=5 / R_AXIS_CS=3 / T_AXIS_CS=4 取代
    // 注意：EXPAND1_AXIS / EXPAND3_AXIS / EXPAND4_AXIS AxisConfig 仍保留，
    // 作为 R/T 等扩展轴的 AxisConfig 模板源（const struct 拷贝引用）

    // DAC80508_1 SPI 片选（HC154 通道号）
    const int DAC8050x_CS = 2;   // HC154 Y2 = DAC80508_1（8LED 模拟输出）

    // 照明 TTL 端口（D1-D8，squid++ 双相机）
    // squid++ 表：pin 32/31/30/29/28 = TTL1-5，pin 25/24/10 = TTL6-8
    const int ILLUMINATION_D1 = 32;
    const int ILLUMINATION_D2 = 31;
    const int ILLUMINATION_D3 = 30;
    const int ILLUMINATION_D4 = 29;
    const int ILLUMINATION_D5 = 28;
    const int ILLUMINATION_D6 = 25;
    const int ILLUMINATION_D7 = 24;
    const int ILLUMINATION_D8 = 10;

    // 激光安全联锁（squid++ pin 38，LOW = 安全）
    const int ILLUMINATION_INTERLOCK = 38;

    // LED 矩阵（APA102，128 像素；SPI2 MOSI/SCK）
    const int LED_MATRIX_DATA  = 26;
    const int LED_MATRIX_CLOCK = 27;

    // LED 驱动 LT3932 SYNC（squid++ 无独立 SYNC 引脚，保留旧常量占位）
    // 原 octoaxes pin 25 现为 TTL6；如需 LT3932 请硬件重新指派
    const int LED_DRIVER_SYNC = 255;  // 无效引脚；analogWrite/pinMode 空操作

    // I²C 总线（Wire1，squid++ pin 18/19；pin 14 为 EEPROM/器件写保护）
    // 占位：未挂载具体设备前不初始化，待外设方案确定后使能
    const int IIC_WP  = 14;
    const int IIC_SDA = 18;
    const int IIC_SCL = 19;

    // Serial2 串口（pin 16=RX2, pin 17=TX2）— 占位，用途待定
    const int RX2 = 16;
    const int TX2 = 17;

    // 相机触发（squid++ 双相机：8 路，来源 documents/squid++（双相机）配置.md §1）
    const int CAMERA_TRIGGER_1 = 9;    // CAM_TRI_OUT1（相机 1）
    const int CAMERA_TRIGGER_2 = 8;    // CAM_TRI_OUT2（相机 2）
    const int CAMERA_TRIGGER_3 = 23;
    const int CAMERA_TRIGGER_4 = 22;
    const int CAMERA_TRIGGER_5 = 15;
    const int CAMERA_TRIGGER_6 = 41;
    const int CAMERA_TRIGGER_7 = 40;
    const int CAMERA_TRIGGER_8 = 39;

    // 外部触发 IN/OUT（squid++ 双相机：与外部设备双向同步，pin 1-4）
    // OUT：固件主动输出脉冲/电平驱动外部设备（如另一台显微镜/光源/采集卡）
    // IN：固件接收外部设备的触发输入（边沿/电平），按命令决定下一步动作
    // 来源：documents/squid++（双相机）配置.md §1
    const int TRIGGER_OUT1 = 1;
    const int TRIGGER_IN1  = 2;
    const int TRIGGER_OUT2 = 3;
    const int TRIGGER_IN2  = 4;

    // 双相机握手 READY 输入（squid++ 双相机：相机就绪/采集完成反馈）
    // 注意：squid++ 文档 pin 6/7 描述与名称不一致（pin 6 标"相机1_触发"、pin 7 标
    // "相机1_等待触发"，与名称 CAM_TRI_READY2/1 不对应），按命名作为输入引脚处理，
    // 文档描述存疑，待核实原表
    const int CAM_TRI_READY1 = 7;   // 相机 1 READY 反馈
    const int CAM_TRI_READY2 = 6;   // 相机 2 READY 反馈

    // 74HC154 4→16 译码器片选（squid++ 双相机）
    // A3:A2:A1:A0 二进制值 n → Yn 输出拉低，其余保持高；作为所有 SPI 设备的统一片选
    // 来源：documents/squid++（双相机）配置.md §2
    const int HC154_A0 = 33;
    const int HC154_A1 = 34;
    const int HC154_A2 = 35;
    const int HC154_A3 = 36;

    enum HC154_Channel : uint8_t {
        HC154_MCP23S17_1   = 0,   // 扩展 IO #1（8 轴 INTR/TARGET）
        HC154_DAC80508_2   = 1,
        HC154_DAC80508_1   = 2,   // 8LED 模拟信号输出
        HC154_AXIS_R       = 3,   // [unused] 物镜旋转 R
        HC154_AXIS_T       = 4,   // [unused, alias of W2] 物镜平移 T
        HC154_AXIS_W2      = 4,   // 滤光转盘 W2（当前用途，与 T 同通道）
        HC154_AXIS_F2      = 5,   // [unused] 滤光转盘 F2
        HC154_AXIS_Z2      = 6,   // [unused, alias of W1] 双焦点 Z2
        HC154_AXIS_W1      = 6,   // 滤光转盘 W1（当前用途，与 Z2 同通道）
        HC154_AXIS_F1      = 7,   // [unused] 滤光转盘 F1
        HC154_AXIS_Z1      = 8,   // 主焦点 Z（firmware 用名 "Z"，上位机 index=2）
        HC154_AXIS_Y       = 9,
        HC154_AXIS_X       = 10,
        HC154_EXPAND_NSCS1 = 11,
        HC154_DAC80508_4   = 12,
        HC154_MCP23S17_2   = 13,
        HC154_MCP23S17_3   = 14,
        HC154_MCP23S17_4   = 15
    };

    inline void hc154_init() {
        pinMode(HC154_A0, OUTPUT);
        pinMode(HC154_A1, OUTPUT);
        pinMode(HC154_A2, OUTPUT);
        pinMode(HC154_A3, OUTPUT);
        digitalWrite(HC154_A0, LOW);
        digitalWrite(HC154_A1, LOW);
        digitalWrite(HC154_A2, LOW);
        digitalWrite(HC154_A3, LOW);
    }

    // 选通 Yn（n ∈ 0..15），对应输出拉低；SPI 事务前调用
    inline void hc154_select(uint8_t channel) {
        digitalWrite(HC154_A0, (channel >> 0) & 0x01);
        digitalWrite(HC154_A1, (channel >> 1) & 0x01);
        digitalWrite(HC154_A2, (channel >> 2) & 0x01);
        digitalWrite(HC154_A3, (channel >> 3) & 0x01);
    }
}

// 系统配置
namespace SystemConfig {
    const uint32_t TMC4361_CLOCK_FREQUENCY = 16000000;
    const unsigned long LIMIT_CHECK_INTERVAL = 3000;
}

// 轴常量定义
namespace AxisConstDefinition {
		const float R_sense_xy = 0.22;
		const float R_sense_z = 0.43;
		const float R_sense_objective = 0.22;
		const float R_sense_filter = 0.1;

		const int FULLSTEPS_PER_REV_X = 200;
		const int FULLSTEPS_PER_REV_Y = 200;
		const int FULLSTEPS_PER_REV_Z = 200;
		const int FULLSTEPS_PER_REV_FILTER = 200;
		const int FULLSTEPS_PER_REV_OBJECTIVES = 200;
		const int FULLSTEPS_PER_REV_THETA = 200;

		const float SCREW_PITCH_X_MM = 2.54;
		const float SCREW_PITCH_Y_MM = 2.54;
		const float SCREW_PITCH_Z_MM = 0.3;
		const float SCREW_PITCH_FILTERWHEEL_MM = 1;   // 2026-05-21 对齐旧 Squid SCREW_PITCH_W_MM=1
		const float SCREW_PITCH_OBJECTIVES_MM = 1;

		const int MICROSTEPPING_X = 256;
		const int MICROSTEPPING_Y = 256;
		const int MICROSTEPPING_Z = 256;
		const int MICROSTEPPING_FILTERWHEEL = 64;     // 2026-05-21 对齐旧 Squid MICROSTEPPING_DEFAULT_W=64
		const int MICROSTEPPING_OBJECTIVES = 64;

		// 编码器分辨率 (μm/pulse)
		const float ENCODER_RESOLUTION_UM_X = 0.05;
		const float ENCODER_RESOLUTION_UM_Y = 0.05;
		const float ENCODER_RESOLUTION_UM_Z = 0.1;

		// Homing 细分（默认 256）
		const int HOMING_MICROSTEPPING_X = 256;
		const int HOMING_MICROSTEPPING_Y = 256;
		const int HOMING_MICROSTEPPING_Z = 256;
		const int HOMING_MICROSTEPPING_FILTERWHEEL = 256;
		const int HOMING_MICROSTEPPING_OBJECTIVES = 256;

		// 2026-05-11 速度优化第一轮：对齐老 Squid HCS v2 配置
		// 老 Squid configuration_HCS_v2.ini: max_velocity_x/y/z_mm = 30/30/3.8
		// AMAX_Z 100 实测反而让 Z 1mm 时间从 697→1569ms（+125%），疑似
		// motor_adjustBows 自动算出的 BOW 过大 + 电机扭矩不够导致 ramp 异常。
		// 保留 vmax 提升，Z 加速度回退到原值 20 mm/s²。
		const float MAX_VELOCITY_X_mm = 30;
		const float MAX_VELOCITY_Y_mm = 30;
		const float MAX_VELOCITY_Z_mm = 3.8;
		const float MAX_VELOCITY_FILTERWHEEL_mm = 4.2 * SCREW_PITCH_FILTERWHEEL_MM;
		const float MAX_VELOCITY_OBJECTIVES_mm = 0.5 * SCREW_PITCH_OBJECTIVES_MM;

		const float MAX_ACCELERATION_X_mm = 500;
		const float MAX_ACCELERATION_Y_mm = 500;
		const float MAX_ACCELERATION_Z_mm = 20;
		const float MAX_ACCELERATION_FILTERWHEEL_mm = 400 * SCREW_PITCH_FILTERWHEEL_MM;
		const float MAX_ACCELERATION_OBJECTIVES_mm = 80 * SCREW_PITCH_OBJECTIVES_MM;   // 2026-06-02 对齐 octoaxes E1（齿轮减速物镜防丢步）

		const float HOMING_VELOCITY_X_MM = 10;
		const float HOMING_VELOCITY_Y_MM = 30;  // 2026-05-12 实测确定：256 微步 + 30 mm/s 最安静
		const float HOMING_VELOCITY_Z_MM = 2;   // 2026-06-06 新 Z：1→2 mm/s（提速 +100%）。仅 Z_AXIS 实际使用（EXPAND3 借用但未实例化）
		const float HOMING_VELOCITY_FILTERWHEEL_MM = 0.15 * SCREW_PITCH_FILTERWHEEL_MM;
		const float HOMING_VELOCITY_OBJECTIVES_MM = 0.25 * SCREW_PITCH_OBJECTIVES_MM;

		// 电机电流设置 (mA) — 峰值电流，非 RMS
		// TMC2660 公式: I_peak = (CS+1)/32 × V_FS/R_sense, I_rms = I_peak/√2
		// CS 范围 0~31，超出会被 clamp，实际峰值受 R_sense 限制
		// 芯片绝对上限: 4A 峰值 (2.8A RMS)
		const float X_MOTOR_PEAK_CURRENT_mA = 1000;       // R=0.22Ω → CS=9, 实际 0.97A
		const float Y_MOTOR_PEAK_CURRENT_mA = 1000;       // R=0.22Ω → CS=9, 实际 0.97A
		const float Z_MOTOR_PEAK_CURRENT_mA = 500;        // R=0.43Ω → CS=21, 实际 0.47A
		const float FILTERWHEEL_MOTOR_PEAK_CURRENT_mA = 3100; // R=0.10Ω → CS=31(满), 实际 3.1A
		const float OBJECTIVES_MOTOR_PEAK_CURRENT_mA = 1800;  // 2026-06-02 对齐 octoaxes E1：TMC2240 I_FS=2A 配齿轮减速物镜防丢步

		const float X_MOTOR_I_HOLD = 0.25;
		const float Y_MOTOR_I_HOLD = 0.25;
		const float Z_MOTOR_I_HOLD = 0.5;
		const float FILTERWHEEL_MOTOR_I_HOLD = 0.5;
		const float OBJECTIVES_MOTOR_I_HOLD = 0.5;

		const float X_SAFEMARGIN = 0.05;
		const float Y_SAFEMARGIN = 0.05;
		const float Z_SAFEMARGIN = 0.05;
		const float FILTERWHEEL_SAFEMARGIN = 0.2;
		const float OBJECTIVES_SAFEMARGIN = 0.004;

		const float X_SAFEPOSITION = 0.6;
		const float Y_SAFEPOSITION = 0.6;
		const float Z_SAFEPOSITION = 0.7;
		const float FILTERWHEEL_SAFEPOSITION = 0;
		const float OBJECTIVES_SAFEPOSITION = 0;
}

// 照明系统配置
namespace IlluminationConfig {
    // DAC80508 寄存器地址
    const uint8_t DAC_CONFIG_ADDR = 0x03;
    const uint8_t DAC_GAIN_ADDR   = 0x04;
    const uint8_t DAC_DAC_ADDR    = 0x08;

    // 默认 DAC 增益：div=0x00, gains=0x80（通道 0-6 增益 1，通道 7 增益 2）
    const uint8_t DAC_DEFAULT_DIV   = 0x00;
    const uint8_t DAC_DEFAULT_GAINS = 0x80;

    // LED 矩阵（APA102，128 像素，BGR 顺序）
    const int   NUM_LEDS          = 128;
    const int   LED_MAX_INTENSITY = 100;
    const float GREEN_ADJUSTMENT  = 1.0f;
    const float RED_ADJUSTMENT    = 1.0f;
    const float BLUE_ADJUSTMENT   = 1.0f;

    // 默认全局强度因子（Squid LED 0-1.5V）
    const float DEFAULT_INTENSITY_FACTOR = 0.6f;

    // 端口数量（D1-D16）
    const int NUM_PORTS = 16;

    // 照明光源码（旧版 API，与协议保持一致）
    // LED 矩阵图案：0-8
    const int LED_ARRAY_FULL       = 0;
    const int LED_ARRAY_LEFT_HALF  = 1;
    const int LED_ARRAY_RIGHT_HALF = 2;
    const int LED_ARRAY_LEFTB_RIGHTR = 3;
    const int LED_ARRAY_LOW_NA     = 4;
    const int LED_ARRAY_LEFT_DOT   = 5;
    const int LED_ARRAY_RIGHT_DOT  = 6;
    const int LED_ARRAY_TOP_HALF   = 7;
    const int LED_ARRAY_BOTTOM_HALF = 8;
    const int LED_EXTERNAL_FET     = 20;
    // TTL 端口光源码（注意 D3/D4 非顺序 — 与旧版 squid 协议兼容）
    const int D1 = 11;
    const int D2 = 12;
    const int D3 = 14;  // 非顺序！
    const int D4 = 13;  // 非顺序！
    const int D5 = 15;
    // squid++ 双相机新增 D6-D8 光源码
    const int D6 = 16;
    const int D7 = 17;
    const int D8 = 18;
}

// 轴配置
namespace AxisConfigs {

    // X轴配置
    const Axis::AxisConfig X_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = LEFT_SW,
        .leftSwitchPolarity = 0,
        .rightSwitchPolarity = 0,
        .leftIsInactive = 0,
        .rightIsInactive = 0,
        .leftFlipped = true,
        .rightFlipped = true,
        .enableLeftLimitSwitch = true,
        .enableRightLimitSwitch = true,
        .r_sense = AxisConstDefinition::R_sense_xy,
        .screwPitchMM = AxisConstDefinition::SCREW_PITCH_X_MM,
        .fullStepsPerRev = AxisConstDefinition::FULLSTEPS_PER_REV_X,
        .microstepping = AxisConstDefinition::MICROSTEPPING_X,
        .homingMicrostepping = AxisConstDefinition::HOMING_MICROSTEPPING_X,
        .maxVelocityMM = AxisConstDefinition::MAX_VELOCITY_X_mm,
        .maxAccelerationMM = AxisConstDefinition::MAX_ACCELERATION_X_mm,
        .homingVelocityMM = AxisConstDefinition::HOMING_VELOCITY_X_MM,
        .motorCurrentMA = AxisConstDefinition::X_MOTOR_PEAK_CURRENT_mA,
        .holdCurrent = AxisConstDefinition::X_MOTOR_I_HOLD,
        .homeSafetyMarginMM = AxisConstDefinition::X_SAFEMARGIN,
        .homeSafetyPositionMM = AxisConstDefinition::X_SAFEPOSITION,
        // StallGuard 参数（仅 TMC2660 SG2 用，TMC2240 SG4 在 axis.cpp 启用处
        // 暂跳过，参数保留待 SG4 调优后启用）
        .enableStallSensitivity = true,
        .stallSensitivity = 12,
        .useSShapedRamp = true,
        .astartMM = 0,
        .dfinalMM = 0,
        .homing_timeout_ms = 30000,
        .homing_direct = -1,
        .driverType = DRIVER_AUTO,
        .currentRange = 0,
        .enableEncoder = false,
        .encoderLinesPerRev = (uint16_t)(AxisConstDefinition::SCREW_PITCH_X_MM * 1000 / AxisConstDefinition::ENCODER_RESOLUTION_UM_X),
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 硬件方向反相，默认 false（octoaxesplus 新硬件待实测）
    };

    // Y轴配置
    const Axis::AxisConfig Y_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = LEFT_SW,
        .leftSwitchPolarity = 0,
        .rightSwitchPolarity = 0,
        .leftIsInactive = 0,
        .rightIsInactive = 0,
        .leftFlipped = true,
        .rightFlipped = true,
        .enableLeftLimitSwitch = true,
        .enableRightLimitSwitch = true,
        .r_sense = AxisConstDefinition::R_sense_xy,
        .screwPitchMM = AxisConstDefinition::SCREW_PITCH_Y_MM,
        .fullStepsPerRev = AxisConstDefinition::FULLSTEPS_PER_REV_Y,
        .microstepping = AxisConstDefinition::MICROSTEPPING_Y,
        .homingMicrostepping = AxisConstDefinition::HOMING_MICROSTEPPING_Y,
        .maxVelocityMM = AxisConstDefinition::MAX_VELOCITY_Y_mm,
        .maxAccelerationMM = AxisConstDefinition::MAX_ACCELERATION_Y_mm,
        .homingVelocityMM = AxisConstDefinition::HOMING_VELOCITY_Y_MM,
        .motorCurrentMA = AxisConstDefinition::Y_MOTOR_PEAK_CURRENT_mA,
        .holdCurrent = AxisConstDefinition::Y_MOTOR_I_HOLD,
        .homeSafetyMarginMM = AxisConstDefinition::Y_SAFEMARGIN,
        .homeSafetyPositionMM = AxisConstDefinition::Y_SAFEPOSITION,
        // 同 X 轴：StallGuard 参数仅 TMC2660 用，TMC2240 在启用处跳过
        .enableStallSensitivity = true,
        .stallSensitivity = 12,
        .useSShapedRamp = true,
        .astartMM = 0,
        .dfinalMM = 0,
        .homing_timeout_ms = 40000,
        .homing_direct = -1,
        .driverType = DRIVER_AUTO,
        .currentRange = 0,
        .enableEncoder = false,
        .encoderLinesPerRev = (uint16_t)(AxisConstDefinition::SCREW_PITCH_Y_MM * 1000 / AxisConstDefinition::ENCODER_RESOLUTION_UM_Y),
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 硬件方向反相，默认 false（octoaxesplus 新硬件待实测）
    };

    // Z轴配置
    // ───────────────────────────────────────────────────────────────────
    // 新旧 Z 变体编译开关（与 software constants.py Z_AXIS_VARIANT 配对，需手动保持一致）
    //   新 Z (MOONS' LE143S-W0601)：坐标翻转(firmware 正=物理左、朝 home 走 XACTUAL 增大)。
    //     【2026-06-09 项目把正负限位传感器物理对调】：原负/home 端传感器移到正端、原正端
    //     传感器移到负/home 端（连线随传感器走）→ 负/home 端现接 TMC4361A STOPR 引脚（原接 STOPL）。
    //     该物理对调恰好抵消坐标翻转 → INVERT_STOP_DIRECTION 由 true 改 false（Z_FLIPPED=false）：
    //       · home 逼近=正向内部速度，STOPR(默认)拦正向 → 正确在 home 硬停；
    //       · 上限位移到 STOPL，拦负向 → 上限位过冲硬停。
    //     STATUS：home 现接 STOPR 引脚、不翻转 → 直接读作 STOPR 位 → homingSwitch 仍 RGHT_SW（不变）。
    //     极性 1；homing 仍以负限位为回零点(homing_direct=1)。
    //     ⚠️ 对调后限位行为须重新上机实测：z_limit_monitor.py 验证 STOPL/STOPR 极性 + 上下硬停方向。
    //     （对调前历史：home 接 STOPL，Z_FLIPPED=true，06-08 实测通过；见 git 历史）
    //   旧 Z：标准约定，home 在右限位、极性 0、不翻转、chip 硬停启用。
    // 这 7 个字段随变体切换；pitch/电流/微步由 GUI 下发覆盖、currentRange=1 两者通用。
    // Z_INVERT_ENCODER：编码器计数方向 boot 默认（ENC-2/ENC-3）。**当前不生效**——
    //   Z_AXIS.enableEncoder=false 把 begin() 里的编码器初始化 gate 掉，方向唯一由 runtime
    //   的 CONFIGURE_STAGE_PID(constants.py encoder_flip_direction) 决定（运行时权威）。
    //   仅当把下方 enableEncoder 改 true 时此 boot 值才生效，那时须与 constants.py 一致
    //   （configureStagePID 已加不一致告警）。详见 documents/audit_octoaxesplus_20260608.md。
    #define Z_VARIANT_NEW    // ← 注释掉此行 = 旧 Z
    #ifdef Z_VARIANT_NEW
      #define Z_HOMING_SWITCH  RGHT_SW
      #define Z_SW_POLARITY    1
      #define Z_ENABLE_LIMITS  true
      #define Z_FLIPPED        false    // 2026-06-09 正负限位传感器对调后 home 接 STOPR 引脚，物理对调抵消坐标翻转 → 无需 INVERT_STOP_DIRECTION（对调前为 true，见上方说明）
      #define Z_INVERT_ENCODER true     // 2026-06-08 实测 flip=1：ENC 跟随 XACTUAL(同向)，朝电机读数变小
    #else
      #define Z_HOMING_SWITCH  RGHT_SW
      #define Z_SW_POLARITY    0
      #define Z_ENABLE_LIMITS  true
      #define Z_FLIPPED        false
      #define Z_INVERT_ENCODER true     // 旧 Z 编码器默认禁用(has_encoder=False)，保留历史 true 值
    #endif
    const Axis::AxisConfig Z_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = Z_HOMING_SWITCH,      // 变体开关：新旧 Z 都 RGHT_SW（新 Z 2026-06-09 传感器对调后 home 接 STOPR 引脚、不翻转，直接读作 STOPR 位，见上方 Z_VARIANT_NEW）
        .leftSwitchPolarity = Z_SW_POLARITY,  // 新 Z=1（极性与旧 Z 相反）/ 旧 Z=0
        .rightSwitchPolarity = Z_SW_POLARITY,
        .leftIsInactive = 0,
        .rightIsInactive = 0,
        .leftFlipped = Z_FLIPPED,    // 变体开关：新 Z=false(2026-06-09 传感器对调抵消坐标翻转，无需 INVERT) / 旧 Z=false
        .rightFlipped = Z_FLIPPED,   // 同上（INVERT_STOP_DIRECTION 单 bit，leftFlipped||rightFlipped 即置位）
        .enableLeftLimitSwitch = Z_ENABLE_LIMITS,   // 新旧 Z 都 true（新 Z 2026-06-09 传感器对调后 home 接 STOPR / 上限位接 STOPL，无需 INVERT，chip 上下硬停正常）
        .enableRightLimitSwitch = Z_ENABLE_LIMITS,  // 同上
        .r_sense = AxisConstDefinition::R_sense_z,
        .screwPitchMM = AxisConstDefinition::SCREW_PITCH_Z_MM,
        .fullStepsPerRev = AxisConstDefinition::FULLSTEPS_PER_REV_Z,
        .microstepping = AxisConstDefinition::MICROSTEPPING_Z,
        .homingMicrostepping = AxisConstDefinition::HOMING_MICROSTEPPING_Z,
        .maxVelocityMM = AxisConstDefinition::MAX_VELOCITY_Z_mm,
        .maxAccelerationMM = AxisConstDefinition::MAX_ACCELERATION_Z_mm,
        .homingVelocityMM = AxisConstDefinition::HOMING_VELOCITY_Z_MM,
        .motorCurrentMA = AxisConstDefinition::Z_MOTOR_PEAK_CURRENT_mA,
        .holdCurrent = AxisConstDefinition::Z_MOTOR_I_HOLD,
        .homeSafetyMarginMM = AxisConstDefinition::Z_SAFEMARGIN,
        .homeSafetyPositionMM = AxisConstDefinition::Z_SAFEPOSITION,
        .enableStallSensitivity = false,
        .stallSensitivity = 6,
        .useSShapedRamp = true,
        .astartMM = 0,
        .dfinalMM = 0,
        .homing_timeout_ms = 40000,   // 2026-06-06 新 Z：20000→40000（×2，配合 homing 提速 + 行程余量）
        .homing_direct = 1,
        .driverType = DRIVER_AUTO,
        .currentRange = 1,         // 2026-06-06 新 Z（LE143S 1.5A）借 octoaxesplus 板调试：TMC2240 I_FS=2A。currentRange 无下发协议、固件独有，必须在此设对（GUI 只发 currentMA，currentRange=0=I_FS1A 时 1500mA 会饱和算错）。旧 Z TMC2660 忽略此字段，安全
        .enableEncoder = false,
        .encoderLinesPerRev = (uint16_t)(AxisConstDefinition::SCREW_PITCH_Z_MM * 1000 / AxisConstDefinition::ENCODER_RESOLUTION_UM_Z),
        .invertEncoderDir = Z_INVERT_ENCODER,   // 变体开关（ENC-3）：boot 默认编码器方向，runtime 被 GUI flip 覆盖
        .invert_direction = false   // 2026-05-25 硬件方向反相，默认 false
    };

    // W轴4配置 (filter wheel)
    const Axis::AxisConfig W_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = LEFT_SW,
        .leftSwitchPolarity = 0,
        .rightSwitchPolarity = 0,
        .leftIsInactive = 0,
        .rightIsInactive = 0,
        .leftFlipped = false,
        .rightFlipped = false,
        .enableLeftLimitSwitch = true,
        .enableRightLimitSwitch = false,
        .r_sense = AxisConstDefinition::R_sense_filter,
        .screwPitchMM = AxisConstDefinition::SCREW_PITCH_FILTERWHEEL_MM,
        .fullStepsPerRev = AxisConstDefinition::FULLSTEPS_PER_REV_FILTER,
        .microstepping = AxisConstDefinition::MICROSTEPPING_FILTERWHEEL,
        .homingMicrostepping = AxisConstDefinition::HOMING_MICROSTEPPING_FILTERWHEEL,
        .maxVelocityMM = AxisConstDefinition::MAX_VELOCITY_FILTERWHEEL_mm,
        .maxAccelerationMM = AxisConstDefinition::MAX_ACCELERATION_FILTERWHEEL_mm,
        .homingVelocityMM = AxisConstDefinition::HOMING_VELOCITY_FILTERWHEEL_MM,
        .motorCurrentMA = AxisConstDefinition::FILTERWHEEL_MOTOR_PEAK_CURRENT_mA,
        .holdCurrent = AxisConstDefinition::FILTERWHEEL_MOTOR_I_HOLD,
        .homeSafetyMarginMM = AxisConstDefinition::FILTERWHEEL_SAFEMARGIN,
        .homeSafetyPositionMM = AxisConstDefinition::FILTERWHEEL_SAFEPOSITION,
        .enableStallSensitivity = false,
        .stallSensitivity = 6,
        .useSShapedRamp = true,
        .astartMM = 0,  // 2026-05-21 对齐旧 Squid sRampInit (rstBits USE_ASTART_AND_VSTART)，禁用 jerk-start 消除短距离 ramp 过冲
        .dfinalMM = 0,                                   // 同 astart
        .homing_timeout_ms = 80000,
        .homing_direct = 1,
        .driverType = DRIVER_AUTO,
        .currentRange = 2,
        .enableEncoder = false,
        .encoderLinesPerRev = 4000,
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 硬件方向反相，默认 false（octoaxesplus 新硬件待实测）
    };

    // 扩展轴1配置 (objectives)
    const Axis::AxisConfig EXPAND1_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = LEFT_SW,           // 2026-06-04 照搬本板 X/Y 限位方案：传感器在 TMC4361A LEFT 输入
        .leftSwitchPolarity = 0,           // 同 X/Y
        .rightSwitchPolarity = 0,          // 同 X/Y
        .leftIsInactive = 0,               // 死字段，不影响 chip
        .rightIsInactive = 0,
        .leftFlipped = false,
        .rightFlipped = false,
        .enableLeftLimitSwitch = true,     // 2026-06-05 对齐 objectives 分支已验证 W_AXIS：启用 chip LEFT 限位硬停，homing 速度模式撞限位时由 TMC4361A 硬件停车（objectives.cpp 只设零、不软件停）。前提：Turret 通道 home 传感器须实际接到该片 REF_L 引脚
        .enableRightLimitSwitch = false,
        .r_sense = AxisConstDefinition::R_sense_objective,
        .screwPitchMM = AxisConstDefinition::SCREW_PITCH_OBJECTIVES_MM,
        .fullStepsPerRev = AxisConstDefinition::FULLSTEPS_PER_REV_OBJECTIVES,
        .microstepping = AxisConstDefinition::MICROSTEPPING_OBJECTIVES,
        .homingMicrostepping = AxisConstDefinition::HOMING_MICROSTEPPING_OBJECTIVES,
        .maxVelocityMM = AxisConstDefinition::MAX_VELOCITY_OBJECTIVES_mm,
        .maxAccelerationMM = AxisConstDefinition::MAX_ACCELERATION_OBJECTIVES_mm,
        .homingVelocityMM = AxisConstDefinition::HOMING_VELOCITY_OBJECTIVES_MM,
        .motorCurrentMA = AxisConstDefinition::OBJECTIVES_MOTOR_PEAK_CURRENT_mA,
        .holdCurrent = AxisConstDefinition::OBJECTIVES_MOTOR_I_HOLD,
        .homeSafetyMarginMM = AxisConstDefinition::OBJECTIVES_SAFEMARGIN,
        .homeSafetyPositionMM = AxisConstDefinition::OBJECTIVES_SAFEPOSITION,
        .enableStallSensitivity = false,
        .stallSensitivity = 15,
        .useSShapedRamp = true,
        .astartMM = 0,
        .dfinalMM = 0,
        .homing_timeout_ms = 80000,
        .homing_direct = 1,
        .driverType = DRIVER_AUTO,
        .currentRange = 1,                 // 2026-06-02 对齐 octoaxes E1：TMC2240 I_FS=2A（原 0=1A 配齿轮减速物镜丢步）
        .enableEncoder = false,
        .encoderLinesPerRev = 0,
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 硬件方向反相，默认 false（octoaxesplus 新硬件待实测）
    };

    // 扩展轴3配置 (Z轴配置)
    const Axis::AxisConfig EXPAND3_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = RGHT_SW,
        .leftSwitchPolarity = 0,
        .rightSwitchPolarity = 0,
        .leftIsInactive = 0,
        .rightIsInactive = 0,
        .leftFlipped = false,
        .rightFlipped = false,
        .enableLeftLimitSwitch = true,
        .enableRightLimitSwitch = true,
        .r_sense = AxisConstDefinition::R_sense_z,
        .screwPitchMM = AxisConstDefinition::SCREW_PITCH_Z_MM,
        .fullStepsPerRev = AxisConstDefinition::FULLSTEPS_PER_REV_Z,
        .microstepping = AxisConstDefinition::MICROSTEPPING_Z,
        .homingMicrostepping = AxisConstDefinition::HOMING_MICROSTEPPING_Z,
        .maxVelocityMM = AxisConstDefinition::MAX_VELOCITY_Z_mm,
        .maxAccelerationMM = AxisConstDefinition::MAX_ACCELERATION_Z_mm,
        .homingVelocityMM = AxisConstDefinition::HOMING_VELOCITY_Z_MM,
        .motorCurrentMA = AxisConstDefinition::Z_MOTOR_PEAK_CURRENT_mA,
        .holdCurrent = AxisConstDefinition::Z_MOTOR_I_HOLD,
        .homeSafetyMarginMM = AxisConstDefinition::Z_SAFEMARGIN,
        .homeSafetyPositionMM = AxisConstDefinition::Z_SAFEPOSITION,
        .enableStallSensitivity = false,
        .stallSensitivity = 6,
        .useSShapedRamp = true,
        .astartMM = 0,
        .dfinalMM = 0,
        .homing_timeout_ms = 20000,
        .homing_direct = 1,
        .driverType = DRIVER_AUTO,
        .currentRange = 0,
        .enableEncoder = false,
        .encoderLinesPerRev = 0,
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 硬件方向反相，默认 false（octoaxesplus 新硬件待实测）
    };

    // ============================================================================
    // squid++ 双相机扩展轴配置
    // 当前 XYZW1W2 五轴方案：W1 / W2 = 滤光转盘（W_AXIS 模板 const struct copy）
    // 预留 Z2/F2/R/T 别名供未来 8 轴扩展（暂未实例化，无副作用）
    // 实测后按需把 `=` 改成完整初始化器单独调参数
    // ============================================================================

    // W1 轴配置 (滤光转盘 1，CS=HC154 通道 6，与 W_AXIS 滤光转盘默认参数一致)
    const Axis::AxisConfig W1_AXIS = W_AXIS;

    // W2 轴配置 (滤光转盘 2，CS=HC154 通道 4，与 W_AXIS 滤光转盘默认参数一致)
    const Axis::AxisConfig W2_AXIS = W_AXIS;

    // ── 预留扩展配置（未实例化，保留 const struct copy 模板） ──────────────────
    const Axis::AxisConfig Z2_AXIS = Z_AXIS;       // 双焦点 Z2，与 Z1 同款电机
    const Axis::AxisConfig F2_AXIS = W_AXIS;       // 双滤光轮 F2，与 F1 同款
    const Axis::AxisConfig R_AXIS  = EXPAND1_AXIS; // 物镜转换器旋转
    const Axis::AxisConfig T_AXIS  = EXPAND1_AXIS; // 物镜转换器平移

    // 扩展轴4配置 (filter wheel)
    const Axis::AxisConfig EXPAND4_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = LEFT_SW,
        .leftSwitchPolarity = 0,
        .rightSwitchPolarity = 0,
        .leftIsInactive = 0,
        .rightIsInactive = 0,
        .leftFlipped = false,
        .rightFlipped = false,
        .enableLeftLimitSwitch = true,
        .enableRightLimitSwitch = false,
        .r_sense = AxisConstDefinition::R_sense_filter,
        .screwPitchMM = AxisConstDefinition::SCREW_PITCH_FILTERWHEEL_MM,
        .fullStepsPerRev = AxisConstDefinition::FULLSTEPS_PER_REV_FILTER,
        .microstepping = AxisConstDefinition::MICROSTEPPING_FILTERWHEEL,
        .homingMicrostepping = AxisConstDefinition::HOMING_MICROSTEPPING_FILTERWHEEL,
        .maxVelocityMM = AxisConstDefinition::MAX_VELOCITY_FILTERWHEEL_mm,
        .maxAccelerationMM = AxisConstDefinition::MAX_ACCELERATION_FILTERWHEEL_mm,
        .homingVelocityMM = AxisConstDefinition::HOMING_VELOCITY_FILTERWHEEL_MM,
        .motorCurrentMA = AxisConstDefinition::FILTERWHEEL_MOTOR_PEAK_CURRENT_mA,
        .holdCurrent = AxisConstDefinition::FILTERWHEEL_MOTOR_I_HOLD,
        .homeSafetyMarginMM = AxisConstDefinition::FILTERWHEEL_SAFEMARGIN,
        .homeSafetyPositionMM = AxisConstDefinition::FILTERWHEEL_SAFEPOSITION,
        .enableStallSensitivity = false,
        .stallSensitivity = 6,
        .useSShapedRamp = true,
        .astartMM = 0,  // 2026-05-21 对齐旧 Squid sRampInit (rstBits USE_ASTART_AND_VSTART)，禁用 jerk-start 消除短距离 ramp 过冲
        .dfinalMM = 0,
        .homing_timeout_ms = 80000,
        .homing_direct = 1,
        .driverType = DRIVER_AUTO,
        .currentRange = 0,
        .enableEncoder = false,
        .encoderLinesPerRev = 0,
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 硬件方向反相，默认 false（octoaxesplus 新硬件待实测）
    };
}

#endif
