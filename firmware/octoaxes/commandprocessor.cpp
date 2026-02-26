#include "commandprocessor.h"
#include "axesmrg.h"
#include "build_opt.h"
#include "illumination.h"
#include "config.h"

CommandProcessor commandProcessor;

CommandProcessor::CommandProcessor() {
  // 构造函数初始化代码
}

CommandProcessor::~CommandProcessor() {
  // 析构函数清理代码
}

// 以下为各个命令处理函数的实现框架
void CommandProcessor::handleMoveX(const byte *data) {
  int32_t relative_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("X");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveX Command");
}

void CommandProcessor::handleMoveY(const byte *data) {
  int32_t relative_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("Y");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveY Command");
}

void CommandProcessor::handleMoveZ(const byte *data) {
  int32_t relative_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("Z");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveZ Command");
}

void CommandProcessor::handleMoveTheta(const byte *data) {
  // TODO: 实现 MOVE_THETA 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: MOVE_THETA");
}

void CommandProcessor::handleMoveW(const byte *data) {
  int32_t relative_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("W");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveW Command");
}

void CommandProcessor::handleHomeOrZero(const byte *data) {
  // TODO: 实现 HOME_OR_ZERO 命令处理
  Axis *axis = axisManager.getAxis(data[2]);
  if (axis)
    axis->startHoming();
}

void CommandProcessor::handleMoveToX(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  float absolute_position_mm = float(absolute_position) / 1000.0f;  // μm → mm
  Axis *axis = axisManager.findAxisByName("X");
  if (axis)
    axis->moveToPosition(absolute_position_mm);

  DEBUG_PRINTLN("Get MoveToX Command");
}

void CommandProcessor::handleMoveToY(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  float absolute_position_mm = float(absolute_position) / 1000.0f;  // μm → mm
  Axis *axis = axisManager.findAxisByName("Y");
  if (axis)
    axis->moveToPosition(absolute_position_mm);

  DEBUG_PRINTLN("Get MoveToY Command");
}

void CommandProcessor::handleMoveToZ(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  float absolute_position_mm = float(absolute_position) / 1000.0f;  // μm → mm
  Axis *axis = axisManager.findAxisByName("Z");
  if (axis)
    axis->moveToPosition(absolute_position_mm);

  DEBUG_PRINTLN("Get MoveToZ Command");
}

void CommandProcessor::handleSetLim(const byte *data) {
  // TODO: 实现 SET_LIM 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_LIM");
}

void CommandProcessor::handleTurnOnIllumination(const byte *data) {
  illumination_source = data[2];
  turn_on_illumination();
}

void CommandProcessor::handleTurnOffIllumination(const byte *data) {
  turn_off_illumination();
}

void CommandProcessor::handleSetIllumination(const byte *data) {
  set_illumination(data[2], (uint16_t(data[3]) << 8) + uint16_t(data[4]));
}

void CommandProcessor::handleSetIlluminationLEDMatrix(const byte *data) {
  set_illumination_led_matrix(data[2], data[3], data[4], data[5]);
}

void CommandProcessor::handleAckJoystickButtonPressed(const byte *data) {
  // TODO: 实现 ACK_JOYSTICK_BUTTON_PRESSED 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: ACK_JOYSTICK_BUTTON_PRESSED");
}

void CommandProcessor::handleAnalogWriteOnboardDAC(const byte *data) {
  // TODO: 实现 ANALOG_WRITE_ONBOARD_DAC 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: ANALOG_WRITE_ONBOARD_DAC");
}

void CommandProcessor::handleSetDAC80508RefDivGain(const byte *data) {
  set_DAC8050x_gain(data[2], data[3]);
}

void CommandProcessor::handleSetIlluminationIntensityFactor(const byte *data) {
  illumination_intensity_factor = float(data[2]) / 100.0f;
}

void CommandProcessor::handleSetPortIntensity(const byte *data) {
  set_port_intensity(data[2], (uint16_t(data[3]) << 8) | uint16_t(data[4]));
}

void CommandProcessor::handleTurnOnPort(const byte *data) {
  turn_on_port(data[2]);
}

void CommandProcessor::handleTurnOffPort(const byte *data) {
  turn_off_port(data[2]);
}

void CommandProcessor::handleSetPortIllumination(const byte *data) {
  set_port_intensity(data[2], (uint16_t(data[3]) << 8) | uint16_t(data[4]));
  if (data[5] != 0) turn_on_port(data[2]);
  else              turn_off_port(data[2]);
}

void CommandProcessor::handleSetMultiPortMask(const byte *data) {
  uint16_t port_mask = (uint16_t(data[2]) << 8) | uint16_t(data[3]);
  uint16_t on_mask   = (uint16_t(data[4]) << 8) | uint16_t(data[5]);
  for (int i = 0; i < IlluminationConfig::NUM_PORTS; i++) {
    if (port_mask & (1 << i)) {
      if (on_mask & (1 << i)) turn_on_port(i);
      else                    turn_off_port(i);
    }
  }
}

void CommandProcessor::handleTurnOffAllPorts(const byte *data) {
  turn_off_all_ports();
}

void CommandProcessor::handleMoveW2(const byte *data) {
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: MOVE_W2");
}

void CommandProcessor::handleSetTriggerMode(const byte *data) {
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_TRIGGER_MODE");
}

void CommandProcessor::handleMoveToW(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  float absolute_position_float = float(absolute_position) / 1000.0;
  Axis *axis = axisManager.findAxisByName("W");
  if (axis)
    axis->moveToPosition(absolute_position_float);

  DEBUG_PRINTLN("Get MoveToW Command");
}

void CommandProcessor::handleSetLimSwitchPolarity(const byte *data) {
  // TODO: 实现 SET_LIM_SWITCH_POLARITY 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_LIM_SWITCH_POLARITY");
}

void CommandProcessor::handleConfigureStepperDriver(const byte *data) {
  // TODO: 实现 CONFIGURE_STEPPER_DRIVER 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: CONFIGURE_STEPPER_DRIVER");
}

void CommandProcessor::handleSetMaxVelocityAcceleration(const byte *data) {
  // TODO: 实现 SET_MAX_VELOCITY_ACCELERATION 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_MAX_VELOCITY_ACCELERATION");
}

void CommandProcessor::handleSetLeadScrewPitch(const byte *data) {
  // TODO: 实现 SET_LEAD_SCREW_PITCH 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_LEAD_SCREW_PITCH");
}

void CommandProcessor::handleSetOffsetVelocity(const byte *data) {
  // TODO: 实现 SET_OFFSET_VELOCITY 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_OFFSET_VELOCITY");
}

void CommandProcessor::handleConfigureStagePID(const byte *data) {
  // TODO: 实现 CONFIGURE_STAGE_PID 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: CONFIGURE_STAGE_PID");
}

void CommandProcessor::handleEnableStagePID(const byte *data) {
  // TODO: 实现 ENABLE_STAGE_PID 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: ENABLE_STAGE_PID");
}

void CommandProcessor::handleDisableStagePID(const byte *data) {
  // TODO: 实现 DISABLE_STAGE_PID 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: DISABLE_STAGE_PID");
}

void CommandProcessor::handleSetHomeSafetyMargin(const byte *data) {
  // TODO: 实现 SET_HOME_SAFETY_MERGIN 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_HOME_SAFETY_MARGIN");
}

void CommandProcessor::handleSetPIDArguments(const byte *data) {
  // TODO: 实现 SET_PID_ARGUMENTS 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_PID_ARGUMENTS");
}

void CommandProcessor::handleSendHardwareTrigger(const byte *data) {
  // TODO: 实现 SEND_HARDWARE_TRIGGER 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SEND_HARDWARE_TRIGGER");
}

void CommandProcessor::handleSetStrobeDelay(const byte *data) {
  // TODO: 实现 SET_STROBE_DELAY 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_STROBE_DELAY");
}

void CommandProcessor::handleSetAxisDisableEnable(const byte *data) {
  // TODO: 实现 SET_AXIS_DISABLE_ENABLE 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_AXIS_DISABLE_ENABLE");
}

void CommandProcessor::handleSetPinLevel(const byte *data) {
  // TODO: 实现 SET_PIN_LEVEL 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_PIN_LEVEL");
}

void CommandProcessor::handleInitFilterWheel(const byte *data) {
  // TODO: 实现 INITFILTERWHEEL 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: INITFILTERWHEEL");
}

void CommandProcessor::handleInitFilterWheelW2(const byte *data) {
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: INITFILTERWHEEL_W2");
}

void CommandProcessor::handleInitialize(const byte *data) {
  // TODO: 实现 INITIALIZE 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: INITIALIZE");
}

void CommandProcessor::handleReset(const byte *data) {
  // TODO: 实现 RESET 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: RESET");
}
