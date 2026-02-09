#include "objectives.h"
#include "build_opt.h"

Objectives::Objectives(uint8_t csPin, uint8_t axisIndex, const char* axisName, uint8_t objectivesCount) 
  : Axis(csPin, axisIndex, axisName), _objectivesCount(objectivesCount), _currentObjective(0) {
  _objectivePositions = new float[objectivesCount];

  // 初始化默认位置：等间距分布，假设每个物镜转换器间隔90度
  for (uint8_t i = 0; i < objectivesCount; i++) {
    _objectivePositions[i] = i * (360.0f / objectivesCount); // 以角度为单位，实际使用时需要转换为毫米
  }
  
}

bool Objectives::begin(const AxisConfig& config) {
  // 调用基类初始化
  bool result = Axis::begin(config);
  
  if (result) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Objectives with ");
    DEBUG_PRINT(_objectivesCount);
    DEBUG_PRINTLN(" Objectives initialized successfully");
  }
  
  return result;
}

void Objectives::update() {
  // 先调用基类更新
  Axis::update();
  
}

bool Objectives::processCommand(const String& command) {
  if (command.startsWith("MOVE_TO_OBJECTIVE")) {
    return handleMoveToObjective(command);
  } else if (command.startsWith("GET_CURRENT_OBJECTIVE")) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":CURRENT_OBJECTIVE:");
    DEBUG_PRINTLN(_currentObjective);
    return true;
  } else if (command.startsWith("GET_OBJECTIVE_COUNT")) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":OBJECTIVE_COUNT:");
    DEBUG_PRINTLN(_objectivesCount);
    return true;
  } else {
    // 其他命令交给基类处理
    return Axis::processCommand(command);
  }
}

void Objectives::performHomingSequence() {
  if (checkTimeout(_homing_timeout_ms)) {
    restoreNormalMicrosteps();
    handleError("Homing timeout");
    return;
  }

  uint8_t limit_state = readLimitSwitches();

  switch (_currentState) {
    case STATE_HOMING_INIT:
      enableSoftLimits(false);
      switchToHomingMicrosteps();

      if (limit_state == OBSW_SW) {
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
      if (limit_state == OBSW_SW) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Home limit switch triggered!");

        motor_setCurrentPositionMicrosteps(_icID, 0);

        _checkHomeReachTimeout = 0;

        setState(STATE_HOMING_SET_ZERO);
      }
      break;

    case STATE_HOMING_SET_ZERO:
      // 等待移动到安全位置完成
      if (isMovementComplete() || _checkHomeReachTimeout >= 500 * 1000) {
        // 恢复正常细分
        restoreNormalMicrosteps();
        // 设置当前位置为0
        DEBUG_PRINT(_axisName);

        if (_checkHomeReachTimeout > 500 * 1000)
          DEBUG_PRINTLN(":Homing Set Current Position to 0 position Timeout");

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

void Objectives::performLeavingHome() {
  if (checkTimeout(LEAVING_HOME_TIMEOUT_MS)) {
    handleError("Leaving home timeout");
    return;
  }

  uint8_t limit_state = readLimitSwitches();

  if (_currentState == STATE_LEAVING_HOME) {
    if (!(limit_state == OBSW_SW)) {
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

bool Objectives::handleSetLimits(const String& command) {
	return true;
}

bool Objectives::handleMoveToObjective(const String& command) {
  int space1 = command.indexOf(' ');
  if (space1 == -1) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVE_TO_OBJECTIVE ERROR: Invalid format");
    return false;
  }
  
  String filterStr = command.substring(space1 + 1);
  uint8_t ObjectivePosition = (uint8_t)filterStr.toInt();
  
	/*
  if (!moveToFilter(ObjectivePosition)) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":MOVE_TO_OBJECTIVE ERROR: Movement failed");
    return false;
  }
	*/
  
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":MOVE_TO_OBJECTIVE: Moving to filter ");
  DEBUG_PRINTLN(ObjectivePosition);
  return true;
}
