#ifndef FILTER_WHEEL_H
#define FILTER_WHEEL_H

#include "axis.h"

class FilterWheel : public Axis {
public:
  // 构造函数
  FilterWheel(uint8_t csPin, uint8_t axisIndex, const char* axisName, uint8_t filterCount = 8);
  
  // 重写基类的初始化函数，添加滤光轮特有配置
  bool begin(const AxisConfig& config) override;
  
  // 滤光轮特有功能
  bool moveToFilter(uint8_t filterPosition);
  uint8_t getCurrentFilter() const;
  uint8_t getFilterCount() const;
  
  // 重写状态机更新，添加滤光轮特有逻辑
  void update() override;
  
  // 重写命令处理，添加滤光轮特有命令
  bool processCommand(const String& command) override;
  
  // 设置滤光轮位置映射
  void setFilterPositions(const float* positions, uint8_t count);
  
private:
	void performHomingSequence() override;
	void performLeavingHome() override;

  uint8_t _filterCount;
  uint8_t _currentFilter;
  float* _filterPositions; // 每个滤光片对应的位置（毫米）
  
  // 滤光轮特有方法
	bool handleSetLimits(const String& command) override;

  bool handleMoveToFilter(const String& command);
  float getFilterPosition(uint8_t filterIndex) const;
  bool isValidFilterPosition(uint8_t filterPosition) const;
};

#endif
