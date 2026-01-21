/*
 * TMC4361A_Compat.h
 *
 * Compatibility layer for transitioning from old pointer-based API
 * to new icID-based API.
 *
 * This header provides wrapper functions that allow existing code
 * using TMC4361ATypeDef* to work with the new API.
 *
 * Created: 2026-01-21
 */

#ifndef TMC4361A_COMPAT_H_
#define TMC4361A_COMPAT_H_

#include "TMC4361A.h"
#include "../../hal/TMC_SPI.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Legacy Type Definitions (for compatibility with existing code)
// ============================================================================

// Configuration state enumeration
typedef enum {
    CONFIG_READY,
    CONFIG_RESET,
    CONFIG_RESTORE
} ConfigState;

// Configuration type (simplified from old API)
typedef struct {
    uint8_t  channel;        // SPI channel (maps to icID)
    int32_t  shadowRegister[TMC4361A_REGISTER_COUNT];
    ConfigState state;
} ConfigurationTypeDef;

// Register constant type (for compatibility)
typedef struct {
    uint8_t  address;
    uint32_t value;
} TMCRegisterConstant;

// Legacy TMC4361A instance type
typedef struct {
    ConfigurationTypeDef *config;
    int32_t  velocity;
    int32_t  oldX;
    uint32_t oldTick;
    int32_t  registerResetState[TMC4361A_REGISTER_COUNT];
    uint8_t  registerAccess[TMC4361A_REGISTER_COUNT];
    int32_t  xmin;
    int32_t  xmax;
    int32_t  xhome;
    int32_t  rampParam[9];      // N_RPARAM
    int32_t  cscaleParam[5];    // N_CPARAM
    float    threadPitch;
    uint16_t stepsPerRev;
    uint16_t microsteps;
    bool     velocity_mode;
    uint8_t  dac_idx;
    uint32_t dac_fullscale_msteps;
    uint8_t  status;
    ConfigurationTypeDef *cover;
    int      target_tolerance;
    int      pid_tolerance;

    // NEW: IC identifier for new API
    uint16_t icID;
} TMC4361ATypeDef;

// Callback type
typedef void (*tmc4361A_callback)(TMC4361ATypeDef*, ConfigState);

// ============================================================================
// Helper Macros (from old API)
// ============================================================================

#define TMC_ADDRESS(x)          ((x) & TMC4361A_ADDRESS_MASK)
#define TMC_IS_READABLE(x)      ((x) & TMC4361A_ACCESS_READ)
#define TMC_ACCESS_DIRTY        TMC4361A_ACCESS_DIRTY

#ifndef BYTE
#define BYTE(value, n)          (((value) >> ((n) * 8)) & 0xFF)
#endif

#ifndef N_A
#define N_A 0x00
#endif

// ============================================================================
// Compatibility Wrapper Functions
// ============================================================================

/**
 * @brief Initialize TMC4361A instance (compatibility wrapper)
 *
 * Sets up the TMC4361ATypeDef structure with the given icID.
 * The icID will be used for all subsequent operations.
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @param icID      IC identifier (0 to TMC4361A_IC_CACHE_COUNT-1)
 * @param config    Pointer to ConfigurationTypeDef
 */
static inline void tmc4361A_initCompat(TMC4361ATypeDef *tmc4361A, uint16_t icID,
                                        ConfigurationTypeDef *config)
{
    tmc4361A->icID = icID;
    tmc4361A->config = config;
    if (config) {
        config->channel = icID;
    }
}

/**
 * @brief Write integer to register (compatibility wrapper)
 *
 * This function wraps the new tmc4361A_writeRegister API for
 * compatibility with code using the old TMC4361ATypeDef* interface.
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @param address   Register address
 * @param value     Value to write
 */
static inline void tmc4361A_writeInt(TMC4361ATypeDef *tmc4361A, uint8_t address, int32_t value)
{
    tmc4361A_writeRegister(tmc4361A->icID, address, value);

    // Also update legacy shadow register for backward compatibility
    if (tmc4361A->config) {
        tmc4361A->config->shadowRegister[TMC_ADDRESS(address)] = value;
    }
}

/**
 * @brief Read integer from register (compatibility wrapper)
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @param address   Register address
 * @return Register value
 */
static inline int32_t tmc4361A_readInt(TMC4361ATypeDef *tmc4361A, uint8_t address)
{
    return tmc4361A_readRegister(tmc4361A->icID, address);
}

/**
 * @brief Write datagram to register (compatibility wrapper)
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @param address   Register address
 * @param x1-x4     Data bytes (MSB first)
 */
static inline void tmc4361A_writeDatagram(TMC4361ATypeDef *tmc4361A, uint8_t address,
                                           uint8_t x1, uint8_t x2, uint8_t x3, uint8_t x4)
{
    int32_t value = ((uint32_t)x1 << 24) | ((uint32_t)x2 << 16) |
                    ((uint32_t)x3 << 8)  | ((uint32_t)x4);
    tmc4361A_writeRegister(tmc4361A->icID, address, value);

    // Update legacy shadow register
    if (tmc4361A->config) {
        tmc4361A->config->shadowRegister[TMC_ADDRESS(address)] = value;
    }
}

/**
 * @brief Read/write through Cover interface (compatibility wrapper)
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @param data      Data buffer
 * @param length    Data length
 */
static inline void tmc4361A_readWriteCoverCompat(TMC4361ATypeDef *tmc4361A,
                                                   uint8_t *data, size_t length)
{
    tmc4361A_readWriteCover(tmc4361A->icID, data, length);
}

/**
 * @brief Get current position (compatibility wrapper)
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @return Current position in microsteps
 */
static inline int32_t tmc4361A_currentPosition(TMC4361ATypeDef *tmc4361A)
{
    return tmc4361A_readRegister(tmc4361A->icID, TMC4361A_XACTUAL);
}

/**
 * @brief Get target position (compatibility wrapper)
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @return Target position in microsteps
 */
static inline int32_t tmc4361A_targetPosition(TMC4361ATypeDef *tmc4361A)
{
    return tmc4361A_readRegister(tmc4361A->icID, TMC4361A_XTARGET);
}

/**
 * @brief Set target position (compatibility wrapper)
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @param position  Target position in microsteps
 */
static inline void tmc4361A_setTargetPosition(TMC4361ATypeDef *tmc4361A, int32_t position)
{
    tmc4361A_writeRegister(tmc4361A->icID, TMC4361A_XTARGET, position);
}

/**
 * @brief Get current velocity (compatibility wrapper)
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @return Current velocity
 */
static inline int32_t tmc4361A_currentVelocity(TMC4361ATypeDef *tmc4361A)
{
    return tmc4361A_readRegister(tmc4361A->icID, TMC4361A_VACTUAL);
}

/**
 * @brief Check if target position is reached (compatibility wrapper)
 *
 * @param tmc4361A  Pointer to TMC4361ATypeDef structure
 * @return true if target reached
 */
static inline bool tmc4361A_isTargetReached(TMC4361ATypeDef *tmc4361A)
{
    int32_t status = tmc4361A_readRegister(tmc4361A->icID, TMC4361A_STATUS);
    return (status & (1 << 0)) != 0;  // TARGET_REACHED bit
}

#ifdef __cplusplus
}
#endif

#endif /* TMC4361A_COMPAT_H_ */
