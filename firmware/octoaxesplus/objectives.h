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

  uint8_t _objectivesCount;
  uint8_t _currentObjective;
  float* _objectivePositions;

  bool handleMoveToObjective(const String& command);
  bool handleSetLimits(const String& command) override;
};

#endif
