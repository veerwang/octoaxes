#ifndef MCP23S17_H
#define MCP23S17_H

#include <Arduino.h>
#include "config.h"

// MCP23S17_1 expansion IO (SPI1 bus, CS via 74HC154 channel HC154_MCP23S17_1 = 0)
//
// hardware address pins A0-A2 = GND (address 000). With IOCON.HAEN=0 the opcode is simply 0x40/0x41,
// and the address bits in opcode bit3:1 have no effect (ignored by the chip).
//
// pin assignment (documents/squid++（双相机）配置.md section 3):
// GPA0/2/4/6 = INTR_Y/X/F1/Z1    (from the TMC4361A INT output, input)
// GPA1/3/5/7 = TARGET_Y/X/F1/Z1  (from TMC4361A TARGET_REACHED, input)
//   GPB0/2/4/6 = INTR_R/T/F2/Z2
//   GPB1/3/5/7 = TARGET_R/T/F2/Z2
// all configured as inputs. INTA/INTB not connected to the Teensy, polling is used.

namespace MCP23S17 {

// IOCON.BANK=0 mode register addresses (power-on default)
constexpr uint8_t REG_IODIRA   = 0x00;
constexpr uint8_t REG_IODIRB   = 0x01;
constexpr uint8_t REG_IPOLA    = 0x02;
constexpr uint8_t REG_IPOLB    = 0x03;
constexpr uint8_t REG_GPINTENA = 0x04;
constexpr uint8_t REG_GPINTENB = 0x05;
constexpr uint8_t REG_DEFVALA  = 0x06;
constexpr uint8_t REG_DEFVALB  = 0x07;
constexpr uint8_t REG_INTCONA  = 0x08;
constexpr uint8_t REG_INTCONB  = 0x09;
constexpr uint8_t REG_IOCON    = 0x0A;
constexpr uint8_t REG_GPPUA    = 0x0C;
constexpr uint8_t REG_GPPUB    = 0x0D;
constexpr uint8_t REG_INTFA    = 0x0E;
constexpr uint8_t REG_INTFB    = 0x0F;
constexpr uint8_t REG_INTCAPA  = 0x10;
constexpr uint8_t REG_INTCAPB  = 0x11;
constexpr uint8_t REG_GPIOA    = 0x12;
constexpr uint8_t REG_GPIOB    = 0x13;
constexpr uint8_t REG_OLATA    = 0x14;
constexpr uint8_t REG_OLATB    = 0x15;

// SPI control bytes (hardware address 000, HAEN disabled)
constexpr uint8_t OPCODE_WRITE = 0x40;
constexpr uint8_t OPCODE_READ  = 0x41;

// SPI timing (MCP23S17 up to 10 MHz; use 5 MHz for margin)
constexpr uint32_t SPI_SPEED_HZ = 5000000;

// GPA / GPB bit masks (per the squid++ expansion IO mapping)
constexpr uint8_t BIT_INTR_Y    = 1 << 0;
constexpr uint8_t BIT_TARGET_Y  = 1 << 1;
constexpr uint8_t BIT_INTR_X    = 1 << 2;
constexpr uint8_t BIT_TARGET_X  = 1 << 3;
constexpr uint8_t BIT_INTR_F1   = 1 << 4;
constexpr uint8_t BIT_TARGET_F1 = 1 << 5;
constexpr uint8_t BIT_INTR_Z1   = 1 << 6;
constexpr uint8_t BIT_TARGET_Z1 = 1 << 7;

constexpr uint8_t BIT_INTR_R    = 1 << 0;
constexpr uint8_t BIT_TARGET_R  = 1 << 1;
constexpr uint8_t BIT_INTR_T    = 1 << 2;
constexpr uint8_t BIT_TARGET_T  = 1 << 3;
constexpr uint8_t BIT_INTR_F2   = 1 << 4;
constexpr uint8_t BIT_TARGET_F2 = 1 << 5;
constexpr uint8_t BIT_INTR_Z2   = 1 << 6;
constexpr uint8_t BIT_TARGET_Z2 = 1 << 7;

}  // namespace MCP23S17

// ============================================================================
// Public API
// ============================================================================

// Initialize MCP23S17_1: reset IOCON, configure all 16 lines as pull-up inputs, disable hardware interrupts.
// precondition: SPI.begin() and Pins::hc154_init() have already been called.
void mcp23s17_init();

// generic register read/write
uint8_t mcp23s17_readReg(uint8_t reg);
void    mcp23s17_writeReg(uint8_t reg, uint8_t value);

// fast port read (for polling)
uint8_t  mcp23s17_readPortA();
uint8_t  mcp23s17_readPortB();
uint16_t mcp23s17_readGPIO();  // low byte=GPA, high byte=GPB

#endif  // MCP23S17_H
