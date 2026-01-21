#include "axesmrg.h"
#include "build_opt.h"
#include "filterwheel.h"
#include "objectives.h"
#include "serial.h"
#include "stepaxis.h"
#include "utils.h"
#include "tmc/hal/TMC_SPI.h"
#include "tmc/motion/MotorControl.h"

void initializeClock(uint8_t clk_pin, uint32_t frequence) {
  pinMode(clk_pin, OUTPUT);
  analogWriteFrequency(clk_pin, frequence);
  analogWrite(clk_pin, 128);
}

void initializeSPIAndPins() {
  // 全部轴去使能
  for (uint8_t i = 0; i < sizeof(Pins::CONTROL_PINS); i++) {
    pinMode(Pins::CONTROL_PINS[i], OUTPUT);
    digitalWrite(Pins::CONTROL_PINS[i], HIGH);
  }

  for (uint8_t i = 0; i < sizeof(Pins::STANDARD_CONTROL_PINS); i++) {
    pinMode(Pins::STANDARD_CONTROL_PINS[i], OUTPUT);
    digitalWrite(Pins::STANDARD_CONTROL_PINS[i], HIGH);
  }

  // 初始化SPI
  SPI.begin();
  delay(50); // 50ms延迟，使用明确的时间单位
}

bool initializePowerManagement() {
  pinMode(Pins::POWER_GOOD, INPUT_PULLUP);

  // 禁用DAC引脚
  pinMode(Pins::DAC8050x_CS, OUTPUT);
  digitalWrite(Pins::DAC8050x_CS, HIGH);

  delay(100);

  // 等待电源就绪
  unsigned long startTime = millis();
  while (!digitalRead(Pins::POWER_GOOD)) {
    if (millis() - startTime > 5000) { // 5秒超时
      DEBUG_PRINTLN("Power management initialization timeout");
      return false;
    }
    delay(50);
  }

  return true;
}

bool initializeSystem() {
  // 初始化电源管理
  if (!initializePowerManagement()) {
    return false;
  }

  // 初始化时钟
  initializeClock(Pins::TMC4361_STANDARD_CLK,
                  SystemConfig::TMC4361_CLOCK_FREQUENCY);
  initializeClock(Pins::TMC4361_EXPAND_CLK,
                  SystemConfig::TMC4361_CLOCK_FREQUENCY);

  // 初始化SPI和引脚
  initializeSPIAndPins();

  // 初始化新架构的运动控制子系统
  motor_initSubsystem();

  // 创建轴对象并添加到管理器
  Axis *xAxis = new StepAxis(Pins::X_AXIS_CS, 0, "X");
  Axis *yAxis = new StepAxis(Pins::Y_AXIS_CS, 1, "Y");
  Axis *zAxis = new StepAxis(Pins::Z_AXIS_CS, 2, "Z");
  // Axis* wAxis = new FilterWheel(Pins::W_AXIS_CS, 3, "W");
  // Axis* expand1Axis = new Objectives(Pins::EXPAND1_AXIS_CS, 4, "E1");
  // Axis* expand3Axis = new StepAxis(Pins::EXPAND3_AXIS_CS, 6, "E3");
  // Axis* expand4Axis = new FilterWheel(Pins::EXPAND4_AXIS_CS, 7, "E4");

  Axis *wAxis = new Objectives(Pins::W_AXIS_CS, 3, "E1");
  // 初始化顺序很重要，homing的时候需要通过这个index获取句柄
  if (!axisManager.addAxis(xAxis) || !axisManager.addAxis(yAxis) ||
      !axisManager.addAxis(zAxis) || !axisManager.addAxis(wAxis)) {
    // if (!axisManager.addAxis(zAxis)|| !axisManager.addAxis(expand1Axis) ||
    // !axisManager.addAxis(expand3Axis) || !axisManager.addAxis(expand4Axis) ||
    // !axisManager.addAxis(wAxis)) {
    DEBUG_PRINTLN("Failed to add axes to manager");
    return false;
  }

  // 初始化所有轴
  if (!axisManager.beginAll()) {
    return false;
  }

  return true;
}

void setup() {
  // 初始化串口
  serialProtocol.begin(115200, 300);

  serialProtocol.waitEngineStartCommand();

  // 初始化状态指示灯
  initializeStartupLED();

  DEBUG_PRINTLN("Engine Start received. Initializing system...");

  // 初始化系统
  if (!initializeSystem()) {
    DEBUG_PRINTLN("System initialization failed!");
    while (1) {
      delay(1000); // 停止执行
    }
  }

  DEBUG_PRINTLN("System initialized successfully");
}

void loop() {
  // 处理串口调试命令
  serialProtocol.processSerialCommands();

  // 更新所有轴状态机
  axisManager.updateAll();
}
