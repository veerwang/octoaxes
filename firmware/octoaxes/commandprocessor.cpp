#include "commandprocessor.h"
#include "axesmrg.h"
#include "build_opt.h"
#include "illumination.h"
#include "trigger.h"
#include "config.h"

// 协议常量（来自 Squid constants_protocol.h）
static const int HOME_POSITIVE     = 0;
static const int HOME_NEGATIVE     = 1;
static const int HOME_OR_ZERO_ZERO = 2;

// SET_LIM 限位码
static const int LIM_CODE_X_POSITIVE = 0;
static const int LIM_CODE_X_NEGATIVE = 1;
static const int LIM_CODE_Y_POSITIVE = 2;
static const int LIM_CODE_Y_NEGATIVE = 3;
static const int LIM_CODE_Z_POSITIVE = 4;
static const int LIM_CODE_Z_NEGATIVE = 5;

// 限位开关极性
static const int POLARITY_ACTIVE_LOW  = 0;
static const int POLARITY_ACTIVE_HIGH = 1;
static const int POLARITY_DISABLED    = 2;

// 偏移速度（与旧架构 globals.cpp 一致）
// enable_offset_velocity 已在 def_octopi_80120.h 中定义
float offset_velocity_x = 0;
float offset_velocity_y = 0;

// 协议轴值 → 轴名称（nullptr = 无效轴）
static const char* protocolAxisToName(uint8_t protocolAxis) {
  switch (protocolAxis) {
    case 0: return "X";
    case 1: return "Y";
    case 2: return "Z";
    case 5: return "W";
    case 6: return "W2";
    default: return nullptr;
  }
}

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
  // data[2]: 协议轴值（0=X,1=Y,2=Z,4=XY,5=W,6=W2）
  // data[3]: HOME_POSITIVE=0, HOME_NEGATIVE=1, HOME_OR_ZERO_ZERO=2
  if (data[3] == HOME_OR_ZERO_ZERO) {
    // 归零模式：将当前位置设为 0，不移动
    if (data[2] == 4) {  // AXES_XY 联合
      Axis *axX = axisManager.findAxisByName("X");
      Axis *axY = axisManager.findAxisByName("Y");
      if (axX) axX->setCurrentPosition(0.0f);
      if (axY) axY->setCurrentPosition(0.0f);
    } else {
      const char *name = protocolAxisToName(data[2]);
      if (name) {
        Axis *axis = axisManager.findAxisByName(name);
        if (axis) axis->setCurrentPosition(0.0f);
      }
    }
    return;
  }
  // Homing 模式（HOME_POSITIVE / HOME_NEGATIVE）
  // 方向由各轴 homing_direct 配置决定，忽略 data[3]
  if (data[2] == 4) {  // AXES_XY 联合归位
    Axis *axX = axisManager.findAxisByName("X");
    Axis *axY = axisManager.findAxisByName("Y");
    if (axX) axX->startHoming();
    if (axY) axY->startHoming();
  } else {
    const char *name = protocolAxisToName(data[2]);
    if (name) {
      Axis *axis = axisManager.findAxisByName(name);
      if (axis) axis->startHoming();
    }
  }
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
  // data[2]: LIM_CODE (0-5), data[3..6]: 限位值 (微步, 32bit 大端序)
  int32_t value = int32_t((uint32_t(data[3]) << 24) | (uint32_t(data[4]) << 16) |
                          (uint32_t(data[5]) << 8)  |  uint32_t(data[6]));
  const char *axisName = nullptr;
  int direction = 0;
  switch (data[2]) {
    case LIM_CODE_X_POSITIVE: axisName = "X"; direction =  1; break;
    case LIM_CODE_X_NEGATIVE: axisName = "X"; direction = -1; break;
    case LIM_CODE_Y_POSITIVE: axisName = "Y"; direction =  1; break;
    case LIM_CODE_Y_NEGATIVE: axisName = "Y"; direction = -1; break;
    case LIM_CODE_Z_POSITIVE: axisName = "Z"; direction =  1; break;
    case LIM_CODE_Z_NEGATIVE: axisName = "Z"; direction = -1; break;
    default: return;
  }
  Axis *axis = axisManager.findAxisByName(axisName);
  if (axis)
    axis->setOneSoftLimit(direction, value);
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
  joystick_button_pressed = false;
}

void CommandProcessor::handleAnalogWriteOnboardDAC(const byte *data) {
  int channel = data[2];
  uint16_t value = (uint16_t(data[3]) << 8) | uint16_t(data[4]);
  set_DAC8050x_output(channel, value);
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
  if (data[2] <= 1)
    trigger_mode = data[2];
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
  // data[2]: 协议轴; data[3]: 极性 (0=ACTIVE_LOW, 1=ACTIVE_HIGH, 2=DISABLED)
  if (data[3] == POLARITY_DISABLED)
    return;
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (!axis) return;
  uint8_t polarity = data[3];
  axis->getMutableConfig().leftSwitchPolarity = polarity;
  axis->getMutableConfig().rightSwitchPolarity = polarity;
}

void CommandProcessor::handleConfigureStepperDriver(const byte *data) {
  // data[2]: 协议轴; data[3]: 微步; data[4..5]: RMS 电流 (mA); data[6]: 保持电流 (0-255)
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (!axis) return;

  // 微步特殊处理: 0→1, 1-128→原值, >128→256
  int microstepping = data[3];
  if (microstepping > 128)
    microstepping = 256;
  if (microstepping == 0)
    microstepping = 1;

  float currentMA = float((uint16_t(data[4]) << 8) | uint16_t(data[5]));
  float holdRatio = float(data[6]) / 255.0f;

  axis->configureDriver((uint16_t)microstepping, currentMA, holdRatio);
}

void CommandProcessor::handleSetMaxVelocityAcceleration(const byte *data) {
  // data[2]: 协议轴; data[3:4]: 速度×100 (mm/s); data[5:6]: 加速度×10 (mm/s²)
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (!axis) return;
  float vel_mm = float((uint16_t(data[3]) << 8) | data[4]) / 100.0f;
  float acc_mm = float((uint16_t(data[5]) << 8) | data[6]) / 10.0f;
  axis->setMotionParameters(vel_mm, acc_mm);
}

void CommandProcessor::handleSetLeadScrewPitch(const byte *data) {
  // data[2]: 协议轴; data[3..4]: 螺距×1000 (uint16, mm)
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (!axis) return;

  float pitchMM = float((uint16_t(data[3]) << 8) | uint16_t(data[4])) / 1000.0f;
  axis->setLeadScrewPitch(pitchMM);
}

void CommandProcessor::handleSetOffsetVelocity(const byte *data) {
  // 与旧架构 callback_set_offset_velocity 一致：
  // 仅在 enable_offset_velocity 为 true 时存储值，供摇杆循环使用
  if (!enable_offset_velocity) return;

  // data[3..6]: int32 大端序 (μm/s), ÷1000000 → mm/s
  float velocityMM =
      float(int32_t(uint32_t(data[3]) << 24 | uint32_t(data[4]) << 16 |
                    uint32_t(data[5]) << 8 | uint32_t(data[6]))) /
      1000000.0f;

  switch (data[2]) {
    case 0: offset_velocity_x = velocityMM; break;  // AXIS_X
    case 1: offset_velocity_y = velocityMM; break;  // AXIS_Y
  }
}

void CommandProcessor::handleConfigureStagePID(const byte *data) {
  // data[2]: 协议轴; data[3]: flip_direction; data[4:5]: transitions_per_rev (大端序)
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (!axis) return;
  bool flip_direction = data[3];
  uint16_t transitions_per_rev = (uint16_t(data[4]) << 8) | uint16_t(data[5]);
  axis->configureStagePID(flip_direction, transitions_per_rev);
}

void CommandProcessor::handleEnableStagePID(const byte *data) {
  // data[2]: 协议轴
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (axis) axis->enableStagePID();
}

void CommandProcessor::handleDisableStagePID(const byte *data) {
  // data[2]: 协议轴
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (axis) axis->disableStagePID();
}

void CommandProcessor::handleSetHomeSafetyMargin(const byte *data) {
  // data[2]: 协议轴; data[3..4]: 裕量×1000 (uint16, mm)
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (!axis) return;

  float marginMM = float((uint16_t(data[3]) << 8) | uint16_t(data[4])) / 1000.0f;
  axis->setHomeSafetyMargin(marginMM);
}

void CommandProcessor::handleSetPIDArguments(const byte *data) {
  // data[2]: 协议轴; data[3:4]: P (大端序 uint16); data[5]: I (uint8); data[6]: D (uint8)
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (!axis) return;
  uint16_t p = (uint16_t(data[3]) << 8) | uint16_t(data[4]);
  uint8_t  i = data[5];
  uint8_t  d = data[6];
  axis->setPIDArguments(p, i, d);
}

void CommandProcessor::handleSendHardwareTrigger(const byte *data) {
  int camera_channel = data[2] & 0x0F;
  if (camera_channel >= NUM_TRIGGER_CHANNELS)
    return;

  noInterrupts();

  control_strobe[camera_channel] = (data[2] >> 7) & 0x01;
  illumination_on_time_us[camera_channel] =
      (uint32_t(data[3]) << 24) | (uint32_t(data[4]) << 16) |
      (uint32_t(data[5]) << 8)  |  uint32_t(data[6]);

  // 触发引脚拉 LOW（负脉冲起始）
  digitalWrite(camera_trigger_pins[camera_channel], LOW);
  trigger_output_level[camera_channel] = LOW;
  timestamp_trigger_rising_edge[camera_channel] = micros();

  // 频闪状态重置
  strobe_on[camera_channel] = false;

  interrupts();
}

void CommandProcessor::handleSetStrobeDelay(const byte *data) {
  int channel = data[2];
  if (channel >= NUM_TRIGGER_CHANNELS)
    return;
  strobe_delay_us[channel] =
      (uint32_t(data[3]) << 24) | (uint32_t(data[4]) << 16) |
      (uint32_t(data[5]) << 8)  |  uint32_t(data[6]);
}

void CommandProcessor::handleSetAxisDisableEnable(const byte *data) {
  // data[2]: 协议轴; data[3]: 0=禁用, 1=启用
  const char *name = protocolAxisToName(data[2]);
  if (!name) return;
  Axis *axis = axisManager.findAxisByName(name);
  if (!axis) return;
  if (data[3] == 0) axis->disableAxis();
  else              axis->enableAxis();
}

void CommandProcessor::handleSetPinLevel(const byte *data) {
  digitalWrite(data[2], data[3]);
}

void CommandProcessor::handleInitFilterWheel(const byte *data) {
  // 触发 W 轴（滤光轮）延迟初始化 homing
  Axis *axis = axisManager.findAxisByName("W");
  if (axis) {
    axis->startHoming();
    DEBUG_PRINTLN("INITFILTERWHEEL: W homing started");
  }
}

void CommandProcessor::handleInitFilterWheelW2(const byte *data) {
  // W2 轴当前未启用，忽略
  Axis *axis = axisManager.findAxisByName("W2");
  if (axis) {
    axis->startHoming();
    DEBUG_PRINTLN("INITFILTERWHEEL_W2: W2 homing started");
  }
}

void CommandProcessor::handleInitialize(const byte *data) {
  // 重新初始化 DAC 和触发系统；TMC 轴已在 setup 中初始化，不重复
  set_DAC8050x_config();
  set_DAC8050x_default_gain();
  trigger_mode = TRIGGER_MODE_NORMAL;
  DEBUG_PRINTLN("INITIALIZE: DAC + trigger_mode reset");
}

void CommandProcessor::handleReset(const byte *data) {
  // 停止所有轴运动，复位触发状态
  trigger_mode = TRIGGER_MODE_NORMAL;
  uint8_t count = axisManager.getAxisCount();
  for (uint8_t i = 0; i < count; i++) {
    Axis *axis = axisManager.getAxis(i);
    if (axis) axis->handleReset();
  }
  DEBUG_PRINTLN("RESET: all axes stopped, trigger_mode = 0");
}
