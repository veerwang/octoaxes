#ifndef INCLUDED_BUILD_OPT_H
#define INCLUDED_BUILD_OPT_H

#define ENABLE_LED_INDICATOR

// ENABLE_DEBUG 仅在调试构建中启用（platformio.ini 的 debug env 传入 -D DEBUG）
// 生产构建（teensy41）不定义 DEBUG，串口只输出二进制协议包，不输出 ASCII 文本
#ifdef DEBUG
  #define ENABLE_DEBUG
#endif

#ifdef ENABLE_DEBUG
  #define DEBUG_PRINT(x) SerialUSB.print(x)
  #define DEBUG_PRINTLN(x) SerialUSB.println(x)
  #define DEBUG_PRINTF(x, y) SerialUSB.print(x, y)
  #define DEBUG_PRINTLNF(x, y) SerialUSB.println(x, y)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x, y)
  #define DEBUG_PRINTLNF(x, y)
#endif

#endif /* INCLUDED_BUILD_OPT_H */
