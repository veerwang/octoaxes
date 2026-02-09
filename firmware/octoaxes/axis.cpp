#include "axis.h"
#include "build_opt.h"
#include "TMC4361A_Register.h"
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
  // 初始化运动参数缓存 (用于新 API 的单位转换)
  MotionConfig motionConfig = {
      .clockFrequency = _config.clockFrequency,
      .screwPitchMM = _config.screwPitchMM,
      .fullStepsPerRev = (uint16_t)_config.fullStepsPerRev,
      .microsteps = (uint16_t)_config.microstepping,
      .maxVelocityMM = _config.maxVelocityMM,
      .maxAccelerationMM = _config.maxAccelerationMM,
      .maxDecelerationMM = _config.maxAccelerationMM,
      .useSShapedRamp = true,
      .bow1 = 0,
      .bow2 = 0,
      .bow3 = 0,
      .bow4 = 0};
  motor_initMotionController(_icID, &motionConfig);

  // 初始化驱动器配置 (与旧 API 一致: CHOPCONF = 0x100C3)
  MotorConfig motorConfig = {
      .rSense = _config.r_sense,
      .runCurrentMA = _config.motorCurrentMA,
      .holdCurrentRatio = _config.holdCurrent,
      .microstepRes = 0,  // 256 microsteps
      .interpolation = true,
      .toff = 3,   // 旧 API: TOFF = 3
      .hstrt = 4,  // 旧 API: HSTRT = 4
      .hend = -2,  // 旧 API: HEND 寄存器值 = 1, 实际值 = 1 + (-3) = -2
      .tbl = 2,    // 旧 API: TBL = 2
      .stallThreshold = (int8_t)_config.stallSensitivity,
      .stallFilter = true};
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

  // 软件限位
  uint32_t i_datagram =
      event & (TMC4361A_VSTOPL_ACTIVE_MASK | TMC4361A_VSTOPR_ACTIVE_MASK);
  i_datagram >>= TMC4361A_VSTOPL_ACTIVE_SHIFT;
  uint8_t result = i_datagram & 0xff;

  // 添加方向的内容
  if ((result == RGHT_SW && _moveDirection == RGHT_DIR) ||
      (result == LEFT_SW && _moveDirection == LEFT_DIR)) {
    DEBUG_PRINT("Software Limit Stop: ");
    DEBUG_PRINTLN(result);
    completeMovement();
    return;
  }

  // 硬件件限位
  i_datagram = event & (TMC4361A_STOPL_EVENT_MASK | TMC4361A_STOPR_EVENT_MASK);
  i_datagram >>= TMC4361A_STOPL_EVENT_SHIFT;
  result = i_datagram & 0xff;

  // 添加方向的内容
  if ((result == RGHT_SW && _moveDirection == RGHT_DIR) ||
      (result == LEFT_SW && _moveDirection == LEFT_DIR)) {
    DEBUG_PRINT("Hardware Limit Stop: ");
    DEBUG_PRINTLN(result);
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
  float positionMM = microstepsToMM(microsteps);
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
  float positionMM = float(value) / 1000.0f;
  positionMM = 1 * positionMM;

  _moveDirection = sgn(value);

  if (!moveRelative(positionMM)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVE_AXIS ERROR: Movement failed");
    return false;
  } else {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":MOVE_AXIS: ");
    DEBUG_PRINTLNF(positionMM, 3);
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
  float positionMM = float(value) / 1000.0f;
  positionMM = 1 * positionMM;

  _moveDirection = sgn(value);

  if (!moveToPosition(positionMM)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVETO_AXIS ERROR: Movement failed");
    return false;
  }

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":MOVETO_AXIS: ");
  DEBUG_PRINTLNF(positionMM, 3);
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
  unsigned long elapsed = micros() - _moveStartMicros;
  _isMoving = false;
  setState(STATE_IDLE);

  // 发送移动完成通知（含精确耗时）
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":MOVEMENT_COMPLETED:");
  DEBUG_PRINT(elapsed / 1000);
  DEBUG_PRINTLN("ms");
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

// 移动到绝对位置
bool Axis::moveToPosition(float positionMM) {
  if (_currentState != STATE_IDLE) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Movement rejected: Axis is busy, current state: ");
    DEBUG_PRINTLN(_currentState);
    return false;
  }

  if (!isValidPosition(positionMM)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Movement rejected: Invalid position");
    DEBUG_PRINTLN(positionMM);
    return false;
  }

  int32_t microsteps = motor_mmToMicrosteps(_icID, positionMM);

  if (!isWithinSoftLimits(microsteps)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Movement rejected: Outside soft limits");
    return false;
  }

  motor_moveToMicrosteps(_icID, microsteps);
  startMovement(); // 设置移动状态
  return true;
}

// 相对移动
bool Axis::moveRelative(float distanceMM) {
  if (_currentState != STATE_IDLE) {
    return false;
  }

  int32_t currentPos = motor_getPositionMicrosteps(_icID);
  int32_t targetPos = currentPos + motor_mmToMicrosteps(_icID, distanceMM);

  if (!isWithinSoftLimits(targetPos)) {
    return false;
  }

  motor_moveToMicrosteps(_icID, targetPos);
  startMovement(); // 设置移动状态
  return true;
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

// 获取当前位置（微步）(使用新 API)
int32_t Axis::getCurrentPositionMicrosteps() const {
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
  const char *stateStr = "UNKNOWN";
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
  const char *stateStr = "UNKNOWN";
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
  float positionMM = microstepsToMM(microsteps);
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Current Position (mm):");
  DEBUG_PRINTLNF(positionMM, 3);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Current Position (microsteps):");
  DEBUG_PRINTLN(microsteps);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":GET_DATA:BEFORE_READ_LIMIT");  // 调试点4

  // 发送限位开关状态
  uint8_t limitState = readLimitSwitches();

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
