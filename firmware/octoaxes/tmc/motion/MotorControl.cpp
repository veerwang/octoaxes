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
#include "../ic/TMC2240/TMC2240.h"
#include "../hal/TMC_SPI.h"
#include <Arduino.h>
#include "../../build_opt.h"

// ============================================================================
// Debug Helper
// ============================================================================

extern "C" void motor_debugPrint(const char* msg, int32_t val)
{
    DEBUG_PRINT(msg);
    DEBUG_PRINT(":");
    DEBUG_PRINTLN(val);
}

// ============================================================================
// TMC2240 HAL Callbacks
// ============================================================================

// TMC2240 通过 TMC4361A 的 40-bit Cover 接口通信
// tmc2240_readWriteSPI 回调：5 字节 SPI 帧 → TMC4361A COVER_HIGH + COVER_LOW
extern "C" void tmc2240_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength)
{
    // 路由到 TMC4361A Cover 接口 (5 字节 = 40-bit)
    tmc4361A_readWriteCover(icID, data, dataLength);
}

extern "C" TMC2240BusType tmc2240_getBusType(uint16_t icID)
{
    (void)icID;
    return TMC2240_BUS_SPI;
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

    DEBUG_PRINT("motor_adjustBows: icID=");
    DEBUG_PRINT(icID);
    DEBUG_PRINT(" AMAX=");
    DEBUG_PRINT(amax);
    DEBUG_PRINT(" VMAX=");
    DEBUG_PRINT(vmax);
    DEBUG_PRINT(" BOW=");
    DEBUG_PRINTLN(bow);
}

// Calculate current scale from peak current (mA) and sense resistor
// TMC2660 公式:
//   I_peak = (CS + 1) / 32 × V_FS / R_sense
//   I_rms  = I_peak / √2
// VSENSE=0 → V_FS = 0.310V; VSENSE=1 → V_FS = 0.165V
// 本项目 DRVCONF 设置 VSENSE=0 (高量程)
// 芯片绝对上限: 4A 峰值 (2.8A RMS), CS 范围 0~31
static uint8_t calculateCurrentScale(float currentMA, float rSense)
{
    // CS = (I_peak × R_sense × 32 / V_FS) - 1
    float cs = (currentMA / 1000.0f) * rSense * 32.0f / 0.310f - 1.0f;

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

// Get TMC2240 full-scale current (A) from CURRENT_RANGE setting
// TMC2240 使用集成电流传感 (ICS)，无外部检流电阻
// I_FS 由 DRV_CONF.CURRENT_RANGE 决定（假设 RREF=默认值）
static float getTMC2240_IFS(uint8_t currentRange)
{
    switch (currentRange & 0x03) {
        case 0:  return 1.0f;   // CURRENT_RANGE=0: ~1.0A
        case 1:  return 2.0f;   // CURRENT_RANGE=1: ~2.0A
        default: return 3.0f;   // CURRENT_RANGE=2/3: ~3.0A
    }
}

// Calculate TMC2240 current scale (IRUN/IHOLD value 0-31)
// TMC2240 公式 (数据手册 §3):
//   I_RMS = (CS_ACTUAL + 1) / 32 × (GLOBALSCALER / 256) × I_FS
//   简化 (GLOBALSCALER=0 即 256):
//   IRUN = round(I_peak / I_FS × 32) - 1
// 注意: currentMA 是峰值电流 (mA)，I_FS 由 CURRENT_RANGE 决定
static uint8_t calculateCurrentScale_TMC2240(float currentMA, uint8_t currentRange, uint8_t globalScaler)
{
    float ifs = getTMC2240_IFS(currentRange);
    float gs = (globalScaler == 0) ? 1.0f : (float)globalScaler / 256.0f;

    // IRUN = (I_peak / I_FS / GLOBALSCALER_ratio) × 32 - 1
    float cs = (currentMA / 1000.0f) / ifs / gs * 32.0f - 1.0f;

    if (cs < 0) cs = 0;
    if (cs > 31) cs = 31;

    return (uint8_t)cs;
}

// Forward declaration
static bool motor_initDriver_TMC2660(uint8_t icID, const MotorConfig *config);
static bool motor_initDriver_TMC2240(uint8_t icID, const MotorConfig *config);

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

    // Initialize TMC2240 cache
    tmc2240_initCache();

    // Clear motor parameters
    for (int i = 0; i < MOTOR_IC_COUNT; i++) {
        motorParams[i].initialized = false;
    }
}

bool motor_init(uint8_t icID, const AxisMotionConfig *config)
{
    if (icID >= MOTOR_IC_COUNT || config == NULL)
        return false;

    // 提前保存驱动类型，motor_initMotionController 需要用来选择 SPI_OUT_CONF
    motorParams[icID].driverType = config->motor.driverType;

    // Initialize motion controller (TMC4361A)
    if (!motor_initMotionController(icID, &config->motion))
        return false;

    // Initialize motor driver (TMC2660 or TMC2240)
    if (!motor_initDriver(icID, &config->motor))
        return false;

    // Configure limit switches
    motor_configLimitSwitches(icID, &config->limits);

    return true;
}

// ============================================================================
// 驱动芯片自动检测
// ============================================================================

uint8_t motor_detectDriverType(uint8_t icID)
{
    // 使用 TMC2660 格式 (format=0x0A, 20-bit auto SPI) 配合手动 40-bit Cover
    // 20-bit auto SPI 不覆盖 40-bit Cover 的完整响应，解决 format=0x0D 干扰问题
    // COVER_DATA_LENGTH=40 确保 Cover 传输使用 40-bit
    // SPI timing: block=4, high=4, low=4
    uint32_t spiOutConf_detect = 0x4445000A;  // CDL=40 + format=0x0A
    tmc4361A_writeRegister(icID, TMC4361A_SPI_OUT_CONF, spiOutConf_detect);
    delayMicroseconds(500);

    // 通过 40-bit Cover 读取 TMC2240 IOIN (地址 0x04)
    // TMC2240 VERSION 字段在 IOIN[31:24] = 0x40
    // TMC2660 收到 40-bit 会返回 20-bit 响应 + 填充，VERSION ≠ 0x40
    int32_t ioin = tmc2240_readRegister(icID, TMC2240_IOIN);
    uint8_t version = (ioin >> 24) & 0xFF;

    DEBUG_PRINT("IC");
    DEBUG_PRINT(icID);
    DEBUG_PRINT(":detect IOIN=0x");
    DEBUG_PRINTF(ioin, HEX);
    DEBUG_PRINT(" ver=0x");
    DEBUG_PRINTF(version, HEX);

    if (version == 0x40) {
        DEBUG_PRINTLN(" -> TMC2240");
        return DRIVER_TMC2240;
    } else {
        DEBUG_PRINTLN(" -> TMC2660");
        return DRIVER_TMC2660;
    }
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

    // 先设置 CLK_FREQ，Cover SPI 时钟依赖此配置
    tmc4361A_writeRegister(icID, TMC4361A_CLK_FREQ, config->clockFrequency);

    // 自动检测驱动芯片类型
    if (motorParams[icID].driverType == DRIVER_AUTO) {
        motorParams[icID].driverType = motor_detectDriverType(icID);
    }

    // Configure GENERAL_CONF
    uint32_t generalConf = 0x00000000;
    if (config->astartMM > 0) {
        generalConf |= TMC4361A_USE_ASTART_AND_VSTART_MASK;  // 使能 ASTART/DFINAL
    }
    // TMC2240 direct_mode 下方向由 TMC4361A 微步表相位序列决定，
    // TMC2240 SHAFT 位无效（仅影响 STEP/DIR 模式）。
    // 反转 TMC4361A 内部微步表方向，补偿 format 0x0D 与 0x0A 的相位映射差异
    if (motorParams[icID].driverType == DRIVER_TMC2240) {
        generalConf |= TMC4361A_REVERSE_MOTOR_DIR_MASK;  // bit 28
    }
    tmc4361A_writeRegister(icID, TMC4361A_GENERAL_CONF, generalConf);

    // Configure SPI_OUT_CONF - 根据驱动芯片类型选择 SPI 输出格式
    uint32_t spiOutConf;
    if (motorParams[icID].driverType == DRIVER_TMC2240) {
        // TMC2240: SPI_OUTPUT_FORMAT=0x0D (TMC2130/TMC2240 SPI 电流传输模式, 40-bit)
        // 等价于 TMC2660 的 SDOFF 模式: TMC4361A 直接控制线圈电流
        // SPI timing: block=4, high=4, low=4
        // COVER_DATA_LENGTH=40 (bits[19:13]), 显式指定 40-bit Cover 长度
        spiOutConf = 0x4445000D;
    } else {
        // TMC2660: SPI_OUTPUT_FORMAT=0x0A (TMC26x 20-bit SPI 模式)
        // 0x4440108A: SCALE_VAL_TRANSFER_EN=1, COVER_DATA_LENGTH for 20-bit
        spiOutConf = 0x4440108A;
    }
    tmc4361A_writeRegister(icID, TMC4361A_SPI_OUT_CONF, spiOutConf);

    // CLK_FREQ 已在检测前设置

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
    // ASTART / DFINAL: 起始加速度和终止减速度
    uint32_t astart = config->astartMM > 0 ? motor_accelMMToInternal(icID, config->astartMM) : 0;
    float dfinalMM = config->dfinalMM > 0 ? config->dfinalMM : config->astartMM;
    uint32_t dfinal = dfinalMM > 0 ? motor_accelMMToInternal(icID, dfinalMM) : 0;
    motorParams[icID].astart = astart;
    motorParams[icID].dfinal = dfinal;

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

    // Set VSTART, VSTOP, ASTART, DFINAL
    tmc4361A_writeRegister(icID, TMC4361A_VSTART, 0);
    tmc4361A_writeRegister(icID, TMC4361A_VSTOP, 0);
    tmc4361A_writeRegister(icID, TMC4361A_ASTART, motorParams[icID].astart);
    tmc4361A_writeRegister(icID, TMC4361A_DFINAL, motorParams[icID].dfinal);

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
    // 关键配置: 电流缩放
    // TMC4361A 内部使用 SCALE_VALUES 计算线圈电流幅值，适用于所有 SPI 输出格式：
    //   - TMC2660 (format 0x0A): 20-bit SPI 数据包含缩放后的电流值
    //   - TMC2240 (format 0x0D): 40-bit SPI 写入 DIRECT_MODE 寄存器，同样需要缩放
    // 不配置 SCALE_VALUES 会导致发送零电流 → 电机不动
    // ========================================================================

    // SCALE_VALUES + CURRENT_CONF (与旧 API tmc4361A_cScaleInit 一致)
    uint32_t scaleValues = (128 << TMC4361A_HOLD_SCALE_VAL_SHIFT) |   // hold = 50%
                           (255 << TMC4361A_DRV2_SCALE_VAL_SHIFT) |   // drv2 = 100%
                           (255 << TMC4361A_DRV1_SCALE_VAL_SHIFT) |   // drv1 = 100%
                           (255 << TMC4361A_BOOST_SCALE_VAL_SHIFT);   // boost = 100%
    tmc4361A_writeRegister(icID, TMC4361A_SCALE_VALUES, scaleValues);

    uint32_t currentConf = tmc4361A_readRegister(icID, TMC4361A_CURRENT_CONF);
    currentConf |= TMC4361A_DRIVE_CURRENT_SCALE_EN_MASK;       // bit 1
    currentConf |= TMC4361A_HOLD_CURRENT_SCALE_EN_MASK;        // bit 0
    currentConf |= TMC4361A_BOOST_CURRENT_ON_ACC_EN_MASK;      // bit 2
    currentConf |= TMC4361A_BOOST_CURRENT_AFTER_START_EN_MASK;  // bit 4
    tmc4361A_writeRegister(icID, TMC4361A_CURRENT_CONF, currentConf);

    return true;
}

bool motor_initDriver(uint8_t icID, const MotorConfig *config)
{
    if (icID >= MOTOR_IC_COUNT || config == NULL)
        return false;

    // 缓存驱动类型和 rSense
    motorParams[icID].driverType = config->driverType;
    motorParams[icID].rSense = config->rSense;

    if (config->driverType == DRIVER_TMC2240) {
        return motor_initDriver_TMC2240(icID, config);
    } else {
        return motor_initDriver_TMC2660(icID, config);
    }
}

// ========================================================================
// TMC2660 驱动初始化
// ========================================================================
static bool motor_initDriver_TMC2660(uint8_t icID, const MotorConfig *config)
{
    // 缓存 toff 用于 enableDriver 恢复
    motorParams[icID].toff = config->toff;

    // Calculate current scale
    uint8_t cs = calculateCurrentScale(config->runCurrentMA, config->rSense);

    // TMC2660 初始化 - 与旧 API 顺序一致
    // 旧 API 顺序: CHOPCONF -> SMARTEN -> SGCSCONF -> DRVCONF
    // 注意: SPI 模式 (SDOFF=1) 下不发送 DRVCTRL

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
    // VSENSE=0: High sense resistor voltage range (V_fs=0.310V)
    // RDSEL=2: StallGuard2 value and CoolStep current level in response
    uint32_t drvconf = TMC2660_SET_RDSEL(2) | TMC2660_SET_VSENSE(0) | TMC2660_SET_SDOFF(1) | 0x01;
    tmc2660_writeRegister(icID, TMC2660_DRVCONF, drvconf);

    // 注意: 在 SPI 模式 (SDOFF=1) 下，不发送 DRVCTRL
    // 微步由 TMC4361A 的 STEP_CONF 寄存器控制

    return true;
}

// ========================================================================
// TMC2240 驱动初始化
// ========================================================================
static bool motor_initDriver_TMC2240(uint8_t icID, const MotorConfig *config)
{
    // 缓存 TMC2240 专用参数（运行时 setRunCurrent / enableDriver 需要）
    motorParams[icID].currentRange = config->currentRange;
    motorParams[icID].toff = config->toff;

    // 注意: SPI_OUTPUT_FORMAT=0x0D 保持活跃，不能禁用 (format=0 会关闭 SPI 输出硬件)
    // Cover 写入与自动 SPI 输出由 TMC4361A 硬件串行化，写入应可靠
    // Cover 读取可能受自动 SPI 干扰（读回值不可靠），但不影响配置写入

    // 1. DRV_CONF - 设置 CURRENT_RANGE 和 SLOPE_CONTROL
    // CURRENT_RANGE: 0=1A, 1=2A, 2=3A, 3=3A
    // SLOPE_CONTROL: 1 = 200V/μs (默认)
    uint32_t drvConf = ((uint32_t)(config->currentRange & 0x03) << 0) |
                       (1 << 4);  // SLOPE_CONTROL=1
    tmc2240_writeRegister(icID, TMC2240_DRV_CONF, drvConf);

    // 2. GLOBAL_SCALER (0=256 即全量程, 32-255 缩放)
    tmc2240_writeRegister(icID, TMC2240_GLOBAL_SCALER,
                          config->globalScaler == 0 ? 0 : config->globalScaler);

    // 3. 计算电流 — 基于 CURRENT_RANGE 的 I_FS
    uint8_t irun = calculateCurrentScale_TMC2240(config->runCurrentMA,
                                                  config->currentRange,
                                                  config->globalScaler);
    uint8_t ihold = (uint8_t)(irun * config->holdCurrentRatio);
    if (ihold > 31) ihold = 31;

    // 4. IHOLD_IRUN - 电流配置
    uint32_t iholdIrun = ((uint32_t)ihold << TMC2240_IHOLD_SHIFT) |
                         ((uint32_t)irun << TMC2240_IRUN_SHIFT) |
                         ((uint32_t)(config->iholdDelay & 0x0F) << TMC2240_IHOLDDELAY_SHIFT);
    tmc2240_writeRegister(icID, TMC2240_IHOLD_IRUN, iholdIrun);

    // 5. TPOWERDOWN - 保持电流延时 (默认 10)
    tmc2240_writeRegister(icID, TMC2240_TPOWERDOWN, 10);

    // 6. GCONF - 全局配置
    uint32_t gconf = 0x00000000;
    // direct_mode (bit 16): TMC4361A 通过 SPI 直接控制线圈电流 (DIRECT_MODE 寄存器)
    // 必须启用，否则 TMC2240 等待 Step/Dir 信号而不响应 SPI 电流指令
    gconf |= TMC2240_DIRECT_MODE_MASK;  // bit 16: direct coil current control via SPI
    // 注意: SHAFT (bit 4) 在 direct_mode 下无效，方向由 TMC4361A REVERSE_MOTOR_DIR 控制
    if (config->enableStealthChop) {
        gconf |= TMC2240_EN_PWM_MODE_MASK;  // bit 2: StealthChop 使能
    }
    tmc2240_writeRegister(icID, TMC2240_GCONF, gconf);

    // 7. CHOPCONF - Chopper 配置 (含 MRES 微步设置)
    // MRES 编码: 0=256, 1=128, 2=64, ..., 8=全步 (与 TMC4361A STEP_CONF 一致)
    uint8_t mresVal = config->microstepRes;  // 由 Axis::begin() 传入，通常为 0 (256微步)

    uint32_t chopconf = ((uint32_t)(config->toff & 0x0F) << TMC2240_TOFF_SHIFT) |
                        ((uint32_t)(config->hstrt & 0x07) << TMC2240_HSTRT_TFD210_SHIFT) |
                        ((uint32_t)((config->hend + 3) & 0x0F) << TMC2240_HEND_OFFSET_SHIFT) |
                        ((uint32_t)(config->tbl & 0x03) << TMC2240_TBL_SHIFT) |
                        ((uint32_t)(mresVal & 0x0F) << TMC2240_MRES_SHIFT) |
                        (config->interpolation ? (1 << TMC2240_INTPOL_SHIFT) : 0);
    tmc2240_writeRegister(icID, TMC2240_CHOPCONF, chopconf);

    // 8. PWMCONF - StealthChop PWM 配置
    if (config->enableStealthChop) {
        // 默认值: pwm_autoscale=1, pwm_autograd=1
        tmc2240_writeRegister(icID, TMC2240_PWMCONF, 0xC44C001E);
    }

    // 清 GSTAT reset 标志
    tmc2240_writeRegister(icID, TMC2240_GSTAT, 0x07);
    // ---- END DEBUG ----

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

    // Invert stop direction: 交换左右限位开关的逻辑含义
    // 与 master 分支旧 API (tmc4361A_enableLimitSwitch) 一致:
    //   if (flipped != 0) setBits(INVERT_STOP_DIRECTION_MASK)
    if (config->leftFlipped || config->rightFlipped) {
        refConf |= TMC4361A_INVERT_STOP_DIRECTION_MASK;  // bit 4
    } else {
        refConf &= ~TMC4361A_INVERT_STOP_DIRECTION_MASK;
    }

    // 注意：不设 SOFT_STOP_EN (bit 5)
    // SOFT_STOP_EN=1 会使限位触发时芯片进入内部软停车状态机，
    // 锁定 RAMPMODE/VMAX/XTARGET 写入，导致 homing 停车失败。
    // 与 master 分支旧 API (tmc4361A_enableLimitSwitch) 保持一致：硬停车。

    tmc4361A_writeRegister(icID, TMC4361A_REFERENCE_CONF, refConf);
}

void motor_setHardwareStopEnable(uint8_t icID, uint8_t side, bool enable)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    uint32_t refConf = tmc4361A_readRegister(icID, TMC4361A_REFERENCE_CONF);

    uint32_t mask = 0;
    if (side & 0x01)  // LEFT_SW
        mask |= TMC4361A_STOP_LEFT_EN_MASK;
    if (side & 0x02)  // RGHT_SW
        mask |= TMC4361A_STOP_RIGHT_EN_MASK;

    if (enable) {
        refConf |= mask;
    } else {
        refConf &= ~mask;
    }

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

        // 2. 恢复 USE_ASTART_AND_VSTART 设置（根据 astart 配置决定）
        uint32_t generalConf = tmc4361A_readRegister(icID, TMC4361A_GENERAL_CONF);
        if (motorParams[icID].astart > 0) {
            generalConf |= TMC4361A_USE_ASTART_AND_VSTART_MASK;
        } else {
            generalConf &= ~TMC4361A_USE_ASTART_AND_VSTART_MASK;
        }
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

    // 无条件写回 VMAX（与旧 API tmc4361A_moveTo 一致）
    // 旧 API 每次 moveTo 都写 VMAX，确保即使被外部（如 motor_stop）
    // 清零后也能恢复正确速度
    tmc4361A_writeRegister(icID, TMC4361A_VMAX, motorParams[icID].vmax);

    // ========================================================================
    // 写入目标位置 (与旧 API tmc4361A_moveTo 一致)
    // ========================================================================

    // 虚拟限位恢复（TMC4361A Programming Guide §10.4）：
    // "前提：停止开关不再激活 或 已禁用停止开关。然后清除事件。"
    //
    // 策略：禁用被激活的 virtual_limit_en → 清除事件 → 写 XTARGET。
    // 不在此处恢复使能位：必须等电机离开边界后才能恢复（由 Axis 层管理）。
    // 如果立即恢复，XACTUAL 仍在边界上 → VSTOP 立即重新触发 → 电机无法移动。
    uint32_t status = tmc4361A_readRegister(icID, TMC4361A_STATUS);
    bool vstopL = status & TMC4361A_VSTOPL_ACTIVE_F_MASK;
    bool vstopR = status & TMC4361A_VSTOPR_ACTIVE_F_MASK;

    if (vstopL || vstopR) {
        uint32_t refConf = tmc4361A_readRegister(icID, TMC4361A_REFERENCE_CONF);

        // 禁用被激活的虚拟限位（满足恢复前提条件）
        if (vstopL)
            refConf &= ~TMC4361A_VIRTUAL_LEFT_LIMIT_EN_MASK;
        if (vstopR)
            refConf &= ~TMC4361A_VIRTUAL_RIGHT_LIMIT_EN_MASK;

        tmc4361A_writeRegister(icID, TMC4361A_REFERENCE_CONF, refConf);
        tmc4361A_readRegister(icID, TMC4361A_EVENTS);  // 清除事件（恢复动作）

        tmc4361A_writeRegister(icID, TMC4361A_XTARGET, position);
        tmc4361A_readRegister(icID, TMC4361A_EVENTS);  // 清除可能的新事件
    } else {
        // 正常路径（无虚拟限位激活）
        tmc4361A_readRegister(icID, TMC4361A_EVENTS);
        tmc4361A_writeRegister(icID, TMC4361A_XTARGET, position);
        tmc4361A_readRegister(icID, TMC4361A_EVENTS);
    }

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
    DEBUG_PRINT("motor_resetRampMode: icID=");
    DEBUG_PRINT(icID);
    DEBUG_PRINT(" RAMPMODE: 0x");
    DEBUG_PRINTF(rampModeBefore, HEX);
    DEBUG_PRINT(" -> 0x");
    DEBUG_PRINTLNF(rampModeAfter, HEX);
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

void motor_setMicrosteps(uint8_t icID, uint16_t microsteps)
{
    if (icID >= MOTOR_IC_COUNT || !motorParams[icID].initialized)
        return;

    // 更新缓存
    motorParams[icID].microsteps = microsteps;
    motorParams[icID].stepsPerMM = (float)(motorParams[icID].fullStepsPerRev * microsteps) / motorParams[icID].screwPitchMM;

    // 计算 MSTEP_PER_FS 值: 256->0, 128->1, ..., 1->8
    uint16_t mstep = microsteps;
    uint8_t mstepVal = 0;
    if (mstep > 0 && (mstep & (mstep - 1)) == 0 && mstep <= 256) {
        uint8_t bitsSet = 0;
        while (mstep > 0) {
            bitsSet++;
            mstep >>= 1;
        }
        mstepVal = 9 - bitsSet;
    }

    // 组合 STEP_CONF: MSTEP_PER_FS (bit 0-3) + FS_PER_REV (bit 4-15)
    uint32_t stepConf = (mstepVal & TMC4361A_MSTEP_PER_FS_MASK) |
                        ((uint32_t)motorParams[icID].fullStepsPerRev << TMC4361A_FS_PER_REV_SHIFT);
    tmc4361A_writeRegister(icID, TMC4361A_STEP_CONF, stepConf);

    // TMC2240: 同步更新 CHOPCONF.MRES（TMC2240 的 MRES 必须与 TMC4361A 的 STEP_CONF 一致）
    // 注意: 不能用 tmc2240_fieldWrite (read-modify-write)，因为 SPI_OUTPUT_FORMAT=0x0D
    // 自动 SPI 输出干扰 Cover 读取，读回值不可靠会损坏 CHOPCONF（TOFF=0→驱动关闭）。
    // 改用 shadow register 获取上次写入的 CHOPCONF 值。
    if (motorParams[icID].driverType == DRIVER_TMC2240) {
        uint32_t chopconf = (uint32_t)tmc2240_shadowRegister[icID][TMC2240_CHOPCONF];
        chopconf = (chopconf & ~((uint32_t)0x0F << TMC2240_MRES_SHIFT)) |
                   ((uint32_t)(mstepVal & 0x0F) << TMC2240_MRES_SHIFT);
        tmc2240_writeRegister(icID, TMC2240_CHOPCONF, chopconf);
    }
}

void motor_setRunCurrent(uint8_t icID, float currentMA)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    if (motorParams[icID].driverType == DRIVER_TMC2240) {
        uint8_t irun = calculateCurrentScale_TMC2240(currentMA,
                                                      motorParams[icID].currentRange, 0);
        tmc2240_setRunCurrent(icID, irun);
    } else {
        float rSense = motorParams[icID].rSense > 0 ? motorParams[icID].rSense : 0.22f;
        uint8_t cs = calculateCurrentScale(currentMA, rSense);
        tmc2660_setRunCurrent(icID, cs);
    }
}

void motor_enableDriver(uint8_t icID, bool enable)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    if (motorParams[icID].driverType == DRIVER_TMC2240) {
        if (enable) {
            // 使用缓存的 TOFF 恢复驱动（而非硬编码默认值）
            uint32_t chopconf = tmc2240_readRegister(icID, TMC2240_CHOPCONF);
            uint8_t currentToff = (chopconf & TMC2240_TOFF_MASK) >> TMC2240_TOFF_SHIFT;
            if (currentToff == 0) {
                uint8_t toff = motorParams[icID].toff > 0 ? motorParams[icID].toff : 3;
                tmc2240_fieldWrite(icID, TMC2240_TOFF_FIELD, toff);
            }
        } else {
            tmc2240_fieldWrite(icID, TMC2240_TOFF_FIELD, 0);
        }
    } else {
        tmc2660_enableDriver(icID, enable);
    }
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

void motor_initABNEncoder(uint8_t icID, uint32_t transitions_per_rev,
                           uint8_t filter_wait_time, uint8_t filter_exponent,
                           uint16_t filter_vmean, bool invert_dir)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Set encoder resolution
    tmc4361A_writeRegister(icID, TMC4361A_ENC_IN_RES, transitions_per_rev);

    // Set encoder velocity mean filter:
    // ENC_VMEAN_FILTER = wait_time | (filter_exp << 8) | (vmean_int << 16)
    uint32_t filterVal = (uint32_t)filter_wait_time
                       | ((uint32_t)filter_exponent << 8)
                       | ((uint32_t)filter_vmean << 16);
    tmc4361A_writeRegister(icID, TMC4361A_ENC_VMEAN_FILTER, filterVal);

    // Set or clear INVERT_ENC_DIR bit (bit 29 of ENC_IN_CONF)
    uint32_t enc_conf = tmc4361A_readRegister(icID, TMC4361A_ENC_IN_CONF);
    if (invert_dir) {
        enc_conf |= TMC4361A_INVERT_ENC_DIR_MASK;
    } else {
        enc_conf &= ~TMC4361A_INVERT_ENC_DIR_MASK;
    }
    tmc4361A_writeRegister(icID, TMC4361A_ENC_IN_CONF, enc_conf);
}

void motor_initPID(uint8_t icID, uint32_t target_tolerance, uint32_t pid_tolerance,
                    uint32_t pid_p, uint32_t pid_i, uint32_t pid_d,
                    uint32_t pid_dclip, uint32_t pid_iclip, uint8_t pid_d_clkdiv)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Closed-loop target tolerance
    tmc4361A_writeRegister(icID, TMC4361A_CL_TR_TOLERANCE, target_tolerance);
    // PID tolerance
    tmc4361A_writeRegister(icID, TMC4361A_PID_TOLERANCE, pid_tolerance);
    // PID gains (24-bit each)
    tmc4361A_writeRegister(icID, TMC4361A_PID_P, pid_p & 0xFFFFFF);
    tmc4361A_writeRegister(icID, TMC4361A_PID_I, pid_i & 0xFFFFFF);
    tmc4361A_writeRegister(icID, TMC4361A_PID_D, pid_d & 0xFFFFFF);
    // PID velocity clip
    tmc4361A_writeRegister(icID, TMC4361A_PID_DV_CLIP, pid_dclip);
    // PID integral clip + derivative clock divider
    // PID_I_CLIP_WR (0x5D) = iclip | (d_clkdiv << 16)
    tmc4361A_writeRegister(icID, TMC4361A_PID_I_CLIP,
                           pid_iclip | ((uint32_t)pid_d_clkdiv << 16));
}

void motor_enablePID(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Set REGULATION_MODUS bits (22-23) of ENC_IN_CONF to 0b10 (PID via BPG0)
    uint32_t enc_conf = tmc4361A_readRegister(icID, TMC4361A_ENC_IN_CONF);
    enc_conf &= ~TMC4361A_REGULATION_MODUS_MASK;
    enc_conf |= (0x02 << TMC4361A_REGULATION_MODUS_SHIFT);
    tmc4361A_writeRegister(icID, TMC4361A_ENC_IN_CONF, enc_conf);
}

void motor_disablePID(uint8_t icID)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    // Clear REGULATION_MODUS bits (22-23) of ENC_IN_CONF to disable PID
    uint32_t enc_conf = tmc4361A_readRegister(icID, TMC4361A_ENC_IN_CONF);
    enc_conf &= ~TMC4361A_REGULATION_MODUS_MASK;
    tmc4361A_writeRegister(icID, TMC4361A_ENC_IN_CONF, enc_conf);
}

void motor_configStallGuard(uint8_t icID, int8_t threshold, bool filterEnable, bool stopOnStall)
{
    if (icID >= MOTOR_IC_COUNT)
        return;

    if (motorParams[icID].driverType == DRIVER_TMC2240) {
        // TMC2240: StallGuard4
        // SGT 在 COOLCONF (0x6D) 的 bits [22:16]
        tmc2240_fieldWrite(icID, TMC2240_SGT_FIELD, (uint32_t)(threshold & 0x7F));
        // SG4_THRS 在 SG4_THRS (0x74) 的 bits [7:0]
        tmc2240_fieldWrite(icID, TMC2240_SG4_FILT_EN_FIELD, filterEnable ? 1 : 0);
    } else {
        // TMC2660: StallGuard2
        tmc2660_setStallGuardThreshold(icID, threshold);
        tmc2660_setStallGuardFilter(icID, filterEnable);
    }

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
