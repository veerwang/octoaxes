#ifndef AXIS_H
#define AXIS_H

#include "tmc/motion/MotorControl.h"
#include <SPI.h>

// 限位开关和方向常量
#define LEFT_SW 0b01
#define RGHT_SW 0b10
#define LEFT_DIR -1
#define RGHT_DIR 1
#define OBSW_SW 0b01 // 用于 Objectives 类

// 驱动芯片类型: DRIVER_TMC2660 / DRIVER_TMC2240 (定义在 MotorControl.h)

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
    int homingMicrostepping;     // homing 时使用的细分，默认 256
    float maxVelocityMM;
    float maxAccelerationMM;
    float homingVelocityMM;
    float motorCurrentMA;           // 峰值电流 (mA), I_rms = I_peak / √2
    float holdCurrent;
    float homeSafetyMarginMM;
    float homeSafetyPositionMM;
    bool enableStallSensitivity;
    int stallSensitivity;
    bool useSShapedRamp;         // true=S形斜坡, false=梯形斜坡
    float astartMM;              // 起始加速度 (mm/s²), 0=不使用
    float dfinalMM;              // 终止减速度 (mm/s²), 0=同 astart
    uint32_t homing_timeout_ms;
    int8_t homing_direct;
    uint8_t driverType;          // 驱动芯片型号，默认 DRIVER_TMC2660
    uint8_t currentRange;        // TMC2240 CURRENT_RANGE: 0=1A, 1=2A, 2=3A (TMC2660 时忽略)
    bool enableEncoder;          // 是否启用 ABN 编码器，默认 false
    uint16_t encoderLinesPerRev; // 编码器线数 (每转), 如 4000。直接作为 transitions 使用
    bool invertEncoderDir;       // 反转编码器计数方向，默认 false
    bool invert_direction;       // 2026-05-25 硬件方向反相：true 时所有 MOVE/HOMING 命令在
                                 // firmware 层反 payload，让镜像装配的硬件（home 标志位与
                                 // 旧 Squid 设计相反）能用相同的上位机命令到达正确物理位置。
                                 // moveTo/moveRelative 反 target/delta；
                                 // getCurrentPositionMicrosteps 反 chip XACTUAL；
                                 // filterwheel.cpp homing search 反速度方向。
                                 // 默认 false（与旧 Squid 行为完全一致）。
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
  int32_t _moveDirection;
  unsigned long _cmdRecvMicros;   // 命令接收时间 (micros)
  unsigned long _moveStartMicros; // 移动开始时间 (micros)

  // 新增：轴使能状态
  bool _isEnabled;

  // 软限位状态追踪（homing 后自动恢复用）
  bool _softLimitsEnabled;

  // 软限位方向感知闸门的 shadow state：
  // SET_LIM 单边设置后记录上位机意图，与 chip 寄存器解耦。
  // 即使 motor_moveToMicrosteps recovery 临时清掉 chip 上的 EN 位，
  // 这里仍保留「该侧是否被设置过」的语义，用于 isMoveAllowedByDirection()。
  struct SoftLimitShadow {
    bool leftEnabled;        // X-/Y-/Z- 是否被 SET_LIM 设置过
    bool rightEnabled;       // X+/Y+/Z+ 是否被 SET_LIM 设置过
    int32_t leftValue;       // VIRT_STOP_LEFT 的最新设置值（微步）
    int32_t rightValue;      // VIRT_STOP_RIGHT 的最新设置值（微步）
  };
  SoftLimitShadow _softLimits = {false, false, INT32_MIN, INT32_MAX};

  // 虚拟限位 recovery 后延迟恢复标志
  // motor_moveToMicrosteps() 在 VSTOP 恢复时禁用限位，
  // 需等电机离开边界后（STATUS 中 VSTOP flags 清除）才能重新使能
  bool _needReenableLimits;

  // PID 状态（每轴独立）
  struct PIDState {
    bool enabled;         // PID 当前是否活跃
    uint16_t p;           // 缓存的 P 参数
    uint8_t  i;           // 缓存的 I 参数
    uint8_t  d;           // 缓存的 D 参数
  } _pidState = {false, 0, 0, 0};

  AxisConfig _config;

  // 超时设置
  static const unsigned long LEAVING_HOME_TIMEOUT_MS = 5000;
  static const unsigned long MOVEMENT_TIMEOUT_MS = 5000;

  elapsedMicros _checkHomeReachTimeout;

  // STATE_MOVING checkLimitPosition 节流（对齐旧 Squid check_limits 10ms 节流，
  // 减少 SPI bus 抢占；hard limit 完成判定容忍 0-10ms 延迟，chip 内部已物理停止）
  // (#5, 2026-05-19)
  elapsedMicros _limitCheckThrottle;

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
  virtual bool moveToPositionMicrosteps(int32_t targetMicrosteps);
  virtual bool moveRelativeMicrosteps(int32_t deltaMicrosteps);
  virtual void setSpeed(float speedMM);
  virtual void smoothStop();

  void disableAxis();
  void enableAxis();

  // 位置控制
  virtual void setCurrentPosition(float positionMM);
  virtual float getCurrentPositionMM() const;
  virtual int32_t getCurrentPosition() const;
  virtual int32_t getCurrentPositionMicrosteps() const;
  virtual int32_t getEncoderPositionMicrosteps() const;
  virtual void setMotionParameters(float maxVelocityMM,
                                   float maxAccelerationMM);

  // 归位控制
  virtual bool startHoming();
  virtual bool handleReset();
  virtual bool handleDebugReg();
  virtual bool isHomingInProgress() const;
  virtual bool isMovementComplete() const;

  // 新增：移动状态查询
  virtual bool isMoving() const { return _isMoving; }

  // 新增：使能状态查询
  virtual bool isEnabled() const { return _isEnabled; }

  // 软限位状态查询
  bool isSoftLimitsEnabled() const { return _softLimitsEnabled; }

  // 限位设置
  virtual void setSoftLimits(float lowerLimitMM, float upperLimitMM);
  virtual void enableSoftLimits(bool enable);
  void setOneSoftLimit(int direction, int32_t valueMicrosteps);

  // 方向感知 clamp：把 target 截到「朝更安全方向移动」原则允许的范围
  // 当前位置 C、target T、_softLimits 中 leftValue=L / rightValue=R：
  //   effective_lower = (C ≤ L) ? C : L  // 越下限时禁止再下；安全区时下界=L
  //   effective_upper = (C ≥ R) ? C : R  // 对称
  //   返回 clamp(T, effective_lower, effective_upper)
  // 未启用的那一侧不参与限制。截到边界后让电机停在边界，与旧 Squid 兼容
  // （旧 Squid 在固件 callback_move_x/y/z 里也做 min/max clamp）
  int32_t clampTargetByDirection(int32_t targetMicrosteps) const;

  // PID 控制
  void configureStagePID(bool flip_direction, uint16_t transitions_per_rev);
  void enableStagePID();
  void disableStagePID();
  void setPIDArguments(uint16_t p, uint8_t i, uint8_t d);
  bool isPIDEnabled() const { return _pidState.enabled; }

  // 运行时配置更新
  void setLeadScrewPitch(float pitchMM);
  void configureDriver(uint16_t microstepping, float currentMA,
                        float holdCurrentRatio);
  void setHomeSafetyMargin(float marginMM);
  // 运行时把 _config 里的限位极性/翻转/使能/homingSwitch 重新写进芯片 REFERENCE_CONF。
  // begin() 只在开机配置一次，cmd 20 (SET_LIM_SWITCH_POLARITY) 改完结构体后须调本方法才真正生效。
  void reapplyLimitSwitches();

  // 配置访问
  uint8_t getIcID() const { return _icID; }
  const AxisConfig &getConfig() const { return _config; }
  AxisConfig &getMutableConfig() { return _config; }

  // 状态查询
  virtual AxisState getCurrentState() const;
  virtual const char *getAxisName() const;
  uint8_t getDriverType() const { return _config.driverType; }
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

  // Homing 细分切换
  void switchToHomingMicrosteps();
  void restoreNormalMicrosteps();

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
