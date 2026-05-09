#include "axesmrg.h"
#include "build_opt.h"
#include "filterwheel.h"
#include "illumination.h"
#include "joystick.h"
#include "trigger.h"
#include "objectives.h"
#include "serial.h"
#include "stepaxis.h"
#include "tmc/hal/TMC_SPI.h"
#include "tmc/motion/MotorControl.h"
#include "tmc/ic/TMC4361A/TMC4361A.h"
#include "utils.h"

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

  // 初始化照明系统（引脚、LED矩阵、DAC、联锁）
  illumination_init();

  // 初始化触发系统（引脚、频闪定时器）
  trigger_init();

  // 初始化新架构的运动控制子系统
  motor_initSubsystem();

  // 创建轴对象并添加到管理器
  //
  // 重要 (2026-05-08 修正): axisName ↔ CS 引脚映射与旧 Squid 硬件接线对齐
  //
  // 旧 Squid firmware 内部 axis 索引 vs 协议轴号映射 (def_v1.h:11-21)：
  //   Protocol: AXIS_X=0, AXIS_Y=1
  //   Internal: x=1, y=0  (注释明确说"Internal indices match hardware wiring")
  // → 旧 Squid 硬件实际接线：
  //   pin_TMC4361_CS[0]=41 → 物理 Y 电机 (因为 internal y=0)
  //   pin_TMC4361_CS[1]=36 → 物理 X 电机 (因为 internal x=1)
  //
  // 因此 axisName="X" 必须绑定到 CS=36 (Pins::Y_AXIS_CS) 才能正确驱动物理 X 电机。
  // 之前用 axisName="X" + Pins::X_AXIS_CS=41 → 实际驱动物理 Y 电机，
  // 引发旧 Squid 点动 X 卡死的现象（X 走到 79.9mm 触发的是 Y 物理限位）。
  //
  // axisIndex (icID) 只是内部数组索引，不影响 CS 物理对应。
  Axis *xAxis = new StepAxis(Pins::Y_AXIS_CS, 0, "X");  // CS=36 = 物理 X 电机
  Axis *yAxis = new StepAxis(Pins::X_AXIS_CS, 1, "Y");  // CS=41 = 物理 Y 电机
  Axis *zAxis = new StepAxis(Pins::Z_AXIS_CS, 2, "Z");
  Axis *wAxis = new FilterWheel(Pins::W_AXIS_CS, 3, "W");
  // Axis* expand1Axis = new Objectives(Pins::EXPAND1_AXIS_CS, 4, "E1");
  // Axis* expand3Axis = new StepAxis(Pins::EXPAND3_AXIS_CS, 6, "E3");
  // Axis* expand4Axis = new FilterWheel(Pins::EXPAND4_AXIS_CS, 7, "E4");

  // 按 axisIndex 顺序添加: X(0), Y(1), Z(2), W(3)
  if (!axisManager.addAxis(xAxis) || !axisManager.addAxis(yAxis) ||
      !axisManager.addAxis(zAxis) || !axisManager.addAxis(wAxis)) {
    DEBUG_PRINTLN("Failed to add axes to manager");
    return false;
  }

  // 初始化所有轴
  if (!axisManager.beginAll()) {
    return false;
  }

  // 初始化手控盒（Serial5 + PacketSerial）
  joystick_init();

  return true;
}

void setup() {
  // 初始化串口
  serialProtocol.begin(115200, 300);

  // 初始化状态指示灯
  initializeStartupLED();

  // 尽早把 APA102 矩阵清零，最小化"启动亮"窗口。
  // 之后的 initializePowerManagement (等 PG) + delay + clock + SPI 初始化
  // 累计可能数百 ms~5s，APA102 在此期间处于上电默认亮态。
  illumination_init_matrix_early();

  DEBUG_PRINTLN("Initializing system...");

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
  static bool firstLoop = true;
  if (firstLoop) {
    DEBUG_PRINTLN("MAIN_LOOP_ENTERED");  // 确认进入主循环
    firstLoop = false;
  }

  // 安全联锁检查：联锁断开时直接拉低 TTL 激光端口（硬编码 GPIO，零开销）
  if (!illumination_interlock_ok()) {
    digitalWrite(Pins::ILLUMINATION_D1, LOW);
    digitalWrite(Pins::ILLUMINATION_D2, LOW);
    digitalWrite(Pins::ILLUMINATION_D3, LOW);
    digitalWrite(Pins::ILLUMINATION_D4, LOW);
    digitalWrite(Pins::ILLUMINATION_D5, LOW);
  }

  // 串口看门狗：通信中断超时后自动关闭所有照明
  watchdog_check();

  // 更新触发脉冲恢复
  trigger_update();

  // 处理串口调试命令
  serialProtocol.processSerialCommands();

  // 10ms 周期位置上报（与旧 Squid 协议兼容）
  serialProtocol.send_position_update();

  // 更新手控盒（PacketSerial 接收 + 摇杆/焦点轮控制）
  joystick_update();

  // 更新所有轴状态机
  axisManager.updateAll();

}
