#ifndef STEP_AXIS_H
#define STEP_AXIS_H

#include "axis.h"

class StepAxis : public Axis {
public:
  // 构造函数
  StepAxis(uint8_t csPin, uint8_t axisIndex, const char* axisName);
  
  // 重写基类的初始化函数，添加步进轴特有配置
  bool begin(const AxisConfig& config) override;
  
  // 步进轴特有功能
  void setBacklashCompensation(float backlashMM);
  void enableBacklashCompensation(bool enable);
  
  // 重写运动控制函数，添加步进轴特有逻辑
  bool moveToPosition(float positionMM) override;
  bool moveRelative(float distanceMM) override;
  
  virtual bool handleSetLimits(const String& command) override;

private:
  float _backlashMM;
  bool _backlashCompensationEnabled;
  
  // 步进轴特有方法
  void applyBacklashCompensation(int32_t direction);

	void performHomingSequence() override;
	void performLeavingHome() override;
};

#endif

