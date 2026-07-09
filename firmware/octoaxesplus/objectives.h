#ifndef OBJECTIVES_H
#define OBJECTIVES_H

#include "axis.h"

class Objectives : public Axis {
public:
  // 构造函数
  Objectives(uint8_t csPin, uint8_t axisIndex, const char* axisName, uint8_t objectivesCount = 4);
  
  // 重写基类的初始化函数，添加滤光轮特有配置
  bool begin(const AxisConfig& config) override;
  
  // 重写状态机更新，添加滤光轮特有逻辑
  void update() override;
  
  // 重写命令处理，添加滤光轮特有命令
  bool processCommand(const String& command) override;
  
private:
	void performHomingSequence() override;
	void performLeavingHome() override;

  // 两段式 homing（融合 new-W-axis a3cde03）：false=第一段全速粗找感应区，
  // true=退出后第二段慢速精确逼近。慢速逼近减少过冲、保证每次 homing 停位一致
  // （解决全速过冲越过感应区再绕一圈）。
  bool _slowApproach = false;
  // 第二段慢速逼近的速度比例（相对 homingVelocityMM）。0.1 = 第一段的 1/10。
  static constexpr float SLOW_APPROACH_RATIO = 0.1f;

  uint8_t _objectivesCount;
  uint8_t _currentObjective;
  float* _objectivePositions;

  bool handleMoveToObjective(const String& command);
  bool handleSetLimits(const String& command) override;
};

#endif
