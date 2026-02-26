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
    restoreNormalMicrosteps();
    handleError("Homing timeout");
    return;
  }

  uint8_t limit_state = readLimitSwitches();

  switch (_currentState) {
    case STATE_HOMING_INIT:
      enableSoftLimits(false);
      switchToHomingMicrosteps();

      if (limit_state & _config.homingSwitch) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Already at home position, moving away first...");
        setState(STATE_LEAVING_HOME);
      } else {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Starting homing process...");
        int32_t speedInternal = _config.homing_direct * motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
        DEBUG_PRINT(_axisName);
        DEBUG_PRINT(":homing_direct=");
        DEBUG_PRINT(_config.homing_direct);
        DEBUG_PRINT(" homingVelocityMM=");
        DEBUG_PRINT(_config.homingVelocityMM);
        DEBUG_PRINT(" speedInternal=");
        DEBUG_PRINTLN(speedInternal);
        motor_setVelocityInternal(_icID, speedInternal);
        setState(STATE_HOMING_SEARCH);
      }
      break;

    case STATE_HOMING_SEARCH:
      {
        // 周期性打印 debug：每 200ms 打印一次限位状态
        static unsigned long lastDbgTime = 0;
        if (millis() - lastDbgTime > 200) {
          uint32_t rawStatus = motor_readStatus(_icID);
          int32_t xactual = motor_getPositionMicrosteps(_icID);
          int32_t vactual = motor_getVelocityInternal(_icID);
          DEBUG_PRINT(_axisName);
          DEBUG_PRINT(":SEARCH limit_state=0x");
          Serial.print(limit_state, HEX);
          DEBUG_PRINT(" homingSwitch=0x");
          Serial.print(_config.homingSwitch, HEX);
          DEBUG_PRINT(" STATUS=0x");
          Serial.print(rawStatus, HEX);
          DEBUG_PRINT(" XACTUAL=");
          DEBUG_PRINT(xactual);
          DEBUG_PRINT(" VACTUAL=");
          DEBUG_PRINTLN(vactual);
          lastDbgTime = millis();
        }
      }
      if (limit_state & _config.homingSwitch) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Home limit switch triggered!");

        // 读取触发前状态
        uint32_t statusBefore = motor_readStatus(_icID);
        int32_t xactualBefore = motor_getPositionMicrosteps(_icID);
        DEBUG_PRINT(_axisName);
        DEBUG_PRINT(":Before stop - XACTUAL=");
        DEBUG_PRINT(xactualBefore);
        DEBUG_PRINT(" STATUS=0x");
        Serial.println(statusBefore, HEX);

        motor_setVelocityInternal(_icID, 0); // 停止

        // 等待完全停止
        delay(100);

        // 确认停车结果
        int32_t xactualAfterStop = motor_getPositionMicrosteps(_icID);
        int32_t vactual = motor_getVelocityInternal(_icID);
        DEBUG_PRINT(_axisName);
        DEBUG_PRINT(":After stop - XACTUAL=");
        DEBUG_PRINT(xactualAfterStop);
        DEBUG_PRINT(" VACTUAL=");
        DEBUG_PRINTLN(vactual);

        int32_t latchedPosition = motor_readLatchPosition(_icID);
        DEBUG_PRINT(_axisName);
        DEBUG_PRINT(":Latched position: ");
        DEBUG_PRINTLN(latchedPosition);

        // 计算安全位置（离开限位开关）
        int32_t safePosition = latchedPosition;
        int32_t margin = motor_mmToMicrosteps(_icID, _config.homeSafetyPositionMM);
        if (_config.homingSwitch == RGHT_SW) {
          safePosition -= margin;
          DEBUG_PRINT(_axisName);
          DEBUG_PRINT(":RGHT_SW: safePos = latched(");
          DEBUG_PRINT(latchedPosition);
          DEBUG_PRINT(") - margin(");
          DEBUG_PRINT(margin);
          DEBUG_PRINT(") = ");
          DEBUG_PRINTLN(safePosition);
        } else {
          safePosition += margin;
          DEBUG_PRINT(_axisName);
          DEBUG_PRINT(":LEFT_SW: safePos = latched + margin = ");
          DEBUG_PRINTLN(safePosition);
        }

        DEBUG_PRINT(_axisName);
        DEBUG_PRINT(":Moving to safe position: ");
        DEBUG_PRINTLN(safePosition);

        motor_moveToMicrosteps(_icID, safePosition);
        _checkHomeReachTimeout = 0;

        setState(STATE_HOMING_SET_ZERO);
      }
      break;

    case STATE_HOMING_SET_ZERO:
      // 等待移动到安全位置完成（超时为 5 秒 = 5,000,000 微秒）
      if (isMovementComplete() || _checkHomeReachTimeout >= 5000000) {
        // 恢复正常细分
        restoreNormalMicrosteps();
        // 设置当前位置为0
        motor_setCurrentPositionMicrosteps(_icID, 0);
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Homing completed! Current position set to 0");
        if (_checkHomeReachTimeout >= 5000000) {
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":Homing Set Current Position to safe position Timeout");
        }
        // 注意：不自动启用软限位，等待上位机发送 SET_LIMITS 命令设置范围后再启用
        // 如果此时启用软限位但范围未设置（默认为0），会导致电机无法移动
        // enableSoftLimits(true);

        // Homing 完成后自动恢复 PID（与旧架构一致）
        if (_pidState.enabled) {
          motor_enablePID(_icID);
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":PID re-enabled after homing");
        }

        setState(STATE_IDLE);
      } else {
        // 可选：添加进度显示
        static unsigned long lastProgressTime = 0;
        if (millis() - lastProgressTime > 500) {
          DEBUG_PRINT(_axisName);
          DEBUG_PRINT(":Moving to safe position... Current: ");
          DEBUG_PRINT(getCurrentPositionMM());
          DEBUG_PRINT("mm, Target: ");
          DEBUG_PRINTLN(motor_microstepsToMM(_icID, motor_getTargetMicrosteps(_icID)));
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
      motor_setVelocityInternal(_icID, 0); // 停止

      // 等待完全停止
      delay(100);

      // 开始真正的归位搜索
      int32_t speedInternal = _config.homing_direct * motor_velocityMMToInternal(_icID, _config.maxVelocityMM);
      motor_setVelocityInternal(_icID, speedInternal);
      setState(STATE_HOMING_SEARCH);
    } else {
      // 继续移动离开home位置
      // 根据限位开关类型设置正确的离开方向
      int32_t speedInternal = -1 * _config.homing_direct * motor_velocityMMToInternal(_icID, _config.maxVelocityMM);
      motor_setVelocityInternal(_icID, speedInternal);
    }
  }
}

