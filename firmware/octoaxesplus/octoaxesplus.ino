#include "axesmrg.h"
#include "build_opt.h"
#include "filterwheel.h"
#include "illumination.h"
#include "joystick.h"
#include "trigger.h"
#include "objectives.h"
#include "serial.h"
#include "stepaxis.h"
#include "tmc/hal/TMC_SPI.h"
#include "tmc/motion/MotorControl.h"
#include "tmc/ic/TMC4361A/TMC4361A.h"
#include "utils.h"
#include "mcp23s17.h"  // squid++: MCP23S17_1 expansion IO (8-axis INTR/TARGET inputs)

void initializeClock(uint8_t clk_pin, uint32_t frequence) {
  pinMode(clk_pin, OUTPUT);
  analogWriteFrequency(clk_pin, frequence);
  analogWrite(clk_pin, 128);
}

void initializeSPIAndPins() {
  // squid++ dual-camera: all SPI device chip-selects go through the 74HC154, no separate pinMode needed
  // call hc154_init() early so illumination_init's DAC communication is available
  // (tmc_spi_init calls it again internally, idempotent)
  Pins::hc154_init();

  // Initialize SPI
  SPI.begin();
  delay(50); // 50ms delay, using explicit time units
}

bool initializePowerManagement() {
  pinMode(Pins::POWER_GOOD, INPUT_PULLUP);

  // the DAC80508_1 chip-select goes through the 74HC154 (Pins::DAC8050x_CS = channel 2), no longer directly driving GPIO

  delay(100);

  // Wait for power to be ready
  unsigned long startTime = millis();
  while (!digitalRead(Pins::POWER_GOOD)) {
    if (millis() - startTime > 5000) { // 5-second timeout
      DEBUG_PRINTLN("Power management initialization timeout");
      return false;
    }
    delay(50);
  }

  return true;
}

bool initializeSystem() {
  // Initialize power management
  if (!initializePowerManagement()) {
    return false;
  }

  // Initialize the clock (squid++ single clock set; EXPAND_CLK removed to avoid sharing pin 28 with TTL5)
  initializeClock(Pins::TMC4361_STANDARD_CLK,
                  SystemConfig::TMC4361_CLOCK_FREQUENCY);

  // Initialize SPI and pins
  initializeSPIAndPins();

  // Initialize the expansion IO (MCP23S17_1, CS via HC154 channel 0; 8-axis INTR/TARGET inputs)
  mcp23s17_init();

  // Initialize the illumination system (pins, LED matrix, DAC, interlock)
  illumination_init();

  // Initialize the trigger system (pins, strobe timer)
  trigger_init();

  // Initialize the new-architecture motion-control subsystem
  motor_initSubsystem();

  // Create axis objects and add them to the manager
  //
  // squid++ dual-camera hardware does not need the octoaxes mainline's X/Y swap:
  // the squid++ HC154 chip-select channel names align with the physical wiring (HC154_AXIS_X=10 directly drives the physical X motor),
  // in tmc_ic_configs[], icID=0 -> HC154_AXIS_Y, icID=1 -> HC154_AXIS_X,
  // so axisName="Y" + icID=0 + Y_AXIS_CS and axisName="X" + icID=1 + X_AXIS_CS is the correct mapping.
  // (the octoaxes mainline swap is to be compatible with the legacy Squid PCB's reversed wiring, see octoaxes/octoaxes.ino)
  //
  // ──────────────────────────────────────────────────────────────────────
  // current mode: XYZW1W2 five axes (since 2026-05-15)
  // ──────────────────────────────────────────────────────────────────────
  // enable the five axes X / Y / Z / W1 / W2:
  // W1 / W2 are filter wheels (FilterWheel); their CS uses the Z2/T channels of the original squid++ 8-axis scheme:
  // W1: HC154 channel 6 (original AXIS_Z2 resource)
  // W2: HC154 channel 4 (original AXIS_T resource)
  // the Z axis uses axisName "Z" (consistent with the host; axesmrg.cpp::beginAll supports both "Z"/"Z1").
  // ──────────────────────────────────────────────────────────────────────
  Axis *yAxis  = new StepAxis    (Pins::Y_AXIS_CS,  0, "Y");    // icID=0, HC154 ch9  = physical Y motor
  Axis *xAxis  = new StepAxis    (Pins::X_AXIS_CS,  1, "X");    // icID=1, HC154 ch10 = physical X motor
  Axis *zAxis  = new StepAxis    (Pins::Z_AXIS_CS,  2, "Z");    // icID=2, HC154 ch8  = main focus Z
  Axis *w1Axis = new FilterWheel (Pins::W1_AXIS_CS, 3, "W1");   // icID=3, HC154 ch6  = filter wheel 1
  Axis *w2Axis = new FilterWheel (Pins::W2_AXIS_CS, 4, "W2");   // icID=4, HC154 ch4  = filter wheel 2
  // 2026-06-02 E1 objective turret (4 objectives): CS=R_AXIS_CS (HC154 ch3), reusing the octoaxes E1 protocol
  // (axisName="Turret" + MOVE_TURRET=44/MOVETO_TURRET=45 + protocol axis code 7). beginAll uses the EXPAND1_AXIS template.
  Axis *turretAxis = new Objectives  (Pins::R_AXIS_CS, 5, "Turret", 4);  // icID=5, HC154 ch3 = objective turret

  // add in axisIndex order; the order must match the HC154 branch of tmc_ic_configs[] in tmc/hal/TMC_SPI.cpp
  // the tmc_ic_configs[] array keeps 8 entries (icID 6-7 slots are empty but never accessed, no side effects)
  if (!axisManager.addAxis(yAxis)  || !axisManager.addAxis(xAxis)  ||
      !axisManager.addAxis(zAxis)  || !axisManager.addAxis(w1Axis) ||
      !axisManager.addAxis(w2Axis) || !axisManager.addAxis(turretAxis)) {
    DEBUG_PRINTLN("Failed to add axes to manager");
    return false;
  }

  // Initialize all axes
  // Note: beginAll() returning false means **at least one axis failed begin** (typical case:
  // TMC4361A SPI not responding, so after motor_initMotionController writes SW_RESET, reading
  // VERSION_NO returns 0/-1). **No longer treated as fatal** -- serial communication and debug commands
  // (S:VERSION / S:HWINFO / S:SPITEST / S:DUMPREGS) must remain available, otherwise
  // the SPI failure root cause cannot be located during bring-up. The failed axis is already identified by axis.cpp's
  // DEBUG_PRINT(_axisName + ":BEGIN_FAIL ...") printed to the serial port.
  if (!axisManager.beginAll()) {
    DEBUG_PRINTLN("WARNING: beginAll() reported partial axis failure (see :BEGIN_FAIL above). Continuing so serial diagnostics remain available.");
  }

  // Initialize the hand controller (Serial5 + PacketSerial)
  joystick_init();

  return true;
}

void setup() {
  // Initialize the serial port
  serialProtocol.begin(115200, 300);

  // Initialize the status indicator LED
  initializeStartupLED();

  // clear the APA102 matrix as early as possible to minimize the "startup glow" window.
  // the subsequent initializePowerManagement (waiting for PG) + delay + clock + SPI init
  // may total hundreds of ms to 5s, during which the APA102 stays in its power-on default lit state.
  illumination_init_matrix_early();

  DEBUG_PRINTLN("Initializing system...");

  // Initialize the system
  if (!initializeSystem()) {
    DEBUG_PRINTLN("System initialization failed!");
    while (1) {
      delay(1000); // halt execution
    }
  }

  DEBUG_PRINTLN("System initialized successfully");
}

void loop() {
  static bool firstLoop = true;
  if (firstLoop) {
    DEBUG_PRINTLN("MAIN_LOOP_ENTERED");  // confirm entry into the main loop
    firstLoop = false;
  }

  // Safety interlock check: when the interlock opens, directly pull all TTL laser ports low (hardcoded GPIO, zero overhead)
  // squid++ dual-camera: D1-D8, 8 TTL lines total
  if (!illumination_interlock_ok()) {
    digitalWrite(Pins::ILLUMINATION_D1, LOW);
    digitalWrite(Pins::ILLUMINATION_D2, LOW);
    digitalWrite(Pins::ILLUMINATION_D3, LOW);
    digitalWrite(Pins::ILLUMINATION_D4, LOW);
    digitalWrite(Pins::ILLUMINATION_D5, LOW);
    digitalWrite(Pins::ILLUMINATION_D6, LOW);
    digitalWrite(Pins::ILLUMINATION_D7, LOW);
    digitalWrite(Pins::ILLUMINATION_D8, LOW);
  }

  // Serial watchdog: automatically turn off all illumination after a communication-loss timeout
  watchdog_check();

  // Update trigger-pulse recovery
  trigger_update();

  // main-loop hook: one-time DAC fallback sync (verified during ttl_test bring-up)
  illumination_update();

  // Process serial debug commands
  serialProtocol.processSerialCommands();

  // 10ms periodic position reporting (compatible with the legacy Squid protocol)
  serialProtocol.send_position_update();

  // Update the hand controller (PacketSerial receive + joystick/focus-wheel control)
  joystick_update();

  // Update all axis state machines
  axisManager.updateAll();

}
