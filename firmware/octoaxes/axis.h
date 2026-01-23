#ifndef AXIS_H
#define AXIS_H

#include "TMC4361A_Fields.h"
#include "tmc/motion/MotorControl.h"
#include <SPI.h>

// 限位开关和方向常量
#define LEFT_SW 0b01
#define RGHT_SW 0b10
#define LEFT_DIR -1
#define RGHT_DIR 1
#define OBSW_SW 0b01 // 用于 Objectives 类

// 状态定义 - 使用更明确的状态命名
enum AxisState {
  STATE_IDLE,
  STATE_HOMING_INIT,
  STATE_HOMING_SEARCH,
  STATE_HOMING_SET_ZERO,
  STATE_LEAVING_HOME,
  STATE_MOVING,
  STATE_ERROR
};

class Axis {

public:
  // 配置参数
  struct AxisConfig {
    uint32_t clockFrequency;
    uint8_t homingSwitch;
    uint8_t leftSwitchPolarity;
    uint8_t rightSwitchPolarity;
    uint8_t leftIsInactive;
    uint8_t rightIsInactive;
    bool leftFlipped;
    bool rightFlipped;
    bool enableLeftLimitSwitch;
    bool enableRightLimitSwitch;
    float r_sense;
    float screwPitchMM;
    int fullStepsPerRev;
    int microstepping;
    float maxVelocityMM;
    float maxAccelerationMM;
    float homingVelocityMM;
    float motorCurrentMA;
    float holdCurrent;
    float homeSafetyMarginMM;
    float homeSafetyPositionMM;
    bool enableStallSensitivity;
    int stallSensitivity;
    uint32_t homing_timeout_ms;
    int8_t homing_direct;
  };

protected:
  // 保护成员变量，派生类可以访问
  uint8_t _csPin;
  uint8_t _axisIndex;
  const char *_axisName;

  // IC 标识符
  uint8_t _icID;

  // 运动参数
  uint32_t _maxVelocityMicrosteps;
  uint32_t _maxAccelerationMicrosteps;

  // 新增：状态变化检测
  AxisState _lastReportedState;       // 上次上报的状态
  bool _stateChanged;                 // 状态是否发生变化标志
  unsigned long _lastStateReportTime; // 上次状态上报时间

  // 状态变量
  AxisState _currentState;
  AxisState _previousState;
  unsigned long _stateStartTime;
  bool _homeFound;

  // 新增：移动状态标志
  bool _isMoving;
  int32_t _lastPosition; // 上次位置，用于检测是否真正在移动
  int32_t _moveDirection;

  // 新增：轴使能状态
  bool _isEnabled;

  AxisConfig _config;

  // 超时设置
  static const unsigned long LEAVING_HOME_TIMEOUT_MS = 5000;
  static const unsigned long MOVEMENT_TIMEOUT_MS = 5000;

  elapsedMicros _checkHomeReachTimeout;

  uint32_t _homing_timeout_ms;

public:
  // 构造函数
  Axis(uint8_t csPin, uint8_t axisIndex, const char *axisName);

  // 虚析构函数
  virtual ~Axis() = default;

  // 初始化函数
  virtual bool begin(const AxisConfig &config);

  // 状态机更新 - 声明为虚函数以便派生类重写
  virtual void update();

  // 极限位置检查
  virtual void checkLimitPosition();

  // 新增：中断服务函数中调用的移动状态检测
  virtual void checkMovementComplete();

  // 命令处理 - 返回处理结果
  virtual bool processCommand(const String &command);

  // 新增：状态上报控制
  virtual void reportStateIfChanged(bool force = false);
  virtual void setStateChangeFlag() { _stateChanged = true; }

  // 运动控制
  virtual bool moveToPosition(float positionMM);
  virtual bool moveRelative(float distanceMM);
  virtual void setSpeed(float speedMM);
  virtual void smoothStop();

  void disableAxis();
  void enableAxis();

  // 位置控制
  virtual void setCurrentPosition(float positionMM);
  virtual float getCurrentPositionMM() const;
  virtual int32_t getCurrentPosition() const;
  virtual int32_t getCurrentPositionMicrosteps() const;
  virtual void setMotionParameters(float maxVelocityMM,
                                   float maxAccelerationMM);

  // 归位控制
  virtual bool startHoming();
  virtual bool handleReset();
  virtual bool isHomingInProgress() const;
  virtual bool isMovementComplete() const;

  // 新增：移动状态查询
  virtual bool isMoving() const { return _isMoving; }

  // 新增：使能状态查询
  virtual bool isEnabled() const { return _isEnabled; }

  // 限位设置
  virtual void setSoftLimits(float lowerLimitMM, float upperLimitMM);
  virtual void enableSoftLimits(bool enable);

  // 状态查询
  virtual AxisState getCurrentState() const;
  virtual const char *getAxisName() const;
  virtual bool isInErrorState() const;
  virtual uint32_t readAxisEvent() const;

  // 限位开关状态
  virtual uint8_t readLimitSwitches() const;
  virtual uint8_t readSwitchEvent() const;

  // 移动轴的接口
  bool moveAxis(int32_t value);

protected:
  // 保护成员方法，派生类可以访问
  virtual void performHomingSequence() = 0;
  virtual void performLeavingHome() = 0;

  virtual void setState(AxisState newState);
  virtual void handleError(const char *errorMsg);
  virtual bool checkTimeout(unsigned long timeoutMs) const;
  virtual int32_t hexStringToInt32(String hex);

  // 命令处理辅助方法
  virtual bool handleGetPosition();
  virtual bool handleSetLimits(const String &command) = 0;
  virtual bool handleMoveAxis(const String &command);
  virtual bool handleMoveToAxis(const String &command);
  virtual bool handleHoming();
  virtual bool handleGetData();
  virtual bool handleEmergency();
  virtual bool handleAxisAbilityToggle(bool);

  // 单位转换
  virtual int32_t mmToMicrosteps(float mm) const;
  virtual float microstepsToMM(int32_t microsteps) const;
  virtual uint32_t velocityMMToMicrosteps(float velocityMM) const;
  virtual uint32_t accelerationMMToMicrosteps(float accelerationMM) const;

  // 运动检查
  virtual bool isValidPosition(float positionMM) const;
  virtual bool isWithinSoftLimits(int32_t microsteps) const;

  // 新增：移动状态管理
  virtual void startMovement();
  virtual void completeMovement();
};

#endif
