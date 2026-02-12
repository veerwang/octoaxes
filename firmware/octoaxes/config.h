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
    const int SET_PIN_LEVEL = 41;
    const int INITFILTERWHEEL = 253;
    const int INITIALIZE = 254;
    const int RESET = 255;
}

// 引脚定义
namespace Pins {
    const int DAC8050x_CS = 33;
    const int POWER_GOOD = 0;
    const int TMC4361_STANDARD_CLK = 37;
    const int TMC4361_EXPAND_CLK = 28;

    const int X_AXIS_CS = 41;
    const int Y_AXIS_CS = 36;
    const int Z_AXIS_CS = 35;
    const int W_AXIS_CS = 34;

    const int EXPAND1_AXIS_CS = 19;
    const int EXPAND2_AXIS_CS = 18;
    const int EXPAND3_AXIS_CS = 17;
    const int EXPAND4_AXIS_CS = 16;
    
    // 控制引脚数组
    const uint8_t CONTROL_PINS[] = {EXPAND1_AXIS_CS, EXPAND2_AXIS_CS, EXPAND3_AXIS_CS, EXPAND4_AXIS_CS};
    const uint8_t STANDARD_CONTROL_PINS[] = {W_AXIS_CS, Z_AXIS_CS, Y_AXIS_CS, X_AXIS_CS};
    const size_t NUM_CONTROL_PINS = 4;
    const size_t NUM_STANDARD_CONTROL_PINS = 4;
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
				.homing_direct = -1
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
				.homing_direct = -1
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
				.homing_direct = 1
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
				.homing_direct = 1
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
				.homing_direct = 1
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
				.homing_direct = 1
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
				.homing_direct = 1
    };
}

#endif
