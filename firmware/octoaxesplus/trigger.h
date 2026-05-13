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

// 外部触发 IN/OUT（squid++ 双相机：与外部设备双向同步，pin 1-4）
// 上位机协议接入待办（CAM_TRI_READY 握手 + handler 命令字 TBD）
const int NUM_EXT_TRIGGERS = 2;

const int ext_trigger_out_pins[NUM_EXT_TRIGGERS] = {
    Pins::TRIGGER_OUT1,  // pin 1
    Pins::TRIGGER_OUT2,  // pin 3
};

const int ext_trigger_in_pins[NUM_EXT_TRIGGERS] = {
    Pins::TRIGGER_IN1,   // pin 2
    Pins::TRIGGER_IN2,   // pin 4
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

// ── 外部触发 IN/OUT（squid++ 双相机）────────────────────────────────
// channel: 0..NUM_EXT_TRIGGERS-1（对应 TRIGGER_OUT/IN 1..2）
// 返回 false 表示 channel 越界

// 向外部输出电平
bool ext_trigger_set_out(uint8_t channel, bool level);

// 在外部输出引脚发送一个固定宽度脉冲（high → wait → low）
bool ext_trigger_pulse_out(uint8_t channel, uint32_t pulse_width_us = TRIGGER_PULSE_LENGTH_us);

// 读取外部输入电平（INPUT_PULLUP，低电平 = 触发激活）
// 返回 true 表示电平 HIGH，false 表示 LOW；channel 越界默认返回 true（去激活态）
bool ext_trigger_read_in(uint8_t channel);

#endif // TRIGGER_H
