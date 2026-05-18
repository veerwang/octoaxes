#ifndef ILLUMINATION_H
#define ILLUMINATION_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// 照明状态变量（extern 声明，定义在 illumination.cpp）
// =============================================================================
extern int      illumination_source;
extern uint16_t illumination_intensity;
extern float    illumination_intensity_factor;
extern uint8_t  led_matrix_r;
extern uint8_t  led_matrix_g;
extern uint8_t  led_matrix_b;
extern bool     illumination_is_on;
extern bool     illumination_port_is_on[IlluminationConfig::NUM_PORTS];
extern uint16_t illumination_port_intensity[IlluminationConfig::NUM_PORTS];

// =============================================================================
// 初始化
// =============================================================================

// 初始化照明硬件：引脚、LED 矩阵、DAC、联锁
void illumination_init();

// 仅初始化 LED 矩阵并清零，幂等。应在 setup() 最早期调用，
// 在 initializePowerManagement (等 PG 信号) 等耗时初始化之前，
// 把 APA102 上电默认亮态压灭最小化用户感知到的"启动亮"窗口。
void illumination_init_matrix_early();

// =============================================================================
// 安全联锁
// =============================================================================

// 联锁检查：pin 2 为 LOW 表示安全
// 编译选项 -DDISABLE_LASER_INTERLOCK 可强制返回 true（无激光系统使用）
bool illumination_interlock_ok();

// =============================================================================
// DAC80508 驱动
// =============================================================================

void set_DAC8050x_output(int channel, uint16_t value);
void set_DAC8050x_gain(uint8_t div, uint8_t gains);
void set_DAC8050x_config();
void set_DAC8050x_default_gain();

// 全部 8 通道 DAC 输出归零（ttl_test bring-up 验证）
void dac_zero_all();

// 两帧 SPI 协议读 DAC80508 寄存器（addr 范围 0x00..0x18）
uint16_t read_DAC8050x_reg(uint8_t addr);

// 主循环钩子：进 loop 后一次性补做 CONFIG/GAIN/zero 同步，
// 规避 setup-time SPI 首事务偶发被丢的硬件 bug（ttl_test bring-up 验证）
void illumination_update();

// =============================================================================
// LED 矩阵（APA102，128 像素）
// =============================================================================

void clear_matrix();
void turn_on_LED_matrix_pattern(int pattern, uint8_t r, uint8_t g, uint8_t b);

// =============================================================================
// 旧版照明 API（单光源模型）
// =============================================================================

// 使用当前 illumination_source 开/关灯
void turn_on_illumination();
void turn_off_illumination();

// 设置光源码和 DAC 强度（可能立即更新输出）
void set_illumination(int source, uint16_t intensity);

// 设置 LED 矩阵颜色/图案（可能立即更新输出）
void set_illumination_led_matrix(int source, uint8_t r, uint8_t g, uint8_t b);

// =============================================================================
// 新版多端口 API（v1.0+）
// =============================================================================

// 开/关指定端口 GPIO（需检查联锁）
void turn_on_port(int port_index);
void turn_off_port(int port_index);

// 设置指定端口 DAC 强度（按 illumination_intensity_factor 缩放后写入）
void set_port_intensity(int port_index, uint16_t intensity);

// 关闭全部端口 + LED 矩阵
void turn_off_all_ports();

// =============================================================================
// 端口映射工具
// =============================================================================

// 旧版光源码 → 端口索引（11→0, 12→1, 14→2, 13→3, 15→4；其他返回 -1）
int illumination_source_to_port_index(int source);

// 端口索引 → GPIO 引脚号（0→5, 1→4, 2→22, 3→3, 4→23；其他返回 -1）
int port_index_to_pin(int port_index);

// 端口索引 → DAC 通道（0-4 直接映射；其他返回 -1）
int port_index_to_dac_channel(int port_index);

// =============================================================================
// 串口看门狗（通信中断后自动关闭照明）
// =============================================================================

// 看门狗默认/最大超时（毫秒）
static const uint32_t DEFAULT_WATCHDOG_TIMEOUT_MS = 5000;
static const uint32_t MAX_WATCHDOG_TIMEOUT_MS = 3600000;  // 1 小时

extern uint32_t last_serial_message_time;
extern uint32_t watchdog_timeout_ms;
extern bool     watchdog_enabled;

// 重置看门狗计时器（在每次收到有效串口消息时调用）
void watchdog_reset_timer();

// 设置看门狗超时并使能（timeout_ms=0 使用默认值，超过最大值自动截断）
void watchdog_set_timeout(uint32_t timeout_ms);

// 主循环中调用：超时后关闭所有照明，单次触发
void watchdog_check();

#endif // ILLUMINATION_H
