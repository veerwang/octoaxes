#include "mcp23s17.h"
#include <SPI.h>

namespace {

inline void spi_begin_txn() {
    SPI.beginTransaction(SPISettings(MCP23S17::SPI_SPEED_HZ, MSBFIRST, SPI_MODE0));
    Pins::hc154_select((uint8_t)Pins::HC154_MCP23S17_1);
}

inline void spi_end_txn() {
    // 归零到未挂 SPI 设备的 EXPAND_NSCS1 通道，确保 MCP23S17_1 片选释放
    Pins::hc154_select((uint8_t)Pins::HC154_EXPAND_NSCS1);
    SPI.endTransaction();
}

}  // namespace

void mcp23s17_init()
{
    // IOCON：BANK=0（顺序寻址）、MIRROR=0、SEQOP=0（地址自增）、
    // DISSLW=0、HAEN=0（单片不启用硬件地址）、ODR=0、INTPOL=0
    mcp23s17_writeReg(MCP23S17::REG_IOCON, 0x00);

    // 16 路全部输入（TMC4361A INT / TARGET_REACHED 接入）
    mcp23s17_writeReg(MCP23S17::REG_IODIRA, 0xFF);
    mcp23s17_writeReg(MCP23S17::REG_IODIRB, 0xFF);

    // 不反转极性；使能 100kΩ 弱上拉（信号悬空时默认高电平，容错）
    mcp23s17_writeReg(MCP23S17::REG_IPOLA, 0x00);
    mcp23s17_writeReg(MCP23S17::REG_IPOLB, 0x00);
    mcp23s17_writeReg(MCP23S17::REG_GPPUA, 0xFF);
    mcp23s17_writeReg(MCP23S17::REG_GPPUB, 0xFF);

    // 关闭硬件中断（INTA/INTB 未接 Teensy，轮询模式）
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
    // 利用 SEQOP=0 的地址自增：一次事务连读 GPIOA/GPIOB
    spi_begin_txn();
    SPI.transfer(MCP23S17::OPCODE_READ);
    SPI.transfer(MCP23S17::REG_GPIOA);
    uint8_t a = SPI.transfer(0x00);
    uint8_t b = SPI.transfer(0x00);
    spi_end_txn();
    return ((uint16_t)b << 8) | a;
}
