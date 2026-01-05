#include "axesmrg.h"
#include "build_opt.h"

AxisManager axisManager;  // 定义全局实例

AxisManager::AxisManager() {
  axisCount = 0;
  // 初始化指针数组为nullptr
  for (uint8_t i = 0; i < MAX_AXES; i++) {
    axes[i] = nullptr;
  }
}

AxisManager::~AxisManager() {
  // 清理资源
  for (uint8_t i = 0; i < axisCount; i++) {
    if (axes[i] != nullptr) {
      delete axes[i];
      axes[i] = nullptr;
    }
  }
}

bool AxisManager::addAxis(Axis* axis) {
  if (axisCount >= MAX_AXES || axis == nullptr) {
    DEBUG_PRINTLN("Cannot add axis: maximum limit reached or null axis");
    return false;
  }
  
  axes[axisCount] = axis;
  axisCount++;
  
  DEBUG_PRINT("Axis added: ");
  DEBUG_PRINTLN(axis->getAxisName());  // 修正：使用正确的函数名 getAxisName()
  DEBUG_PRINT("Total axes: ");
  DEBUG_PRINTLN(axisCount);
  
  return true;
}

bool AxisManager::beginAll() {
  bool allSuccess = true;
  
  for (uint8_t i = 0; i < axisCount; i++) {
    if (axes[i] != nullptr) {
      bool success = false;
      
      // 根据轴名称选择相应的配置
      String axisName = String(axes[i]->getAxisName());  // 修正：使用 getAxisName() 并转换为 String
      
      // 修正：使用 equals() 方法进行字符串比较
      if (axisName.equals("X")) {
        success = axes[i]->begin(AxisConfigs::X_AXIS);
      } else if (axisName.equals("Y")) {
        success = axes[i]->begin(AxisConfigs::Y_AXIS);
      } else if (axisName.equals("Z")) {
        success = axes[i]->begin(AxisConfigs::Z_AXIS);
      } else if (axisName.equals("W")) {
        success = axes[i]->begin(AxisConfigs::W_AXIS);
      } else if (axisName.equals("E1")) {
        success = axes[i]->begin(AxisConfigs::EXPAND1_AXIS);
      } else if (axisName.equals("E3")) {
        success = axes[i]->begin(AxisConfigs::EXPAND3_AXIS);
      } else if (axisName.equals("E4")) {
        success = axes[i]->begin(AxisConfigs::EXPAND4_AXIS);
      } else {
        DEBUG_PRINT("Unknown axis configuration for: ");
        DEBUG_PRINTLN(axisName);
        success = false;
      }
      
      if (!success) {
        DEBUG_PRINT("Failed to initialize axis: ");
        DEBUG_PRINTLN(axisName);
        allSuccess = false;
      } else {
        DEBUG_PRINT("Successfully initialized axis: ");
        DEBUG_PRINTLN(axisName);
      }
    }
  }
  
  return allSuccess;
}

void AxisManager::updateAll() {
  for (uint8_t i = 0; i < axisCount; i++) {
    if (axes[i] != nullptr) {
      axes[i]->update();
    }
  }
}

Axis* AxisManager::findAxisByName(const String& axisName) {
  for (uint8_t i = 0; i < axisCount; i++) {
    // 修正：使用 getAxisName() 并转换为 String 进行比较
    if (axes[i] != nullptr && String(axes[i]->getAxisName()).equals(axisName)) {
      return axes[i];
    }
  }
  return nullptr;
}

bool AxisManager::processCommand(const String& command) {
  // 命令格式: "轴名称:命令内容"，例如 "E3:HOMING"
  int colonIndex = command.indexOf(':');
  
  if (colonIndex == -1) {
    DEBUG_PRINTLN("Invalid command format. Expected: AXIS:COMMAND");
    return false;
  }
  
  String axisName = command.substring(0, colonIndex);
  String cmd = command.substring(colonIndex + 1);
  
  axisName.trim();
  cmd.trim();
  
  if (axisName.length() == 0 || cmd.length() == 0) {
    DEBUG_PRINTLN("Empty axis name or command");
    return false;
  }
  
  // 查找对应的轴
  Axis* targetAxis = findAxisByName(axisName);
  if (targetAxis == nullptr) {
    DEBUG_PRINT("Axis not found: ");
    DEBUG_PRINTLN(axisName);
    return false;
  }
  
  // 将命令传递给对应的轴处理
  bool success = targetAxis->processCommand(cmd);
  
  if (success) {
    DEBUG_PRINT("Command '");
    DEBUG_PRINT(cmd);
    DEBUG_PRINT("' sent to axis ");
    DEBUG_PRINTLN(axisName);
  } else {
    DEBUG_PRINT("Failed to process command '");
    DEBUG_PRINT(cmd);
    DEBUG_PRINT("' on axis ");
    DEBUG_PRINTLN(axisName);
  }
  
  return success;
}

Axis* AxisManager::getAxis(uint8_t index) {
  if (index < axisCount) {
    return axes[index];
  }
  return nullptr;
}
