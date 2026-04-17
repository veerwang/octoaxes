#ifndef MCP23S17_H
#define MCP23S17_H

#include <Arduino.h>
#include "config.h"

// MCP23S17_1 扩展 IO（SPI1 总线，CS 走 74HC154 通道 HC154_MCP23S17_1 = 0）
//
// 硬件地址引脚 A0-A2 = GND（地址 000）。IOCON.HAEN=0 时 opcode 直接取 0x40/0x41，
// 地址位在 opcode bit3:1 也无影响（芯片忽略）。
//
// 引脚分配（documents/squid++（双相机）配置.md §3）：
//   GPA0/2/4/6 = INTR_Y/X/F1/Z1    （来自 TMC4361A INT 输出，输入）
//   GPA1/3/5/7 = TARGET_Y/X/F1/Z1  （来自 TMC4361A TARGET_REACHED，输入）
//   GPB0/2/4/6 = INTR_R/T/F2/Z2
//   GPB1/3/5/7 = TARGET_R/T/F2/Z2
// 全部配置为输入。INTA/INTB 未接 Teensy，采用轮询方式。

namespace MCP23S17 {

// IOCON.BANK=0 模式寄存器地址（上电默认）
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

// SPI 控制字节（硬件地址 000，HAEN 关闭）
constexpr uint8_t OPCODE_WRITE = 0x40;
constexpr uint8_t OPCODE_READ  = 0x41;

// SPI 时序（MCP23S17 最高 10 MHz，取 5 MHz 留裕量）
constexpr uint32_t SPI_SPEED_HZ = 5000000;

// GPA / GPB 位掩码（按 squid++ 扩展 IO 映射）
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

// 初始化 MCP23S17_1：复位 IOCON、16 路全部配置为上拉输入、关闭硬件中断。
// 调用前提：SPI.begin() 已执行、Pins::hc154_init() 已执行。
void mcp23s17_init();

// 通用寄存器读写
uint8_t mcp23s17_readReg(uint8_t reg);
void    mcp23s17_writeReg(uint8_t reg, uint8_t value);

// 端口快速读（轮询用）
uint8_t  mcp23s17_readPortA();
uint8_t  mcp23s17_readPortB();
uint16_t mcp23s17_readGPIO();  // 低字节=GPA，高字节=GPB

#endif  // MCP23S17_H
