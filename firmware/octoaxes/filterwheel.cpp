#include "filterwheel.h"
#include "build_opt.h"

FilterWheel::FilterWheel(uint8_t csPin, uint8_t axisIndex, const char* axisName, uint8_t filterCount) 
  : Axis(csPin, axisIndex, axisName), _filterCount(filterCount), _currentFilter(0) {
  _filterPositions = new float[filterCount];
  
  // 初始化默认位置：等间距分布，假设每个滤光片间隔60度
  for (uint8_t i = 0; i < filterCount; i++) {
    _filterPositions[i] = i * (360.0f / filterCount); // 以角度为单位，实际使用时需要转换为毫米
  }
}

bool FilterWheel::begin(const AxisConfig& config) {
  // 调用基类初始化
  bool result = Axis::begin(config);
  
  if (result) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":FilterWheel with ");
    DEBUG_PRINT(_filterCount);
    DEBUG_PRINTLN(" filters initialized successfully");
  }
  
  return result;
}

bool FilterWheel::moveToFilter(uint8_t filterPosition) {
  if (!isValidFilterPosition(filterPosition)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Invalid filter position: ");
    DEBUG_PRINTLN(filterPosition);
    return false;
  }
  
  if (_currentState != STATE_IDLE) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":Filter wheel is busy");
    return false;
  }
  
  float targetPosition = getFilterPosition(filterPosition);
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Moving to filter ");
  DEBUG_PRINT(filterPosition);
  DEBUG_PRINT(" at position ");
  DEBUG_PRINTLN(targetPosition);
  
  if (Axis::moveToPosition(targetPosition)) {
    _currentFilter = filterPosition;
    return true;
  }
  
  return false;
}

uint8_t FilterWheel::getCurrentFilter() const {
  return _currentFilter;
}

uint8_t FilterWheel::getFilterCount() const {
  return _filterCount;
}

void FilterWheel::update() {
  // 先调用基类更新
  Axis::update();
  
  // 滤光轮特有的更新逻辑可以在这里添加
  // 例如：检查是否到达目标滤光片位置等
}

bool FilterWheel::processCommand(const String& command) {
  if (command.startsWith("MOVE_TO_FILTER")) {
    return handleMoveToFilter(command);
  } else if (command.startsWith("GET_CURRENT_FILTER")) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":CURRENT_FILTER:");
    DEBUG_PRINTLN(_currentFilter);
    return true;
  } else if (command.startsWith("GET_FILTER_COUNT")) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":FILTER_COUNT:");
    DEBUG_PRINTLN(_filterCount);
    return true;
  } else {
    // 其他命令交给基类处理
    return Axis::processCommand(command);
  }
}

void FilterWheel::setFilterPositions(const float* positions, uint8_t count) {
  if (count > _filterCount) {
    count = _filterCount;
  }
  
  for (uint8_t i = 0; i < count; i++) {
    _filterPositions[i] = positions[i];
  }
  
  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":Filter positions updated");
}

bool FilterWheel::handleMoveToFilter(const String& command) {
  int space1 = command.indexOf(' ');
  if (space1 == -1) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVE_TO_FILTER ERROR: Invalid format");
    return false;
  }
  
  String filterStr = command.substring(space1 + 1);
  uint8_t filterPosition = (uint8_t)filterStr.toInt();
  
  if (!moveToFilter(filterPosition)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVE_TO_FILTER ERROR: Movement failed");
    return false;
  }
  
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":MOVE_TO_FILTER: Moving to filter ");
  DEBUG_PRINTLN(filterPosition);
  return true;
}

float FilterWheel::getFilterPosition(uint8_t filterIndex) const {
  if (filterIndex < _filterCount) {
    return _filterPositions[filterIndex];
  }
  return 0.0f;
}

bool FilterWheel::isValidFilterPosition(uint8_t filterPosition) const {
  return (filterPosition < _filterCount);
}

void FilterWheel::performHomingSequence() {
  if (checkTimeout(_homing_timeout_ms)) {
    handleError("Homing timeout");
    return;
  }

  uint8_t limit_state = readLimitSwitches();

  switch (_currentState) {
    case STATE_HOMING_INIT:
      enableSoftLimits(false);

      if (limit_state == 0x00) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Already at home position, moving away first...");
        setState(STATE_LEAVING_HOME);
      } else {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Starting homing process...");

        DEBUG_PRINTLN(_config.homingVelocityMM);
        int32_t speedInternal = motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
        motor_setVelocityInternal(_icID, speedInternal);
        setState(STATE_HOMING_SEARCH);
      }
      break;

    case STATE_HOMING_SEARCH:
      if (limit_state == 0x00) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Home limit switch triggered!");

        int32_t latchedPosition = motor_readLatchPosition(_icID);
        DEBUG_PRINT(_axisName);
        DEBUG_PRINT(":Latched position: ");
        DEBUG_PRINTLN(latchedPosition);

        motor_setCurrentPositionMicrosteps(_icID, latchedPosition);

        _checkHomeReachTimeout = 0;

        setState(STATE_HOMING_SET_ZERO);
      }
      break;

    case STATE_HOMING_SET_ZERO:
      // 等待移动到安全位置完成
      if (isMovementComplete() || _checkHomeReachTimeout >= 500 * 1000) {
        // 设置当前位置为0
        motor_setCurrentPositionMicrosteps(_icID, 0);
        DEBUG_PRINT(_axisName);

        if (_checkHomeReachTimeout > 500 * 1000)
          DEBUG_PRINTLN(":Homing Set Current Position to Latched position Timeout");

        DEBUG_PRINTLN(":Homing completed! Current position set to 0");

        setState(STATE_IDLE);
      } else {
        // 可选：添加进度显示
        static unsigned long lastProgressTime = 0;
        if (millis() - lastProgressTime > 500) {
          DEBUG_PRINT(_axisName);
          DEBUG_PRINT(":Moving to safe position... Current :");
          DEBUG_PRINT(getCurrentPositionMicrosteps());
          DEBUG_PRINT(" microsteps, Target: ");
          DEBUG_PRINT(motor_getTargetMicrosteps(_icID));
          DEBUG_PRINTLN(" microsteps");
          lastProgressTime = millis();
        }
      }
      break;

    default:
      break;
  }
}

void FilterWheel::performLeavingHome() {
  if (checkTimeout(LEAVING_HOME_TIMEOUT_MS)) {
    handleError("Leaving home timeout");
    return;
  }

  uint8_t limit_state = readLimitSwitches();

  if (_currentState == STATE_LEAVING_HOME) {
    if (!(limit_state == 0x00)) {
      DEBUG_PRINT(_axisName);
      DEBUG_PRINTLN(":Left home position, starting homing...");

      // 开始真正的归位搜索
      int32_t speedInternal = motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
      motor_setVelocityInternal(_icID, speedInternal);
      setState(STATE_HOMING_SEARCH);
    } else {
      // 继续移动离开home位置
      // 根据限位开关类型设置正确的离开方向
      int32_t speedInternal;
      if (_config.homingSwitch == RGHT_SW) {
        speedInternal = motor_velocityMMToInternal(_icID, _config.homingVelocityMM); // 向左移动离开右限位
      } else {
        speedInternal = -1 * motor_velocityMMToInternal(_icID, _config.homingVelocityMM); // 向右移动离开左限位
      }
      motor_setVelocityInternal(_icID, speedInternal);
    }
  }
}

bool FilterWheel::handleSetLimits(const String& command) {
	return true;
}
