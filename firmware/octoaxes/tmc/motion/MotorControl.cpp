/*
 * MotorControl.cpp
 *
 * High-level motion control layer implementation.
 *
 * Created: 2026-01-21
 */

#include "MotorControl.h"
#include "../ic/TMC4361A/TMC4361A.h"
#include "../ic/TMC2660/TMC2660.h"
#include "../hal/TMC_SPI.h"

// ============================================================================
// Motor Parameters Cache
// ============================================================================

MotorParams motorParams[MOTOR_IC_COUNT] = {};

// ============================================================================
// Internal Helper Functions
// ============================================================================

// Calculate current scale from mA and sense resistor
static uint8_t calculateCurrentScale(float currentMA, float rSense)
{
    // TMC2660 formula: I_rms = (CS + 1) / 32 * V_fs / R_sense
    // V_fs = 0.165V (VSENSE=1) or 0.310V (VSENSE=0)
    // Using VSENSE=1 (low range) for better resolution
    // CS = (I_rms * R_sense * 32 / 0.165) - 1

    float cs = (currentMA / 1000.0f) * rSense * 32.0f / 0.165f - 1.0f;

    if (cs < 0) cs = 0;
    if (cs > 31) cs = 31;

    return (uint8_t)cs;
}

// Calculate microstep resolution register value (reserved for future use)
__attribute__((unused))
static uint8_t calculateMresValue(uint16_t microsteps)
{
    // MRES: 0=256, 1=128, 2=64, 3=32, 4=16, 5=8, 6=4, 7=2, 8=1
    switch (microsteps) {
        case 256: return 0;
        case 128: return 1;
        case 64:  return 2;
        case 32:  return 3;
        case 16:  return 4;
        case 8:   return 5;
        case 4:   return 6;
        case 2:   return 7;
        case 1:   return 8;
        default:  return 0;  // Default to 256
    }
}

// ============================================================================
// Initialization
// ============================================================================

void motor_initSubsystem(void)
{
    // Initialize SPI HAL
    tmc_spi_init();

    // Initialize TMC4361A cache
    tmc4361A_initCache();

    // Initialize TMC2660 cache
    tmc2660_initCache();

    // Clear motor parameters
    for (int i = 0; i < MOTOR_IC_COUNT; i++) {
        motorParams[i].initialized = false;
    }
}

bool motor_init(uint8_t icID, const AxisMotionConfig *config)
{
    if (icID >= MOTOR_IC_COUNT || config == NULL)
        return false;

    // Initialize motion controller (TMC4361A)
    if (!motor_initMotionController(icID, &config->motion))
        return false;

    // Initialize motor driver (TMC2660)
    if (!motor_initDriver(icID, &config->motor))
        return false;

    // Configure limit switches
    motor_configLimitSwitches(icID, &config->limits);

    return true;
}

bool motor_initMotionController(uint8_t icID, const MotionConfig *config)
{
    if (icID >= MOTOR_IC_COUNT || config == NULL)
        return false;

    // Store motion parameters for unit conversion
    motorParams[icID].clockFrequency = config->clockFrequency;
    motorParams[icID].screwPitchMM = config->screwPitchMM;
    motorParams[icID].fullStepsPerRev = config->fullStepsPerRev;
    motorParams[icID].microsteps = config->microsteps;
    motorParams[icID].stepsPerMM = (float)(config->fullStepsPerRev * config->microsteps) / config->screwPitchMM;
    motorParams[icID].initialized = true;

    // Read VERSION_NO to verify communication
    int32_t version = tmc4361A_readRegister(icID, TMC4361A_VERSION_NO);
    if (version == 0 || version == -1) {
        return false;  // Communication failed
    }

    // Configure GENERAL_CONF
    // Bit 0: use_astart_and_vstart = 1
    // Bit 8: pol_dir_in = 0 (normal direction)
    uint32_t generalConf = 0x00000001;
    tmc4361A_writeRegister(icID, TMC4361A_GENERAL_CONF, generalConf);

    // Configure SPI_OUT_CONF for TMC2660 SPI mode communication
    // 0x4440108A: SCALE_VAL_TRANSFER_EN=1, SPI timing, COVER_DATA_LENGTH=1
    uint32_t spiOutConf = 0x4440108A;
    tmc4361A_writeRegister(icID, TMC4361A_SPI_OUT_CONF, spiOutConf);

    // Set clock frequency
    tmc4361A_writeRegister(icID, TMC4361A_CLK_FREQ, config->clockFrequency);

    // Configure ramp mode
    // RAMPMODE: 0=hold, 1=trapezoid, 2=S-shaped, 4=position mode
    uint32_t rampMode = config->useSShapedRamp ? 6 : 5;  // Position mode + ramp type
    tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);

    // Set velocity and acceleration parameters
    motor_setMaxVelocity(icID, config->maxVelocityMM);
    motor_setMaxAcceleration(icID, config->maxAccelerationMM);

    float decel = config->maxDecelerationMM > 0 ? config->maxDecelerationMM : config->maxAccelerationMM;
    motor_setMaxDeceleration(icID, decel);

    // Configure S-shaped ramp if enabled
    if (config->useSShapedRamp) {
        tmc4361A_writeRegister(icID, TMC4361A_BOW1, config->bow1);
        tmc4361A_writeRegister(icID, TMC4361A_BOW2, config->bow2);
        tmc4361A_writeRegister(icID, TMC4361A_BOW3, config->bow3);
        tmc4361A_writeRegister(icID, TMC4361A_BOW4, config->bow4);
    }

    // Set VSTART and VSTOP
    tmc4361A_writeRegister(icID, TMC4361A_VSTART, 0);
    tmc4361A_writeRegister(icID, TMC4361A_VSTOP, 0);

    return true;
}

bool motor_initDriver(uint8_t icID, const MotorConfig *config)
{
    if (icID >= MOTOR_IC_COUNT || config == NULL)
        return false;

    // Calculate current scale
    uint8_t cs = calculateCurrentScale(config->runCurrentMA, config->rSense);

    // Configure DRVCONF (matching old API value 0x000A1)
    // SDOFF=1: SPI mode (motion control via SPI, not Step/Dir)
    // VSENSE=0: High sense resistor voltage range
    // RDSEL=2: StallGuard2 value and CoolStep current level in response
    uint32_t drvconf = TMC2660_SET_RDSEL(2) | TMC2660_SET_VSENSE(0) | TMC2660_SET_SDOFF(1) | 0x01;
    tmc2660_writeRegister(icID, TMC2660_DRVCONF, drvconf);

    // Configure CHOPCONF
    uint8_t hend_reg = (uint8_t)(config->hend + 3);  // Offset by 3
    uint32_t chopconf = TMC2660_SET_TBL(config->tbl) |
                        TMC2660_SET_HEND(hend_reg) |
                        TMC2660_SET_HSTRT(config->hstrt) |
                        TMC2660_SET_TOFF(config->toff);
    tmc2660_writeRegister(icID, TMC2660_CHOPCONF, chopconf);

    // Configure SGCSCONF (current and StallGuard)
    uint8_t sgt = (uint8_t)(config->stallThreshold & 0x7F);
    uint32_t sgcsconf = TMC2660_SET_CS(cs) |
                        TMC2660_SET_SGT(sgt) |
                        TMC2660_SET_SFILT(config->stallFilter ? 1 : 0);
    tmc2660_writeRegister(icID, TMC2660_SGCSCONF, sgcsconf);

    // Configure SMARTEN (CoolStep - disabled by default)
    tmc2660_writeRegister(icID, TMC2660_SMARTEN, 0);

    // Configure DRVCTRL (microstep and interpolation)
    uint32_t drvctrl = TMC2660_SET_MRES(config->microstepRes) |
                       TMC2660_SET_INTERPOL(config->interpolation ? 1 : 0);
    tmc2660_writeRegister(icID, TMC2660_DRVCTRL, drvctrl);

    return true;
}

void motor_configLimitSwitches(uint8_t icID, const LimitConfig *config)
{
    if (icID >= MOTOR_IC_COUNT || config == NULL)
        return;

    // Configure REFERENCE_CONF register
    uint32_t refConf = 0;

    // Left switch configuration
    if (config->enableLeft) {
        refConf |= (1 << 0);  // STOP_LEFT_EN
        if (config->leftPolarity)
            refConf |= (1 << 4);  // POL_STOP_LEFT
    }

    // Right switch configuration
    if (config->enableRight) {
        refConf |= (1 << 1);  // STOP_RIGHT_EN
        if (config->rightPolarity)
            refConf |= (1 << 5);  // POL_STOP_RIGHT
    }

    // Soft stop enable
    refConf |= (1 << 10);  // SOFT_STOP_EN

    tmc4361A_writeRegister(icID, TMC4361A_REFERENCE_CONF, refConf);
}

// ============================================================================
// Motion Control
// ============================================================================

void motor_moveToPosition(uint8_t icID, float positionMM)
{
    int32_t microsteps = motor_mmToMicrosteps(icID, positionMM);
    motor_moveToMicrosteps(icID, microsteps);
}

void motor_moveByDistance(uint8_t icID, float distanceMM)
{
    int32_t current = motor_getPositionMicrosteps(icID);
    int32_t delta = motor_mmToMicrosteps(icID, distanceMM);
    motor_moveToMicrosteps(icID, current + delta);
}

void motor_moveToMicrosteps(uint8_t icID, int32_t position)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Ensure position mode
    uint32_t rampMode = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);
    rampMode |= TMC4361A_RAMP_POSITION;  // Set position mode bit
    tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);

    // Set target position
    tmc4361A_writeRegister(icID, TMC4361A_XTARGET, position);
}

void motor_rotateVelocity(uint8_t icID, float velocityMM)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Switch to velocity mode
    uint32_t rampMode = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);
    rampMode &= ~TMC4361A_RAMP_POSITION;  // Clear position mode bit
    tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);

    // Set velocity
    int32_t vel = motor_velocityMMToInternal(icID, velocityMM);
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, vel >= 0 ? vel : -vel);

    // Direction is determined by sign of VMAX in velocity mode
    if (vel < 0) {
        // For negative velocity, we need to handle direction
        // This depends on the specific implementation
    }
}

void motor_stop(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Set VMAX to 0 for smooth stop
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, 0);
}

void motor_emergencyStop(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Set target to current position for immediate stop
    int32_t current = tmc4361A_readRegister(icID, TMC4361A_XACTUAL);
    tmc4361A_writeRegister(icID, TMC4361A_XTARGET, current);
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, 0);
}

// ============================================================================
// Status Query
// ============================================================================

bool motor_isTargetReached(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return true;

    int32_t status = tmc4361A_readRegister(icID, TMC4361A_STATUS);
    return (status & (1 << 0)) != 0;  // TARGET_REACHED bit
}

bool motor_isRunning(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return false;

    int32_t velocity = tmc4361A_readRegister(icID, TMC4361A_VACTUAL);
    return velocity != 0;
}

float motor_getPositionMM(uint8_t icID)
{
    int32_t microsteps = motor_getPositionMicrosteps(icID);
    return motor_microstepsToMM(icID, microsteps);
}

int32_t motor_getPositionMicrosteps(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    return tmc4361A_readRegister(icID, TMC4361A_XACTUAL);
}

int32_t motor_getTargetMicrosteps(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    return tmc4361A_readRegister(icID, TMC4361A_XTARGET);
}

float motor_getVelocityMM(uint8_t icID)
{
    int32_t velInternal = motor_getVelocityInternal(icID);
    return motor_velocityInternalToMM(icID, velInternal);
}

int32_t motor_getVelocityInternal(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    return tmc4361A_readRegister(icID, TMC4361A_VACTUAL);
}

uint8_t motor_readLimitSwitches(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    uint32_t status = tmc4361A_readRegister(icID, TMC4361A_STATUS);

    // STOPL_ACTIVE_F is bit 7 (0x80), STOPR_ACTIVE_F is bit 8 (0x100)
    // Mask and shift to get bits 0 and 1
    status &= (TMC4361A_STOPL_ACTIVE_F_MASK | TMC4361A_STOPR_ACTIVE_F_MASK);
    status >>= TMC4361A_STOPL_ACTIVE_F_SHIFT;

    return (uint8_t)(status & 0x03);
}

uint32_t motor_readStatus(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    return tmc4361A_readRegister(icID, TMC4361A_STATUS);
}

uint32_t motor_readEvents(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    return tmc4361A_readRegister(icID, TMC4361A_EVENTS);
}

// ============================================================================
// Parameter Setting
// ============================================================================

void motor_setMaxVelocity(uint8_t icID, float velocityMM)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    int32_t vel = motor_velocityMMToInternal(icID, velocityMM);
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, vel);
}

void motor_setMaxAcceleration(uint8_t icID, float accelerationMM)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    uint32_t accel = motor_accelMMToInternal(icID, accelerationMM);
    tmc4361A_writeRegister(icID, TMC4361A_AMAX, accel);
}

void motor_setMaxDeceleration(uint8_t icID, float decelerationMM)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    uint32_t decel = motor_accelMMToInternal(icID, decelerationMM);
    tmc4361A_writeRegister(icID, TMC4361A_DMAX, decel);
}

void motor_setCurrentPosition(uint8_t icID, float positionMM)
{
    int32_t microsteps = motor_mmToMicrosteps(icID, positionMM);
    motor_setCurrentPositionMicrosteps(icID, microsteps);
}

void motor_setCurrentPositionMicrosteps(uint8_t icID, int32_t position)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Set both XACTUAL and XTARGET to avoid movement
    tmc4361A_writeRegister(icID, TMC4361A_XACTUAL, position);
    tmc4361A_writeRegister(icID, TMC4361A_XTARGET, position);
}

void motor_setRunCurrent(uint8_t icID, float currentMA)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Need rSense for calculation - use default or cached value
    // For now, assume 0.22 ohm typical
    uint8_t cs = calculateCurrentScale(currentMA, 0.22f);
    tmc2660_setRunCurrent(icID, cs);
}

void motor_enableDriver(uint8_t icID, bool enable)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    tmc2660_enableDriver(icID, enable);
}

// ============================================================================
// Unit Conversion
// ============================================================================

int32_t motor_mmToMicrosteps(uint8_t icID, float mm)
{
    if (icID >= MOTOR_IC_COUNT || !motorParams[icID].initialized)
        return 0;

    return (int32_t)(mm * motorParams[icID].stepsPerMM);
}

float motor_microstepsToMM(uint8_t icID, int32_t microsteps)
{
    if (icID >= MOTOR_IC_COUNT || !motorParams[icID].initialized)
        return 0.0f;

    return (float)microsteps / motorParams[icID].stepsPerMM;
}

int32_t motor_velocityMMToInternal(uint8_t icID, float velocityMM)
{
    if (icID >= MOTOR_IC_COUNT || !motorParams[icID].initialized)
        return 0;

    // TMC4361A velocity format: multiply by 2^8 (256) to account for 8 decimal places
    // Formula matches old API: (1 << 8) * mm * stepsPerMM
    int32_t velocity = (int32_t)((1 << 8) * velocityMM * motorParams[icID].stepsPerMM);

    return velocity;
}

float motor_velocityInternalToMM(uint8_t icID, int32_t velocityInternal)
{
    if (icID >= MOTOR_IC_COUNT || !motorParams[icID].initialized)
        return 0.0f;

    // Reverse of above
    float velocityPPS = (float)velocityInternal * (float)motorParams[icID].clockFrequency / 65536.0f;
    return velocityPPS / motorParams[icID].stepsPerMM;
}

uint32_t motor_accelMMToInternal(uint8_t icID, float accelMM)
{
    if (icID >= MOTOR_IC_COUNT || !motorParams[icID].initialized)
        return 0;

    // TMC4361A acceleration format: multiply by 2^2 (4) to account for 2 decimal places
    // Formula matches old API: (1 << 2) * mm * stepsPerMM
    uint32_t accel = (uint32_t)((1 << 2) * accelMM * motorParams[icID].stepsPerMM);

    return accel;
}

// ============================================================================
// Homing
// ============================================================================

void motor_startHoming(uint8_t icID, int8_t direction, float velocityMM)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Configure for velocity mode toward limit switch
    motor_rotateVelocity(icID, direction > 0 ? velocityMM : -velocityMM);
}

void motor_setHomePosition(uint8_t icID, float positionMM)
{
    motor_setCurrentPosition(icID, positionMM);
}

void motor_enableHomingLimit(uint8_t icID, uint8_t polarity, uint8_t whichSwitch,
                              int32_t safetyMarginMicrosteps)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Configure HOME_SAFETY_MARGIN
    tmc4361A_writeRegister(icID, TMC4361A_HOME_SAFETY_MARGIN, safetyMarginMicrosteps);

    // Read current REFERENCE_CONF and update homing settings
    uint32_t refConf = tmc4361A_readRegister(icID, TMC4361A_REFERENCE_CONF);

    // Configure latch position on home switch
    if (whichSwitch == 0x01) {  // Left switch
        refConf |= (1 << 8);  // LATCH_X_ON_ACTIVE_L
        if (polarity)
            refConf |= (1 << 4);  // POL_STOP_LEFT
        else
            refConf &= ~(1 << 4);
    } else {  // Right switch
        refConf |= (1 << 9);  // LATCH_X_ON_ACTIVE_R
        if (polarity)
            refConf |= (1 << 5);  // POL_STOP_RIGHT
        else
            refConf &= ~(1 << 5);
    }

    tmc4361A_writeRegister(icID, TMC4361A_REFERENCE_CONF, refConf);
}

// ============================================================================
// Soft Limit Implementation
// ============================================================================

void motor_setSoftLimits(uint8_t icID, int32_t lowerLimitMicrosteps, int32_t upperLimitMicrosteps)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Set virtual stop positions
    tmc4361A_writeRegister(icID, TMC4361A_VIRT_STOP_LEFT, lowerLimitMicrosteps);
    tmc4361A_writeRegister(icID, TMC4361A_VIRT_STOP_RIGHT, upperLimitMicrosteps);
}

void motor_enableSoftLimits(uint8_t icID, bool enableLower, bool enableUpper)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Read current REFERENCE_CONF
    uint32_t refConf = tmc4361A_readRegister(icID, TMC4361A_REFERENCE_CONF);

    // Configure virtual stop enables
    if (enableLower)
        refConf |= (1 << 2);  // VIRT_STOP_LEFT_EN
    else
        refConf &= ~(1 << 2);

    if (enableUpper)
        refConf |= (1 << 3);  // VIRT_STOP_RIGHT_EN
    else
        refConf &= ~(1 << 3);

    tmc4361A_writeRegister(icID, TMC4361A_REFERENCE_CONF, refConf);
}

// ============================================================================
// Advanced Configuration Implementation
// ============================================================================

void motor_disablePID(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Clear PID mode bits in ENC_IN_CONF or relevant register
    // TMC4361A PID is controlled via RAMPMODE and ENC_IN_CONF
    uint32_t rampMode = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);
    rampMode &= ~(0x03 << 8);  // Clear PID mode bits
    tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);
}

void motor_configStallGuard(uint8_t icID, int8_t threshold, bool filterEnable, bool stopOnStall)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Configure TMC2660 StallGuard
    tmc2660_setStallGuardThreshold(icID, threshold);
    tmc2660_setStallGuardFilter(icID, filterEnable);

    // Configure TMC4361A to react to stall event
    if (stopOnStall) {
        // Read SPI_STATUS_SELECTION and enable stall detection
        uint32_t spiStatus = tmc4361A_readRegister(icID, TMC4361A_SPI_STATUS_SELECTION);
        spiStatus |= (1 << 0);  // Enable SG status from TMC2660
        tmc4361A_writeRegister(icID, TMC4361A_SPI_STATUS_SELECTION, spiStatus);

        // Configure to stop on stall
        uint32_t eventConfig = tmc4361A_readRegister(icID, TMC4361A_INTR_CONF);
        eventConfig |= (1 << 29);  // Enable stall event
        tmc4361A_writeRegister(icID, TMC4361A_INTR_CONF, eventConfig);
    }
}

uint8_t motor_readSwitchEvent(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    // Read EVENTS register and extract switch events
    // STOPL_EVENT is bit 11 (0x0800), STOPR_EVENT is bit 12 (0x1000)
    uint32_t events = tmc4361A_readRegister(icID, TMC4361A_EVENTS);

    // Mask and shift to get bits 0 and 1
    events &= (TMC4361A_STOPL_EVENT_MASK | TMC4361A_STOPR_EVENT_MASK);
    events >>= TMC4361A_STOPL_EVENT_SHIFT;

    return (uint8_t)(events & 0x03);
}

void motor_setVelocityInternal(uint8_t icID, int32_t velocityInternal)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Clear EVENTS register (reading clears it) - matches old API behavior
    tmc4361A_readRegister(icID, TMC4361A_EVENTS);

    // Switch to velocity mode: clear position and hold bits
    uint32_t rampMode = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);
    rampMode &= ~(TMC4361A_RAMP_POSITION | TMC4361A_RAMP_HOLD);
    tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);

    // Set velocity directly to VMAX (signed value determines direction)
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, velocityInternal);
}

int32_t motor_readLatchPosition(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    return tmc4361A_readRegister(icID, TMC4361A_X_LATCH);
}
