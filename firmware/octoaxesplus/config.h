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
    // Multi-port illumination commands (v1.0+)
    const int SET_PORT_INTENSITY = 34;
    const int TURN_ON_PORT = 35;
    const int TURN_OFF_PORT = 36;
    const int SET_PORT_ILLUMINATION = 37;
    const int SET_MULTI_PORT_MASK = 38;
    const int TURN_OFF_ALL_PORTS = 39;
    // Safety and heartbeat
    const int SET_WATCHDOG_TIMEOUT = 40;  // set the serial watchdog timeout (ms); once enabled, a communication loss automatically turns off the lights
    const int SET_PIN_LEVEL = 41;
    const int HEARTBEAT = 42;             // no-op heartbeat (the watchdog is reset by received packets, not by this command)
    const int MOVETO_W2 = 43;             // octoaxesplus W2 absolute-move command (companion to MOVE_W2=19)
    const int MOVE_TURRET   = 44;             // E1 objective turret relative move, data[2..5] = int32 microsteps big-endian (reuses the octoaxes E1 protocol)
    const int MOVETO_TURRET = 45;             // E1 objective turret absolute move
    const int INITFILTERWHEEL_W2 = 252;
    const int INITFILTERWHEEL = 253;
    const int INITIALIZE = 254;
    const int RESET = 255;
}

// Pin definitions
namespace Pins {
    // squid++ dual-camera: all SPI chip-selects go through the 74HC154 (see the HC154_Channel enum in this file)
    // the X/Y/Z/W_AXIS_CS and DAC8050x_CS below are no longer GPIO pin numbers but HC154 channel numbers (0-15)
    // under the USE_HC154_CS build branch, the TMC_SPI HAL calls Pins::hc154_select() by channel number
    const int POWER_GOOD = 0;
    const int TMC4361_STANDARD_CLK = 37;
    // squid++ dual-camera removes the TMC4361 expansion clock (the original pin 28 is used by TTL5, and all 8 axes are on SPI1
    // a single clock is enough). CLOCK_EXPAND is kept only as a runtime marker in tmc_ic_configs

    // axis SPI chip-selects (HC154 channel numbers, not GPIO)
    const int X_AXIS_CS  = 10;  // HC154 Y10 = AXIS_X
    const int Y_AXIS_CS  = 9;   // HC154 Y9  = AXIS_Y
    const int Z_AXIS_CS  = 8;   // HC154 Y8  = AXIS_Z (main focus Z)
    const int W1_AXIS_CS = 6;   // HC154 Y6  = AXIS_W1 (filter wheel 1, uses the original Z2 CS)
    const int W2_AXIS_CS = 4;   // HC154 Y4  = AXIS_W2 (filter wheel 2, uses the original T CS)

    // historical aliases (kept for a possible future restore of the squid++ 8-axis scheme, currently unreferenced by code)
    const int W_AXIS_CS  = 7;   // HC154 Y7  = original AXIS_F1 (octoaxes mainline W filter wheel)
    const int Z2_AXIS_CS = 6;   // [deprecated] now replaced by W1_AXIS_CS=6
    const int F2_AXIS_CS = 5;   // HC154 Y5  = original AXIS_F2 (unused, reserved)
    const int R_AXIS_CS  = 3;   // HC154 Y3  = original AXIS_R (unused, reserved)
    const int T_AXIS_CS  = 4;   // [deprecated] now replaced by W2_AXIS_CS=4

    // the old EXPAND1-4_AXIS_CS aliases were removed on 2026-05-13 (audit found them completely unreferenced)
    // squid++ replaces them with Z2_AXIS_CS=6 / F2_AXIS_CS=5 / R_AXIS_CS=3 / T_AXIS_CS=4
    // note: the EXPAND1_AXIS / EXPAND3_AXIS / EXPAND4_AXIS AxisConfig are still kept,
    // as the AxisConfig template source for extension axes like R/T (const struct copy reference)

    // DAC80508_1 SPI chip-select (HC154 channel number)
    const int DAC8050x_CS = 2;   // HC154 Y2 = DAC80508_1 (8-LED analog output)

    // illumination TTL ports (D1-D8, squid++ dual-camera)
    // squid++ table: pin 32/31/30/29/28 = TTL1-5, pin 25/24/10 = TTL6-8
    const int ILLUMINATION_D1 = 32;
    const int ILLUMINATION_D2 = 31;
    const int ILLUMINATION_D3 = 30;
    const int ILLUMINATION_D4 = 29;
    const int ILLUMINATION_D5 = 28;
    const int ILLUMINATION_D6 = 25;
    const int ILLUMINATION_D7 = 24;
    const int ILLUMINATION_D8 = 10;

    // laser safety interlock (squid++ pin 38, LOW = safe)
    const int ILLUMINATION_INTERLOCK = 38;

    // LED matrix (APA102, 128 pixels; SPI2 MOSI/SCK)
    const int LED_MATRIX_DATA  = 26;
    const int LED_MATRIX_CLOCK = 27;

    // LED driver LT3932 SYNC (squid++ has no dedicated SYNC pin; the old constant is kept as a placeholder)
    // the original octoaxes pin 25 is now TTL6; if LT3932 is needed, reassign it in hardware
    const int LED_DRIVER_SYNC = 255;  // invalid pin; analogWrite/pinMode are no-ops

    // I2C bus (Wire1, squid++ pin 18/19; pin 14 is EEPROM/device write-protect)
    // placeholder: not initialized until a specific device is attached; enable once the peripheral plan is finalized
    const int IIC_WP  = 14;
    const int IIC_SDA = 18;
    const int IIC_SCL = 19;

    // Serial2 (pin 16=RX2, pin 17=TX2) -- placeholder, purpose TBD
    const int RX2 = 16;
    const int TX2 = 17;

    // camera trigger (squid++ dual-camera: 8 lines)
    // 2026-07-20 settled by measurement (cmd41 per-pin pulses + Toupcam external-trigger frame
    // counting): actual wiring follows the "function description" column of
    // documents/squid++（双相机）配置.md §1 (xlsx survey); schematic signal names are NOT
    // trustworthy — camera 1 trigger is wired to pin 6 (signal name CAM_TRI_READY2), camera 2
    // trigger to pin 4 (signal name TRIGGER_IN2); pin 9/8 (CAM_TRI_OUT1/2) measured unwired
    // (pulses produced 0 frames).
    const int CAMERA_TRIGGER_1 = 6;    // camera 1 trigger (measured: pulse → Toupcam frame out)
    const int CAMERA_TRIGGER_2 = 4;    // camera 2 trigger (per the same survey notes; re-verify once camera 2 is installed)
    const int CAMERA_TRIGGER_3 = 23;
    const int CAMERA_TRIGGER_4 = 22;
    const int CAMERA_TRIGGER_5 = 15;
    const int CAMERA_TRIGGER_6 = 41;
    const int CAMERA_TRIGGER_7 = 40;
    const int CAMERA_TRIGGER_8 = 39;

    // external trigger IN/OUT (squid++ dual-camera: bidirectional sync with external devices, pin 1-4)
    // OUT: firmware actively outputs pulses/levels to drive external devices (e.g. another microscope/light source/capture card)
    // IN: firmware receives trigger input from external devices (edge/level) and decides the next action by command
    // source: documents/squid++（双相机）配置.md section 1
    const int TRIGGER_OUT1 = 1;
    const int TRIGGER_IN1  = 2;
    const int TRIGGER_OUT2 = 3;
    // TRIGGER_IN2 (pin 4): 2026-07-20 measured — this pin is actually wired as the camera 2
    // trigger -> repurposed as CAMERA_TRIGGER_2, no longer an ext trigger IN (see trigger.h NUM_EXT_TRIGGER_IN)

    // dual-camera handshake READY inputs (squid++ dual-camera: camera-ready / capture-complete feedback)
    // 2026-07-20 settled: pin 6 measured as the camera 1 trigger line (old "treat as input per
    // naming" assumption is void); READY follows the survey table — camera1_wait-trigger = pin 7
    const int CAM_TRI_READY1 = 7;   // camera 1 READY feedback

    // autofocus AF laser (2026-07-22 user confirmed actual wiring = pin 5, superseding the survey
    // table's old "camera2_wait-trigger" assignment; biforst _def.py MCU_PINS.AF_LASER=5 settled in sync).
    // At boot, illumination_init forces OUTPUT+LOW for a deterministic off (previously nobody
    // initialized it and trigger_init instead configured it as a READY input with INPUT_PULLUP weak
    // pull-up, risking a spurious laser-on at power-up); at runtime the host controls it directly
    // via cmd 41 SET_PIN_LEVEL.
    // Camera 2 READY feedback pin to be re-surveyed once camera 2 is installed and re-verified.
    const int AF_LASER = 5;

    // 74HC154 4->16 decoder chip-select (squid++ dual-camera)
    // the binary value n on A3:A2:A1:A0 -> pulls output Yn low, the rest stay high; serves as the unified chip-select for all SPI devices
    // source: documents/squid++（双相机）配置.md section 2
    const int HC154_A0 = 33;
    const int HC154_A1 = 34;
    const int HC154_A2 = 35;
    const int HC154_A3 = 36;

    enum HC154_Channel : uint8_t {
        HC154_MCP23S17_1   = 0,   // expansion IO #1 (8-axis INTR/TARGET)
        HC154_DAC80508_2   = 1,
        HC154_DAC80508_1   = 2,   // 8-LED analog signal output
        HC154_AXIS_R       = 3,   // [unused] objective rotation R
        HC154_AXIS_T       = 4,   // [unused, alias of W2] objective translation T
        HC154_AXIS_W2      = 4,   // filter wheel W2 (current use, same channel as T)
        HC154_AXIS_F2      = 5,   // [unused] filter wheel F2
        HC154_AXIS_Z2      = 6,   // [unused, alias of W1] dual-focus Z2
        HC154_AXIS_W1      = 6,   // filter wheel W1 (current use, same channel as Z2)
        HC154_AXIS_F1      = 7,   // [unused] filter wheel F1
        HC154_AXIS_Z1      = 8,   // main focus Z (firmware name "Z", host index=2)
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

    // select Yn (n in 0..15), pulling the corresponding output low; call before an SPI transaction
    inline void hc154_select(uint8_t channel) {
        digitalWrite(HC154_A0, (channel >> 0) & 0x01);
        digitalWrite(HC154_A1, (channel >> 1) & 0x01);
        digitalWrite(HC154_A2, (channel >> 2) & 0x01);
        digitalWrite(HC154_A3, (channel >> 3) & 0x01);
    }
}

// System configuration
namespace SystemConfig {
    const uint32_t TMC4361_CLOCK_FREQUENCY = 16000000;
    const unsigned long LIMIT_CHECK_INTERVAL = 3000;
}

// Axis constant definitions
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
		const float SCREW_PITCH_FILTERWHEEL_MM = 1;   // 2026-05-21 matches legacy Squid SCREW_PITCH_W_MM=1
		const float SCREW_PITCH_OBJECTIVES_MM = 1;

		const int MICROSTEPPING_X = 256;
		const int MICROSTEPPING_Y = 256;
		const int MICROSTEPPING_Z = 256;
		const int MICROSTEPPING_FILTERWHEEL = 64;     // 2026-05-21 matches legacy Squid MICROSTEPPING_DEFAULT_W=64
		const int MICROSTEPPING_OBJECTIVES = 64;

		// encoder resolution (um/pulse)
		const float ENCODER_RESOLUTION_UM_X = 0.05;
		const float ENCODER_RESOLUTION_UM_Y = 0.05;
		const float ENCODER_RESOLUTION_UM_Z = 0.1;

		// Homing microstepping (default 256)
		const int HOMING_MICROSTEPPING_X = 256;
		const int HOMING_MICROSTEPPING_Y = 256;
		const int HOMING_MICROSTEPPING_Z = 256;
		const int HOMING_MICROSTEPPING_FILTERWHEEL = 256;
		const int HOMING_MICROSTEPPING_OBJECTIVES = 256;

		// 2026-05-11 first speed-optimization round: matches legacy Squid HCS v2 config
		// legacy Squid configuration_HCS_v2.ini: max_velocity_x/y/z_mm = 30/30/3.8
		// AMAX_Z 100 measured to actually increase Z 1mm time from 697->1569ms (+125%), suspected to be
		// motor_adjustBows auto-computing too large a BOW + insufficient motor torque causing an abnormal ramp.
		// keep the vmax increase, roll Z acceleration back to the original 20 mm/s2.
		const float MAX_VELOCITY_X_mm = 30;
		const float MAX_VELOCITY_Y_mm = 30;
		const float MAX_VELOCITY_Z_mm = 3.8;
		const float MAX_VELOCITY_FILTERWHEEL_mm = 4.2 * SCREW_PITCH_FILTERWHEEL_MM;
		const float MAX_VELOCITY_OBJECTIVES_mm = 0.5 * SCREW_PITCH_OBJECTIVES_MM;

		const float MAX_ACCELERATION_X_mm = 500;
		const float MAX_ACCELERATION_Y_mm = 500;
		const float MAX_ACCELERATION_Z_mm = 20;
		const float MAX_ACCELERATION_FILTERWHEEL_mm = 200 * SCREW_PITCH_FILTERWHEEL_MM;   // 2026-07-17 400→200 aligned with octoaxes (user confirmed both machines share the same filter wheel hardware; value octoaxes settled on 07-09 after measured W Test step loss on repeated next/previous), pending on-machine regression
		const float MAX_ACCELERATION_OBJECTIVES_mm = 80 * SCREW_PITCH_OBJECTIVES_MM;   // 2026-06-02 aligned with octoaxes E1 (prevents step loss on the gear-reduced objective)

		const float HOMING_VELOCITY_X_MM = 30;  // 2026-08-11 10→30 aligned with Y (consistent with the octoaxes 07-20 alignment): 10 was an untuned legacy value, X/Y share the same platform motors, so adopt Y's measured tuned value
		const float HOMING_VELOCITY_Y_MM = 30;  // 2026-05-12 measured: 256 microsteps + 30 mm/s is quietest
		const float HOMING_VELOCITY_Z_MM = 1;   // safe boot default = 1mm/s (old Z historical value, drop-in equivalent; legacy Squid has no channel to send the homing speed, so this default is all it can use). For the new Z, the GUI sends S:SET_HOMING_VEL per variant at startup to raise it to 2mm/s (avoiding long-travel homing timeouts). Only Z_AXIS actually uses it (EXPAND3 borrows it but is not instantiated)
		const float HOMING_VELOCITY_FILTERWHEEL_MM = 0.15 * SCREW_PITCH_FILTERWHEEL_MM;
		const float HOMING_VELOCITY_OBJECTIVES_MM = 0.25 * SCREW_PITCH_OBJECTIVES_MM;

		// motor current setting (mA) -- peak current, not RMS
		// TMC2660 formula: I_peak = (CS+1)/32 * V_FS/R_sense, I_rms = I_peak/sqrt(2)
		// CS range 0~31, out-of-range is clamped, the actual peak is limited by R_sense
		// chip absolute max: 4A peak (2.8A RMS)
		const float X_MOTOR_PEAK_CURRENT_mA = 1000;       // R=0.22ohm -> CS=9, actual 0.97A
		const float Y_MOTOR_PEAK_CURRENT_mA = 1000;       // R=0.22ohm -> CS=9, actual 0.97A
		const float Z_MOTOR_PEAK_CURRENT_mA = 500;        // R=0.43ohm -> CS=21, actual 0.47A
		const float FILTERWHEEL_MOTOR_PEAK_CURRENT_mA = 3100; // R=0.10ohm -> CS=31 (max), actual 3.1A
		const float OBJECTIVES_MOTOR_PEAK_CURRENT_mA = 1800;  // 2026-06-02 aligned with octoaxes E1: TMC2240 I_FS=2A to prevent step loss on the gear-reduced objective

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

// Illumination-system configuration
namespace IlluminationConfig {
    // DAC80508 register addresses
    const uint8_t DAC_CONFIG_ADDR = 0x03;
    const uint8_t DAC_GAIN_ADDR   = 0x04;
    const uint8_t DAC_DAC_ADDR    = 0x08;

    // default DAC gain: div=0x00, gains=0x80 (channels 0-6 gain 1, channel 7 gain 2)
    const uint8_t DAC_DEFAULT_DIV   = 0x00;
    const uint8_t DAC_DEFAULT_GAINS = 0x80;

    // LED matrix (APA102, 128 pixels, BGR order)
    const int   NUM_LEDS          = 128;
    const int   LED_MAX_INTENSITY = 100;
    const float GREEN_ADJUSTMENT  = 1.0f;
    const float RED_ADJUSTMENT    = 1.0f;
    const float BLUE_ADJUSTMENT   = 1.0f;

    // default global intensity factor (Squid LED 0-1.5V)
    const float DEFAULT_INTENSITY_FACTOR = 0.6f;

    // number of ports (D1-D16)
    const int NUM_PORTS = 16;

    // illumination light-source codes (legacy API, kept consistent with the protocol)
    // LED matrix patterns: 0-8
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
    // TTL-port light-source codes (note: D3/D4 out of order -- compatible with the legacy squid protocol)
    const int D1 = 11;
    const int D2 = 12;
    const int D3 = 14;  // out of order!
    const int D4 = 13;  // out of order!
    const int D5 = 15;
    // squid++ dual-camera adds D6-D8 light-source codes
    const int D6 = 16;
    const int D7 = 17;
    const int D8 = 18;
}

// Axis configuration
namespace AxisConfigs {

    // X-axis configuration
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
        // StallGuard parameters (only used by TMC2660 SG2; TMC2240 SG4 is
        // temporarily skipped where it is enabled in axis.cpp, parameters kept to enable after SG4 tuning)
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
        .invert_direction = false   // 2026-05-25 hardware direction inversion, default false (octoaxesplus new hardware pending testing)
    };

    // Y-axis configuration
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
        // same as X: StallGuard parameters only used by TMC2660; TMC2240 is skipped where enabled
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
        .invert_direction = false   // 2026-05-25 hardware direction inversion, default false (octoaxesplus new hardware pending testing)
    };

    // Z-axis configuration
    // ───────────────────────────────────────────────────────────────────
    // * Z-variant software switch: switching old/new Z only changes one line, Z_AXIS_VARIANT in software/octoaxesplus/constants.py
    // (the GUI sends pitch/current/microstepping at startup + limit polarity via cmd 20); [no firmware reflash needed, no compile switch needed].
    // After the positive/negative limit sensors were physically swapped on 06-09, the only firmware-side difference between old/new Z = limit polarity (new=1/old=0), which
    // is sent by the host via cmd 20 (SET_LIM_SWITCH_POLARITY) and overridden by reapplyLimitSwitches() re-writing the chip,
    // so the original #define Z_VARIANT_NEW compile switch was removed (2026-06-09). The fields below are the "boot-window defaults"
    // (effective before GUI config, overridden by what is sent afterward); homingSwitch/flip/enable/invertEncoder values for old/new Z
    // are already identical; only the polarity needs software differentiation, so the new default value 1 is used here.
    // 06-09 sensor swap: the home end now connects to the STOPR pin without flipping -> homingSwitch=RGHT_SW, flipped=false, enable=true.
    // pitch/current/microstepping are overridden by the GUI; currentRange=1 is common to both.
    // invertEncoderDir: boot default (ENC-2/ENC-3), not effective while enableEncoder=false; the direction is
    // determined at runtime by CONFIGURE_STAGE_PID (constants.py encoder_flip_direction) (see audit_octoaxesplus_20260608.md).
    const Axis::AxisConfig Z_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = RGHT_SW,         // boot default (both old/new Z use RGHT_SW; after the 06-09 sensor swap, home connects to the STOPR pin without flipping and is read directly as the STOPR bit)
        .leftSwitchPolarity = 0,         // boot default = old Z (0, active-low) -- consistent with the constants.py Z_AXIS_VARIANT="old" default; the new Z (1) is switched via cmd 20 sent at GUI startup
        .rightSwitchPolarity = 0,
        .polarityAffectsChip = true,     // Z only: allow cmd 20 to write the polarity to the chip (Z-variant software switch; the new Z sends 1 to override the boot default 0); X/Y etc. omit it = false, not writing the chip, preserving legacy Squid drop-in
        .leftIsInactive = 0,
        .rightIsInactive = 0,
        .leftFlipped = false,    // false for both old/new Z (the 06-09 sensor swap cancels the coordinate inversion, so INVERT_STOP_DIRECTION is not needed)
        .rightFlipped = false,
        .enableLeftLimitSwitch = true,   // true for both old/new Z (the chip's upper/lower hard stops work fine)
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
        .homing_timeout_ms = 60000,   // 60s: leaves ample margin for new-Z + legacy-Squid (can only use the default 1mm/s, ~34.5mm travel, ~34.5s worst case). Increasing the timeout has no side effects
        .homing_direct = 1,
        .driverType = DRIVER_AUTO,
        .currentRange = 1,         // 2026-06-06 new Z (LE143S 1.5A) debugged on the borrowed octoaxesplus board: TMC2240 I_FS=2A. currentRange has no send protocol and is firmware-only, so it must be set correctly here (the GUI only sends currentMA; with currentRange=0=I_FS1A, 1500mA would saturate and miscalculate). Old Z TMC2660 ignores this field, safe
        .enableEncoder = false,
        .encoderLinesPerRev = (uint16_t)(AxisConstDefinition::SCREW_PITCH_Z_MM * 1000 / AxisConstDefinition::ENCODER_RESOLUTION_UM_Z),
        .invertEncoderDir = true,   // boot default (ENC-3, not effective while enableEncoder=false); at runtime overridden by GUI CONFIGURE_STAGE_PID per constants.py encoder_flip_direction
        .invert_direction = false   // 2026-05-25 hardware direction inversion, default false
    };

    // W axis 4 configuration (filter wheel)
    const Axis::AxisConfig W_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = LEFT_SW,
        // 2026-07-20 polarity/hard-stop reverted to the octoaxes values (0 / true): user confirmed
        // both machines share the same filter wheel hardware, and [the octoaxesplus hardware side
        // will rework the sensor signal chain to match octoaxes].
        // ⚠️ Precondition: the hardware rework is complete — measured 2026-07-15 on an un-reworked
        // board: with polarity 0, STOPL is constantly active over the full revolution and reads
        // inactive only in the sensor window (~5°) -> homing falsely completes instantly + STOP_LEFT
        // blocks all negative motion (three bugs, see ce359fd). Flashing an un-reworked board revives
        // all three bugs verbatim; diagnosis then: disable the axis, hand-turn the wheel + monitor
        // STOPL via S:DUMPREGS (see SESSION 2026-07-15).
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
        .astartMM = 22.5f * AxisConstDefinition::SCREW_PITCH_FILTERWHEEL_MM,  // 2026-07-17 0→22.5 aligned with octoaxes (user confirmed both machines share the same filter wheel hardware; measured value from octoaxes 5-26 W speed optimization round 2, jerk-start 22.5 rev/s² ≈ 288K µstep/s² chip register @ms=64), pending on-machine regression
        .dfinalMM = 0,                                   // same as astart
        .homing_timeout_ms = 80000,
        .homing_direct = -1,   // 2026-07-17 filter wheel mapping = search direction inverted (-1 -> search +, historical behavior); boot default matches the protocol value each host writes when sending NEGATIVE for sign=1
        .driverType = DRIVER_AUTO,
        .currentRange = 2,
        .enableEncoder = false,
        .encoderLinesPerRev = 4000,
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 hardware direction inversion, default false (octoaxesplus new hardware pending testing)
    };

    // Expansion axis 1 configuration (objectives)
    const Axis::AxisConfig EXPAND1_AXIS = {
        .clockFrequency = SystemConfig::TMC4361_CLOCK_FREQUENCY,
        .homingSwitch = LEFT_SW,           // 2026-06-04 copied this board's X/Y limit scheme: the sensor is on the TMC4361A LEFT input
        .leftSwitchPolarity = 0,           // same as X/Y
        .rightSwitchPolarity = 0,          // same as X/Y
        .leftIsInactive = 0,               // dead field, does not affect the chip
        .rightIsInactive = 0,
        .leftFlipped = false,
        .rightFlipped = false,
        .enableLeftLimitSwitch = true,     // 2026-06-05 aligned with the objectives branch's verified W_AXIS: enable the chip LEFT-limit hard stop, so when homing (velocity mode) hits the limit the TMC4361A stops in hardware (objectives.cpp only zeroes, no software stop). Precondition: the Turret channel's home sensor must actually be connected to that chip's REF_L pin
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
        .currentRange = 1,                 // 2026-06-02 aligned with octoaxes E1: TMC2240 I_FS=2A (the original 0=1A lost steps with the gear-reduced objective)
        .enableEncoder = false,
        .encoderLinesPerRev = 0,
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 hardware direction inversion, default false (octoaxesplus new hardware pending testing)
    };

    // Expansion axis 3 configuration (Z-axis configuration)
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
        .currentRange = 1,         // audit F-8: unified to 1 with Z_AXIS. EXPAND3 reuses the Z template; if a 1.5A new Z (TMC2240 I_FS=2A) is connected, this value is needed for correct current; old Z TMC2660 ignores this field, safe. EXPAND3 is currently not instantiated
        .enableEncoder = false,
        .encoderLinesPerRev = 0,
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 hardware direction inversion, default false (octoaxesplus new hardware pending testing)
    };

    // ============================================================================
    // squid++ dual-camera expansion-axis configuration
    // current XYZW1W2 five-axis scheme: W1 / W2 = filter wheels (const struct copy of the W_AXIS template)
    // Z2/F2/R/T aliases are reserved for a future 8-axis expansion (not yet instantiated, no side effects)
    // after testing, change `=` to a full initializer as needed to tune parameters individually
    // ============================================================================

    // W1 axis configuration (filter wheel 1, CS=HC154 channel 6, same defaults as the W_AXIS filter wheel)
    const Axis::AxisConfig W1_AXIS = W_AXIS;

    // W2 axis configuration (filter wheel 2, CS=HC154 channel 4, same defaults as the W_AXIS filter wheel)
    const Axis::AxisConfig W2_AXIS = W_AXIS;

    // -- reserved expansion configs (not instantiated, kept as const struct copy templates) --------------------
    const Axis::AxisConfig Z2_AXIS = Z_AXIS;       // dual-focus Z2, same motor as Z1
    const Axis::AxisConfig F2_AXIS = W_AXIS;       // dual filter wheel F2, same as F1
    const Axis::AxisConfig R_AXIS  = EXPAND1_AXIS; // objective turret rotation
    const Axis::AxisConfig T_AXIS  = EXPAND1_AXIS; // objective turret translation

    // Expansion axis 4 configuration (filter wheel)
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
        .astartMM = 22.5f * AxisConstDefinition::SCREW_PITCH_FILTERWHEEL_MM,  // 2026-07-17 0→22.5 aligned with octoaxes (user confirmed both machines share the same filter wheel hardware; measured value from octoaxes 5-26 W speed optimization round 2, jerk-start 22.5 rev/s² ≈ 288K µstep/s² chip register @ms=64), pending on-machine regression
        .dfinalMM = 0,
        .homing_timeout_ms = 80000,
        .homing_direct = -1,   // 2026-07-17 filter wheel mapping = search direction inverted (-1 -> search +, historical behavior); boot default matches the protocol value each host writes when sending NEGATIVE for sign=1
        .driverType = DRIVER_AUTO,
        .currentRange = 0,
        .enableEncoder = false,
        .encoderLinesPerRev = 0,
        .invertEncoderDir = false,
        .invert_direction = false   // 2026-05-25 hardware direction inversion, default false (octoaxesplus new hardware pending testing)
    };
}

#endif
