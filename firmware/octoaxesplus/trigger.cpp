#include "trigger.h"
#include "build_opt.h"
#include "illumination.h"

// =============================================================================
// State-variable definitions
// =============================================================================

bool          trigger_output_level[NUM_TRIGGER_CHANNELS];
volatile bool          control_strobe[NUM_TRIGGER_CHANNELS];
volatile bool          strobe_on[NUM_TRIGGER_CHANNELS];
volatile int           strobe_active_source[NUM_TRIGGER_CHANNELS];
volatile unsigned long strobe_delay_us[NUM_TRIGGER_CHANNELS];
volatile uint32_t      illumination_on_time_us[NUM_TRIGGER_CHANNELS];
volatile unsigned long timestamp_trigger_rising_edge[NUM_TRIGGER_CHANNELS];
volatile uint8_t trigger_mode = TRIGGER_MODE_NORMAL;

// Joystick state
bool          joystick_button_pressed = false;
unsigned long joystick_button_pressed_timestamp = 0;

// Strobe timer
static IntervalTimer strobeTimer;

// =============================================================================
// Initialization
// =============================================================================

void trigger_init()
{
    // initialize the trigger pins: OUTPUT + HIGH (idle is high, negative-pulse triggered)
    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        pinMode(camera_trigger_pins[i], OUTPUT);
        digitalWrite(camera_trigger_pins[i], HIGH);
    }

    // initialize the state arrays
    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        trigger_output_level[i] = HIGH;
        control_strobe[i] = false;
        strobe_on[i] = false;
        strobe_active_source[i] = 0;
        strobe_delay_us[i] = 0;
        illumination_on_time_us[i] = 0;
        timestamp_trigger_rising_edge[i] = 0;
    }

    trigger_mode = TRIGGER_MODE_NORMAL;

    // external trigger IN/OUT (squid++ dual-camera)
    // OUT: default LOW idle (adjust per the external device's convention as needed)
    // IN: INPUT_PULLUP, floating defaults HIGH meaning no trigger
    for (int i = 0; i < NUM_EXT_TRIGGERS; i++) {
        pinMode(ext_trigger_out_pins[i], OUTPUT);
        digitalWrite(ext_trigger_out_pins[i], LOW);
    }
    for (int i = 0; i < NUM_EXT_TRIGGER_IN; i++) {
        pinMode(ext_trigger_in_pins[i], INPUT_PULLUP);
    }

    // camera READY inputs: INPUT_PULLUP to resist floating
    // (pin 5 has left this table — it is actually the AF laser, see the trigger.h 2026-07-22 comment)
    for (int i = 0; i < NUM_CAM_TRI_READY; i++) {
        pinMode(cam_tri_ready_pins[i], INPUT_PULLUP);
    }

    // start the strobe timer (100us interval)
    strobeTimer.begin(ISR_strobeTimer, STROBE_TIMER_INTERVAL_us);

    DEBUG_PRINTLN("Trigger system initialized");
}

// =============================================================================
// external trigger IN/OUT helpers (squid++ dual-camera)
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
    if (channel >= NUM_EXT_TRIGGER_IN) return true;  // out-of-range defaults to the deactivated state
    return digitalRead(ext_trigger_in_pins[channel]) == HIGH;
}

bool cam_tri_read_ready(uint8_t channel)
{
    if (channel >= NUM_CAM_TRI_READY) return false;  // out-of-range defaults to not-ready
    return digitalRead(cam_tri_ready_pins[channel]) == HIGH;
}

// =============================================================================
// main-loop update: manage trigger-pulse recovery
// =============================================================================

void trigger_update()
{
    unsigned long now = micros();

    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        // only process channels that have been triggered (LOW)
        if (trigger_output_level[i] == LOW) {
            if (trigger_mode == TRIGGER_MODE_NORMAL) {
                // mode 0: restore HIGH after a fixed 50us pulse width
                if (now - timestamp_trigger_rising_edge[i] >= TRIGGER_PULSE_LENGTH_us) {
                    digitalWrite(camera_trigger_pins[i], HIGH);
                    trigger_output_level[i] = HIGH;
                }
            } else {
                // mode 1: pulse width = strobe_delay + illumination_on_time
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
// state reset (RESET / INITIALIZE commands)
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

    // Flags are cleared and the ISR will no longer touch these channels; turn off
    // any light lit mid-strobe outside the critical section
    // (clear_matrix/FastLED take milliseconds, unfit to run with interrupts off).
    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        if (lamp_on[i]) {
            turn_off_illumination_source(lamp_source[i]);
            if (illumination_source == lamp_source[i])
                illumination_is_on = false;
        }
    }
}

// =============================================================================
// strobe timer ISR (100us interval)
// =============================================================================

void ISR_strobeTimer()
{
    unsigned long now = micros();

    for (int i = 0; i < NUM_TRIGGER_CHANNELS; i++) {
        // only process channels that have strobe control enabled.
        // 2026-07-20 fix: removed the `trigger_output_level[i] == HIGH` guard — in NORMAL mode
        // the trigger pulse returns to HIGH after only 50µs while strobe_delay (rolling shutter
        // cameras) is millisecond-scale, so the guard meant the strobe turn-on never ran
        // (illumination permanently off in hardware-trigger mode). The strobe lifecycle is fully
        // described by control_strobe/strobe_on, independent of the trigger pin level (aligned
        // with the legacy Squid ISR).
        if (!control_strobe[i])
            continue;

        unsigned long elapsed = now - timestamp_trigger_rising_edge[i];

        if (illumination_on_time_us[i] <= 30000) {
            // short exposure (<= 30ms): synchronous mode
            // wait strobe_delay then turn on the light, keep it on for illumination_on_time, then turn off
            if (!strobe_on[i] && elapsed >= strobe_delay_us[i]) {
                // Latch the source for this strobe: on/off must act on the same
                // source. Without the latch, a host switching illumination_source
                // inside the strobe window (multi-channel acquisition changes the
                // source per channel) would make the off land on the new source,
                // leaving the old one (e.g. a laser) stuck on.
                strobe_active_source[i] = illumination_source;
                illumination_is_on = true;
                turn_on_illumination_source(strobe_active_source[i]);
                strobe_on[i] = true;
                // short exposure uses delayMicroseconds for precise control
                delayMicroseconds(illumination_on_time_us[i]);
                turn_off_illumination_source(strobe_active_source[i]);
                // if the host already switched the source and explicitly turned it
                // on, preserve its illumination_is_on state
                if (illumination_source == strobe_active_source[i])
                    illumination_is_on = false;
                strobe_on[i] = false;
                control_strobe[i] = false;  // one strobe done, clear the flag
            }
        } else {
            // long exposure (> 30ms): asynchronous mode, split into two steps
            if (!strobe_on[i] && elapsed >= strobe_delay_us[i]) {
                // step 1: turn on the light (latch the source, same rationale as
                // the short-exposure path)
                strobe_active_source[i] = illumination_source;
                illumination_is_on = true;
                turn_on_illumination_source(strobe_active_source[i]);
                strobe_on[i] = true;
            } else if (strobe_on[i] &&
                       elapsed >= strobe_delay_us[i] + illumination_on_time_us[i]) {
                // step 2: turn off the light using the latched source
                turn_off_illumination_source(strobe_active_source[i]);
                if (illumination_source == strobe_active_source[i])
                    illumination_is_on = false;
                strobe_on[i] = false;
                control_strobe[i] = false;  // one strobe done, clear the flag
            }
        }
    }
}
