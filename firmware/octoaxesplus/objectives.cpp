#include "objectives.h"
#include "build_opt.h"

Objectives::Objectives(uint8_t csPin, uint8_t axisIndex, const char* axisName, uint8_t objectivesCount) 
  : Axis(csPin, axisIndex, axisName), _objectivesCount(objectivesCount), _currentObjective(0) {
  _objectivePositions = new float[objectivesCount];

  // Initialize default positions: evenly spaced, assuming each objective is 90 degrees apart
  for (uint8_t i = 0; i < objectivesCount; i++) {
    _objectivePositions[i] = i * (360.0f / objectivesCount); // in degrees; must be converted to mm when actually used
  }
  
}

bool Objectives::begin(const AxisConfig& config) {
  // call the base-class init
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
  // call the base-class update first
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
    // hand other commands to the base class
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
      // directly disable the virtual limits in hardware without changing the _softLimitsEnabled flag
      motor_enableSoftLimits(_icID, false, false);

      // Enable the right hard-stop (STOP_RIGHT_EN): during homing it's needed for hardware-level
      // hard-stop anti-overshoot when the fast search hits the home zone. During normal operation
      // this hard-stop is off (see the end of SET_ZERO), so it must be explicitly enabled at the
      // start of every homing. (merged from new-W-axis b97e814)
      motor_setHardwareStopEnable(_icID, RGHT_SW, true);

      // Unlock the hard-stop latch (aligned with StepAxis::performHomingSequence INIT, merged from aad7b42).
      // If the start point is already pressed on the home/reference switch, the chip's hard-stop latch
      // is active, and a later motor_setVelocityInternal only writes VMAX without releasing the latch ->
      // the motor doesn't move -> the LEAVING stage times out after 5s. Writing XTARGET=XACTUAL causes
      // no movement, it only triggers the chip to re-evaluate the ramp state and reset the latch.
      motor_moveToMicrosteps(_icID, motor_getPositionMicrosteps(_icID));

      switchToHomingMicrosteps();
      _slowApproach = false;  // two-stage: start from stage 1 (fast coarse search)

      if (limit_state == _config.homingSwitch) {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Already at home position, moving away first...");
        setState(STATE_LEAVING_HOME);
      } else {
        DEBUG_PRINT(_axisName);
        DEBUG_PRINTLN(":Starting homing process...");

        DEBUG_PRINTLN(_config.homingVelocityMM);
        // direction decided by config homing_direct (consistent with StepAxis, no longer hardcoded. merged from 9e72ddd)
        int32_t speedInternal = _config.homing_direct * motor_velocityMMToInternal(_icID, _config.homingVelocityMM);
        motor_setVelocityInternal(_icID, speedInternal);
        setState(STATE_HOMING_SEARCH);
      }
      break;

    case STATE_HOMING_SEARCH:
      if (limit_state == _config.homingSwitch) {
        motor_setVelocityInternal(_icID, 0);  // stop first
        delay(100);                            // wait until fully stopped before deciding, to avoid reversing with speed

        if (!_slowApproach) {
          // stage 1 (full speed) found the sensor zone -> back off to do stage 2 slow precise approach (merged from a3cde03)
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":Sensor found (fast), backing off for slow approach...");
          _slowApproach = true;
          setState(STATE_LEAVING_HOME);
        } else {
          // stage 2 (slow) precise approach complete -> lock and zero
          DEBUG_PRINT(_axisName);
          DEBUG_PRINTLN(":Sensor found (slow), homing locked!");
          motor_setCurrentPositionMicrosteps(_icID, 0);
          _checkHomeReachTimeout = 0;
          setState(STATE_HOMING_SET_ZERO);
        }
      }
      break;

    case STATE_HOMING_SET_ZERO:
      // wait for the move to the safe position to complete
      if (isMovementComplete() || _checkHomeReachTimeout >= 500 * 1000) {
        // restore normal microstepping (setMotionParameters inside will rewrite VMAX)
        restoreNormalMicrosteps();

        // ★ Critical fix (merged from new-W-axis 8d01838): homing runs in velocity mode throughout;
        //   at the end the chip is left in velocity mode + restoreNormalMicrosteps writes VMAX back to
        //   full speed. If the landing point is just outside the home zone (STOPR=0), the residual +
        //   unrestrained speed lets the motor free-spin out of the zone; repeated homing pushes it out
        //   of the home zone step by step -> after ~5 times it flings out -> the next search approaches
        //   nearly a full turn toward + ("drift/circling" root cause). Switch back to position mode
        //   holding at 0 to clear the residual velocity.
        motor_moveToMicrosteps(_icID, 0);

        // ★ Fix (merged from b97e814): turn off the right hard-stop for normal operation. The objective
        //   turret is a ring with multiple stations; the home flag is only used during homing. Normal
        //   station changes must be able to rotate freely through the home zone. If the hard-stop stayed
        //   on, a station change toward the home zone would be blocked at the edge and never arrive. So
        //   turn off the right hard-stop at the end of homing (the next homing's INIT re-enables it).
        //   Software polling reads the STOPR_ACTIVE_F live bit and X_LATCH uses LATCH_X_ON_ACTIVE_R,
        //   both independent of STOP_RIGHT_EN, so homing detection is unaffected.
        motor_setHardwareStopEnable(_icID, RGHT_SW, false);

        // set the current position to 0
        DEBUG_PRINT(_axisName);

        if (_checkHomeReachTimeout > 500 * 1000) {
          DEBUG_PRINTLN(":Homing Set Current Position to 0 position Timeout");
        }

        DEBUG_PRINTLN(":Homing completed! Current position set to 0");

        // after homing completes, restore soft limits and PID
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
        // optional: add a progress display
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
    // two-stage: in stage 2 (_slowApproach=true) both leaving and re-approaching use the slow speed, reducing overshoot and improving repeatability.
    float homingVel = _config.homingVelocityMM * (_slowApproach ? SLOW_APPROACH_RATIO : 1.0f);

    if (!(limit_state == _config.homingSwitch)) {
      DEBUG_PRINT(_axisName);
      DEBUG_PRINTLN(_slowApproach ? ":Left sensor, slow approach..."
                                  : ":Left home position, starting homing...");

      // start the homing search (direction decided by homing_direct, consistent with StepAxis. merged from 9e72ddd)
      int32_t speedInternal = _config.homing_direct * motor_velocityMMToInternal(_icID, homingVel);
      motor_setVelocityInternal(_icID, speedInternal);
      setState(STATE_HOMING_SEARCH);
    } else {
      // keep moving to leave the home position: the direction must = opposite of the search
      // direction (-homing_direct).
      // Fix (merged from dddeff3): the original homingSwitch check made leaving the same direction
      // as the search -> when the start point is already at home it keeps pressing the sensor and
      // can't leave -> never enters search -> homing times out. Consistent with StepAxis, use
      // -homing_direct.
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
