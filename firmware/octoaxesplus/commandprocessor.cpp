#include "commandprocessor.h"
#include <string.h>  // strcmp
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
// octoaxesplus: 协议 5 对应 W（octoaxes）/ W1（octoaxesplus）；调用方需要做双名兜底
static const char* protocolAxisToName(uint8_t protocolAxis) {
  switch (protocolAxis) {
    case 0: return "X";
    case 1: return "Y";
    case 2: return "Z";
    case 5: return "W";
    case 6: return "W2";
    case 7: return "Turret";   // 2026-06-02 物镜转换器（HOME_OR_ZERO axis=7，复用 octoaxes E1 协议）
    default: return nullptr;
  }
}

// 找轴：先按字面 name 找，若找不到 + name="W"，再 fallback 找 "W1"
// （octoaxes 用 "W"，octoaxesplus 用 "W1"，协议轴码=5 兼容两种命名）
static Axis* findAxisByNameWithFallback(const char* name) {
  if (!name) return nullptr;
  Axis* axis = axisManager.findAxisByName(name);
  if (axis) return axis;
  // W → W1 兜底
  if (strcmp(name, "W") == 0) return axisManager.findAxisByName("W1");
  return nullptr;
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
  // octoaxes 主线轴名 "W"；octoaxesplus 双相机轴名 "W1"（占用同 firmware icID=3）
  // 兼容两种命名（参考 axesmrg.cpp::beginAll 的 "W"/"F1" 双名映射）
  Axis *axis = axisManager.findAxisByName("W");
  if (!axis) axis = axisManager.findAxisByName("W1");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveW Command");
}

void CommandProcessor::handleHomeOrZero(const byte *data) {
  // data[2]: 协议轴值（0=X,1=Y,2=Z,4=XY,5=W,6=W2）
  // data[3]: HOME_POSITIVE=0 (朝+方向), HOME_NEGATIVE=1 (朝-方向), HOME_OR_ZERO_ZERO=2 (仅清零)
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
        Axis *axis = findAxisByNameWithFallback(name);
        if (axis) axis->setCurrentPosition(0.0f);
      }
    }
    return;
  }
  // Homing 模式（HOME_POSITIVE / HOME_NEGATIVE）：
  // 2026-05-11：按协议 data[3] 解析方向，兼容老 Squid software
  //   老 Squid microcontroller.py:88 按 stage_movement_sign_x 派生 data[3]，
  //   老 Squid firmware (main_controller_teensy41.ino:1252) 读 data[3] 决定方向。
  // 之前 octoaxes 忽略 data[3] 仅用 config.homing_direct，在 octoaxes GUI 下行为正确
  // （constants.py 的 sign 与 config.homing_direct 配对一致），但老 Squid software
  // 发的 data[3] 不被解读时 → 方向可能反（X home 朝物理 + 端撞限位）。
  //
  // 兼容策略：用 data[3] 覆盖 config.homing_direct：
  //   HOME_POSITIVE (0) → homing_direct = +1（朝 + 方向）
  //   HOME_NEGATIVE (1) → homing_direct = -1（朝 - 方向）
  // 永久写入 _config，后续 startHoming() 即按新方向走。
  int8_t new_direct = (data[3] == HOME_NEGATIVE) ? -1 : +1;
  if (data[2] == 4) {  // AXES_XY 联合归位
    Axis *axX = axisManager.findAxisByName("X");
    Axis *axY = axisManager.findAxisByName("Y");
    if (axX) {
      axX->getMutableConfig().homing_direct = new_direct;
      axX->startHoming();
    }
    if (axY) {
      axY->getMutableConfig().homing_direct = new_direct;
      axY->startHoming();
    }
  } else {
    const char *name = protocolAxisToName(data[2]);
    if (name) {
      Axis *axis = findAxisByNameWithFallback(name);
      if (axis) {
        axis->getMutableConfig().homing_direct = new_direct;
        axis->startHoming();
      }
    }
  }
}

void CommandProcessor::handleMoveToX(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("X");
  if (axis)
    axis->moveToPositionMicrosteps(absolute_position);

  DEBUG_PRINTLN("Get MoveToX Command");
}

void CommandProcessor::handleMoveToY(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("Y");
  if (axis)
    axis->moveToPositionMicrosteps(absolute_position);

  DEBUG_PRINTLN("Get MoveToY Command");
}

void CommandProcessor::handleMoveToZ(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("Z");
  if (axis)
    axis->moveToPositionMicrosteps(absolute_position);

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
  // 与老 Squid main_controller_teensy41.ino:1529 对齐：不动 illumination_source。
  // 老 Squid 上位机 turn_on_illumination() 命令包 cmd[2]=0；若此处读 data[2] 写 source，
  // 会把 source 强制覆盖为 0 (LED_ARRAY_FULL = 明场)，导致切荧光通道后明场被点亮。
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

void CommandProcessor::handleSetWatchdogTimeout(const byte *data) {
  uint32_t timeout = ((uint32_t)data[2] << 24) | ((uint32_t)data[3] << 16)
                   | ((uint32_t)data[4] << 8)  | (uint32_t)data[5];
  watchdog_set_timeout(timeout);
}

void CommandProcessor::handleHeartbeat(const byte *data) {
  // 空操作：看门狗计时器在收到有效串口消息时已重置
}

void CommandProcessor::handleMoveW2(const byte *data) {
  int32_t relative_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("W2");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveW2 Command");
}

void CommandProcessor::handleMoveTurret(const byte *data) {
  // 2026-06-02 MOVE_TURRET (cmd 44)：物镜转换器相对运动，data[2..5] 为 int32 微步大端序。
  // E1 板未插时 axesmrg::beginAll 已删除该轴 → findAxisByName 返回 nullptr → silent no-op。
  int32_t relative_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("Turret");
  if (axis)
    axis->moveAxis(relative_position);

  DEBUG_PRINTLN("Get MoveTurret Command");
}

void CommandProcessor::handleMoveToTurret(const byte *data) {
  // 2026-06-02 MOVETO_TURRET (cmd 45)：物镜转换器绝对运动。
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("Turret");
  if (axis)
    axis->moveToPositionMicrosteps(absolute_position);

  DEBUG_PRINTLN("Get MoveToTurret Command");
}

void CommandProcessor::handleSetTriggerMode(const byte *data) {
  if (data[2] <= 1)
    trigger_mode = data[2];
}

void CommandProcessor::handleMoveToW(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  // 同 handleMoveW：兼容 W (octoaxes) / W1 (octoaxesplus) 双命名
  Axis *axis = axisManager.findAxisByName("W");
  if (!axis) axis = axisManager.findAxisByName("W1");
  if (axis)
    axis->moveToPositionMicrosteps(absolute_position);

  DEBUG_PRINTLN("Get MoveToW Command");
}

void CommandProcessor::handleMoveToW2(const byte *data) {
  int32_t absolute_position =
      int32_t((uint32_t(data[2]) << 24) + (uint32_t(data[3]) << 16) +
              (uint32_t(data[4]) << 8) + uint32_t(data[5]));
  Axis *axis = axisManager.findAxisByName("W2");
  if (axis)
    axis->moveToPositionMicrosteps(absolute_position);

  DEBUG_PRINTLN("Get MoveToW2 Command");
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
  // 仅对 polarityAffectsChip=true 的轴（=Z）把极性重写进芯片 REFERENCE_CONF——这是「Z 变体软件化」
  // 的关键（上位机按 Z_AXIS_VARIANT 下发极性，切换无需重烧）。X/Y 等固定硬件极性轴只改结构体不碰芯片，
  // 与旧 Squid 固件一致（旧 Squid cmd 20 也只设软件变量），避免旧 Squid 下发的 X/Y 极性误翻芯片。
  if (axis->getConfig().polarityAffectsChip)
    axis->reapplyLimitSwitches();
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

  // Level trigger 模式下，通道已在触发中则丢弃新命令，防止覆盖进行中的时序
  if (trigger_mode != TRIGGER_MODE_NORMAL &&
      trigger_output_level[camera_channel] == LOW) {
    interrupts();
    return;
  }

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
  // 防御：若上位机请求的 pin 在 illumination_init 中未显式 OUTPUT，
  // INPUT 模式下 digitalWrite 不改实际电平。第一次写入时强制配 OUTPUT。
  pinMode(data[2], OUTPUT);
  digitalWrite(data[2], data[3]);
}

void CommandProcessor::handleInitFilterWheel(const byte *data) {
  // 2026-05-26 修复字节级 drop-in 偏差（与 octoaxes 同步）：
  // 旧 Squid callback_initfilterwheel 是原子操作（仅 chip re-init，不 homing）。
  // 之前 octoaxes 在此处 startHoming() 让老 Squid software 的 wait_till_operation_is_completed
  // 在 set_leadscrew_pitch 后 5s 超时（详见 octoaxes/commandprocessor.cpp 完整注释）。
  // 修复：no-op + 日志。W1 已在 startup 配好 filter wheel 模式，homing 由后续 home_w() 单独触发。
  DEBUG_PRINTLN("INITFILTERWHEEL: no-op (W1 configured at startup; awaiting HOME_OR_ZERO for actual homing)");
}

void CommandProcessor::handleInitFilterWheelW2(const byte *data) {
  // 同 handleInitFilterWheel，旧 Squid 协议下仅 chip re-init，不触发 homing。
  DEBUG_PRINTLN("INITFILTERWHEEL_W2: no-op (W2 configured at startup; awaiting HOME_OR_ZERO for actual homing)");
}

void CommandProcessor::handleInitialize(const byte *data) {
  // 对齐老 Squid 行为：cmd 254 INITIALIZE = 等价于"断电再上电"。
  // 老 Squid 在 tmc4361A_tmc2660_init 第一行写 RESET_REG=0x52535400 做 chip 软复位，
  // 然后重写全部配置。这样上位机重启 GUI（chip 不断电）后 XACTUAL/EVENTS/RAMPMODE
  // 等残留状态被清掉，cmd 9 SET_LIM 和 cmd 29 HOME 才能从干净状态开始。
  //
  // Axis::begin() 内 motor_initMotionController 第一行 SW_RESET = 0x52535400 等价。
  // beginAll 后再调 handleReset 重置 C++ 软件状态机（_currentState/_isMoving 等）。
  if (!axisManager.beginAll()) {
    DEBUG_PRINTLN("INITIALIZE: beginAll FAILED");
  }
  uint8_t count = axisManager.getAxisCount();
  for (uint8_t i = 0; i < count; i++) {
    Axis *axis = axisManager.getAxis(i);
    if (axis) axis->handleReset();
  }
  // DAC + trigger 重置
  set_DAC8050x_config();
  set_DAC8050x_default_gain();
  trigger_mode = TRIGGER_MODE_NORMAL;
  DEBUG_PRINTLN("INITIALIZE: chip SW_RESET + reconfig + state machine reset done");
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
