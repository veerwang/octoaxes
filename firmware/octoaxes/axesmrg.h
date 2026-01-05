#ifndef AXES_MANAGER_H
#define AXES_MANAGER_H

#include <Arduino.h>
#include "axis.h"
#include "config.h"

class AxisManager {
private:
  static const uint8_t MAX_AXES = 8;  // 最大支持8个轴
  Axis* axes[MAX_AXES];              // 轴对象指针数组
  uint8_t axisCount;                 // 当前轴数量
  
public:
  AxisManager();
  ~AxisManager();
  
  // 添加轴到管理器
  bool addAxis(Axis* axis);
  
  // 初始化所有轴
  bool beginAll();
  
  // 更新所有轴状态机
  void updateAll();
  
  // 处理串口命令
  bool processCommand(const String& command);
  
  // 获取轴数量
  uint8_t getAxisCount() const { return axisCount; }
  
  // 根据索引获取轴
  Axis* getAxis(uint8_t index);

  // 根据轴名称查找轴对象
  Axis* findAxisByName(const String& axisName);
};

extern AxisManager axisManager;  // 全局轴管理器实例

#endif
