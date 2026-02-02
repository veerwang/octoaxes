/*
 * TMC_SPI.cpp
 *
 * Implementation of SPI Hardware Abstraction Layer for TMC ICs.
 *
 * Created: 2026-01-21
 */

#include "TMC_SPI.h"
#include <SPI.h>
#include <Arduino.h>

// ============================================================================
// Configuration Constants (from config.h Pins namespace)
// ============================================================================

// CS Pin definitions
#define PIN_CS_X     41
#define PIN_CS_Y     36
#define PIN_CS_Z     35
#define PIN_CS_W     34
#define PIN_CS_E1    19
#define PIN_CS_E3    17
#define PIN_CS_E4    16

// Clock source identifiers
#define CLOCK_STANDARD  0   // Pin 37
#define CLOCK_EXPAND    1   // Pin 28

// SPI Configuration
#define TMC_SPI_SPEED       500000      // 500 kHz
#define TMC_SPI_MODE        SPI_MODE0   // CPOL=0, CPHA=0
#define TMC_SPI_BIT_ORDER   MSBFIRST
#define TMC_CS_DELAY_US     100         // Delay after CS low

// ============================================================================
// IC Configuration Array
// ============================================================================

const TMC_IC_Config tmc_ic_configs[TMC4361A_IC_COUNT] = {
    // 注意: 顺序必须与 axisManager.addAxis() 调用顺序一致！
    // octoaxes.ino 中添加顺序: Y(0), X(1), Z(2), W(3), E1(4), E3(5), E4(6)
    // icID 0: Y axis - standard clock
    { .csPin = PIN_CS_Y,  .clockSource = CLOCK_STANDARD },
    // icID 1: X axis - standard clock
    { .csPin = PIN_CS_X,  .clockSource = CLOCK_STANDARD },
    // icID 2: Z axis - standard clock
    { .csPin = PIN_CS_Z,  .clockSource = CLOCK_STANDARD },
    // icID 3: W axis (Filter wheel 1) - standard clock
    { .csPin = PIN_CS_W,  .clockSource = CLOCK_STANDARD },
    // icID 4: E1 axis (Objectives) - expand clock
    { .csPin = PIN_CS_E1, .clockSource = CLOCK_EXPAND },
    // icID 5: E3 axis (Extended Z) - expand clock
    { .csPin = PIN_CS_E3, .clockSource = CLOCK_EXPAND },
    // icID 6: E4 axis (Filter wheel 2) - expand clock
    { .csPin = PIN_CS_E4, .clockSource = CLOCK_EXPAND },
};

// ============================================================================
// Debug Status Storage (Optional)
// ============================================================================

#ifdef TMC_SPI_DEBUG
static uint8_t  tmc_lastStatus[TMC4361A_IC_COUNT] = {0};
static uint32_t tmc_transferCount[TMC4361A_IC_COUNT] = {0};
#endif

// ============================================================================
// Initialization
// ============================================================================

void tmc_spi_init(void)
{
    // Initialize all CS pins as OUTPUT and set HIGH (inactive)
    for (uint8_t i = 0; i < TMC4361A_IC_COUNT; i++) {
        pinMode(tmc_ic_configs[i].csPin, OUTPUT);
        digitalWrite(tmc_ic_configs[i].csPin, HIGH);
    }

    // Initialize SPI bus
    SPI.begin();
}

// ============================================================================
// TMC4361A SPI Callbacks
// ============================================================================

void tmc4361A_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength)
{
    // Validate IC ID
    if (icID >= TMC4361A_IC_COUNT) {
        return;
    }

    uint8_t csPin = tmc_ic_configs[icID].csPin;

    // Begin SPI transaction
    SPI.beginTransaction(SPISettings(TMC_SPI_SPEED, TMC_SPI_BIT_ORDER, TMC_SPI_MODE));

    // Assert CS (active low)
    digitalWrite(csPin, LOW);

    // Wait for chip ready
    delayMicroseconds(TMC_CS_DELAY_US);

    // Full-duplex transfer
    for (size_t i = 0; i < dataLength; i++) {
        data[i] = SPI.transfer(data[i]);
    }

    // Deassert CS
    digitalWrite(csPin, HIGH);

    // End SPI transaction
    SPI.endTransaction();

#ifdef TMC_SPI_DEBUG
    tmc_transferCount[icID]++;
#endif
}

void tmc4361A_setStatus(uint16_t icID, uint8_t *data)
{
    // Validate IC ID
    if (icID >= TMC4361A_IC_COUNT) {
        return;
    }

#ifdef TMC_SPI_DEBUG
    // Store status byte (first byte of response)
    tmc_lastStatus[icID] = data[0];
#endif

    // Status byte interpretation (for future error handling):
    // Bit 7: RESET_FLAG - Indicates reset occurred
    // Bit 6: DRV_ERR - Driver error
    // Bit 5: UV_SF - Undervoltage
    // Bit 4-0: Various status flags

    // Currently just store for debugging, can be extended for error handling
    (void)data;  // Suppress unused parameter warning if debug disabled
}

// ============================================================================
// TMC2660 SPI Callbacks (Reserved)
// ============================================================================

void tmc2660_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength)
{
    // Reserved for direct SPI communication with TMC2660
    // Currently TMC2660 is controlled through TMC4361A Cover interface
    //
    // If direct SPI is needed in the future, implement similar to tmc4361A_readWriteSPI
    // but with TMC2660-specific timing and data format (20-bit datagrams)

    (void)icID;
    (void)data;
    (void)dataLength;
}

// ============================================================================
// Utility Functions
// ============================================================================

uint8_t tmc_getCSPin(uint16_t icID)
{
    if (icID >= TMC4361A_IC_COUNT) {
        return 0xFF;  // Invalid
    }
    return tmc_ic_configs[icID].csPin;
}

uint8_t tmc_getClockSource(uint16_t icID)
{
    if (icID >= TMC4361A_IC_COUNT) {
        return 0xFF;  // Invalid
    }
    return tmc_ic_configs[icID].clockSource;
}

bool tmc_isValidICID(uint16_t icID)
{
    return (icID < TMC4361A_IC_COUNT);
}

// ============================================================================
// Debug Functions (Optional)
// ============================================================================

#ifdef TMC_SPI_DEBUG
uint8_t tmc_getLastStatus(uint16_t icID)
{
    if (icID >= TMC4361A_IC_COUNT) {
        return 0xFF;
    }
    return tmc_lastStatus[icID];
}

uint32_t tmc_getTransferCount(uint16_t icID)
{
    if (icID >= TMC4361A_IC_COUNT) {
        return 0;
    }
    return tmc_transferCount[icID];
}
#endif
