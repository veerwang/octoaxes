#ifndef INCLUDED_BUILD_OPT_H
#define INCLUDED_BUILD_OPT_H

#define ENABLE_LED_INDICATOR
#define ENABLE_DEBUG

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
