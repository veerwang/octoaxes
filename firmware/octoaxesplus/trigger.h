#ifndef TRIGGER_H
#define TRIGGER_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// 触发模式常量
// =============================================================================

const uint8_t TRIGGER_MODE_NORMAL = 0;  // 固定 50μs 脉冲
const uint8_t TRIGGER_MODE_LEVEL  = 1;  // 电平触发（strobe_delay + on_time）

// 触发脉冲参数
const int TRIGGER_PULSE_LENGTH_us = 50;
const int NUM_TRIGGER_CHANNELS = 8;

// 频闪定时器间隔
const int STROBE_TIMER_INTERVAL_us = 100;

// 相机触发引脚映射
const int camera_trigger_pins[NUM_TRIGGER_CHANNELS] = {
    Pins::CAMERA_TRIGGER_1,  // pin 9  CAM_TRI_OUT1
    Pins::CAMERA_TRIGGER_2,  // pin 8  CAM_TRI_OUT2
    Pins::CAMERA_TRIGGER_3,  // pin 23
    Pins::CAMERA_TRIGGER_4,  // pin 22
    Pins::CAMERA_TRIGGER_5,  // pin 15
    Pins::CAMERA_TRIGGER_6,  // pin 41
    Pins::CAMERA_TRIGGER_7,  // pin 40
    Pins::CAMERA_TRIGGER_8   // pin 39
};

// =============================================================================
// 状态变量（extern 声明，定义在 trigger.cpp）
// =============================================================================

extern bool          trigger_output_level[NUM_TRIGGER_CHANNELS];
extern bool          control_strobe[NUM_TRIGGER_CHANNELS];
extern bool          strobe_output_level[NUM_TRIGGER_CHANNELS];
extern bool          strobe_on[NUM_TRIGGER_CHANNELS];
extern unsigned long strobe_delay_us[NUM_TRIGGER_CHANNELS];
extern uint32_t      illumination_on_time_us[NUM_TRIGGER_CHANNELS];
extern unsigned long timestamp_trigger_rising_edge[NUM_TRIGGER_CHANNELS];
extern volatile uint8_t trigger_mode;

// Joystick 状态
extern bool          joystick_button_pressed;
extern unsigned long joystick_button_pressed_timestamp;

// =============================================================================
// API
// =============================================================================

// 初始化触发系统：引脚、状态数组、定时器
void trigger_init();

// 主循环调用：管理触发脉冲恢复（HIGH 电平）
void trigger_update();

// 定时器中断回调：管理频闪照明时序
void ISR_strobeTimer();

#endif // TRIGGER_H
