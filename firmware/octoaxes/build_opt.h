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

// 取消下行注释 → 临时禁用 24 字节二进制位置上报，让 SerialUSB 只输出 ASCII，
// 方便 Arduino IDE Serial Monitor / 普通 terminal 查看 DEBUG_PRINT 输出
// 不被二进制乱码干扰。注意：禁用后上位机收不到位置上报，无法连接。
// === 调试 [FOCUS] / 其他 ASCII 输出时启用，调试完成后注释回 ===
// #define DISABLE_BINARY_POS_UPDATE

#endif /* INCLUDED_BUILD_OPT_H */
