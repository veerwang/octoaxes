#include "axis.h"
#include "build_opt.h"
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

  // 初始化TMC4361A
  tmc4361A_init(&_tmc4361, _csPin, &_tmc4361Config,
                tmc4361A_defaultRegisterResetState);

  // 配置电机参数
  tmc4361A_tmc2660_config(
      &_tmc4361, (_config.motorCurrentMA / 1000) * _config.r_sense / 0.2298,
      _config.holdCurrent, 1, 1, 1, _config.screwPitchMM,
      _config.fullStepsPerRev, _config.microstepping);

  // 初始化TMC4361和TMC2660
  tmc4361A_tmc2660_init(&_tmc4361, _config.clockFrequency);

  // 设置运动参数
  setMotionParameters(_config.maxVelocityMM, _config.maxAccelerationMM);

  // 初始化斜坡参数
  initializeRamp();

  // 使能限位开关读取
  if (_config.enableLeftLimitSwitch)
    tmc4361A_enableLimitSwitch(&_tmc4361, _config.leftSwitchPolarity, LEFT_SW,
                               _config.leftFlipped, _config.leftIsInactive);
  if (_config.enableRightLimitSwitch)
    tmc4361A_enableLimitSwitch(&_tmc4361, _config.rightSwitchPolarity, RGHT_SW,
                               _config.rightFlipped, _config.rightIsInactive);

  // 使能归位限位
  tmc4361A_enableHomingLimit(&_tmc4361, _config.rightSwitchPolarity,
                             _config.homingSwitch,
                             mmToMicrosteps(_config.homeSafetyMarginMM));

  // 禁用虚拟限位开关（初始状态）
  enableSoftLimits(false);

  // 禁用PID
  tmc4361A_set_PID(&_tmc4361, PID_DISABLE);

  if (_config.enableStallSensitivity)
    tmc4361A_config_init_stallGuard(&_tmc4361, _config.stallSensitivity, true,
                                    1);

  // 默认使能轴
  enableAxis();

  return true;
}

// 设置运动参数
void Axis::setMotionParameters(float maxVelocityMM, float maxAccelerationMM) {
  _maxVelocityMicrosteps = velocityMMToMicrosteps(maxVelocityMM);
  _maxAccelerationMicrosteps = accelerationMMToMicrosteps(maxAccelerationMM);

  tmc4361A_setMaxSpeed(&_tmc4361, _maxVelocityMicrosteps);
  tmc4361A_setMaxAcceleration(&_tmc4361, _maxAccelerationMicrosteps);
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

// 新增：移动状态检测函数
void Axis::checkMovementComplete() {
  if (!_isMoving)
    return;

  int32_t currentPos = tmc4361A_currentPosition(&_tmc4361);
  int32_t targetPos = tmc4361A_targetPosition(&_tmc4361);

  // 检查是否到达目标位置
  if (currentPos == targetPos) {
    completeMovement();
  } else {
    // 更新最后位置，用于后续的移动检测
    _lastPosition = currentPos;
  }
}

// 新增：开始移动
void Axis::startMovement() {
  _isMoving = true;
  _lastPosition = tmc4361A_currentPosition(&_tmc4361);
  setState(STATE_MOVING);
}

// 新增：完成移动
void Axis::completeMovement() {
  _isMoving = false;
  setState(STATE_IDLE);

  // 可选：发送移动完成通知
  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":MOVEMENT_COMPLETED");
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

  // 清除状态
  readLimitSwitches();
  readSwitchEvent();

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

  int32_t microsteps = mmToMicrosteps(positionMM);

  if (!isWithinSoftLimits(microsteps)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Movement rejected: Outside soft limits");
    return false;
  }

  tmc4361A_moveTo(&_tmc4361, microsteps);
  startMovement(); // 设置移动状态
  return true;
}

// 相对移动
bool Axis::moveRelative(float distanceMM) {
  if (_currentState != STATE_IDLE) {
    return false;
  }

  int32_t currentPos = tmc4361A_currentPosition(&_tmc4361);
  int32_t targetPos = currentPos + mmToMicrosteps(distanceMM);

  if (!isWithinSoftLimits(targetPos)) {
    return false;
  }

  tmc4361A_moveTo(&_tmc4361, targetPos);
  startMovement(); // 设置移动状态
  return true;
}

// 设置速度
void Axis::setSpeed(float speedMM) {
  int32_t speedMicrosteps = velocityMMToMicrosteps(speedMM);
  tmc4361A_setSpeed(&_tmc4361, speedMicrosteps);
}

// 平滑停止
void Axis::smoothStop() {
  tmc4361A_setSpeed(&_tmc4361, 0);
  completeMovement(); // 停止移动状态
}

// 运动控制函数
void Axis::disableAxis() {
  tmc4361A_tmc2660_disable_driver(&_tmc4361);
  _isEnabled = false; // 更新使能状态
}

void Axis::enableAxis() {
  tmc4361A_tmc2660_enable_driver(&_tmc4361);
  _isEnabled = true; // 更新使能状态
}

// 设置当前位置
void Axis::setCurrentPosition(float positionMM) {
  int32_t microsteps = mmToMicrosteps(positionMM);
  tmc4361A_setCurrentPosition(&_tmc4361, microsteps);
}

// 获取当前位置microsteps
int32_t Axis::getCurrentPosition() const {
  int32_t microsteps = tmc4361A_currentPosition(&_tmc4361);
  return microsteps;
}

// 获取当前位置（毫米）
float Axis::getCurrentPositionMM() const {
  int32_t microsteps = tmc4361A_currentPosition(&_tmc4361);
  return microstepsToMM(microsteps);
}

// 获取当前位置（微步）
int32_t Axis::getCurrentPositionMicrosteps() const {
  return tmc4361A_currentPosition(&_tmc4361);
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

// 检查运动是否完成
bool Axis::isMovementComplete() const {
  return tmc4361A_currentPosition(&_tmc4361) ==
         tmc4361A_targetPosition(&_tmc4361);
}

// 设置软限位
void Axis::setSoftLimits(float lowerLimitMM, float upperLimitMM) {
  int32_t lowerMicrosteps = mmToMicrosteps(lowerLimitMM);
  int32_t upperMicrosteps = mmToMicrosteps(upperLimitMM);

  tmc4361A_setVirtualLimit(&_tmc4361, -1, lowerMicrosteps);
  tmc4361A_setVirtualLimit(&_tmc4361, 1, upperMicrosteps);

  enableSoftLimits(true);
}

// 启用/禁用软限位
void Axis::enableSoftLimits(bool enable) {
  if (enable) {
    tmc4361A_enableVirtualLimitSwitch(&_tmc4361, 1);
    tmc4361A_enableVirtualLimitSwitch(&_tmc4361, -1);
  } else {
    tmc4361A_disableVirtualLimitSwitch(&_tmc4361, 1);
    tmc4361A_disableVirtualLimitSwitch(&_tmc4361, -1);
  }
}

// 获取当前状态
AxisState Axis::getCurrentState() const { return _currentState; }

// 获取轴名称
const char *Axis::getAxisName() const { return _axisName; }

// 检查是否在错误状态
bool Axis::isInErrorState() const { return _currentState == STATE_ERROR; }

// 读取电子限位开关状态
uint8_t Axis::readLimitSwitches() const {
  return tmc4361A_readLimitSwitches(&_tmc4361);
}

// 读取开关事件
uint8_t Axis::readSwitchEvent() const {
  return tmc4361A_readSwitchEvent(&_tmc4361);
}

// 读取轴事件
uint32_t Axis::readAxisEvent() const {
  return tmc4361A_readInt(&_tmc4361, TMC4361A_EVENTS);
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

void Axis::initializeRamp() {
  _tmc4361.rampParam[ASTART_IDX] = 0;
  _tmc4361.rampParam[DFINAL_IDX] = 0;
  tmc4361A_sRampInit(&_tmc4361);
}

// 单位转换函数
int32_t Axis::mmToMicrosteps(float mm) const {
  return tmc4361A_xmmToMicrosteps(&_tmc4361, mm);
}

float Axis::microstepsToMM(int32_t microsteps) const {
  return tmc4361A_xmicrostepsTomm(&_tmc4361, microsteps);
}

uint32_t Axis::velocityMMToMicrosteps(float velocityMM) const {
  return tmc4361A_vmmToMicrosteps(&_tmc4361, velocityMM);
}

uint32_t Axis::accelerationMMToMicrosteps(float accelerationMM) const {
  return tmc4361A_ammToMicrosteps(&_tmc4361, accelerationMM);
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

  // 发送当前位置
  int32_t microsteps = getCurrentPosition();
  float positionMM = microstepsToMM(microsteps);
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Current Position (mm):");
  DEBUG_PRINTLNF(positionMM, 3);

  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Current Position (microsteps):");
  DEBUG_PRINTLN(microsteps);

  // 发送限位开关状态
  uint8_t limitState = readLimitSwitches();
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
