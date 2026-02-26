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

// =============================================================================
// 初始化
// =============================================================================

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

    // LED 驱动 SYNC：2 MHz PWM，50% 占空比
    pinMode(Pins::LED_DRIVER_SYNC, OUTPUT);
    analogWriteFrequency(Pins::LED_DRIVER_SYNC, 2000000);
    analogWrite(Pins::LED_DRIVER_SYNC, 128);

    // LED 矩阵初始化（APA102，BGR 顺序）
    FastLED.addLeds<APA102, Pins::LED_MATRIX_DATA, Pins::LED_MATRIX_CLOCK, BGR>(
        led_matrix, IlluminationConfig::NUM_LEDS);
    clear_matrix();

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
    digitalWrite(Pins::DAC8050x_CS, LOW);
    SPI.transfer(IlluminationConfig::DAC_GAIN_ADDR);
    SPI.transfer16(value);
    digitalWrite(Pins::DAC8050x_CS, HIGH);
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
    digitalWrite(Pins::DAC8050x_CS, LOW);
    SPI.transfer(IlluminationConfig::DAC_CONFIG_ADDR);
    SPI.transfer16(value);
    digitalWrite(Pins::DAC8050x_CS, HIGH);
    SPI.endTransaction();
}

void set_DAC8050x_output(int channel, uint16_t value)
{
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE2));
    digitalWrite(Pins::DAC8050x_CS, LOW);
    SPI.transfer(IlluminationConfig::DAC_DAC_ADDR + channel);
    SPI.transfer16(value);
    digitalWrite(Pins::DAC8050x_CS, HIGH);
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
        default: return -1;
    }
}

int port_index_to_dac_channel(int port_index)
{
    if (port_index >= 0 && port_index < 5)
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
        default: break;
    }

    // 若已开灯，立即更新输出
    if (illumination_is_on)
        turn_on_illumination();
}

void set_illumination_led_matrix(int source, uint8_t r, uint8_t g, uint8_t b)
{
    illumination_source = source;
    led_matrix_r = r;
    led_matrix_g = g;
    led_matrix_b = b;
    if (illumination_is_on)
        turn_on_illumination();
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
