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
const int NUM_TRIGGER_CHANNELS = 4;

// Strobe timer interval
const int STROBE_TIMER_INTERVAL_us = 100;

// Camera-trigger pin mapping
const int camera_trigger_pins[NUM_TRIGGER_CHANNELS] = {
    Pins::CAMERA_TRIGGER_1,  // pin 29
    Pins::CAMERA_TRIGGER_2,  // pin 30
    Pins::CAMERA_TRIGGER_3,  // pin 31
    Pins::CAMERA_TRIGGER_4   // pin 32
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

#endif // TRIGGER_H
