#include "axis.h"
#include "build_opt.h"
#include "tmc/ic/TMC4361A/TMC4361A.h"
#include <SPI.h>

static inline int sgn(int val) {
  if (val < 0)
    return -1;
  if (val == 0)
    return 0;
  return 1;
}

// 构造函数
Axis::Axis(uint8_t csPin, uint8_t axisIndex, const char *axisName)
    : _csPin(csPin), _axisIndex(axisIndex), _axisName(axisName) {

  _currentState = STATE_IDLE;
  _previousState = STATE_IDLE;
  _stateStartTime = 0;
  _homeFound = false;

  _maxVelocityMicrosteps = 0;
  _maxAccelerationMicrosteps = 0;

  // 新架构: 使用 axisIndex 作为 IC 标识符
  _icID = axisIndex;

  // 新增：初始化状态变化检测
  _lastReportedState = STATE_IDLE;
  _stateChanged = false;
  _lastStateReportTime = 0;

  // 新增：初始化移动状态
  _isMoving = false;
  _lastPosition = 0;
  _moveDirection = 0;
  _softLimitsEnabled = false;
  _needReenableLimits = false;

  // 初始化配置结构体
  memset(&_config, 0, sizeof(_config));
}

// 初始化函数
bool Axis::begin(const AxisConfig &config) {
  _config = config;

  // HOME timeout ms
  _homing_timeout_ms = _config.homing_timeout_ms;

  // 配置CS引脚
  pinMode(_csPin, OUTPUT);
  digitalWrite(_csPin, HIGH);

  // ========== 新架构初始化 ==========
  // 设置驱动类型（DRIVER_AUTO 时由 motor_initMotionController 自动检测）
  motorParams[_icID].driverType = _config.driverType;

  // 初始化运动参数缓存 (用于新 API 的单位转换)
  MotionConfig motionConfig = {
      .clockFrequency = _config.clockFrequency,
      .screwPitchMM = _config.screwPitchMM,
      .fullStepsPerRev = (uint16_t)_config.fullStepsPerRev,
      .microsteps = (uint16_t)_config.microstepping,
      .maxVelocityMM = _config.maxVelocityMM,
      .maxAccelerationMM = _config.maxAccelerationMM,
      .maxDecelerationMM = _config.maxAccelerationMM,
      .useSShapedRamp = _config.useSShapedRamp,
      .astartMM = _config.astartMM,
      .dfinalMM = _config.dfinalMM,
      .bow1 = 0,
      .bow2 = 0,
      .bow3 = 0,
      .bow4 = 0};
  motor_initMotionController(_icID, &motionConfig);

  // 自动检测完成后，回写实际驱动类型
  if (_config.driverType == DRIVER_AUTO) {
    _config.driverType = motorParams[_icID].driverType;
  }

  // 初始化驱动器配置
  MotorConfig motorConfig = {
      .driverType = _config.driverType,
      .rSense = _config.r_sense,
      .runCurrentMA = _config.motorCurrentMA,
      .holdCurrentRatio = _config.holdCurrent,
      .microstepRes = 0,  // 256 microsteps
      .interpolation = true,
      .toff = 3,   // TOFF = 3
      .hstrt = 4,  // HSTRT = 4
      .hend = -2,  // HEND 寄存器值 = 1, 实际值 = 1 + (-3) = -2
      .tbl = 2,    // TBL = 2
      .stallThreshold = (int8_t)_config.stallSensitivity,
      .stallFilter = true,
      .enableStealthChop = false,
      .globalScaler = 0,   // 全量程 (256)
      .iholdDelay = 7,
      .currentRange = _config.currentRange};
  motor_initDriver(_icID, &motorConfig);

  // 配置限位开关
  LimitConfig limitConfig = {
      .enableLeft = _config.enableLeftLimitSwitch,
      .enableRight = _config.enableRightLimitSwitch,
      .leftPolarity = _config.leftSwitchPolarity,
      .rightPolarity = _config.rightSwitchPolarity,
      .leftFlipped = _config.leftFlipped,
      .rightFlipped = _config.rightFlipped,
      .homingSwitch = _config.homingSwitch,
      .homeSafetyMarginMM = _config.homeSafetyMarginMM};
  motor_configLimitSwitches(_icID, &limitConfig);

  // 设置运动参数
  setMotionParameters(_config.maxVelocityMM, _config.maxAccelerationMM);

  // 使能归位限位
  motor_enableHomingLimit(_icID, _config.rightSwitchPolarity,
                          _config.homingSwitch,
                          mmToMicrosteps(_config.homeSafetyMarginMM));

  // 禁用虚拟限位开关（初始状态）
  enableSoftLimits(false);

  // 编码器初始化
  if (_config.enableEncoder && _config.encoderLinesPerRev > 0) {
    uint32_t transitions = (uint32_t)_config.encoderLinesPerRev;
    motor_initABNEncoder(_icID, transitions,
                          32,    // filter_wait_time
                          4,     // filter_exponent
                          512,   // filter_vmean
                          _config.invertEncoderDir);
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":ENCODER_INIT lines=");
    DEBUG_PRINT(_config.encoderLinesPerRev);
    DEBUG_PRINT(" transitions=");
    DEBUG_PRINTLN(transitions);
  }

  // 禁用 PID (使用新 API)
  motor_disablePID(_icID);

  // 配置 StallGuard (使用新 API)
  if (_config.enableStallSensitivity)
    motor_configStallGuard(_icID, _config.stallSensitivity, true, true);

  // 默认使能轴
  enableAxis();

  return true;
}

// 设置运动参数 (使用新 API)
void Axis::setMotionParameters(float maxVelocityMM, float maxAccelerationMM) {
  _maxVelocityMicrosteps = motor_velocityMMToInternal(_icID, maxVelocityMM);
  _maxAccelerationMicrosteps = motor_accelMMToInternal(_icID, maxAccelerationMM);

  motor_setMaxVelocity(_icID, maxVelocityMM);
  motor_setMaxAcceleration(_icID, maxAccelerationMM);
}

// 状态机更新
void Axis::update() {
  // 保存旧状态用于比较
  AxisState oldState = _currentState;

  switch (_currentState) {
  case STATE_HOMING_INIT:
  case STATE_HOMING_SEARCH:
  case STATE_HOMING_SET_ZERO:
    performHomingSequence();
    break;

  case STATE_LEAVING_HOME:
    performLeavingHome();
    break;

  case STATE_MOVING: {
    checkMovementComplete();

    // 极限状态检测
    checkLimitPosition();

    // VSTOP recovery 后延迟恢复虚拟限位：
    // 等电机离开边界（STATUS 中 VSTOP flags 清除）后才重新使能限位，
    // 避免在边界上立即重新触发 VSTOP。
    if (_needReenableLimits) {
      uint32_t st = motor_readStatus(_icID);
      bool vstopStillActive = (st & TMC4361A_VSTOPL_ACTIVE_F_MASK) ||
                              (st & TMC4361A_VSTOPR_ACTIVE_F_MASK);
      if (!vstopStillActive) {
        motor_enableSoftLimits(_icID, true, true);
        _needReenableLimits = false;
      }
    }

    // 移动状态下的超时检查
    if (checkTimeout(MOVEMENT_TIMEOUT_MS)) {
      handleError("Movement timeout");
    }
  } break;

  case STATE_IDLE:
    // 空闲状态不需要特殊处理
    break;

  case STATE_ERROR:
    // 错误状态需要外部干预
    break;
  }

  // 检查状态是否发生变化
  if (oldState != _currentState) {
    _stateChanged = true;
  }

  // 上报状态变化（如果需要）
  reportStateIfChanged();
}

// 新增：状态上报函数
void Axis::reportStateIfChanged(bool force) {
  // 检查是否需要上报
  bool shouldReport = false;

  if (force) {
    // 强制上报
    shouldReport = true;
  } else if (_stateChanged) {
    // 状态发生变化
    shouldReport = true;
  } else if (_currentState == STATE_MOVING) {
  } else if (_currentState == STATE_HOMING_INIT ||
             _currentState == STATE_HOMING_SEARCH ||
             _currentState == STATE_HOMING_SET_ZERO ||
             _currentState == STATE_LEAVING_HOME) {
  } else {
  }

  if (shouldReport) {
    handleEmergency();
    _stateChanged = false;
    _lastStateReportTime = millis();
    _lastReportedState = _currentState;
  }
}

// 极限位的处理函数
void Axis::checkLimitPosition() {
  uint32_t event = readAxisEvent();

  // 虚拟限位（软件限位）：不检查方向，因为 TMC4361A 硬件已保证
  // VSTOPR 仅在正向越界时触发，VSTOPL 仅在负向越界时触发。
  // 移除方向检查可避免 recovery 路径重新触发 VSTOP 后方向不匹配的问题。
  uint32_t vstop_bits =
      event & (TMC4361A_VSTOPL_ACTIVE_MASK | TMC4361A_VSTOPR_ACTIVE_MASK);
  if (vstop_bits) {
    DEBUG_PRINT("Software Limit Stop: event=0x");
    DEBUG_PRINTLNF(event, HEX);
    completeMovement();
    return;
  }

  // 硬件限位（保留方向检查，硬件限位需要方向匹配）
  uint32_t hw_datagram = event & (TMC4361A_STOPL_EVENT_MASK | TMC4361A_STOPR_EVENT_MASK);
  hw_datagram >>= TMC4361A_STOPL_EVENT_SHIFT;
  uint8_t hw_result = hw_datagram & 0xff;

  if ((hw_result == RGHT_SW && _moveDirection == RGHT_DIR) ||
      (hw_result == LEFT_SW && _moveDirection == LEFT_DIR)) {
    DEBUG_PRINT("Hardware Limit Stop: ");
    DEBUG_PRINTLN(hw_result);
    completeMovement();
    return;
  }

  // 判断是否是stall状态
  if (event & 0x20000000) {
    DEBUG_PRINTLN("Axis Is Stop for Stalling");
    DEBUG_PRINTLNF(event, HEX);
  } else {
    if (event != 0) {
      DEBUG_PRINT("Axis Event is not Zero: ");
      DEBUG_PRINTLNF(event, HEX);
    }
  }
}

// 命令处理
bool Axis::processCommand(const String &command) {
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":CMD_RECV:");
  DEBUG_PRINTLN(command);  // 调试点0 - 命令接收

  if (command.startsWith("GET_POSITION")) {
    return handleGetPosition();
  } else if (command.startsWith("SET_LIMITS")) {
    return handleSetLimits(command);
  } else if (command.startsWith("MOVE_AXIS")) {
    return handleMoveAxis(command);
  } else if (command.startsWith("MOVETO_AXIS")) {
    return handleMoveToAxis(command);
  } else if (command.startsWith("HOMING")) {
    return handleHoming();
  } else if (command.startsWith("GET_DATA")) {
    return handleGetData();
  } else if (command.startsWith("DISABLE")) {
    return handleAxisAbilityToggle(false);
  } else if (command.startsWith("ENABLE")) {
    return handleAxisAbilityToggle(true);
  } else if (command.startsWith("RESET")) {
    return handleReset();
  } else if (command.startsWith("DEBUG_REG")) {
    return handleDebugReg();
  } else {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Unknown command: ");
    DEBUG_PRINTLN(command);
    return false;
  }
}

// 命令处理辅助方法
bool Axis::handleGetPosition() {
  int32_t microsteps = getCurrentPosition();
  [[maybe_unused]] float positionMM = microstepsToMM(microsteps);
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Current Position (mm):");
  DEBUG_PRINTLNF(positionMM, 3); // 增加精度显示
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Current Position (microsteps):");
  DEBUG_PRINTLN(microsteps);
  return true;
}

int32_t Axis::hexStringToInt32(String hex) {
  char *endptr;
  uint32_t value = strtoul(hex.c_str(), &endptr, 16);
  return (int32_t)value;
}

bool Axis::moveAxis(int32_t value) {
  _cmdRecvMicros = micros();
  _moveDirection = sgn(value);

  if (!moveRelativeMicrosteps(value)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVE_AXIS ERROR: Movement failed");
    return false;
  } else {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":MOVE_AXIS (usteps): ");
    DEBUG_PRINTLN(value);
  }
  return true;
}

bool Axis::handleMoveAxis(const String &command) {
  int space1 = command.indexOf(' ');
  int space2 = command.indexOf(' ', space1 + 1);

  if (space1 == -1 || space2 == -1) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVE_AXIS ERROR: Invalid format");
    return false;
  }
  String dataType = command.substring(space1 + 1, space2);
  String hexData = command.substring(space2 + 1);

  int32_t value = hexStringToInt32(hexData);

  return moveAxis(value);
}

bool Axis::handleMoveToAxis(const String &command) {
  int space1 = command.indexOf(' ');
  int space2 = command.indexOf(' ', space1 + 1);

  if (space1 == -1 || space2 == -1) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVETO_AXIS ERROR: Invalid format");
    return false;
  }
  String dataType = command.substring(space1 + 1, space2);
  String hexData = command.substring(space2 + 1);

  int32_t value = hexStringToInt32(hexData);
  _moveDirection = sgn(value);

  if (!moveToPositionMicrosteps(value)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVETO_AXIS ERROR: Movement failed");
    return false;
  }

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":MOVETO_AXIS (usteps): ");
  DEBUG_PRINTLN(value);
  return true;
}

// 新增：移动状态检测函数 (使用新 API)
void Axis::checkMovementComplete() {
  if (!_isMoving)
    return;

  int32_t currentPos = motor_getPositionMicrosteps(_icID);
  int32_t targetPos = motor_getTargetMicrosteps(_icID);

  // 检查是否到达目标位置
  if (currentPos == targetPos) {
    completeMovement();
  } else {
    // 更新最后位置，用于后续的移动检测
    _lastPosition = currentPos;
  }
}

// 新增：开始移动 (使用新 API)
void Axis::startMovement() {
  _isMoving = true;
  _moveStartMicros = micros();
  _lastPosition = motor_getPositionMicrosteps(_icID);
  setState(STATE_MOVING);
}

// 新增：完成移动
void Axis::completeMovement() {
  unsigned long now = micros();
  [[maybe_unused]] unsigned long motorTime = now - _moveStartMicros;
  [[maybe_unused]] unsigned long totalTime = now - _cmdRecvMicros;
  [[maybe_unused]] unsigned long prepTime = _moveStartMicros - _cmdRecvMicros;
  [[maybe_unused]] int32_t endPos = motor_getPositionMicrosteps(_icID);
  [[maybe_unused]] int32_t targetPos = motor_getTargetMicrosteps(_icID);
  _isMoving = false;
  setState(STATE_IDLE);

  // 移动完成时恢复虚拟限位（如果 recovery 路径禁用了限位且 update 循环未及时恢复）
  if (_needReenableLimits && _softLimitsEnabled) {
    motor_enableSoftLimits(_icID, true, true);
    _needReenableLimits = false;
  }

  // 格式: DONE: total=Xus prep=Yus motor=Zus pos=N tgt=N err=N
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":DONE: total=");
  DEBUG_PRINT(totalTime);
  DEBUG_PRINT("us prep=");
  DEBUG_PRINT(prepTime);
  DEBUG_PRINT("us motor=");
  DEBUG_PRINT(motorTime);
  DEBUG_PRINT("us pos=");
  DEBUG_PRINT(endPos);
  DEBUG_PRINT(" tgt=");
  DEBUG_PRINT(targetPos);
  DEBUG_PRINT(" err=");
  DEBUG_PRINTLN(endPos - targetPos);
}

bool Axis::handleHoming() {
  if (!startHoming()) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":HOMING ERROR: Already in progress or busy");
    return false;
  }

  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":Received HOME command, starting homing process...");
  return true;
}

bool Axis::handleReset() {
  _isMoving = false;

  // 恢复细分（可能在 homing 中被改变）
  restoreNormalMicrosteps();

  // 清除状态
  readLimitSwitches();
  readSwitchEvent();

  // 重置 RAMPMODE（硬件限位触发后可能变成 HOLD 模式）
  motor_resetRampMode(_icID);

  // 恢复运动参数（VMAX/AMAX 可能被 stop 清零）
  setMotionParameters(_config.maxVelocityMM, _config.maxAccelerationMM);

  setState(STATE_IDLE);

  // 立即上报状态
  reportStateIfChanged(true);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":Received RESET command, starting reset process...");
  return true;
}

// 移动到绝对位置（微步单位，协议层入口）
bool Axis::moveToPositionMicrosteps(int32_t targetMicrosteps) {
  // 自动从错误状态恢复（虚拟限位超时等非硬件故障）
  if (_currentState == STATE_ERROR) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Auto-recovery from error state");
    handleReset();
  }

  if (_currentState != STATE_IDLE) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Movement rejected: Axis is busy, current state: ");
    DEBUG_PRINTLN(_currentState);
    return false;
  }

  if (!isWithinSoftLimits(targetMicrosteps)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Movement rejected: Outside soft limits");
    return false;
  }

  // 检查是否需要 VSTOP recovery（motor_moveToMicrosteps 会禁用限位）
  uint32_t preStatus = motor_readStatus(_icID);
  bool vstopWasActive = (preStatus & TMC4361A_VSTOPL_ACTIVE_F_MASK) ||
                        (preStatus & TMC4361A_VSTOPR_ACTIVE_F_MASK);

  motor_moveToMicrosteps(_icID, targetMicrosteps);
  startMovement(); // 设置移动状态

  if (vstopWasActive && _softLimitsEnabled) {
    _needReenableLimits = true;
  }

  return true;
}

// 移动到绝对位置（mm 单位，薄包装）
bool Axis::moveToPosition(float positionMM) {
  if (!isValidPosition(positionMM)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Movement rejected: Invalid position");
    DEBUG_PRINTLN(positionMM);
    return false;
  }
  return moveToPositionMicrosteps(motor_mmToMicrosteps(_icID, positionMM));
}

// 相对移动（微步单位，协议层入口）
bool Axis::moveRelativeMicrosteps(int32_t deltaMicrosteps) {
  // 自动从错误状态恢复
  if (_currentState == STATE_ERROR) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Auto-recovery from error state");
    handleReset();
  }

  if (_currentState != STATE_IDLE) {
    return false;
  }

  int32_t currentPos = motor_getPositionMicrosteps(_icID);
  int32_t targetPos = currentPos + deltaMicrosteps;

  if (!isWithinSoftLimits(targetPos)) {
    return false;
  }

  // 检查是否需要 VSTOP recovery（motor_moveToMicrosteps 会禁用限位）
  uint32_t preStatus = motor_readStatus(_icID);
  bool vstopWasActive = (preStatus & TMC4361A_VSTOPL_ACTIVE_F_MASK) ||
                        (preStatus & TMC4361A_VSTOPR_ACTIVE_F_MASK);

  motor_moveToMicrosteps(_icID, targetPos);
  startMovement(); // 设置移动状态

  if (vstopWasActive && _softLimitsEnabled) {
    _needReenableLimits = true;
  }

  return true;
}

// 相对移动（mm 单位，薄包装）
bool Axis::moveRelative(float distanceMM) {
  return moveRelativeMicrosteps(motor_mmToMicrosteps(_icID, distanceMM));
}

// 设置速度
void Axis::setSpeed(float speedMM) {
  motor_setMaxVelocity(_icID, speedMM);
}

// 平滑停止
void Axis::smoothStop() {
  motor_stop(_icID);
  completeMovement(); // 停止移动状态
}

// 运动控制函数
void Axis::disableAxis() {
  motor_enableDriver(_icID, false);
  _isEnabled = false; // 更新使能状态
}

void Axis::enableAxis() {
  motor_enableDriver(_icID, true);
  _isEnabled = true; // 更新使能状态
}

// 设置当前位置
void Axis::setCurrentPosition(float positionMM) {
  motor_setCurrentPosition(_icID, positionMM);
}

// 获取当前位置 microsteps (使用新 API)
int32_t Axis::getCurrentPosition() const {
  return motor_getPositionMicrosteps(_icID);
}

// 获取当前位置（毫米）(使用新 API)
float Axis::getCurrentPositionMM() const {
  return motor_getPositionMM(_icID);
}

// 获取当前位置（微步）
// 编码器使能时返回 ENC_POS（经 ENC_CONST 换算，单位与微步一致）
// 未使能时返回 XACTUAL（开环位置）
int32_t Axis::getCurrentPositionMicrosteps() const {
  if (_config.enableEncoder) {
    return (int32_t)tmc4361A_readRegister(_icID, TMC4361A_ENC_POS);
  }
  return motor_getPositionMicrosteps(_icID);
}

// 获取编码器位置（微步单位，经 ENC_CONST 换算）
// 未启用编码器时返回 XACTUAL
int32_t Axis::getEncoderPositionMicrosteps() const {
  if (_config.enableEncoder) {
    return (int32_t)tmc4361A_readRegister(_icID, TMC4361A_ENC_POS);
  }
  return motor_getPositionMicrosteps(_icID);
}

// Homing 细分切换
void Axis::switchToHomingMicrosteps() {
  if (_config.homingMicrostepping != _config.microstepping) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Switch microsteps for homing: ");
    DEBUG_PRINT(_config.microstepping);
    DEBUG_PRINT(" -> ");
    DEBUG_PRINTLN(_config.homingMicrostepping);
    motor_setMicrosteps(_icID, _config.homingMicrostepping);
    setMotionParameters(_config.maxVelocityMM, _config.maxAccelerationMM);
  }
}

void Axis::restoreNormalMicrosteps() {
  if (_config.homingMicrostepping != _config.microstepping) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Restore microsteps after homing: ");
    DEBUG_PRINT(_config.homingMicrostepping);
    DEBUG_PRINT(" -> ");
    DEBUG_PRINTLN(_config.microstepping);
    motor_setMicrosteps(_icID, _config.microstepping);
    setMotionParameters(_config.maxVelocityMM, _config.maxAccelerationMM);
  }
}

// 开始归位
bool Axis::startHoming() {
  // 自动从错误状态恢复
  if (_currentState == STATE_ERROR) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Auto-recovery from error state for homing");
    handleReset();
  }

  if (_currentState != STATE_IDLE) {
    return false;
  }

  setState(STATE_HOMING_INIT);
  return true;
}

// 检查是否正在归位
bool Axis::isHomingInProgress() const {
  return _currentState == STATE_HOMING_INIT ||
         _currentState == STATE_HOMING_SEARCH ||
         _currentState == STATE_HOMING_SET_ZERO ||
         _currentState == STATE_LEAVING_HOME;
}

// 检查运动是否完成 (使用新 API)
bool Axis::isMovementComplete() const {
  return motor_getPositionMicrosteps(_icID) == motor_getTargetMicrosteps(_icID);
}

// 设置软限位 (使用新 API)
void Axis::setSoftLimits(float lowerLimitMM, float upperLimitMM) {
  int32_t lowerMicrosteps = motor_mmToMicrosteps(_icID, lowerLimitMM);
  int32_t upperMicrosteps = motor_mmToMicrosteps(_icID, upperLimitMM);

  motor_setSoftLimits(_icID, lowerMicrosteps, upperMicrosteps);
  enableSoftLimits(true);
}

// 启用/禁用软限位 (使用新 API)
void Axis::enableSoftLimits(bool enable) {
  motor_enableSoftLimits(_icID, enable, enable);
  _softLimitsEnabled = enable;
}

// 设置单侧软限位 (direction: +1=上限/右, -1=下限/左)
void Axis::setOneSoftLimit(int direction, int32_t valueMicrosteps) {
  // 先将 XTARGET 设为当前位置，防止放宽限位后电机自动恢复运动
  int32_t xactual = tmc4361A_readRegister(_icID, TMC4361A_XACTUAL);
  tmc4361A_writeRegister(_icID, TMC4361A_XTARGET, xactual);

  uint32_t refConf = tmc4361A_readRegister(_icID, TMC4361A_REFERENCE_CONF);
  if (direction > 0) {
    tmc4361A_writeRegister(_icID, TMC4361A_VIRT_STOP_RIGHT, valueMicrosteps);
    refConf |= TMC4361A_VIRTUAL_RIGHT_LIMIT_EN_MASK;
    refConf |= (1 << TMC4361A_VIRT_STOP_MODE_SHIFT);
  } else {
    tmc4361A_writeRegister(_icID, TMC4361A_VIRT_STOP_LEFT, valueMicrosteps);
    refConf |= TMC4361A_VIRTUAL_LEFT_LIMIT_EN_MASK;
    refConf |= (1 << TMC4361A_VIRT_STOP_MODE_SHIFT);
  }
  tmc4361A_writeRegister(_icID, TMC4361A_REFERENCE_CONF, refConf);
  _softLimitsEnabled = true;
}

// PID 控制
void Axis::configureStagePID(bool flip_direction, uint16_t transitions_per_rev) {
  // 运行时使能编码器（上位机下发后生效，getCurrentPositionMicrosteps 将读 ENC_POS）
  _config.enableEncoder = true;

  // ABN 编码器初始化（硬编码参数与旧架构一致）
  motor_initABNEncoder(_icID, transitions_per_rev,
                        32,    // filter_wait_time
                        4,     // filter_exponent
                        512,   // filter_vmean
                        flip_direction);

  // PID 参数初始化（按轴类型区分）
  // pid_dclip = VMAX (内部单位), 已缓存在 motorParams 中
  uint32_t vmax_usteps = (uint32_t)motorParams[_icID].vmax;
  uint32_t target_tolerance, pid_tolerance, pid_iclip;

  // 根据轴名称区分参数
  if (strcmp(_axisName, "W") == 0 || strcmp(_axisName, "W2") == 0) {
    target_tolerance = 2;
    pid_tolerance = 2;
    pid_iclip = 4096;
  } else if (strcmp(_axisName, "Z") == 0) {
    target_tolerance = 25;
    pid_tolerance = 25;
    pid_iclip = 4096;
  } else {
    // X, Y 及其他
    target_tolerance = 25;
    pid_tolerance = 25;
    pid_iclip = 32767;
  }

  motor_initPID(_icID, target_tolerance, pid_tolerance,
                _pidState.p, _pidState.i, _pidState.d,
                vmax_usteps, pid_iclip, 2);  // pid_d_clkdiv = 2

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":CONFIGURE_STAGE_PID flip=");
  DEBUG_PRINT(flip_direction);
  DEBUG_PRINT(" tpr=");
  DEBUG_PRINT(transitions_per_rev);
  DEBUG_PRINT(" P=");
  DEBUG_PRINT(_pidState.p);
  DEBUG_PRINT(" I=");
  DEBUG_PRINT(_pidState.i);
  DEBUG_PRINT(" D=");
  DEBUG_PRINTLN(_pidState.d);
}

void Axis::enableStagePID() {
  _pidState.enabled = true;
  motor_enablePID(_icID);
  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":ENABLE_STAGE_PID");
}

void Axis::disableStagePID() {
  _pidState.enabled = false;
  motor_disablePID(_icID);
  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":DISABLE_STAGE_PID");
}

void Axis::setPIDArguments(uint16_t p, uint8_t i, uint8_t d) {
  _pidState.p = p;
  _pidState.i = i;
  _pidState.d = d;
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":SET_PID_ARGUMENTS P=");
  DEBUG_PRINT(p);
  DEBUG_PRINT(" I=");
  DEBUG_PRINT(i);
  DEBUG_PRINT(" D=");
  DEBUG_PRINTLN(d);
}

// 运行时更新丝杆导程
void Axis::setLeadScrewPitch(float pitchMM) {
  _config.screwPitchMM = pitchMM;
  motorParams[_icID].screwPitchMM = pitchMM;
  motorParams[_icID].stepsPerMM =
      (float)(motorParams[_icID].fullStepsPerRev * motorParams[_icID].microsteps) /
      pitchMM;
}

// 运行时重新配置步进驱动器（微步 + 电流）
void Axis::configureDriver(uint16_t microstepping, float currentMA,
                            float holdCurrentRatio) {
  _config.microstepping = microstepping;
  _config.motorCurrentMA = currentMA;
  _config.holdCurrent = holdCurrentRatio;

  // 更新 TMC4361A 控制器侧微步 + stepsPerMM 缓存
  motor_setMicrosteps(_icID, microstepping);

  // 重新初始化驱动器（电流 + chopper 参数）
  MotorConfig motorConfig = {
      .driverType = _config.driverType,
      .rSense = _config.r_sense,
      .runCurrentMA = currentMA,
      .holdCurrentRatio = holdCurrentRatio,
      .microstepRes = 0,
      .interpolation = true,
      .toff = 3,
      .hstrt = 4,
      .hend = -2,
      .tbl = 2,
      .stallThreshold = (int8_t)_config.stallSensitivity,
      .stallFilter = true,
      .enableStealthChop = false,
      .globalScaler = 0,
      .iholdDelay = 7,
      .currentRange = _config.currentRange};
  motor_initDriver(_icID, &motorConfig);

  // 微步变化导致 stepsPerMM 变化，重新计算运动参数
  setMotionParameters(_config.maxVelocityMM, _config.maxAccelerationMM);
}

// 运行时更新 homing 安全裕量
void Axis::setHomeSafetyMargin(float marginMM) {
  _config.homeSafetyMarginMM = marginMM;
  motor_enableHomingLimit(_icID, _config.rightSwitchPolarity,
                          _config.homingSwitch,
                          mmToMicrosteps(marginMM));
}

// 获取当前状态
AxisState Axis::getCurrentState() const { return _currentState; }

// 获取轴名称
const char *Axis::getAxisName() const { return _axisName; }

// 检查是否在错误状态
bool Axis::isInErrorState() const { return _currentState == STATE_ERROR; }

// 读取电子限位开关状态 (使用新 API)
uint8_t Axis::readLimitSwitches() const {
  return motor_readLimitSwitches(_icID);
}

// 读取开关事件 (使用新 API)
uint8_t Axis::readSwitchEvent() const {
  return motor_readSwitchEvent(_icID);
}

// 读取轴事件 (使用新 API)
uint32_t Axis::readAxisEvent() const {
  return motor_readEvents(_icID);
}

// 私有方法实现
void Axis::setState(AxisState newState) {
  if (_currentState != newState) {
    _previousState = _currentState;
    _currentState = newState;
    _stateStartTime = millis();
    _stateChanged = true; // 标记状态已变化
  }
}

void Axis::handleError(const char *errorMsg) {
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Axis Error: ");
  DEBUG_PRINTLN(errorMsg);
  smoothStop();
  setState(STATE_ERROR);
}

bool Axis::checkTimeout(unsigned long timeoutMs) const {
  return (millis() - _stateStartTime) > timeoutMs;
}


// 单位转换函数 (使用新 API)
int32_t Axis::mmToMicrosteps(float mm) const {
  return motor_mmToMicrosteps(_icID, mm);
}

float Axis::microstepsToMM(int32_t microsteps) const {
  return motor_microstepsToMM(_icID, microsteps);
}

uint32_t Axis::velocityMMToMicrosteps(float velocityMM) const {
  return motor_velocityMMToInternal(_icID, velocityMM);
}

uint32_t Axis::accelerationMMToMicrosteps(float accelerationMM) const {
  return motor_accelMMToInternal(_icID, accelerationMM);
}

bool Axis::isValidPosition(float positionMM) const {
  // 检查位置是否在合理范围内
  return (positionMM >= -1000.0f && positionMM <= 1000.0f); // 根据实际情况调整
}

bool Axis::isWithinSoftLimits(int32_t microsteps) const {
  // 这里需要根据实际的软限位设置来检查
  // 暂时返回true，需要根据具体实现完善
  return true;
}

bool Axis::handleEmergency() {
  // 发送轴状态
  [[maybe_unused]] const char *stateStr = "UNKNOWN";
  switch (_currentState) {
  case STATE_IDLE:
    stateStr = "IDLE";
    break;
  case STATE_HOMING_INIT:
    stateStr = "HOMING_INIT";
    break;
  case STATE_HOMING_SEARCH:
    stateStr = "HOMING_SEARCH";
    break;
  case STATE_HOMING_SET_ZERO:
    stateStr = "HOMING_SET_ZERO";
    break;
  case STATE_LEAVING_HOME:
    stateStr = "LEAVING_HOME";
    break;
  case STATE_MOVING:
    stateStr = "MOVING";
    break;
  case STATE_ERROR:
    stateStr = "ERROR";
    break;
  }

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":EMERGENCY:");
  DEBUG_PRINTLN(stateStr);

  return true;
}

bool Axis::handleGetData() {
  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":GET_DATA:START");  // 调试点1

  // 发送轴状态
  [[maybe_unused]] const char *stateStr = "UNKNOWN";
  switch (_currentState) {
  case STATE_IDLE:
    stateStr = "IDLE";
    break;
  case STATE_HOMING_INIT:
    stateStr = "HOMING_INIT";
    break;
  case STATE_HOMING_SEARCH:
    stateStr = "HOMING_SEARCH";
    break;
  case STATE_HOMING_SET_ZERO:
    stateStr = "HOMING_SET_ZERO";
    break;
  case STATE_LEAVING_HOME:
    stateStr = "LEAVING_HOME";
    break;
  case STATE_MOVING:
    stateStr = "MOVING";
    break;
  case STATE_ERROR:
    stateStr = "ERROR";
    break;
  }

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":STATE:");
  DEBUG_PRINTLN(stateStr);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":GET_DATA:BEFORE_GET_POS");  // 调试点2

  // 发送当前位置
  int32_t microsteps = getCurrentPosition();

  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":GET_DATA:AFTER_GET_POS");  // 调试点3
  [[maybe_unused]] float positionMM = microstepsToMM(microsteps);
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Current Position (mm):");
  DEBUG_PRINTLNF(positionMM, 3);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Current Position (microsteps):");
  DEBUG_PRINTLN(microsteps);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":GET_DATA:BEFORE_READ_LIMIT");  // 调试点4

  // 发送限位开关状态
  [[maybe_unused]] uint8_t limitState = readLimitSwitches();

  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":GET_DATA:AFTER_READ_LIMIT");  // 调试点5

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":LIMIT_SWITCHES:0x");
  DEBUG_PRINTLNF(limitState, HEX);

  // 新增：发送移动状态
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":IS_MOVING:");
  DEBUG_PRINTLN(_isMoving ? "YES" : "NO");

  // 新增：发送使能状态
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":IS_ENABLED:");
  DEBUG_PRINTLN(_isEnabled ? "YES" : "NO");

  // 发送综合状态信息（用于标签显示）
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":AXIS_STATUS:");
  DEBUG_PRINT(stateStr);
  DEBUG_PRINT(" | Pos:");
  DEBUG_PRINTF(positionMM, 3);
  DEBUG_PRINT("mm | Moving:");
  DEBUG_PRINT(_isMoving ? "YES" : "NO");
  DEBUG_PRINT(" | Enabled:");
  DEBUG_PRINT(_isEnabled ? "YES" : "NO");
  DEBUG_PRINT(" | Limits:0x");
  DEBUG_PRINTLNF(limitState, HEX);

  return true;
}

bool Axis::handleAxisAbilityToggle(bool action) {
  if (action == true) {
    enableAxis();
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":AXIS Enable");
  } else {
    disableAxis();
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":AXIS Disable");
  }
  return true;
}

bool Axis::handleDebugReg() {
  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":DEBUG_REG:START");

  // 读取关键 TMC4361A 寄存器
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:GENERAL_CONF(0x00)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_GENERAL_CONF), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:REFERENCE_CONF(0x01)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_REFERENCE_CONF), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:SPI_OUT_CONF(0x05)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_SPI_OUT_CONF), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:STATUS(0x0E)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_STATUS), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:EVENTS(0x0F)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_EVENTS), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:CLK_FREQ(0x1F)=");
  DEBUG_PRINTLN(tmc4361A_readRegister(_icID, TMC4361A_CLK_FREQ));

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:RAMPMODE(0x20)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_RAMPMODE), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:XACTUAL(0x21)=");
  DEBUG_PRINTLN(tmc4361A_readRegister(_icID, TMC4361A_XACTUAL));

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:VACTUAL(0x22)=");
  DEBUG_PRINTLN(tmc4361A_readRegister(_icID, TMC4361A_VACTUAL));

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:XTARGET(0x2D)=");
  DEBUG_PRINTLN(tmc4361A_readRegister(_icID, TMC4361A_XTARGET));

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:VMAX(0x24)=");
  DEBUG_PRINTLN(tmc4361A_readRegister(_icID, TMC4361A_VMAX));

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:AMAX(0x28)=");
  DEBUG_PRINTLN(tmc4361A_readRegister(_icID, TMC4361A_AMAX));

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:DMAX(0x29)=");
  DEBUG_PRINTLN(tmc4361A_readRegister(_icID, TMC4361A_DMAX));

  // 新增：关键配置寄存器
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:STEP_CONF(0x0A)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_STEP_CONF), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:CURRENT_CONF(0x05)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_CURRENT_CONF), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":REG:SCALE_VALUES(0x06)=0x");
  DEBUG_PRINTLNF(tmc4361A_readRegister(_icID, TMC4361A_SCALE_VALUES), HEX);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":DEBUG_REG:END");

  return true;
}
