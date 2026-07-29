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
int           strobe_active_source[NUM_TRIGGER_CHANNELS];
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
        strobe_active_source[i] = 0;
        strobe_delay_us[i] = 0;
        illumination_on_time_us[i] = 0;
        timestamp_trigger_rising_edge[i] = 0;
    }

    trigger_mode = TRIGGER_MODE_NORMAL;

    // 启动频闪定时器（100μs 间隔）
    strobeTimer.begin(ISR_strobeTimer, STROBE_TIMER_INTERVAL_us);

    DEBUG_PRINTLN("Trigger system initialized");
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
// 状态复位（RESET / INITIALIZE 命令）
// =============================================================================

void trigger_reset_state()
{
    bool lamp_on[NUM_TRIGGER_CHANNELS];
    int  lamp_source[NUM_TRIGGER_CHANNELS];

    noInterrupts();
    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        lamp_on[i]     = strobe_on[i];
        lamp_source[i] = strobe_active_source[i];
        control_strobe[i] = false;
        strobe_on[i] = false;
        digitalWrite(camera_trigger_pins[i], HIGH);
        trigger_output_level[i] = HIGH;
    }
    trigger_mode = TRIGGER_MODE_NORMAL;
    interrupts();

    // 标志已清、ISR 不会再碰这些通道；在临界区外补关频闪点亮中的灯
    //（clear_matrix/FastLED 毫秒级，不宜在关中断下执行）
    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        if (lamp_on[i]) {
            turn_off_illumination_source(lamp_source[i]);
            if (illumination_source == lamp_source[i])
                illumination_is_on = false;
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
        // 仅处理启用了频闪控制的通道。
        // 2026-07-20 修复（与 octoaxesplus 同步）：删除 `trigger_output_level[i] == HIGH`
        // 门卫——NORMAL 模式触发脉冲仅 50µs 即恢复 HIGH，而 strobe_delay 是毫秒级，
        // 该门卫使频闪开灯永远不执行（硬件触发模式下照明恒灭）。频闪生命周期由
        // control_strobe/strobe_on 完整描述，与触发引脚电平无关（对齐旧 Squid ISR）。
        if (!control_strobe[i])
            continue;

        unsigned long elapsed = now - timestamp_trigger_rising_edge[i];

        if (illumination_on_time_us[i] <= 30000) {
            // 短曝光（≤ 30ms）：同步模式
            // 等待 strobe_delay 后开灯，持续 illumination_on_time 后关灯
            if (!strobe_on[i] && elapsed >= strobe_delay_us[i]) {
                // 锁存本次频闪的光源：开/关必须作用于同一源。不锁存的话，
                // 上位机在频闪窗口内切换 illumination_source（多通道采集逐通道
                // 换光源）会让关灯落到新源上，旧源（如激光）滞留常亮。
                strobe_active_source[i] = illumination_source;
                illumination_is_on = true;
                turn_on_illumination_source(strobe_active_source[i]);
                strobe_on[i] = true;
                // 短曝光直接用 delayMicroseconds 精确控制
                delayMicroseconds(illumination_on_time_us[i]);
                turn_off_illumination_source(strobe_active_source[i]);
                // 光源已被上位机切换并显式开灯时，保留其 illumination_is_on 状态
                if (illumination_source == strobe_active_source[i])
                    illumination_is_on = false;
                strobe_on[i] = false;
                control_strobe[i] = false;  // 完成一次频闪，清除标志
            }
        } else {
            // 长曝光（> 30ms）：异步模式，两步分离
            if (!strobe_on[i] && elapsed >= strobe_delay_us[i]) {
                // 步骤 1：开灯（锁存光源，理由同短曝光路径）
                strobe_active_source[i] = illumination_source;
                illumination_is_on = true;
                turn_on_illumination_source(strobe_active_source[i]);
                strobe_on[i] = true;
            } else if (strobe_on[i] &&
                       elapsed >= strobe_delay_us[i] + illumination_on_time_us[i]) {
                // 步骤 2：按锁存值关灯
                turn_off_illumination_source(strobe_active_source[i]);
                if (illumination_source == strobe_active_source[i])
                    illumination_is_on = false;
                strobe_on[i] = false;
                control_strobe[i] = false;  // 完成一次频闪，清除标志
            }
        }
    }
}
