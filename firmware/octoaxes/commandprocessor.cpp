#include "commandprocessor.h"
#include "axesmrg.h"
#include "build_opt.h"

CommandProcessor commandProcessor;

CommandProcessor::CommandProcessor() {
  // 构造函数初始化代码
}

CommandProcessor::~CommandProcessor() {
  // 析构函数清理代码
}

// 以下为各个命令处理函数的实现框架
void CommandProcessor::handleMoveX(const byte *data) {
  // TODO: 实现 MOVE_X 命令处理
  int32_t relative_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("X");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveX Command");
}

void CommandProcessor::handleMoveY(const byte *data) {
  // TODO: 实现 MOVE_Y 命令处理
  int32_t relative_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("Y");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveY Command");
}

void CommandProcessor::handleMoveZ(const byte *data) {
  // TODO: 实现 MOVE_Z 命令处理
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
  // TODO: 实现 MOVETO_X 命令处理
  int32_t obsolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  float obsolute_position_float = float(obsolute_position) / 1000.0;
  Axis *axis = axisManager.findAxisByName("X");
  if (axis)
    axis->moveToPosition(obsolute_position_float);

  DEBUG_PRINTLN("Get MoveToX Command");
}

void CommandProcessor::handleMoveToY(const byte *data) {
  // TODO: 实现 MOVETO_Y 命令处理
  int32_t obsolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  float obsolute_position_float = float(obsolute_position) / 1000.0;
  Axis *axis = axisManager.findAxisByName("Y");
  if (axis)
    axis->moveToPosition(obsolute_position_float);

  DEBUG_PRINTLN("Get MoveToY Command");
}

void CommandProcessor::handleMoveToZ(const byte *data) {
  // TODO: 实现 MOVETO_Z 命令处理
  int32_t obsolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  float obsolute_position_float = float(obsolute_position) / 1000.0;
  Axis *axis = axisManager.findAxisByName("Z");
  if (axis)
    axis->moveToPosition(obsolute_position_float);

  DEBUG_PRINTLN("Get MoveToZ Command");
}

void CommandProcessor::handleSetLim(const byte *data) {
  // TODO: 实现 SET_LIM 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_LIM");
}

void CommandProcessor::handleTurnOnIllumination(const byte *data) {
  // TODO: 实现 TURN_ON_ILLUMINATION 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: TURN_ON_ILLUMINATION");
}

void CommandProcessor::handleTurnOffIllumination(const byte *data) {
  // TODO: 实现 TURN_OFF_ILLUMINATION 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: TURN_OFF_ILLUMINATION");
}

void CommandProcessor::handleSetIllumination(const byte *data) {
  // TODO: 实现 SET_ILLUMINATION 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_ILLUMINATION");
}

void CommandProcessor::handleSetIlluminationLEDMatrix(const byte *data) {
  // TODO: 实现 SET_ILLUMINATION_LED_MATRIX 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_ILLUMINATION_LED_MATRIX");
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
  // TODO: 实现 SET_DAC80508_REFDIV_GAIN 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_DAC80508_REFDIV_GAIN");
}

void CommandProcessor::handleSetIlluminationIntensityFactor(const byte *data) {
  // TODO: 实现 SET_ILLUMINATION_INTENSITY_FACTOR 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: SET_ILLUMINATION_INTENSITY_FACTOR");
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

void CommandProcessor::handleInitialize(const byte *data) {
  // TODO: 实现 INITIALIZE 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: INITIALIZE");
}

void CommandProcessor::handleReset(const byte *data) {
  // TODO: 实现 RESET 命令处理
  DEBUG_PRINTLN("CMD_NOT_IMPLEMENTED: RESET");
}
