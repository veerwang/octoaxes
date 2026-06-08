#include "stepaxis.h"
#include "build_opt.h"
#include "tmc/ic/TMC4361A/TMC4361A.h"

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
      // 直接操作硬件禁用虚拟限位，不改变 _softLimitsEnabled 标志
      motor_enableSoftLimits(_icID, false, false);

      // 解锁 hard-stop latch：复用 motor_moveToMicrosteps 已验证的完整
      // VSTOP recovery 路径（禁 EN→清 EVENTS→写 XTARGET→再清 EVENTS）。
      //
      // 场景：固件复位后 XACTUAL=0，SET_LIM x_neg=5mm 立即触发 VSTOPL_ACTIVE_F
      // hard-stop，chip ramp generator 被锁住。后续 motor_setVelocityInternal
      // 仅写 VMAX 不能解除 hard-stop latch，电机不动。
      //
      // 写 XTARGET=XACTUAL 不引起电机移动，仅触发 chip 重新评估 ramp 状态，
      // 让 hard-stop latch 复位。这是 motor_moveToMicrosteps 的 VSTOP recovery
      // 路径已在 2026-02-27 commit 验证有效。
      motor_moveToMicrosteps(_icID, motor_getPositionMicrosteps(_icID));

      switchToHomingMicrosteps();

      if (limit_state & _config.homingSwitch) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Already at home position, moving away first...");
        setState(STATE_LEAVING_HOME);
      } else {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Starting homing process...");
        int32_t speedInternal = _config.homing_direct * motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
        motor_setVelocityInternal(_icID, speedInternal);
        setState(STATE_HOMING_SEARCH);
      }
      break;

    case STATE_HOMING_SEARCH:
      if (limit_state & _config.homingSwitch) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Home limit switch triggered!");

        motor_setVelocityInternal(_icID, 0); // 停止
        delay(100);                          // 等待完全停止

        int32_t latchedPosition = motor_readLatchPosition(_icID);

        // 计算安全位置（离开限位开关）
        int32_t safePosition = latchedPosition;
        int32_t margin = motor_mmToMicrosteps(_icID, _config.homeSafetyPositionMM);
        // 退回方向 = 搜索方向(homing_direct)的反方向，永远离开刚撞到的限位。
        // 比"按 homingSwitch ±margin"鲁棒：当 homingSwitch 与搜索方向不符合常规约定
        // （如新 Z：LEFT_SW 但 homing_direct=+1 朝物理左，左限位在 firmware 正方向端）
        // 时，旧逻辑会朝限位更深处退 → 离不开感应区。对常规 X/Y/Z 等价（无回归）。
        safePosition -= _config.homing_direct * margin;

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
        // 恢复 homing 前的软限位状态（上位机初始化时已设置 VIRT_STOP 值）
        if (_softLimitsEnabled) {
          enableSoftLimits(true);
        }

        // Homing 完成后自动恢复 PID（与旧架构一致）
        if (_pidState.enabled) {
          motor_enablePID(_icID);
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":PID re-enabled after homing");
        }

        setState(STATE_IDLE);
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

