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
#include "mcp23s17.h"  // squid++：MCP23S17_1 扩展 IO（8 轴 INTR/TARGET 输入）

void initializeClock(uint8_t clk_pin, uint32_t frequence) {
  pinMode(clk_pin, OUTPUT);
  analogWriteFrequency(clk_pin, frequence);
  analogWrite(clk_pin, 128);
}

void initializeSPIAndPins() {
  // squid++ 双相机：所有 SPI 设备片选走 74HC154，无需单独 pinMode
  // 提前 hc154_init() 以便 illumination_init 的 DAC 通信可用
  // （tmc_spi_init 内部会再调一次，幂等）
  Pins::hc154_init();

  // 初始化SPI
  SPI.begin();
  delay(50); // 50ms延迟，使用明确的时间单位
}

bool initializePowerManagement() {
  pinMode(Pins::POWER_GOOD, INPUT_PULLUP);

  // DAC80508_1 片选走 74HC154（Pins::DAC8050x_CS = 通道 2），不再直接操控 GPIO

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

  // 初始化时钟（squid++ 单套时钟，取消 EXPAND_CLK 避免与 TTL5 共用 pin 28）
  initializeClock(Pins::TMC4361_STANDARD_CLK,
                  SystemConfig::TMC4361_CLOCK_FREQUENCY);

  // 初始化SPI和引脚
  initializeSPIAndPins();

  // 初始化扩展 IO（MCP23S17_1，CS 走 HC154 通道 0；8 轴 INTR/TARGET 输入）
  mcp23s17_init();

  // 初始化照明系统（引脚、LED矩阵、DAC、联锁）
  illumination_init();

  // 初始化触发系统（引脚、频闪定时器）
  trigger_init();

  // 初始化新架构的运动控制子系统
  motor_initSubsystem();

  // 创建轴对象并添加到管理器
  //
  // squid++ 双相机硬件不需要 octoaxes 主线的 X/Y swap：
  // squid++ HC154 片选通道命名与物理硬件接线对齐（HC154_AXIS_X=10 直接驱动物理 X 电机），
  // tmc_ic_configs[] 中 icID=0 → HC154_AXIS_Y, icID=1 → HC154_AXIS_X，
  // 故 axisName="Y" + icID=0 + Y_AXIS_CS、axisName="X" + icID=1 + X_AXIS_CS 即正确映射。
  // (octoaxes 主线的 swap 是为了兼容老 Squid PCB 的反向接线，详见 octoaxes/octoaxes.ino)
  Axis *yAxis = new StepAxis(Pins::Y_AXIS_CS, 0, "Y");  // HC154 通道 9 = 物理 Y 电机
  Axis *xAxis = new StepAxis(Pins::X_AXIS_CS, 1, "X");  // HC154 通道 10 = 物理 X 电机
  Axis *zAxis = new StepAxis(Pins::Z_AXIS_CS, 2, "Z");
  Axis *wAxis = new FilterWheel(Pins::W_AXIS_CS, 3, "W");
  // Axis* expand1Axis = new Objectives(Pins::EXPAND1_AXIS_CS, 4, "E1");
  // Axis* expand3Axis = new StepAxis(Pins::EXPAND3_AXIS_CS, 6, "E3");
  // Axis* expand4Axis = new FilterWheel(Pins::EXPAND4_AXIS_CS, 7, "E4");

  // 按 axisIndex 顺序添加: Y(0), X(1), Z(2), W(3)
  if (!axisManager.addAxis(yAxis) || !axisManager.addAxis(xAxis) ||
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

  // 安全联锁检查：联锁断开时直接拉低所有 TTL 激光端口（硬编码 GPIO，零开销）
  // squid++ 双相机：D1-D8 共 8 路 TTL
  if (!illumination_interlock_ok()) {
    digitalWrite(Pins::ILLUMINATION_D1, LOW);
    digitalWrite(Pins::ILLUMINATION_D2, LOW);
    digitalWrite(Pins::ILLUMINATION_D3, LOW);
    digitalWrite(Pins::ILLUMINATION_D4, LOW);
    digitalWrite(Pins::ILLUMINATION_D5, LOW);
    digitalWrite(Pins::ILLUMINATION_D6, LOW);
    digitalWrite(Pins::ILLUMINATION_D7, LOW);
    digitalWrite(Pins::ILLUMINATION_D8, LOW);
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
