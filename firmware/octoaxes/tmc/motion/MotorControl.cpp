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
#include <Arduino.h>

// ============================================================================
// Debug Helper
// ============================================================================

extern "C" void motor_debugPrint(const char* msg, int32_t val)
{
    SerialUSB.print(msg);
    SerialUSB.print(":");
    SerialUSB.println(val);
}

// ============================================================================
// Motor Parameters Cache
// ============================================================================

MotorParams motorParams[MOTOR_IC_COUNT] = {};

// ============================================================================
// Internal Helper Functions
// ============================================================================

// BOW 参数最大值 (与旧 API BOWMAX 一致)
#define BOWMAX ((1 << 24) - 1)

// 自动计算 BOW 参数 (与旧 API tmc4361A_adjustBows 完全一致)
// 公式: BOW = AMAX² / VMAX
// 目的: 最小化在 AMAX 饱和的时间
static void motor_adjustBows(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT || !motorParams[icID].initialized)
        return;

    // 获取 AMAX 和 VMAX (内部单位)
    int32_t vmax = motorParams[icID].vmax;
    uint32_t amax = motorParams[icID].amax;

    if (vmax == 0) {
        // 避免除以零
        motorParams[icID].bow1 = 0;
        motorParams[icID].bow2 = 0;
        motorParams[icID].bow3 = 0;
        motorParams[icID].bow4 = 0;
        return;
    }

    // 转换为 mm 单位进行计算 (与旧 API 一致)
    // VMAX 内部单位是 24.8 定点数，需要除以 256
    float vmax_mm = (float)vmax / 256.0f / motorParams[icID].stepsPerMM;

    // AMAX 内部单位是 22.2 定点数，需要除以 4
    float amax_mm = (float)amax / 4.0f / motorParams[icID].stepsPerMM;

    // 计算 BOW 值: AMAX² / VMAX
    float bowval_mm = (amax_mm * amax_mm) / vmax_mm;

    // 转换回内部单位 (微步)
    int32_t bow = (int32_t)(bowval_mm * motorParams[icID].stepsPerMM);
    if (bow < 0) bow = -bow;  // abs
    if (bow > BOWMAX) bow = BOWMAX;

    // 所有 4 个 BOW 参数设为相同值 (与旧 API 一致)
    motorParams[icID].bow1 = bow;
    motorParams[icID].bow2 = bow;
    motorParams[icID].bow3 = bow;
    motorParams[icID].bow4 = bow;

    Serial.print("motor_adjustBows: icID=");
    Serial.print(icID);
    Serial.print(" AMAX=");
    Serial.print(amax);
    Serial.print(" VMAX=");
    Serial.print(vmax);
    Serial.print(" BOW=");
    Serial.println(bow);
}

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
    motorParams[icID].velocity_mode = false;  // 与旧 API tmc4361A_init 一致

    // Reset TMC4361A (same as old API)
    tmc4361A_writeRegister(icID, TMC4361A_SW_RESET, 0x52535400);

    // Read VERSION_NO to verify communication
    int32_t version = tmc4361A_readRegister(icID, TMC4361A_VERSION_NO);
    if (version == 0 || version == -1) {
        return false;  // Communication failed
    }

    // Configure GENERAL_CONF
    // 重要：与旧 API sRampInit 一致，清除 use_astart_and_vstart 位
    // 旧 API: tmc4361A_rstBits(tmc4361A, TMC4361A_GENERAL_CONF, TMC4361A_USE_ASTART_AND_VSTART_MASK);
    uint32_t generalConf = 0x00000000;  // use_astart_and_vstart = 0
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

    // ========================================================================
    // 缓存斜坡参数 (与旧 API rampParam[] 一致)
    // ========================================================================

    // 计算并缓存速度/加速度参数
    int32_t vmax = motor_velocityMMToInternal(icID, config->maxVelocityMM);
    uint32_t amax = motor_accelMMToInternal(icID, config->maxAccelerationMM);
    float decelMM = config->maxDecelerationMM > 0 ? config->maxDecelerationMM : config->maxAccelerationMM;
    uint32_t dmax = motor_accelMMToInternal(icID, decelMM);

    motorParams[icID].vmax = vmax;
    motorParams[icID].amax = amax;
    motorParams[icID].dmax = dmax;
    motorParams[icID].astart = 0;
    motorParams[icID].dfinal = 0;

    // 写入硬件寄存器
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, vmax);
    tmc4361A_writeRegister(icID, TMC4361A_AMAX, amax);
    tmc4361A_writeRegister(icID, TMC4361A_DMAX, dmax);

    // Configure S-shaped ramp if enabled
    if (config->useSShapedRamp) {
        // 如果 BOW 参数为 0，自动计算 (与旧 API tmc4361A_adjustBows 一致)
        if (config->bow1 == 0 && config->bow2 == 0 && config->bow3 == 0 && config->bow4 == 0) {
            motor_adjustBows(icID);
        } else {
            motorParams[icID].bow1 = config->bow1;
            motorParams[icID].bow2 = config->bow2;
            motorParams[icID].bow3 = config->bow3;
            motorParams[icID].bow4 = config->bow4;
        }

        // 写入 BOW 寄存器
        tmc4361A_writeRegister(icID, TMC4361A_BOW1, motorParams[icID].bow1);
        tmc4361A_writeRegister(icID, TMC4361A_BOW2, motorParams[icID].bow2);
        tmc4361A_writeRegister(icID, TMC4361A_BOW3, motorParams[icID].bow3);
        tmc4361A_writeRegister(icID, TMC4361A_BOW4, motorParams[icID].bow4);
    } else {
        motorParams[icID].bow1 = 0;
        motorParams[icID].bow2 = 0;
        motorParams[icID].bow3 = 0;
        motorParams[icID].bow4 = 0;
    }

    // Set VSTART, VSTOP, ASTART, DFINAL (与旧 API sRampInit 一致)
    tmc4361A_writeRegister(icID, TMC4361A_VSTART, 0);
    tmc4361A_writeRegister(icID, TMC4361A_VSTOP, 0);
    tmc4361A_writeRegister(icID, TMC4361A_ASTART, 0);  // initial acceleration
    tmc4361A_writeRegister(icID, TMC4361A_DFINAL, 0);  // final deceleration

    // ========================================================================
    // 关键配置: 微步和每转步数 (与旧 API tmc4361A_writeMicrosteps/writeSPR 一致)
    // ========================================================================

    // 计算 MSTEP_PER_FS 值: 256->0, 128->1, ..., 1->8
    uint16_t mstep = config->microsteps;
    uint8_t mstepVal = 0;
    if (mstep > 0 && (mstep & (mstep - 1)) == 0 && mstep <= 256) {
        // 计算 log2(mstep) + 1, 然后 9 - result
        uint8_t bitsSet = 0;
        while (mstep > 0) {
            bitsSet++;
            mstep >>= 1;
        }
        mstepVal = 9 - bitsSet;
    }

    // 组合 STEP_CONF: MSTEP_PER_FS (bit 0-3) + FS_PER_REV (bit 4-15)
    uint32_t stepConf = (mstepVal & TMC4361A_MSTEP_PER_FS_MASK) |
                        ((uint32_t)config->fullStepsPerRev << TMC4361A_FS_PER_REV_SHIFT);
    tmc4361A_writeRegister(icID, TMC4361A_STEP_CONF, stepConf);

    // ========================================================================
    // 关键配置: 电流缩放 (与旧 API tmc4361A_cScaleInit 一致)
    // ========================================================================

    // SCALE_VALUES: 使用默认值 (hold=128, drv1=255, drv2=255, boost=128)
    // 这些值对应旧 API 的 0.5, 1.0, 1.0, 0.5 缩放系数
    uint32_t scaleValues = (128 << TMC4361A_HOLD_SCALE_VAL_SHIFT) |   // hold = 50%
                           (255 << TMC4361A_DRV2_SCALE_VAL_SHIFT) |   // drv2 = 100%
                           (255 << TMC4361A_DRV1_SCALE_VAL_SHIFT) |   // drv1 = 100%
                           (128 << TMC4361A_BOOST_SCALE_VAL_SHIFT);   // boost = 50%
    tmc4361A_writeRegister(icID, TMC4361A_SCALE_VALUES, scaleValues);

    // CURRENT_CONF: 使能驱动电流缩放和保持电流缩放
    uint32_t currentConf = tmc4361A_readRegister(icID, TMC4361A_CURRENT_CONF);
    currentConf |= TMC4361A_DRIVE_CURRENT_SCALE_EN_MASK;  // bit 1
    currentConf |= TMC4361A_HOLD_CURRENT_SCALE_EN_MASK;   // bit 0
    tmc4361A_writeRegister(icID, TMC4361A_CURRENT_CONF, currentConf);

    return true;
}

bool motor_initDriver(uint8_t icID, const MotorConfig *config)
{
    if (icID >= MOTOR_IC_COUNT || config == NULL)
        return false;

    // Calculate current scale
    uint8_t cs = calculateCurrentScale(config->runCurrentMA, config->rSense);

    // ========================================================================
    // TMC2660 初始化 - 与旧 API 顺序一致
    // 旧 API 顺序: CHOPCONF -> SMARTEN -> SGCSCONF -> DRVCONF
    // 注意: SPI 模式 (SDOFF=1) 下不发送 DRVCTRL
    // ========================================================================

    // 1. Configure CHOPCONF (旧 API: 0x000900C3)
    uint8_t hend_reg = (uint8_t)(config->hend + 3);  // Offset by 3
    uint32_t chopconf = TMC2660_SET_TBL(config->tbl) |
                        TMC2660_SET_HEND(hend_reg) |
                        TMC2660_SET_HSTRT(config->hstrt) |
                        TMC2660_SET_TOFF(config->toff);
    tmc2660_writeRegister(icID, TMC2660_CHOPCONF, chopconf);

    // 2. Configure SMARTEN (旧 API: 0x000A0000, CoolStep disabled)
    tmc2660_writeRegister(icID, TMC2660_SMARTEN, 0);

    // 3. Configure SGCSCONF (旧 API: 0x000C000A)
    uint8_t sgt = (uint8_t)(config->stallThreshold & 0x7F);
    uint32_t sgcsconf = TMC2660_SET_CS(cs) |
                        TMC2660_SET_SGT(sgt) |
                        TMC2660_SET_SFILT(config->stallFilter ? 1 : 0);
    tmc2660_writeRegister(icID, TMC2660_SGCSCONF, sgcsconf);

    // 4. Configure DRVCONF (旧 API: 0x000E00A1)
    // SDOFF=1: SPI mode (motion control via SPI, not Step/Dir)
    // VSENSE=0: High sense resistor voltage range
    // RDSEL=2: StallGuard2 value and CoolStep current level in response
    uint32_t drvconf = TMC2660_SET_RDSEL(2) | TMC2660_SET_VSENSE(0) | TMC2660_SET_SDOFF(1) | 0x01;
    tmc2660_writeRegister(icID, TMC2660_DRVCONF, drvconf);

    // 注意: 在 SPI 模式 (SDOFF=1) 下，不发送 DRVCTRL
    // 微步由 TMC4361A 的 STEP_CONF 寄存器控制

    return true;
}

void motor_configLimitSwitches(uint8_t icID, const LimitConfig *config)
{
    if (icID >= MOTOR_IC_COUNT || config == NULL)
        return;

    // Read current REFERENCE_CONF to preserve other bits (与旧 API setBits 行为一致)
    uint32_t refConf = tmc4361A_readRegister(icID, TMC4361A_REFERENCE_CONF);

    // Left switch configuration
    if (config->enableLeft) {
        refConf |= TMC4361A_STOP_LEFT_EN_MASK;   // bit 0
        if (config->leftPolarity)
            refConf |= TMC4361A_POL_STOP_LEFT_MASK;  // bit 2
        else
            refConf &= ~TMC4361A_POL_STOP_LEFT_MASK;
        // Enable position latching on limit switch activation (与旧 API 一致)
        refConf |= TMC4361A_LATCH_X_ON_ACTIVE_L_MASK;  // bit 11
    }

    // Right switch configuration
    if (config->enableRight) {
        refConf |= TMC4361A_STOP_RIGHT_EN_MASK;  // bit 1
        if (config->rightPolarity)
            refConf |= TMC4361A_POL_STOP_RIGHT_MASK;  // bit 3
        else
            refConf &= ~TMC4361A_POL_STOP_RIGHT_MASK;
        // Enable position latching on limit switch activation (与旧 API 一致)
        refConf |= TMC4361A_LATCH_X_ON_ACTIVE_R_MASK;  // bit 13
    }

    // Soft stop enable
    refConf |= TMC4361A_SOFT_STOP_EN_MASK;  // bit 5

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

    // Debug: read before
    uint32_t rampModeBefore = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);

    // ========================================================================
    // 与旧 API tmc4361A_moveTo 完全一致的实现
    // ========================================================================

    // 状态恢复: 仅当 velocity_mode == true 时调用 sRampInit
    // 与旧 API: if(tmc4361A->velocity_mode) { tmc4361A_sRampInit(); velocity_mode = false; }
    if (motorParams[icID].velocity_mode) {
        // ====================================================================
        // sRampInit 等效实现 (与旧 API tmc4361A_sRampInit 完全一致)
        // ====================================================================

        // 1. RAMPMODE: 使用 setBits 设置位置模式 + S-shaped 斜坡
        //    旧 API: tmc4361A_setBits(tmc4361A, TMC4361A_RAMPMODE, TMC4361A_RAMP_POSITION | TMC4361A_RAMP_SSHAPE);
        uint32_t rampMode = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);
        rampMode |= (TMC4361A_RAMP_POSITION | TMC4361A_RAMP_SSHAPE);
        tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);

        // 2. 清除 USE_ASTART_AND_VSTART
        //    旧 API: tmc4361A_rstBits(tmc4361A, TMC4361A_GENERAL_CONF, TMC4361A_USE_ASTART_AND_VSTART_MASK);
        uint32_t generalConf = tmc4361A_readRegister(icID, TMC4361A_GENERAL_CONF);
        generalConf &= ~TMC4361A_USE_ASTART_AND_VSTART_MASK;
        tmc4361A_writeRegister(icID, TMC4361A_GENERAL_CONF, generalConf);

        // 3. 重写所有斜坡参数 (与旧 API sRampInit 一致)
        tmc4361A_writeRegister(icID, TMC4361A_BOW1, motorParams[icID].bow1);
        tmc4361A_writeRegister(icID, TMC4361A_BOW2, motorParams[icID].bow2);
        tmc4361A_writeRegister(icID, TMC4361A_BOW3, motorParams[icID].bow3);
        tmc4361A_writeRegister(icID, TMC4361A_BOW4, motorParams[icID].bow4);
        tmc4361A_writeRegister(icID, TMC4361A_AMAX, motorParams[icID].amax);
        tmc4361A_writeRegister(icID, TMC4361A_DMAX, motorParams[icID].dmax);
        tmc4361A_writeRegister(icID, TMC4361A_ASTART, motorParams[icID].astart);
        tmc4361A_writeRegister(icID, TMC4361A_DFINAL, motorParams[icID].dfinal);
        tmc4361A_writeRegister(icID, TMC4361A_VMAX, motorParams[icID].vmax);

        // 4. 清除 velocity_mode 标志
        motorParams[icID].velocity_mode = false;

    }

    // ========================================================================
    // 写入目标位置 (与旧 API tmc4361A_moveTo 一致)
    // ========================================================================

    // Read events before and after to clear the register
    tmc4361A_readRegister(icID, TMC4361A_EVENTS);
    tmc4361A_writeRegister(icID, TMC4361A_XTARGET, position);
    tmc4361A_readRegister(icID, TMC4361A_EVENTS);

    // Read X_ACTUAL to get it to refresh
    tmc4361A_readRegister(icID, TMC4361A_XACTUAL);
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
    motorParams[icID].vmax = vel;  // 保存用于位置模式恢复 (与旧 API rampParam[VMAX_IDX] 一致)

    // 与旧 API tmc4361A_setMaxSpeed 一致: 自动重新计算 BOW 参数
    motor_adjustBows(icID);

    // 写入硬件 (sRampInit 等效)
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, motorParams[icID].vmax);
    tmc4361A_writeRegister(icID, TMC4361A_BOW1, motorParams[icID].bow1);
    tmc4361A_writeRegister(icID, TMC4361A_BOW2, motorParams[icID].bow2);
    tmc4361A_writeRegister(icID, TMC4361A_BOW3, motorParams[icID].bow3);
    tmc4361A_writeRegister(icID, TMC4361A_BOW4, motorParams[icID].bow4);
}

void motor_resetRampMode(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // 读取当前 RAMPMODE
    uint32_t rampModeBefore = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);

    // 重置 RAMPMODE 为位置模式 + S-shaped 斜坡 (与初始化一致)
    // 这在 RESET 命令或硬件限位触发后需要调用
    uint32_t rampMode = 0x06;  // S-shaped position mode
    tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);

    // 读取设置后的 RAMPMODE
    uint32_t rampModeAfter = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);

    // 调试输出
    Serial.print("motor_resetRampMode: icID=");
    Serial.print(icID);
    Serial.print(" RAMPMODE: 0x");
    Serial.print(rampModeBefore, HEX);
    Serial.print(" -> 0x");
    Serial.println(rampModeAfter, HEX);
}

void motor_setMaxAcceleration(uint8_t icID, float accelerationMM)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    uint32_t accel = motor_accelMMToInternal(icID, accelerationMM);
    motorParams[icID].amax = accel;  // 缓存 (与旧 API rampParam[AMAX_IDX] 一致)
    motorParams[icID].dmax = accel;  // 与旧 API 一致: DMAX = AMAX

    // 与旧 API tmc4361A_setMaxAcceleration 一致: 自动重新计算 BOW 参数
    motor_adjustBows(icID);

    // 写入硬件 (sRampInit 等效)
    tmc4361A_writeRegister(icID, TMC4361A_AMAX, motorParams[icID].amax);
    tmc4361A_writeRegister(icID, TMC4361A_DMAX, motorParams[icID].dmax);
    tmc4361A_writeRegister(icID, TMC4361A_BOW1, motorParams[icID].bow1);
    tmc4361A_writeRegister(icID, TMC4361A_BOW2, motorParams[icID].bow2);
    tmc4361A_writeRegister(icID, TMC4361A_BOW3, motorParams[icID].bow3);
    tmc4361A_writeRegister(icID, TMC4361A_BOW4, motorParams[icID].bow4);
}

void motor_setMaxDeceleration(uint8_t icID, float decelerationMM)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    uint32_t decel = motor_accelMMToInternal(icID, decelerationMM);
    motorParams[icID].dmax = decel;  // 缓存 (与旧 API rampParam[DMAX_IDX] 一致)
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

    // 与旧 API tmc4361A_setCurrentPosition 行为一致：
    // 1. 先停止电机（设置 VMAX=0）
    // 2. 设置 XACTUAL 和 XTARGET
    // 3. 设置 velocity_mode=true，下次 moveToMicrosteps 时会恢复 VMAX
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, 0);
    tmc4361A_writeRegister(icID, TMC4361A_XACTUAL, position);
    tmc4361A_writeRegister(icID, TMC4361A_XTARGET, position);
    motorParams[icID].velocity_mode = true;
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

    // Read current REFERENCE_CONF
    uint32_t refConf = tmc4361A_readRegister(icID, TMC4361A_REFERENCE_CONF);

    // Configure HOME_EVENT and home switch (与旧 API tmc4361A_enableHomingLimit 一致)
    if (whichSwitch == 0x01) {  // Left switch (LEFT_SW)
        if (polarity != 0) {
            // Active high: HOME_REF = 0 indicates positive direction
            refConf |= (0b1100 << TMC4361A_HOME_EVENT_SHIFT);
        } else {
            // Active low: HOME_REF = 0 indicates negative direction
            refConf |= (0b0011 << TMC4361A_HOME_EVENT_SHIFT);
        }
        // Use stop left as home
        refConf |= TMC4361A_STOP_LEFT_IS_HOME_MASK;
    } else {  // Right switch (RGHT_SW)
        if (polarity != 0) {
            // Active high
            refConf |= (0b0011 << TMC4361A_HOME_EVENT_SHIFT);
        } else {
            // Active low
            refConf |= (0b1100 << TMC4361A_HOME_EVENT_SHIFT);
        }
        // Use stop right as home (bit 15)
        refConf |= (1 << 15);  // TMC4361A_STOP_RIGHT_IS_HOME
    }

    tmc4361A_writeRegister(icID, TMC4361A_REFERENCE_CONF, refConf);

    // Set HOME_SAFETY_MARGIN
    tmc4361A_writeRegister(icID, TMC4361A_HOME_SAFETY_MARGIN, safetyMarginMicrosteps);
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

    // Configure virtual stop enables (使用官方宏定义)
    if (enableLower) {
        refConf |= TMC4361A_VIRTUAL_LEFT_LIMIT_EN_MASK;   // bit 6
        // Set VIRT_STOP_MODE = 1 for hard stop (与旧 API 一致)
        refConf |= (1 << TMC4361A_VIRT_STOP_MODE_SHIFT);  // bit 8
    } else {
        refConf &= ~TMC4361A_VIRTUAL_LEFT_LIMIT_EN_MASK;
    }

    if (enableUpper) {
        refConf |= TMC4361A_VIRTUAL_RIGHT_LIMIT_EN_MASK;  // bit 7
        // Set VIRT_STOP_MODE = 1 for hard stop (与旧 API 一致)
        refConf |= (1 << TMC4361A_VIRT_STOP_MODE_SHIFT);  // bit 8
    } else {
        refConf &= ~TMC4361A_VIRTUAL_RIGHT_LIMIT_EN_MASK;
    }

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

    // Configure TMC4361A to react to stall event (与旧 API 一致)
    if (stopOnStall) {
        // Set VSTALL_LIMIT (与旧 API 一致)
        // 0 = react at any velocity > 0
        tmc4361A_writeRegister(icID, TMC4361A_VSTALL_LIMIT, 0);

        // Enable stop on stall in REFERENCE_CONF (bit 26)
        uint32_t refConf = tmc4361A_readRegister(icID, TMC4361A_REFERENCE_CONF);
        refConf |= TMC4361A_STOP_ON_STALL_MASK;      // Enable stop on stall
        refConf &= ~TMC4361A_DRV_AFTER_STALL_MASK;   // Disable drive after stall
        tmc4361A_writeRegister(icID, TMC4361A_REFERENCE_CONF, refConf);
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

    // ========================================================================
    // 与旧 API tmc4361A_setSpeed 完全一致的实现
    // ========================================================================

    // 1. 设置 velocity_mode 标志 (与旧 API: tmc4361A->velocity_mode = true)
    motorParams[icID].velocity_mode = true;

    // 2. Clear EVENTS register (reading clears it)
    tmc4361A_readRegister(icID, TMC4361A_EVENTS);

    // 3. 清除 POSITION 位和 HOLD 位，保留 S-shaped 位
    //    旧 API: tmc4361A_rstBits(tmc4361A, TMC4361A_RAMPMODE, TMC4361A_RAMP_POSITION | TMC4361A_RAMP_HOLD);
    //    如果原来是 0x06 (S-shaped 位置模式)，结果是 0x02 (S-shaped 速度模式)
    uint32_t rampModeBefore = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);
    uint32_t rampMode = rampModeBefore & ~(TMC4361A_RAMP_POSITION | TMC4361A_RAMP_HOLD);
    tmc4361A_writeRegister(icID, TMC4361A_RAMPMODE, rampMode);

    // 4. Set velocity directly to VMAX (signed value determines direction)
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, velocityInternal);

}

int32_t motor_readLatchPosition(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return 0;

    return tmc4361A_readRegister(icID, TMC4361A_X_LATCH);
}
