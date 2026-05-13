#include "trigger.h"
#include "build_opt.h"
#include "illumination.h"

// =============================================================================
// 状态变量定义
// =============================================================================

bool          trigger_output_level[NUM_TRIGGER_CHANNELS];
bool          control_strobe[NUM_TRIGGER_CHANNELS];
bool          strobe_output_level[NUM_TRIGGER_CHANNELS];
bool          strobe_on[NUM_TRIGGER_CHANNELS];
unsigned long strobe_delay_us[NUM_TRIGGER_CHANNELS];
uint32_t      illumination_on_time_us[NUM_TRIGGER_CHANNELS];
unsigned long timestamp_trigger_rising_edge[NUM_TRIGGER_CHANNELS];
volatile uint8_t trigger_mode = TRIGGER_MODE_NORMAL;

// Joystick 状态
bool          joystick_button_pressed = false;
unsigned long joystick_button_pressed_timestamp = 0;

// 频闪定时器
static IntervalTimer strobeTimer;

// =============================================================================
// 初始化
// =============================================================================

void trigger_init()
{
    // 初始化触发引脚：OUTPUT + HIGH（空闲状态为高，负脉冲触发）
    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        pinMode(camera_trigger_pins[i], OUTPUT);
        digitalWrite(camera_trigger_pins[i], HIGH);
    }

    // 初始化状态数组
    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        trigger_output_level[i] = HIGH;
        control_strobe[i] = false;
        strobe_output_level[i] = LOW;
        strobe_on[i] = false;
        strobe_delay_us[i] = 0;
        illumination_on_time_us[i] = 0;
        timestamp_trigger_rising_edge[i] = 0;
    }

    trigger_mode = TRIGGER_MODE_NORMAL;

    // 外部触发 IN/OUT（squid++ 双相机）
    // OUT：默认 LOW 空闲（外部设备约定按需求修改）
    // IN：INPUT_PULLUP，悬空默认 HIGH 表示无触发
    for (int i = 0; i < NUM_EXT_TRIGGERS; i++) {
        pinMode(ext_trigger_out_pins[i], OUTPUT);
        digitalWrite(ext_trigger_out_pins[i], LOW);
        pinMode(ext_trigger_in_pins[i], INPUT_PULLUP);
    }

    // 双相机 READY 输入（squid++ 双相机）：INPUT_PULLUP 抗悬空
    for (int i = 0; i < NUM_EXT_TRIGGERS; i++) {
        pinMode(cam_tri_ready_pins[i], INPUT_PULLUP);
    }

    // 启动频闪定时器（100μs 间隔）
    strobeTimer.begin(ISR_strobeTimer, STROBE_TIMER_INTERVAL_us);

    DEBUG_PRINTLN("Trigger system initialized");
}

// =============================================================================
// 外部触发 IN/OUT helpers（squid++ 双相机）
// =============================================================================

bool ext_trigger_set_out(uint8_t channel, bool level)
{
    if (channel >= NUM_EXT_TRIGGERS) return false;
    digitalWrite(ext_trigger_out_pins[channel], level ? HIGH : LOW);
    return true;
}

bool ext_trigger_pulse_out(uint8_t channel, uint32_t pulse_width_us)
{
    if (channel >= NUM_EXT_TRIGGERS) return false;
    int pin = ext_trigger_out_pins[channel];
    digitalWrite(pin, HIGH);
    delayMicroseconds(pulse_width_us);
    digitalWrite(pin, LOW);
    return true;
}

bool ext_trigger_read_in(uint8_t channel)
{
    if (channel >= NUM_EXT_TRIGGERS) return true;  // 越界默认去激活态
    return digitalRead(ext_trigger_in_pins[channel]) == HIGH;
}

bool cam_tri_read_ready(uint8_t channel)
{
    if (channel >= NUM_EXT_TRIGGERS) return false;  // 越界默认未就绪
    return digitalRead(cam_tri_ready_pins[channel]) == HIGH;
}

// =============================================================================
// 主循环更新：管理触发脉冲恢复
// =============================================================================

void trigger_update()
{
    unsigned long now = micros();

    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        // 仅处理已触发（LOW）的通道
        if (trigger_output_level[i] == LOW) {
            if (trigger_mode == TRIGGER_MODE_NORMAL) {
                // 模式 0：固定 50μs 脉宽后恢复 HIGH
                if (now - timestamp_trigger_rising_edge[i] >= TRIGGER_PULSE_LENGTH_us) {
                    digitalWrite(camera_trigger_pins[i], HIGH);
                    trigger_output_level[i] = HIGH;
                }
            } else {
                // 模式 1：脉宽 = strobe_delay + illumination_on_time
                unsigned long pulse_duration = strobe_delay_us[i] + illumination_on_time_us[i];
                if (now - timestamp_trigger_rising_edge[i] >= pulse_duration) {
                    digitalWrite(camera_trigger_pins[i], HIGH);
                    trigger_output_level[i] = HIGH;
                }
            }
        }
    }
}

// =============================================================================
// 频闪定时器 ISR（100μs 间隔）
// =============================================================================

void ISR_strobeTimer()
{
    unsigned long now = micros();

    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        // 仅处理启用了频闪控制的已触发通道
        if (!control_strobe[i] || trigger_output_level[i] == HIGH)
            continue;

        unsigned long elapsed = now - timestamp_trigger_rising_edge[i];

        if (illumination_on_time_us[i] <= 30000) {
            // 短曝光（≤ 30ms）：同步模式
            // 等待 strobe_delay 后开灯，持续 illumination_on_time 后关灯
            if (!strobe_on[i] && elapsed >= strobe_delay_us[i]) {
                turn_on_illumination();
                strobe_on[i] = true;
                // 短曝光直接用 delayMicroseconds 精确控制
                delayMicroseconds(illumination_on_time_us[i]);
                turn_off_illumination();
                strobe_on[i] = false;
                control_strobe[i] = false;  // 完成一次频闪，清除标志
            }
        } else {
            // 长曝光（> 30ms）：异步模式，两步分离
            if (!strobe_on[i] && elapsed >= strobe_delay_us[i]) {
                // 步骤 1：开灯
                turn_on_illumination();
                strobe_on[i] = true;
            } else if (strobe_on[i] &&
                       elapsed >= strobe_delay_us[i] + illumination_on_time_us[i]) {
                // 步骤 2：关灯
                turn_off_illumination();
                strobe_on[i] = false;
                control_strobe[i] = false;  // 完成一次频闪，清除标志
            }
        }
    }
}
