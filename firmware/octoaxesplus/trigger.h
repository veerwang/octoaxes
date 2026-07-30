#ifndef TRIGGER_H
#define TRIGGER_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// Trigger-mode constants
// =============================================================================

const uint8_t TRIGGER_MODE_NORMAL = 0;  // fixed 50us pulse
const uint8_t TRIGGER_MODE_LEVEL  = 1;  // level trigger (strobe_delay + on_time)

// Trigger-pulse parameters
const int TRIGGER_PULSE_LENGTH_us = 50;
const int NUM_TRIGGER_CHANNELS = 8;

// Strobe timer interval
const int STROBE_TIMER_INTERVAL_us = 100;

// Camera-trigger pin mapping
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

// external trigger IN/OUT (squid++ dual-camera: bidirectional sync with external devices)
// host protocol integration TODO (CAM_TRI_READY handshake + handler command word TBD)
// 2026-07-20: pin 4 (formerly TRIGGER_IN2) measured as the camera 2 trigger line, removed from ext IN -> only 1 IN line remains
const int NUM_EXT_TRIGGERS = 2;
const int NUM_EXT_TRIGGER_IN = 1;

const int ext_trigger_out_pins[NUM_EXT_TRIGGERS] = {
    Pins::TRIGGER_OUT1,  // pin 1
    Pins::TRIGGER_OUT2,  // pin 3
};

const int ext_trigger_in_pins[NUM_EXT_TRIGGER_IN] = {
    Pins::TRIGGER_IN1,   // pin 2
};

// dual-camera READY feedback inputs
// 2026-07-22: pin 5 (formerly "camera2_wait-trigger" in the survey table) user-confirmed to be
// actually wired as the AF laser -> removed from the READY table (otherwise trigger_init's
// INPUT_PULLUP would override illumination_init's OUTPUT+LOW shutdown of the AF laser).
// Camera 2 READY to be re-surveyed after re-verification.
const int NUM_CAM_TRI_READY = 1;
const int cam_tri_ready_pins[NUM_CAM_TRI_READY] = {
    Pins::CAM_TRI_READY1,  // pin 7 (camera 1 wait-for-trigger)
};

// =============================================================================
// State variables (extern declarations, defined in trigger.cpp)
// =============================================================================

extern bool          trigger_output_level[NUM_TRIGGER_CHANNELS];
// volatile (down to timestamp_trigger_rising_edge): shared read/write between ISR_strobeTimer and the main loop;
// trigger_output_level is used only by the main loop (handler/trigger_update), intentionally not marked
extern volatile bool          control_strobe[NUM_TRIGGER_CHANNELS];
extern volatile bool          strobe_on[NUM_TRIGGER_CHANNELS];
extern volatile int           strobe_active_source[NUM_TRIGGER_CHANNELS];
extern volatile unsigned long strobe_delay_us[NUM_TRIGGER_CHANNELS];
extern volatile uint32_t      illumination_on_time_us[NUM_TRIGGER_CHANNELS];
extern volatile unsigned long timestamp_trigger_rising_edge[NUM_TRIGGER_CHANNELS];
extern volatile uint8_t trigger_mode;

// Joystick state
extern bool          joystick_button_pressed;
extern unsigned long joystick_button_pressed_timestamp;

// =============================================================================
// API
// =============================================================================

// Initialize the trigger system: pins, state arrays, timer
void trigger_init();

// Called from the main loop: manage trigger-pulse recovery (HIGH level)
void trigger_update();

// Reset the trigger/strobe state: restore the trigger pins to HIGH, clear the
// strobe flags, turn off any light lit mid-strobe, and return the mode to
// NORMAL. Used by the RESET / INITIALIZE commands.
void trigger_reset_state();

// Timer interrupt callback: manage strobe-illumination timing
void ISR_strobeTimer();

// -- external trigger IN/OUT (squid++ dual-camera) --------------------------------
// channel: 0..NUM_EXT_TRIGGERS-1 (corresponds to TRIGGER_OUT/IN 1..2)
// returns false if the channel is out of range

// drive the external output level
bool ext_trigger_set_out(uint8_t channel, bool level);

// send a fixed-width pulse on the external output pin (high -> wait -> low)
bool ext_trigger_pulse_out(uint8_t channel, uint32_t pulse_width_us = TRIGGER_PULSE_LENGTH_us);

// read the external input level (INPUT_PULLUP, low = trigger active)
// returns true for HIGH, false for LOW; an out-of-range channel defaults to true (deactivated state)
bool ext_trigger_read_in(uint8_t channel);

// read the camera READY feedback level (squid++ dual-camera)
// channel: 0=CAM1, 1=CAM2; out-of-range returns false (default not-ready)
// the HIGH/LOW semantics will be wrapped into cam_is_ready() once the camera scheme is finalized
bool cam_tri_read_ready(uint8_t channel);

#endif // TRIGGER_H
