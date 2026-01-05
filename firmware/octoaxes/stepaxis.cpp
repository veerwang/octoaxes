#include "stepaxis.h"
#include "build_opt.h"

StepAxis::StepAxis(uint8_t csPin, uint8_t axisIndex, const char* axisName) 
  : Axis(csPin, axisIndex, axisName) {
  _backlashMM = 0.0f;
  _backlashCompensationEnabled = false;
}

bool StepAxis::begin(const AxisConfig& config) {
  // 调用基类初始化
  bool result = Axis::begin(config);
  
  if (result) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":StepAxis initialized successfully");
  }
  
  return result;
}

void StepAxis::setBacklashCompensation(float backlashMM) {
  _backlashMM = backlashMM;
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Backlash compensation set to ");
  DEBUG_PRINTF(backlashMM, 3);
  DEBUG_PRINTLN("mm");
}

void StepAxis::enableBacklashCompensation(bool enable) {
  _backlashCompensationEnabled = enable;
  DEBUG_PRINT(_axisName);
  DEBUG_PRINT(":Backlash compensation ");
  DEBUG_PRINTLN(enable ? "enabled" : "disabled");
}

bool StepAxis::moveToPosition(float positionMM) {
  // 在步进轴中，可以添加反向间隙补偿逻辑
  if (_backlashCompensationEnabled && _backlashMM > 0) {
    // 计算移动方向
    float currentPos = getCurrentPositionMM();
    int32_t direction = (positionMM > currentPos) ? 1 : -1;
    
    // 应用反向间隙补偿
    applyBacklashCompensation(direction);
  }
  
  // 调用基类移动函数
  return Axis::moveToPosition(positionMM);
}

bool StepAxis::moveRelative(float distanceMM) {
  // 在步进轴中，可以添加反向间隙补偿逻辑
  if (_backlashCompensationEnabled && _backlashMM > 0) {
    int32_t direction = (distanceMM > 0) ? 1 : -1;
    applyBacklashCompensation(direction);
  }
  
  // 调用基类移动函数
  return Axis::moveRelative(distanceMM);
}

void StepAxis::applyBacklashCompensation(int32_t direction) {
  // 简单的反向间隙补偿实现
  // 在实际应用中可能需要更复杂的逻辑
  if (_backlashMM > 0) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINT(":Applying backlash compensation: ");
    DEBUG_PRINTF(_backlashMM, 3);
    DEBUG_PRINTLN("mm");
    
    // 先向反方向移动消除间隙，再向目标方向移动
    float compensationDistance = direction * _backlashMM;
    Axis::moveRelative(compensationDistance);
    
    // 等待补偿移动完成
    while (isMoving()) {
      delay(10);
    }
  }
}

bool StepAxis::handleSetLimits(const String& command) {
  int space1 = command.indexOf(' ');
  int space2 = command.indexOf(' ', space1 + 1);
  int space3 = command.indexOf(' ', space2 + 1);
  
  if (space1 == -1 || space2 == -1 || space3 == -1) {
    DEBUG_PRINT(_axisName);
    DEBUG_PRINTLN(":SET_LIMITS ERROR: Invalid format");
    return false;
  }
  
  String s_down_limit = command.substring(space2 + 1, space3);
  String s_up_limit = command.substring(space3 + 1);

  int32_t down_limit = hexStringToInt32(s_down_limit);
  int32_t up_limit = hexStringToInt32(s_up_limit);

  float lowerLimitMM = down_limit / 1000.0;
  float upperLimitMM = up_limit / 1000.0;

	DEBUG_PRINT("1.LowLimit: ");
	DEBUG_PRINTLN(lowerLimitMM);

	DEBUG_PRINT("2.UpperLimit: ");
	DEBUG_PRINTLN(upperLimitMM);
  
  setSoftLimits(lowerLimitMM, upperLimitMM);
  DEBUG_PRINT(_axisName);
  DEBUG_PRINTLN(":SET_LIMITS OK");
  return true;
}


void StepAxis::performHomingSequence() {
  if (checkTimeout(_homing_timeout_ms)) {
    handleError("Homing timeout");
    return;
  }

  uint8_t limit_state = readLimitSwitches();
  
  switch (_currentState) {
    case STATE_HOMING_INIT:
      enableSoftLimits(false);
      
      if (limit_state & _config.homingSwitch) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Already at home position, moving away first...");
        setState(STATE_LEAVING_HOME);
      } else {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Starting homing process...");
        int32_t speedMicrosteps = _config.homing_direct * velocityMMToMicrosteps(_config.homingVelocityMM);
        tmc4361A_setSpeed(&_tmc4361, speedMicrosteps);
        setState(STATE_HOMING_SEARCH);
      }
      break;
      
    case STATE_HOMING_SEARCH:
      if (limit_state & _config.homingSwitch) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Home limit switch triggered!");
        tmc4361A_setSpeed(&_tmc4361, 0); // 直接停止
        
        // 等待完全停止
        delay(100);
        
        int32_t latchedPosition = tmc4361A_readInt(&_tmc4361, TMC4361A_X_LATCH_RD);
        DEBUG_PRINT(_axisName);
        DEBUG_PRINT(":Latched position: ");
        DEBUG_PRINTLN(latchedPosition);	
        if (_config.homingSwitch == RGHT_SW) {
          _tmc4361.xmax = latchedPosition;
        } else {
          _tmc4361.xmin = latchedPosition;
        }
        // 计算安全位置（离开限位开关）
        int32_t safePosition = latchedPosition;
        if (_config.homingSwitch == RGHT_SW) {
          safePosition -= mmToMicrosteps(_config.homeSafetyPositionMM);
        } else {
          safePosition += mmToMicrosteps(_config.homeSafetyPositionMM);
        }
        
        DEBUG_PRINT(_axisName);
        DEBUG_PRINT(":Moving to safe position: ");
        DEBUG_PRINTLN(safePosition);
        
        tmc4361A_moveTo(&_tmc4361, safePosition);
        _checkHomeReachTimeout = 0;

        setState(STATE_HOMING_SET_ZERO);
      }
      break;
      
    case STATE_HOMING_SET_ZERO:
      // 等待移动到安全位置完成
      if (isMovementComplete() || _checkHomeReachTimeout >= 1000 * 1000) {
        // 设置当前位置为0
        tmc4361A_setCurrentPosition(&_tmc4361, 0);
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Homing completed! Current position set to 0");
        if (_checkHomeReachTimeout > 1000 * 1000)
          DEBUG_PRINTLN(":Homing Set Current Position to safe position Timeout");
        enableSoftLimits(true);
        
        setState(STATE_IDLE);
      } else {
        // 可选：添加进度显示
        static unsigned long lastProgressTime = 0;
        if (millis() - lastProgressTime > 500) {
          DEBUG_PRINT(_axisName);
          DEBUG_PRINT(":Moving to safe position... Current: ");
          DEBUG_PRINT(getCurrentPositionMM());
          DEBUG_PRINT("mm, Target: ");
          DEBUG_PRINTLN(microstepsToMM(tmc4361A_targetPosition(&_tmc4361)));
          lastProgressTime = millis();
        }
      }
      break;
      
    default:
      break;
  }
}

void StepAxis::performLeavingHome() {
  if (checkTimeout(LEAVING_HOME_TIMEOUT_MS)) {
    handleError("Leaving home timeout");
    return;
  }

  uint8_t limit_state = readLimitSwitches();
  
  if (_currentState == STATE_LEAVING_HOME) {
    if (!(limit_state & _config.homingSwitch)) {
      DEBUG_PRINT(_axisName);
      DEBUG_PRINTLN(":Left home position, starting homing...");
      tmc4361A_setSpeed(&_tmc4361, 0); // 直接停止
      
      // 等待完全停止
      delay(100);
      
      // 开始真正的归位搜索
      int32_t speedMicrosteps = _config.homing_direct * velocityMMToMicrosteps(_config.maxVelocityMM);
      tmc4361A_setSpeed(&_tmc4361, speedMicrosteps);
      setState(STATE_HOMING_SEARCH);
    } else {
      // 继续移动离开home位置
      // 根据限位开关类型设置正确的离开方向
      int32_t speedMicrosteps;
      speedMicrosteps = -1 *_config.homing_direct * velocityMMToMicrosteps(_config.maxVelocityMM); // 向左移动离开右限位
      tmc4361A_setSpeed(&_tmc4361, speedMicrosteps);
    }
  }
}

