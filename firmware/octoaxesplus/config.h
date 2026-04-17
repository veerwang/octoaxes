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
    const int TMC4361_EXPAND_CLK = 28;

    // 轴 SPI 片选（HC154 通道号，非 GPIO）
    const int X_AXIS_CS = 10;  // HC154 Y10 = AXIS_X
    const int Y_AXIS_CS = 9;   // HC154 Y9  = AXIS_Y
    const int Z_AXIS_CS = 8;   // HC154 Y8  = AXIS_Z1（Z 主轴）
    const int W_AXIS_CS = 7;   // HC154 Y7  = AXIS_F1（占位：squid++ 原 W=滤光转盘 1）

    // 扩展轴（HC154 通道号，暂未启用）
    const int EXPAND1_AXIS_CS = 11;  // HC154 Y11 EXPAND_NSCS1 占位
    const int EXPAND2_AXIS_CS = 11;
    const int EXPAND3_AXIS_CS = 11;
    const int EXPAND4_AXIS_CS = 11;

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

    // 相机触发（squid++ 双相机：8 路，来源 documents/squid++（双相机）配置.md §1）
    const int CAMERA_TRIGGER_1 = 9;    // CAM_TRI_OUT1（相机 1）
    const int CAMERA_TRIGGER_2 = 8;    // CAM_TRI_OUT2（相机 2）
    const int CAMERA_TRIGGER_3 = 23;
    const int CAMERA_TRIGGER_4 = 22;
    const int CAMERA_TRIGGER_5 = 15;
    const int CAMERA_TRIGGER_6 = 41;
    const int CAMERA_TRIGGER_7 = 40;
    const int CAMERA_TRIGGER_8 = 39;

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
        HC154_AXIS_R       = 3,
        HC154_AXIS_T       = 4,
        HC154_AXIS_F2      = 5,   // 滤光转盘 F2
        HC154_AXIS_Z2      = 6,
        HC154_AXIS_F1      = 7,   // 滤光转盘 F1
        HC154_AXIS_Z1      = 8,
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
		const float SCREW_PITCH_FILTERWHEEL_MM = 100;
		const float SCREW_PITCH_OBJECTIVES_MM = 1;

		const int MICROSTEPPING_X = 256;
		const int MICROSTEPPING_Y = 256;
		const int MICROSTEPPING_Z = 256;
		const int MICROSTEPPING_FILTERWHEEL = 8;
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

		const float MAX_VELOCITY_X_mm = 25;
		const float MAX_VELOCITY_Y_mm = 25;
		const float MAX_VELOCITY_Z_mm = 3;
		const float MAX_VELOCITY_FILTERWHEEL_mm = 4.2 * SCREW_PITCH_FILTERWHEEL_MM;
		const float MAX_VELOCITY_OBJECTIVES_mm = 0.5 * SCREW_PITCH_OBJECTIVES_MM;

		const float MAX_ACCELERATION_X_mm = 500;
		const float MAX_ACCELERATION_Y_mm = 500;
		const float MAX_ACCELERATION_Z_mm = 20;
		const float MAX_ACCELERATION_FILTERWHEEL_mm = 400 * SCREW_PITCH_FILTERWHEEL_MM;
		const float MAX_ACCELERATION_OBJECTIVES_mm = 200 * SCREW_PITCH_OBJECTIVES_MM;

		const float HOMING_VELOCITY_X_MM = 10;
		const float HOMING_VELOCITY_Y_MM = 10;
		const float HOMING_VELOCITY_Z_MM = 1;
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
		const float OBJECTIVES_MOTOR_PEAK_CURRENT_mA = 1000;  // R=0.22Ω → CS=9, 实际 0.97A

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
        .invertEncoderDir = false
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
        .invertEncoderDir = false
    };

    // Z轴配置
    const Axis::AxisConfig Z_AXIS = {
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
        .encoderLinesPerRev = (uint16_t)(AxisConstDefinition::SCREW_PITCH_Z_MM * 1000 / AxisConstDefinition::ENCODER_RESOLUTION_UM_Z),
        .invertEncoderDir = true
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
        .astartMM = 180 * AxisConstDefinition::SCREW_PITCH_FILTERWHEEL_MM,  // 起始加速度 180 rev/s²
        .dfinalMM = 0,                                   // 同 astart
        .homing_timeout_ms = 80000,
        .homing_direct = 1,
        .driverType = DRIVER_AUTO,
        .currentRange = 2,
        .enableEncoder = false,
        .encoderLinesPerRev = 4000,
        .invertEncoderDir = false
    };

    // 扩展轴1配置 (objectives)
    const Axis::AxisConfig EXPAND1_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = LEFT_SW,
        .leftSwitchPolarity = 0,
        .rightSwitchPolarity = 0,
        .leftIsInactive = 1,
        .rightIsInactive = 1,
        .leftFlipped = false,
        .rightFlipped = false,
        .enableLeftLimitSwitch = true,
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
        .currentRange = 0,
        .enableEncoder = false,
        .encoderLinesPerRev = 0,
        .invertEncoderDir = false
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
        .invertEncoderDir = false
    };

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
        .astartMM = 180 * AxisConstDefinition::SCREW_PITCH_FILTERWHEEL_MM,  // 起始加速度 180 rev/s²
        .dfinalMM = 0,
        .homing_timeout_ms = 80000,
        .homing_direct = 1,
        .driverType = DRIVER_AUTO,
        .currentRange = 0,
        .enableEncoder = false,
        .encoderLinesPerRev = 0,
        .invertEncoderDir = false
    };
}

#endif
