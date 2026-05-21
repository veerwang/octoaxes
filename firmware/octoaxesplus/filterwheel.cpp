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
    restoreNormalMicrosteps();
    handleError("Homing timeout");
    return;
  }

  uint8_t limit_state = readLimitSwitches();

  switch (_currentState) {
    case STATE_HOMING_INIT:
      // 直接操作硬件禁用虚拟限位，不改变 _softLimitsEnabled 标志
      motor_enableSoftLimits(_icID, false, false);
      _slowApproach = false;
      switchToHomingMicrosteps();

      if (limit_state == 0x00) {
        // 已在感应区，先移出
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Already at home, moving away first...");
        setState(STATE_LEAVING_HOME);
      } else {
        // 不在感应区，快速搜索
        // 2026-05-21 方向 bug 修复：乘 _config.homing_direct 跟随上位机请求的方向，
        // 与 stepaxis.cpp 对齐。原版硬编码 + 方向，无视上位机 HOME_NEGATIVE 命令。
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Fast search...");
        int32_t speedInternal = _config.homing_direct *
                                motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
        motor_setVelocityInternal(_icID, speedInternal);
        setState(STATE_HOMING_SEARCH);
      }
      break;

    case STATE_HOMING_SEARCH:
      if (limit_state == 0x00) {
        // 触碰到感应区
        motor_setVelocityInternal(_icID, 0);  // 停车
        delay(100);

        if (!_slowApproach) {
          // 第一阶段（快速）：找到感应区后，移出再慢速逼近
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":Sensor found (fast), moving away for slow approach...");
          _slowApproach = true;
          setState(STATE_LEAVING_HOME);
        } else {
          // 第二阶段（慢速）：精确定位完成
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":Sensor found (slow), homing position locked.");

          // 先停车设零，再切回位置模式，最后恢复细分和运动参数
          motor_setCurrentPositionMicrosteps(_icID, 0);  // VMAX=0 停车，设零，velocity_mode=true
          motor_moveToMicrosteps(_icID, 0);              // 触发 sRampInit 切回位置模式（target=0=current，无移动）
          restoreNormalMicrosteps();                      // 安全恢复细分和 VMAX/AMAX
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":Homing completed! Current position set to 0");

          // Homing 完成后恢复软限位和 PID
          if (_softLimitsEnabled) {
            enableSoftLimits(true);
          }
          if (_pidState.enabled) {
            motor_enablePID(_icID);
            DEBUG_PRINT(_axisName);
            DEBUG_PRINTLN(":PID re-enabled after homing");
          }

          setState(STATE_IDLE);
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
      // 已离开感应区
      DEBUG_PRINT(_axisName);

      // 2026-05-21 方向 bug 修复：search/leave 方向跟随 _config.homing_direct，
      // 与 stepaxis.cpp 对齐。leave 方向 = -search 方向。
      if (_slowApproach) {
        // 先停车，确保慢速逼近起点一致
        motor_setVelocityInternal(_icID, 0);
        delay(100);
        DEBUG_PRINTLN(":Left sensor, slow approach...");
        int32_t speedInternal = _config.homing_direct *
                                motor_velocityMMToInternal(_icID, _config.homingVelocityMM / 5.0);
        motor_setVelocityInternal(_icID, speedInternal);
      } else {
        // 快速搜索感应区
        DEBUG_PRINTLN(":Left sensor, fast search...");
        int32_t speedInternal = _config.homing_direct *
                                motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
        motor_setVelocityInternal(_icID, speedInternal);
      }
      setState(STATE_HOMING_SEARCH);
    } else {
      // 仍在感应区，继续移出（反向于 search 方向）
      float leaveSpeed = _slowApproach
        ? _config.homingVelocityMM / 5.0   // 慢速移出，减少过冲
        : _config.homingVelocityMM;          // 全速移出
      int32_t speedInternal = -1 * _config.homing_direct *
                              motor_velocityMMToInternal(_icID, leaveSpeed);
      motor_setVelocityInternal(_icID, speedInternal);
    }
  }
}

bool FilterWheel::handleSetLimits(const String& command) {
	return true;
}
