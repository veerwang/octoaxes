#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>

// 初始化手控盒（Serial5 + PacketSerial，在 setup 中调用）
void joystick_init();

// 主循环调用（PacketSerial 接收 + 30ms 周期运动更新 + 焦点轮控制）
void joystick_update();

// 打印协议帧统计计数（legacy/crc_ok/crc_fail），供 S:JOYSTICK_STATS 调用
void joystick_print_stats();

#endif // JOYSTICK_H
