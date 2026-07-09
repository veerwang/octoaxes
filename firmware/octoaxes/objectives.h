#ifndef OBJECTIVES_H
#define OBJECTIVES_H

#include "axis.h"

class Objectives : public Axis {
public:
  // Constructor
  Objectives(uint8_t csPin, uint8_t axisIndex, const char* axisName, uint8_t objectivesCount = 4);
  
  // Override the base-class init function to add filter-wheel-specific configuration
  bool begin(const AxisConfig& config) override;
  
  // Override the state-machine update to add filter-wheel-specific logic
  void update() override;
  
  // Override command processing to add filter-wheel-specific commands
  bool processCommand(const String& command) override;
  
private:
	void performHomingSequence() override;
	void performLeavingHome() override;

  // Two-stage homing (merged from new-W-axis a3cde03): false = stage 1 fast coarse search for the
  // sensor zone, true = stage 2 slow precise approach after leaving. The slow approach reduces
  // overshoot and keeps the homing stop position consistent every time (fixes the full-speed
  // overshoot past the sensor zone that then loops around another turn).
  bool _slowApproach = false;
  // Speed ratio of the stage-2 slow approach (relative to homingVelocityMM). 0.1 = 1/10 of stage 1.
  static constexpr float SLOW_APPROACH_RATIO = 0.1f;

  uint8_t _objectivesCount;
  uint8_t _currentObjective;
  float* _objectivePositions;

  bool handleMoveToObjective(const String& command);
  bool handleSetLimits(const String& command) override;
};

#endif
