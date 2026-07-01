#include "mcp23s17.h"
#include <SPI.h>

namespace {

inline void spi_begin_txn() {
    SPI.beginTransaction(SPISettings(MCP23S17::SPI_SPEED_HZ, MSBFIRST, SPI_MODE0));
    Pins::hc154_select((uint8_t)Pins::HC154_MCP23S17_1);
}

inline void spi_end_txn() {
    // return to the EXPAND_NSCS1 channel (no SPI device attached) to ensure the MCP23S17_1 chip-select is released
    Pins::hc154_select((uint8_t)Pins::HC154_EXPAND_NSCS1);
    SPI.endTransaction();
}

}  // namespace

void mcp23s17_init()
{
    // IOCON: BANK=0 (sequential addressing), MIRROR=0, SEQOP=0 (address auto-increment),
    // DISSLW=0, HAEN=0 (single chip, hardware addressing not enabled), ODR=0, INTPOL=0
    mcp23s17_writeReg(MCP23S17::REG_IOCON, 0x00);

    // all 16 lines as inputs (TMC4361A INT / TARGET_REACHED connected)
    mcp23s17_writeReg(MCP23S17::REG_IODIRA, 0xFF);
    mcp23s17_writeReg(MCP23S17::REG_IODIRB, 0xFF);

    // do not invert polarity; enable the 100kohm weak pull-ups (floating signals default HIGH, for fault tolerance)
    mcp23s17_writeReg(MCP23S17::REG_IPOLA, 0x00);
    mcp23s17_writeReg(MCP23S17::REG_IPOLB, 0x00);
    mcp23s17_writeReg(MCP23S17::REG_GPPUA, 0xFF);
    mcp23s17_writeReg(MCP23S17::REG_GPPUB, 0xFF);

    // disable hardware interrupts (INTA/INTB not connected to the Teensy, polling mode)
    mcp23s17_writeReg(MCP23S17::REG_GPINTENA, 0x00);
    mcp23s17_writeReg(MCP23S17::REG_GPINTENB, 0x00);
}

uint8_t mcp23s17_readReg(uint8_t reg)
{
    spi_begin_txn();
    SPI.transfer(MCP23S17::OPCODE_READ);
    SPI.transfer(reg);
    uint8_t value = SPI.transfer(0x00);
    spi_end_txn();
    return value;
}

void mcp23s17_writeReg(uint8_t reg, uint8_t value)
{
    spi_begin_txn();
    SPI.transfer(MCP23S17::OPCODE_WRITE);
    SPI.transfer(reg);
    SPI.transfer(value);
    spi_end_txn();
}

uint8_t mcp23s17_readPortA()
{
    return mcp23s17_readReg(MCP23S17::REG_GPIOA);
}

uint8_t mcp23s17_readPortB()
{
    return mcp23s17_readReg(MCP23S17::REG_GPIOB);
}

uint16_t mcp23s17_readGPIO()
{
    // use the SEQOP=0 address auto-increment: read GPIOA/GPIOB back-to-back in one transaction
    spi_begin_txn();
    SPI.transfer(MCP23S17::OPCODE_READ);
    SPI.transfer(MCP23S17::REG_GPIOA);
    uint8_t a = SPI.transfer(0x00);
    uint8_t b = SPI.transfer(0x00);
    spi_end_txn();
    return ((uint16_t)b << 8) | a;
}
