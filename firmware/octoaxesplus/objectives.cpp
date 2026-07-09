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
      // 直接操作硬件禁用虚拟限位，不改变 _softLimitsEnabled 标志
      motor_enableSoftLimits(_icID, false, false);

      // 开右硬停（STOP_RIGHT_EN）：homing 期间需要它在 fast search 撞到 home 区时
      // 硬件级硬停防过冲。正常运行期间此硬停是关的（见 SET_ZERO 末尾），故每次
      // homing 开始都要显式打开。（融合 new-W-axis b97e814）
      motor_setHardwareStopEnable(_icID, RGHT_SW, true);

      // 解锁 hard-stop latch（对齐 StepAxis::performHomingSequence INIT，融合 aad7b42）。
      // 若起点已压在 home/参考开关上，chip 的 hard-stop latch 处于激活态，
      // 后续 motor_setVelocityInternal 仅写 VMAX 解不开 latch → 电机不动 →
      // LEAVING 阶段 5 秒超时。写 XTARGET=XACTUAL 不引起移动，仅触发 chip
      // 重新评估 ramp 状态复位 latch。
      motor_moveToMicrosteps(_icID, motor_getPositionMicrosteps(_icID));

      switchToHomingMicrosteps();
      _slowApproach = false;  // 两段式：从第一段(全速粗找)开始

      if (limit_state == _config.homingSwitch) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Already at home position, moving away first...");
        setState(STATE_LEAVING_HOME);
      } else {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Starting homing process...");

        DEBUG_PRINTLN(_config.homingVelocityMM);
        // 方向由配置 homing_direct 决定（与 StepAxis 一致，不再写死。融合 9e72ddd）
        int32_t speedInternal = _config.homing_direct * motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
        motor_setVelocityInternal(_icID, speedInternal);
        setState(STATE_HOMING_SEARCH);
      }
      break;

    case STATE_HOMING_SEARCH:
      if (limit_state == _config.homingSwitch) {
        motor_setVelocityInternal(_icID, 0);  // 先停车
        delay(100);                            // 等完全停稳再决定，避免带速反转

        if (!_slowApproach) {
          // 第一段(全速)找到感应区 → 退出去做第二段慢速精确逼近（融合 a3cde03）
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":Sensor found (fast), backing off for slow approach...");
          _slowApproach = true;
          setState(STATE_LEAVING_HOME);
        } else {
          // 第二段(慢速)精确逼近完成 → 锁定清零
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":Sensor found (slow), homing locked!");
          motor_setCurrentPositionMicrosteps(_icID, 0);
          _checkHomeReachTimeout = 0;
          setState(STATE_HOMING_SET_ZERO);
        }
      }
      break;

    case STATE_HOMING_SET_ZERO:
      // 等待移动到安全位置完成
      if (isMovementComplete() || _checkHomeReachTimeout >= 500 * 1000) {
        // 恢复正常细分（内部 setMotionParameters 会重写 VMAX）
        restoreNormalMicrosteps();

        // ★ 关键修复（融合 new-W-axis 8d01838）：homing 全程跑 velocity 模式，结束时
        //   芯片停在 velocity 模式 + restoreNormalMicrosteps 又把 VMAX 写回满速。落点恰在
        //   home 区外(STOPR=0)时残留 + 速度无人拦 → 电机自由旋转飞出区，反复 homing 逐次
        //   顶出 home 区 → 约 5 次后甩出 → 下次朝 + 搜近一圈（「漂移/绕圈」根因）。
        //   切回 position 模式 hold 在 0，清掉残留 velocity。
        motor_moveToMicrosteps(_icID, 0);

        // ★ 修复（融合 b97e814）：关右硬停供正常运行。物镜盘是环形多工位，home 标志只在
        //   homing 用；正常换位需能自由转过 home 区。若硬停常开，朝 home 区方向的换位会被
        //   硬停拦在边沿到不了。故 homing 结束关右硬停（下次 homing 的 INIT 重新打开）。
        //   软件轮询读 STOPR_ACTIVE_F 实时位、X_LATCH 走 LATCH_X_ON_ACTIVE_R，均与
        //   STOP_RIGHT_EN 无关，故 homing 检测不受影响。
        motor_setHardwareStopEnable(_icID, RGHT_SW, false);

        // 设置当前位置为0
        DEBUG_PRINT(_axisName);

        if (_checkHomeReachTimeout > 500 * 1000) {
          DEBUG_PRINTLN(":Homing Set Current Position to 0 position Timeout");
        }

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
    // 两段式：第二段(_slowApproach=true)离开与再逼近都用慢速，减少过冲、提升重复性。
    float homingVel = _config.homingVelocityMM * (_slowApproach ? SLOW_APPROACH_RATIO : 1.0f);

    if (!(limit_state == _config.homingSwitch)) {
      DEBUG_PRINT(_axisName);
      DEBUG_PRINTLN(_slowApproach ? ":Left sensor, slow approach..."
                                  : ":Left home position, starting homing...");

      // 开始归位搜索（方向由 homing_direct 决定，与 StepAxis 一致。融合 9e72ddd）
      int32_t speedInternal = _config.homing_direct * motor_velocityMMToInternal(_icID, homingVel);
      motor_setVelocityInternal(_icID, speedInternal);
      setState(STATE_HOMING_SEARCH);
    } else {
      // 继续移动离开 home 位置：方向必须 = 反 search 方向（-homing_direct）。
      // 修复（融合 dddeff3）：原 homingSwitch 判断让 leaving 与 search 同向 → 起点已在
      // home 时一直顶着 sensor 离不开 → 永不进 search → homing 超时。与 StepAxis 一致
      // 用 -homing_direct。
      int32_t speedInternal = -1 * _config.homing_direct * motor_velocityMMToInternal(_icID, homingVel);
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
  [[maybe_unused]] uint8_t ObjectivePosition = (uint8_t)filterStr.toInt();
  
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
