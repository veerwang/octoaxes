#include "illumination.h"
#include "build_opt.h"
#include <FastLED.h>
#include <SPI.h>

// =============================================================================
// 状态变量定义
// =============================================================================

int      illumination_source           = 0;
uint16_t illumination_intensity        = 0;
float    illumination_intensity_factor = IlluminationConfig::DEFAULT_INTENSITY_FACTOR;
uint8_t  led_matrix_r = 0;
uint8_t  led_matrix_g = 0;
uint8_t  led_matrix_b = 0;
bool     illumination_is_on = false;
bool     illumination_port_is_on[IlluminationConfig::NUM_PORTS]     = {false};
uint16_t illumination_port_intensity[IlluminationConfig::NUM_PORTS] = {0};

// LED 矩阵像素数组（APA102，BGR 顺序）
static CRGB led_matrix[IlluminationConfig::NUM_LEDS];

// 矩阵 addLeds 是否已注册（防止重复注册）
static bool s_matrix_inited = false;

// =============================================================================
// 初始化
// =============================================================================

void illumination_init_matrix_early()
{
    if (s_matrix_inited) return;
    s_matrix_inited = true;

    // FastLED addLeds：APA102 + BGR + 1 MHz SPI（与旧 Squid init.cpp:44 一致）
    FastLED.addLeds<APA102, Pins::LED_MATRIX_DATA, Pins::LED_MATRIX_CLOCK, BGR, 1>(
        led_matrix, IlluminationConfig::NUM_LEDS);

    // APA102 上电默认输出未定义，许多批次默认全亮。多次推全 0 帧 + 短延迟，
    // 强制 LED 锁存到关闭状态，对抗上电瞬态。
    for (int i = 0; i < IlluminationConfig::NUM_LEDS; i++)
        led_matrix[i].setRGB(0, 0, 0);
    for (int k = 0; k < 4; k++) {
        FastLED.show();
        delay(2);
    }
}

void illumination_init()
{
    // 安全联锁引脚
    pinMode(Pins::ILLUMINATION_INTERLOCK, INPUT_PULLUP);

    // TTL 端口引脚：初始 LOW（关闭）
    pinMode(Pins::ILLUMINATION_D1, OUTPUT); digitalWrite(Pins::ILLUMINATION_D1, LOW);
    pinMode(Pins::ILLUMINATION_D2, OUTPUT); digitalWrite(Pins::ILLUMINATION_D2, LOW);
    pinMode(Pins::ILLUMINATION_D3, OUTPUT); digitalWrite(Pins::ILLUMINATION_D3, LOW);
    pinMode(Pins::ILLUMINATION_D4, OUTPUT); digitalWrite(Pins::ILLUMINATION_D4, LOW);
    pinMode(Pins::ILLUMINATION_D5, OUTPUT); digitalWrite(Pins::ILLUMINATION_D5, LOW);
    // squid++ 双相机扩展到 8 路 TTL 端口
    pinMode(Pins::ILLUMINATION_D6, OUTPUT); digitalWrite(Pins::ILLUMINATION_D6, LOW);
    pinMode(Pins::ILLUMINATION_D7, OUTPUT); digitalWrite(Pins::ILLUMINATION_D7, LOW);
    pinMode(Pins::ILLUMINATION_D8, OUTPUT); digitalWrite(Pins::ILLUMINATION_D8, LOW);

    // 通用数字输出引脚：与旧 Squid `init_io()` (init.cpp:74) 行为一致。
    // 包含激光对焦 AF_LASER（pin 15，旧 Squid `MCU_PINS.AF_LASER`），
    // 上位机通过 cmd 41 SET_PIN_LEVEL 控制。必须显式 OUTPUT，否则 pin 处于
    // INPUT 高阻态时控制板内部上拉会让激光默认开启，且 digitalWrite 在
    // INPUT 模式下不改变实际电平 → 关不掉。
    static const int kDigitalOutputPins[] = {6, 9, 10, 15};
    for (size_t i = 0; i < sizeof(kDigitalOutputPins)/sizeof(kDigitalOutputPins[0]); i++) {
        pinMode(kDigitalOutputPins[i], OUTPUT);
        digitalWrite(kDigitalOutputPins[i], LOW);
    }

    // LED 驱动 SYNC：2 MHz PWM，50% 占空比
    pinMode(Pins::LED_DRIVER_SYNC, OUTPUT);
    analogWriteFrequency(Pins::LED_DRIVER_SYNC, 2000000);
    analogWrite(Pins::LED_DRIVER_SYNC, 128);

    // LED 矩阵初始化（幂等：若已在 setup 早期调用过 early 版本则跳过）
    illumination_init_matrix_early();

    // DAC 初始化
    set_DAC8050x_config();
    set_DAC8050x_default_gain();

    // 状态变量初始化
    illumination_intensity_factor = IlluminationConfig::DEFAULT_INTENSITY_FACTOR;
    illumination_is_on = false;
    for (int i = 0; i < IlluminationConfig::NUM_PORTS; i++) {
        illumination_port_is_on[i]     = false;
        illumination_port_intensity[i] = 0;
    }

    DEBUG_PRINTLN("Illumination initialized");
}

// =============================================================================
// 安全联锁
// =============================================================================

bool illumination_interlock_ok()
{
#ifdef DISABLE_LASER_INTERLOCK
    return true;
#else
    return digitalRead(Pins::ILLUMINATION_INTERLOCK) == LOW;
#endif
}

// =============================================================================
// DAC80508 驱动
// =============================================================================

void set_DAC8050x_gain(uint8_t div, uint8_t gains)
{
    uint16_t value = (uint16_t(div) << 8) | gains;
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE2));
    // DAC80508_1 片选走 74HC154 通道 2（Pins::DAC8050x_CS）
    Pins::hc154_select(Pins::DAC8050x_CS);
    SPI.transfer(IlluminationConfig::DAC_GAIN_ADDR);
    SPI.transfer16(value);
    // 归零到空闲通道（EXPAND_NSCS1）释放 DAC 片选
    Pins::hc154_select((uint8_t)Pins::HC154_EXPAND_NSCS1);
    SPI.endTransaction();
}

void set_DAC8050x_default_gain()
{
    set_DAC8050x_gain(IlluminationConfig::DAC_DEFAULT_DIV,
                      IlluminationConfig::DAC_DEFAULT_GAINS);
}

void set_DAC8050x_config()
{
    uint16_t value = 0;
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE2));
    // DAC80508_1 片选走 74HC154 通道 2（Pins::DAC8050x_CS）
    Pins::hc154_select(Pins::DAC8050x_CS);
    SPI.transfer(IlluminationConfig::DAC_CONFIG_ADDR);
    SPI.transfer16(value);
    // 归零到空闲通道（EXPAND_NSCS1）释放 DAC 片选
    Pins::hc154_select((uint8_t)Pins::HC154_EXPAND_NSCS1);
    SPI.endTransaction();
}

void set_DAC8050x_output(int channel, uint16_t value)
{
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE2));
    // DAC80508_1 片选走 74HC154 通道 2（Pins::DAC8050x_CS）
    Pins::hc154_select(Pins::DAC8050x_CS);
    SPI.transfer(IlluminationConfig::DAC_DAC_ADDR + channel);
    SPI.transfer16(value);
    // 归零到空闲通道（EXPAND_NSCS1）释放 DAC 片选
    Pins::hc154_select((uint8_t)Pins::HC154_EXPAND_NSCS1);
    SPI.endTransaction();
}

// =============================================================================
// LED 矩阵辅助函数（内部使用）
// =============================================================================

static void led_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < IlluminationConfig::NUM_LEDS; i++)
        led_matrix[i].setRGB(r, g, b);
}

static void led_set_left(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < IlluminationConfig::NUM_LEDS / 2; i++)
        led_matrix[i].setRGB(r, g, b);
}

static void led_set_right(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = IlluminationConfig::NUM_LEDS / 2; i < IlluminationConfig::NUM_LEDS; i++)
        led_matrix[i].setRGB(r, g, b);
}

static void led_set_top(uint8_t r, uint8_t g, uint8_t b)
{
    static const int idx[] = {
        0, 1, 2, 3,
        15, 14, 13, 12,
        16, 17, 18, 19, 20, 21,
        39, 38, 37, 36, 35, 34,
        40, 41, 42, 43, 44, 45,
        63, 62, 61, 60, 59, 58,
        64, 65, 66, 67, 68, 69,
        87, 86, 85, 84, 83, 82,
        88, 89, 90, 91, 92, 93,
        111, 110, 109, 108, 107, 106,
        112, 113, 114, 115,
        127, 126, 125, 124};
    for (int i = 0; i < 64; i++)
        led_matrix[idx[i]].setRGB(r, g, b);
}

static void led_set_bottom(uint8_t r, uint8_t g, uint8_t b)
{
    static const int idx[] = {
        4, 5, 6, 7,
        11, 10, 9, 8,
        22, 23, 24, 25, 26, 27,
        33, 32, 31, 30, 29, 28,
        46, 47, 48, 49, 50, 51,
        57, 56, 55, 54, 53, 52,
        70, 71, 72, 73, 74, 75,
        81, 80, 79, 78, 77, 76,
        94, 95, 96, 97, 98, 99,
        105, 104, 103, 102, 101, 100,
        116, 117, 118, 119,
        123, 122, 121, 120};
    for (int i = 0; i < 64; i++)
        led_matrix[idx[i]].setRGB(r, g, b);
}

static void led_set_low_na(uint8_t r, uint8_t g, uint8_t b)
{
    led_matrix[45].setRGB(r, g, b); led_matrix[46].setRGB(r, g, b);
    led_matrix[56].setRGB(r, g, b); led_matrix[57].setRGB(r, g, b);
    led_matrix[58].setRGB(r, g, b); led_matrix[59].setRGB(r, g, b);
    led_matrix[68].setRGB(r, g, b); led_matrix[69].setRGB(r, g, b);
    led_matrix[70].setRGB(r, g, b); led_matrix[71].setRGB(r, g, b);
    led_matrix[81].setRGB(r, g, b); led_matrix[82].setRGB(r, g, b);
}

static void led_set_left_dot(uint8_t r, uint8_t g, uint8_t b)
{
    led_matrix[3].setRGB(r, g, b);  led_matrix[4].setRGB(r, g, b);
    led_matrix[11].setRGB(r, g, b); led_matrix[12].setRGB(r, g, b);
}

static void led_set_right_dot(uint8_t r, uint8_t g, uint8_t b)
{
    led_matrix[115].setRGB(r, g, b); led_matrix[116].setRGB(r, g, b);
    led_matrix[123].setRGB(r, g, b); led_matrix[124].setRGB(r, g, b);
}

// =============================================================================
// LED 矩阵公共函数
// =============================================================================

void clear_matrix()
{
    for (int i = 0; i < IlluminationConfig::NUM_LEDS; i++)
        led_matrix[i].setRGB(0, 0, 0);
    FastLED.show();
}

void turn_on_LED_matrix_pattern(int pattern, uint8_t r, uint8_t g, uint8_t b)
{
    // 强度缩放（0-255 → 0-LED_MAX_INTENSITY），注意：APA102 BGR 顺序
    uint8_t scaled_g = uint8_t(float(g) / 255.0f * IlluminationConfig::LED_MAX_INTENSITY * IlluminationConfig::GREEN_ADJUSTMENT);
    uint8_t scaled_r = uint8_t(float(r) / 255.0f * IlluminationConfig::LED_MAX_INTENSITY * IlluminationConfig::RED_ADJUSTMENT);
    uint8_t scaled_b = uint8_t(float(b) / 255.0f * IlluminationConfig::LED_MAX_INTENSITY * IlluminationConfig::BLUE_ADJUSTMENT);

    led_set_all(0, 0, 0);  // 先清空

    switch (pattern)
    {
        case IlluminationConfig::LED_ARRAY_FULL:
            led_set_all(scaled_g, scaled_r, scaled_b); break;
        case IlluminationConfig::LED_ARRAY_LEFT_HALF:
            led_set_left(scaled_g, scaled_r, scaled_b); break;
        case IlluminationConfig::LED_ARRAY_RIGHT_HALF:
            led_set_right(scaled_g, scaled_r, scaled_b); break;
        case IlluminationConfig::LED_ARRAY_LEFTB_RIGHTR:
            led_set_left(0, 0, scaled_b);
            led_set_right(0, scaled_r, 0);
            break;
        case IlluminationConfig::LED_ARRAY_LOW_NA:
            led_set_low_na(scaled_g, scaled_r, scaled_b); break;
        case IlluminationConfig::LED_ARRAY_LEFT_DOT:
            led_set_left_dot(scaled_g, scaled_r, scaled_b); break;
        case IlluminationConfig::LED_ARRAY_RIGHT_DOT:
            led_set_right_dot(scaled_g, scaled_r, scaled_b); break;
        case IlluminationConfig::LED_ARRAY_TOP_HALF:
            led_set_top(scaled_g, scaled_r, scaled_b); break;
        case IlluminationConfig::LED_ARRAY_BOTTOM_HALF:
            led_set_bottom(scaled_g, scaled_r, scaled_b); break;
        default: break;
    }
    FastLED.show();
}

// =============================================================================
// 端口映射工具
// =============================================================================

int illumination_source_to_port_index(int source)
{
    switch (source)
    {
        case IlluminationConfig::D1: return 0;  // 11 → 0
        case IlluminationConfig::D2: return 1;  // 12 → 1
        case IlluminationConfig::D3: return 2;  // 14 → 2（非顺序！）
        case IlluminationConfig::D4: return 3;  // 13 → 3（非顺序！）
        case IlluminationConfig::D5: return 4;  // 15 → 4
        case IlluminationConfig::D6: return 5;  // 16 → 5（squid++）
        case IlluminationConfig::D7: return 6;  // 17 → 6（squid++）
        case IlluminationConfig::D8: return 7;  // 18 → 7（squid++）
        default: return -1;
    }
}

int port_index_to_pin(int port_index)
{
    switch (port_index)
    {
        case 0: return Pins::ILLUMINATION_D1;
        case 1: return Pins::ILLUMINATION_D2;
        case 2: return Pins::ILLUMINATION_D3;
        case 3: return Pins::ILLUMINATION_D4;
        case 4: return Pins::ILLUMINATION_D5;
        case 5: return Pins::ILLUMINATION_D6;
        case 6: return Pins::ILLUMINATION_D7;
        case 7: return Pins::ILLUMINATION_D8;
        default: return -1;
    }
}

int port_index_to_dac_channel(int port_index)
{
    // squid++ DAC80508_1 有 8 通道 (0-7)，对应 D1-D8
    if (port_index >= 0 && port_index < 8)
        return port_index;
    return -1;
}

// =============================================================================
// 旧版照明 API
// =============================================================================

void turn_on_illumination()
{
    illumination_is_on = true;

    // 同步多端口状态（向后兼容）
    int port_index = illumination_source_to_port_index(illumination_source);
    if (port_index >= 0)
        illumination_port_is_on[port_index] = true;

    switch (illumination_source)
    {
        case IlluminationConfig::LED_ARRAY_FULL:
        case IlluminationConfig::LED_ARRAY_LEFT_HALF:
        case IlluminationConfig::LED_ARRAY_RIGHT_HALF:
        case IlluminationConfig::LED_ARRAY_LEFTB_RIGHTR:
        case IlluminationConfig::LED_ARRAY_LOW_NA:
        case IlluminationConfig::LED_ARRAY_LEFT_DOT:
        case IlluminationConfig::LED_ARRAY_RIGHT_DOT:
        case IlluminationConfig::LED_ARRAY_TOP_HALF:
        case IlluminationConfig::LED_ARRAY_BOTTOM_HALF:
            turn_on_LED_matrix_pattern(illumination_source,
                                        led_matrix_r, led_matrix_g, led_matrix_b);
            break;
        case IlluminationConfig::LED_EXTERNAL_FET:
            break;
        case IlluminationConfig::D1:
            if (illumination_interlock_ok())
                digitalWrite(Pins::ILLUMINATION_D1, HIGH);
            break;
        case IlluminationConfig::D2:
            if (illumination_interlock_ok())
                digitalWrite(Pins::ILLUMINATION_D2, HIGH);
            break;
        case IlluminationConfig::D3:
            if (illumination_interlock_ok())
                digitalWrite(Pins::ILLUMINATION_D3, HIGH);
            break;
        case IlluminationConfig::D4:
            if (illumination_interlock_ok())
                digitalWrite(Pins::ILLUMINATION_D4, HIGH);
            break;
        case IlluminationConfig::D5:
            if (illumination_interlock_ok())
                digitalWrite(Pins::ILLUMINATION_D5, HIGH);
            break;
        case IlluminationConfig::D6:
            if (illumination_interlock_ok())
                digitalWrite(Pins::ILLUMINATION_D6, HIGH);
            break;
        case IlluminationConfig::D7:
            if (illumination_interlock_ok())
                digitalWrite(Pins::ILLUMINATION_D7, HIGH);
            break;
        case IlluminationConfig::D8:
            if (illumination_interlock_ok())
                digitalWrite(Pins::ILLUMINATION_D8, HIGH);
            break;
        default: break;
    }
}

void turn_off_illumination()
{
    // 同步多端口状态（向后兼容）
    int port_index = illumination_source_to_port_index(illumination_source);
    if (port_index >= 0)
        illumination_port_is_on[port_index] = false;

    switch (illumination_source)
    {
        case IlluminationConfig::LED_ARRAY_FULL:
        case IlluminationConfig::LED_ARRAY_LEFT_HALF:
        case IlluminationConfig::LED_ARRAY_RIGHT_HALF:
        case IlluminationConfig::LED_ARRAY_LEFTB_RIGHTR:
        case IlluminationConfig::LED_ARRAY_LOW_NA:
        case IlluminationConfig::LED_ARRAY_LEFT_DOT:
        case IlluminationConfig::LED_ARRAY_RIGHT_DOT:
        case IlluminationConfig::LED_ARRAY_TOP_HALF:
        case IlluminationConfig::LED_ARRAY_BOTTOM_HALF:
            clear_matrix();
            break;
        case IlluminationConfig::LED_EXTERNAL_FET:
            break;
        case IlluminationConfig::D1: digitalWrite(Pins::ILLUMINATION_D1, LOW); break;
        case IlluminationConfig::D2: digitalWrite(Pins::ILLUMINATION_D2, LOW); break;
        case IlluminationConfig::D3: digitalWrite(Pins::ILLUMINATION_D3, LOW); break;
        case IlluminationConfig::D4: digitalWrite(Pins::ILLUMINATION_D4, LOW); break;
        case IlluminationConfig::D5: digitalWrite(Pins::ILLUMINATION_D5, LOW); break;
        case IlluminationConfig::D6: digitalWrite(Pins::ILLUMINATION_D6, LOW); break;
        case IlluminationConfig::D7: digitalWrite(Pins::ILLUMINATION_D7, LOW); break;
        case IlluminationConfig::D8: digitalWrite(Pins::ILLUMINATION_D8, LOW); break;
        default: break;
    }
    illumination_is_on = false;
}

void set_illumination(int source, uint16_t intensity)
{
    illumination_source    = source;
    illumination_intensity = uint16_t(intensity * illumination_intensity_factor);

    // 同步多端口强度（向后兼容）
    int port_index = illumination_source_to_port_index(source);
    if (port_index >= 0)
        illumination_port_intensity[port_index] = intensity;

    // 写 DAC
    switch (source)
    {
        case IlluminationConfig::D1: set_DAC8050x_output(0, illumination_intensity); break;
        case IlluminationConfig::D2: set_DAC8050x_output(1, illumination_intensity); break;
        case IlluminationConfig::D3: set_DAC8050x_output(2, illumination_intensity); break;
        case IlluminationConfig::D4: set_DAC8050x_output(3, illumination_intensity); break;
        case IlluminationConfig::D5: set_DAC8050x_output(4, illumination_intensity); break;
        case IlluminationConfig::D6: set_DAC8050x_output(5, illumination_intensity); break;
        case IlluminationConfig::D7: set_DAC8050x_output(6, illumination_intensity); break;
        case IlluminationConfig::D8: set_DAC8050x_output(7, illumination_intensity); break;
        default: break;
    }

    // 若已开灯，立即更新输出
    if (illumination_is_on)
        turn_on_illumination();
}

void set_illumination_led_matrix(int source, uint8_t r, uint8_t g, uint8_t b)
{
    // 与旧 Squid functions.cpp:359-368 一致：仅缓存参数，不立即点亮、不动
    // illumination_is_on。上位机启动时常用此命令"预设"明场颜色/pattern，
    // 立即点亮会导致后续切到 D 通道时矩阵仍亮（双开）。
    illumination_source = source;
    led_matrix_r = r;
    led_matrix_g = g;
    led_matrix_b = b;
    if (illumination_is_on)
        turn_on_illumination();  // 当前已开灯时才把内容刷到当前 source
}

// =============================================================================
// 新版多端口 API
// =============================================================================

void turn_on_port(int port_index)
{
    if (port_index < 0 || port_index >= IlluminationConfig::NUM_PORTS)
        return;
    int pin = port_index_to_pin(port_index);
    if (pin < 0) return;
    if (illumination_interlock_ok()) {
        digitalWrite(pin, HIGH);
        illumination_port_is_on[port_index] = true;
    }
}

void turn_off_port(int port_index)
{
    if (port_index < 0 || port_index >= IlluminationConfig::NUM_PORTS)
        return;
    int pin = port_index_to_pin(port_index);
    if (pin < 0) return;
    digitalWrite(pin, LOW);
    illumination_port_is_on[port_index] = false;
}

void set_port_intensity(int port_index, uint16_t intensity)
{
    if (port_index < 0 || port_index >= IlluminationConfig::NUM_PORTS)
        return;
    int dac_ch = port_index_to_dac_channel(port_index);
    if (dac_ch < 0) return;
    uint16_t scaled = uint16_t(intensity * illumination_intensity_factor);
    set_DAC8050x_output(dac_ch, scaled);
    illumination_port_intensity[port_index] = intensity;  // 存原始值
}

void turn_off_all_ports()
{
    for (int i = 0; i < IlluminationConfig::NUM_PORTS; i++) {
        int pin = port_index_to_pin(i);
        if (pin >= 0) {
            digitalWrite(pin, LOW);
            illumination_port_is_on[i] = false;
        }
    }
    clear_matrix();
    illumination_is_on = false;
}

// =============================================================================
// 串口看门狗
// =============================================================================

uint32_t last_serial_message_time = 0;
uint32_t watchdog_timeout_ms = DEFAULT_WATCHDOG_TIMEOUT_MS;
bool     watchdog_enabled = false;

void watchdog_reset_timer()
{
    last_serial_message_time = millis();
}

void watchdog_set_timeout(uint32_t timeout_ms)
{
    if (timeout_ms == 0)
        timeout_ms = DEFAULT_WATCHDOG_TIMEOUT_MS;
    if (timeout_ms > MAX_WATCHDOG_TIMEOUT_MS)
        timeout_ms = MAX_WATCHDOG_TIMEOUT_MS;

    watchdog_timeout_ms = timeout_ms;
    watchdog_enabled = true;
    watchdog_reset_timer();
}

void watchdog_check()
{
    if (watchdog_enabled && (millis() - last_serial_message_time >= watchdog_timeout_ms)) {
        turn_off_all_ports();
        watchdog_enabled = false;  // 单次触发，不重复
    }
}
