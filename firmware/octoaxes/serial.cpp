#include "serial.h"
#include "axesmrg.h"
#include "build_opt.h"
#include "commandprocessor.h"
#include "illumination.h"
#include "trigger.h"
#include "tmc/motion/MotorControl.h"
#include "tmc/ic/TMC4361A/TMC4361A.h"
#include <stdarg.h>

// 协议状态字节
static const uint8_t STATUS_COMPLETED    = 0;
static const uint8_t STATUS_IN_PROGRESS  = 1;
static const uint8_t STATUS_CRC_ERROR    = 2;

// 固件版本（byte[22]：高半字节=主版本，低半字节=次版本）
static const uint8_t FIRMWARE_VERSION_MAJOR = 1;
static const uint8_t FIRMWARE_VERSION_MINOR = 7;

// 位置上报周期（10ms，与旧 Squid 一致）
static const uint32_t INTERVAL_SEND_POS_US = 10000;

static const uint8_t CRC_TABLE[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31,
    0x24, 0x23, 0x2A, 0x2D, 0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D, 0xE0, 0xE7, 0xEE, 0xE9,
    0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1,
    0xB4, 0xB3, 0xBA, 0xBD, 0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA, 0xB7, 0xB0, 0xB9, 0xBE,
    0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16,
    0x03, 0x04, 0x0D, 0x0A, 0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A, 0x89, 0x8E, 0x87, 0x80,
    0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8,
    0xDD, 0xDA, 0xD3, 0xD4, 0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44, 0x19, 0x1E, 0x17, 0x10,
    0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F,
    0x6A, 0x6D, 0x64, 0x63, 0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13, 0xAE, 0xA9, 0xA0, 0xA7,
    0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF,
    0xFA, 0xFD, 0xF4, 0xF3};

SerialProtocolHandler serialProtocol;

static const uint32_t VERSION = 106;

SerialProtocolHandler::SerialProtocolHandler()
    : buffer_rx_ptr(0), cmd_id(0), mcu_cmd_execution_in_progress(false),
      checksum_error(false) {
  memset(buffer_rx, 0, sizeof(buffer_rx));
}

void SerialProtocolHandler::begin(long baudRate, uint32_t timeout) {
  SerialUSB.begin(baudRate);
  delay(500);
  SerialUSB.setTimeout(timeout);
  buffer_rx_ptr = 0;
  while (!SerialUSB) {
    ; // 等待串口连接
  }
}


void SerialProtocolHandler::sendDebugInfo(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // 发送带协议头的调试信息
  DEBUG_PRINTLN(buffer);
}

uint8_t SerialProtocolHandler::crc8ccitt(byte *data, uint8_t n) {
  uint8_t val = 0;
  uint8_t *pos = (uint8_t *)data;
  uint8_t *end = pos + n;

  while (pos < end) {
    val = CRC_TABLE[val ^ *pos];
    pos++;
  }

  return val;
}

bool SerialProtocolHandler::checkForCommand() {
  bool commandReceived = false;

  // 读取串口数据
  while (SerialUSB.available()) {
    buffer_rx[buffer_rx_ptr] = SerialUSB.read();
    buffer_rx_ptr = buffer_rx_ptr + 1;

    if (buffer_rx_ptr == CMD_LENGTH) {
      buffer_rx_ptr = 0;
      cmd_id = buffer_rx[0];

      // 校验和检查
      uint8_t checksum = crc8ccitt(buffer_rx, CMD_LENGTH - 1);
      if (checksum != buffer_rx[CMD_LENGTH - 1]) {
        checksum_error = true;
        // 清空串口缓冲区，因为字节级不同步也可能导致此错误
        while (SerialUSB.available()) {
          SerialUSB.read();
        }
        return false;
      } else {
        checksum_error = false;
        commandReceived = true;
        watchdog_reset_timer();
      }
      break; // 一次只处理一个命令
    }
  }

  return commandReceived;
}

void SerialProtocolHandler::sendResponse(byte cmd_id, byte status,
                                         int32_t x_pos, int32_t y_pos,
                                         int32_t z_pos, int32_t w_pos,
                                         bool joystick_button_pressed) {
  byte buffer_tx[MSG_LENGTH];
  memset(buffer_tx, 0, MSG_LENGTH);

  buffer_tx[0] = cmd_id;
  buffer_tx[1] = status;

  // X 轴位置 (bytes 2-5)
  buffer_tx[2] = byte(x_pos >> 24);
  buffer_tx[3] = byte((x_pos >> 16) & 0xFF);
  buffer_tx[4] = byte((x_pos >> 8) & 0xFF);
  buffer_tx[5] = byte(x_pos & 0xFF);

  // Y 轴位置 (bytes 6-9)
  buffer_tx[6] = byte(y_pos >> 24);
  buffer_tx[7] = byte((y_pos >> 16) & 0xFF);
  buffer_tx[8] = byte((y_pos >> 8) & 0xFF);
  buffer_tx[9] = byte(y_pos & 0xFF);

  // Z 轴位置 (bytes 10-13)
  buffer_tx[10] = byte(z_pos >> 24);
  buffer_tx[11] = byte((z_pos >> 16) & 0xFF);
  buffer_tx[12] = byte((z_pos >> 8) & 0xFF);
  buffer_tx[13] = byte(z_pos & 0xFF);

  // W 轴位置 (bytes 14-17)
  buffer_tx[14] = byte(w_pos >> 24);
  buffer_tx[15] = byte((w_pos >> 16) & 0xFF);
  buffer_tx[16] = byte((w_pos >> 8) & 0xFF);
  buffer_tx[17] = byte(w_pos & 0xFF);

  // 状态位 byte[18]：bit0 = 摇杆按钮
  static const int BIT_POS_JOYSTICK_BUTTON = 0;
  buffer_tx[18] = (joystick_button_pressed ? (1 << BIT_POS_JOYSTICK_BUTTON) : 0);

  // bytes[19-21]: 保留

  // 固件版本 byte[22]：高半字节=主版本，低半字节=次版本
  buffer_tx[22] = (FIRMWARE_VERSION_MAJOR << 4) | (FIRMWARE_VERSION_MINOR & 0x0F);

  // CRC-8-CCITT 校验（对 byte[0..22] 计算）
  uint8_t checksum = crc8ccitt(buffer_tx, MSG_LENGTH - 1);
  buffer_tx[MSG_LENGTH - 1] = checksum;

  SerialUSB.write(buffer_tx, MSG_LENGTH);
}

void SerialProtocolHandler::send_position_update() {
#ifdef DISABLE_BINARY_POS_UPDATE
  // build_opt.h 中临时定义的开关：跳过 24 字节二进制位置上报，
  // 让 SerialUSB 只剩 ASCII 调试输出，方便 Arduino Serial Monitor 看
  return;
#endif

  // 先算 any_moving，用于检测「移动完成」下降沿（true→false）
  bool any_moving = false;
  uint8_t count = axisManager.getAxisCount();
  for (uint8_t i = 0; i < count; i++) {
    Axis *axis = axisManager.getAxis(i);
    if (axis && (axis->isMoving() || axis->isHomingInProgress())) {
      any_moving = true;
      break;
    }
  }
  // 完成边缘：所有轴刚刚停下。绕过 10ms 心跳节流立即发一帧 COMPLETED，
  // 让上位机 wait_till_operation_is_completed 在物理停止后 < 1ms 内被唤醒
  // （平均省 5ms，worst case 省 10ms 心跳延迟）。下降沿每次 transition 只触发一次。
  bool falling_edge = _last_any_moving && !any_moving;
  _last_any_moving = any_moving;

  if (_us_since_last_pos_update < INTERVAL_SEND_POS_US && !falling_edge)
    return;
  _us_since_last_pos_update = 0;

  // 读取各轴位置（微步，与旧 Squid tmc4361A_currentPosition 一致）
  Axis *xAxis = axisManager.findAxisByName("X");
  Axis *yAxis = axisManager.findAxisByName("Y");
  Axis *zAxis = axisManager.findAxisByName("Z");
  Axis *wAxis = axisManager.findAxisByName("W");

  int32_t x_pos = xAxis ? xAxis->getCurrentPositionMicrosteps() : 0;
  int32_t y_pos = yAxis ? yAxis->getCurrentPositionMicrosteps() : 0;
  int32_t z_pos = zAxis ? zAxis->getCurrentPositionMicrosteps() : 0;
  int32_t w_pos = wAxis ? wAxis->getCurrentPositionMicrosteps() : 0;

  // 摇杆按钮失效安全：超过 1000ms 未 ACK 则自动清除
  if (joystick_button_pressed &&
      millis() - joystick_button_pressed_timestamp > 1000) {
    joystick_button_pressed = false;
  }

  uint8_t status;
  if (checksum_error)
    status = STATUS_CRC_ERROR;
  else
    status = any_moving ? STATUS_IN_PROGRESS : STATUS_COMPLETED;

  sendResponse(cmd_id, status, x_pos, y_pos, z_pos, w_pos,
               joystick_button_pressed);
}

void SerialProtocolHandler::processSerialCommands() {
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 5000) {  // 每5秒打印一次
    DEBUG_PRINT("LOOP_ALIVE:");
    DEBUG_PRINTLN(SerialUSB.available());
    lastPrint = millis();
  }

  if (SerialUSB.available() >= 2) {
    DEBUG_PRINT("RX_AVAIL:");
    DEBUG_PRINTLN(SerialUSB.available());  // 调试：收到数据

    // 查看前两个字节但不移除
    int firstByte = SerialUSB.peek();

    if (firstByte == DEBUG_PROTOCOL_HEADER_1) {
      // 查看第二个字节（第二个字节在索引1位置）
      // 我们需要先读取第一个字节才能查看第二个字节
      SerialUSB.read();                  // 移除第一个字节
      int secondByte = SerialUSB.peek(); // 查看第二个字节

      if (secondByte == DEBUG_PROTOCOL_HEADER_2) {
        // 确认是调试协议，移除第二个字节
        SerialUSB.read(); // 移除第二个字节
        processSerialDebugCommands();
      } else {
        // 不是调试协议，将第一个字节放回缓冲区
        // 由于我们已经移除了第一个字节，需要将其放回buffer_rx
        buffer_rx[0] = DEBUG_PROTOCOL_HEADER_1;
        buffer_rx_ptr = 1;
        // 继续处理标准命令
        // 第二个字节还在串口缓冲区中，会在checkForCommand中读取
        processSerialStandardCommands();
      }
    } else {
      // 不是调试协议头，处理标准命令
      processSerialStandardCommands();
    }
  } else if (SerialUSB.available() == 1) {
    // 只有一个字节可用，直接处理标准命令
    processSerialStandardCommands();
  }
}

void SerialProtocolHandler::processSerialDebugCommands() {
  // 读取直到换行符
  String command = SerialUSB.readStringUntil('\n');
  command.trim(); // 去除首尾空白字符

  if (command.length() > 0) {
    if (command == "S:VERSION") {
      // 版本回复始终发送（不受 ENABLE_DEBUG 控制）
      char vbuf[32];
      snprintf(vbuf, sizeof(vbuf), "S:VERSION:%lu", (unsigned long)VERSION);
      SerialUSB.println(vbuf);
      return;
    }

    if (command == "S:Engine Start") {
      // 保留命令兼容性，不再需要启动流程
      sendDebugInfo("System already running (Engine Start is no longer required)");
      return;
    }

    if (command == "S:ENCPOS") {
      char buf[120];
      // 先打印 W 轴编码器寄存器诊断
      uint8_t wID = 3;
      uint32_t genConf = tmc4361A_readRegister(wID, TMC4361A_GENERAL_CONF);
      uint32_t encInConf = tmc4361A_readRegister(wID, TMC4361A_ENC_IN_CONF);
      uint32_t stepConf = tmc4361A_readRegister(wID, TMC4361A_STEP_CONF);
      uint32_t encInRes = tmc4361A_readRegister(wID, TMC4361A_ENC_IN_RES);
      snprintf(buf, sizeof(buf), "S:ENCDIAG:W GENERAL_CONF=0x%08lX diff_dis=%d ser_mode=%d",
               (unsigned long)genConf, (int)((genConf >> 12) & 1), (int)((genConf >> 10) & 3));
      SerialUSB.println(buf);
      snprintf(buf, sizeof(buf), "S:ENCDIAG:W ENC_IN_CONF=0x%08lX STEP_CONF=0x%08lX ENC_IN_RES=%lu",
               (unsigned long)encInConf, (unsigned long)stepConf, (unsigned long)encInRes);
      SerialUSB.println(buf);

      // 打印各轴编码器位置
      for (uint8_t i = 0; i < axisManager.getAxisCount(); i++) {
        Axis *axis = axisManager.getAxis(i);
        if (axis) {
          int32_t encPos = (int32_t)tmc4361A_readRegister(i, TMC4361A_ENC_POS);
          int32_t xActual = (int32_t)tmc4361A_readRegister(i, TMC4361A_XACTUAL);
          snprintf(buf, sizeof(buf), "S:ENCPOS:%s:enc=%ld xactual=%ld dev=%ld",
                   axis->getAxisName(), (long)encPos, (long)xActual, (long)(encPos - xActual));
          SerialUSB.println(buf);
        }
      }
      SerialUSB.println("S:ENCPOS:END");
      return;
    }

    if (command == "S:HWINFO") {
      char buf[64];
      for (uint8_t i = 0; i < axisManager.getAxisCount(); i++) {
        Axis *axis = axisManager.getAxis(i);
        if (axis) {
          const char *driverName;
          switch (axis->getDriverType()) {
            case DRIVER_TMC2660: driverName = "TMC2660"; break;
            case DRIVER_TMC2240: driverName = "TMC2240"; break;
            default:             driverName = "UNKNOWN"; break;
          }
          snprintf(buf, sizeof(buf), "S:HWINFO:%s:TMC4361A+%s",
                   axis->getAxisName(), driverName);
          SerialUSB.println(buf);
        }
      }
      SerialUSB.println("S:HWINFO:END");
      return;
    }

    // S:DUMPREGS [axisName]
    // 不带参数 → dump 所有轴；带参数（X/Y/Z/W）→ 只 dump 指定轴
    // 用于卡死现场取证：打印 TMC4361A 关键寄存器，定位 ramp generator 异常根因
    if (command.startsWith("S:DUMPREGS")) {
      String filter = command.length() > 11 ? command.substring(11) : String("");
      filter.trim();
      char buf[160];
      for (uint8_t i = 0; i < axisManager.getAxisCount(); i++) {
        Axis *axis = axisManager.getAxis(i);
        if (!axis) continue;
        const char *name = axis->getAxisName();
        if (filter.length() > 0 && filter != String(name)) continue;

        uint8_t icID = axis->getIcID();
        uint32_t status   = tmc4361A_readRegister(icID, TMC4361A_STATUS);
        uint32_t events   = tmc4361A_readRegister(icID, TMC4361A_EVENTS);
        uint32_t rampMode = tmc4361A_readRegister(icID, TMC4361A_RAMPMODE);
        uint32_t refConf  = tmc4361A_readRegister(icID, TMC4361A_REFERENCE_CONF);
        int32_t  xactual  = (int32_t)tmc4361A_readRegister(icID, TMC4361A_XACTUAL);
        int32_t  xtarget  = (int32_t)tmc4361A_readRegister(icID, TMC4361A_XTARGET);
        int32_t  vactual  = (int32_t)tmc4361A_readRegister(icID, TMC4361A_VACTUAL);
        int32_t  vmax     = (int32_t)tmc4361A_readRegister(icID, TMC4361A_VMAX);
        int32_t  vstopL   = (int32_t)tmc4361A_readRegister(icID, TMC4361A_VIRT_STOP_LEFT);
        int32_t  vstopR   = (int32_t)tmc4361A_readRegister(icID, TMC4361A_VIRT_STOP_RIGHT);
        uint32_t stepConf = tmc4361A_readRegister(icID, TMC4361A_STEP_CONF);

        snprintf(buf, sizeof(buf),
                 "S:DUMP %s STATUS=0x%08lX EVENTS=0x%08lX RAMPMODE=0x%08lX",
                 name, (unsigned long)status, (unsigned long)events,
                 (unsigned long)rampMode);
        SerialUSB.println(buf);
        snprintf(buf, sizeof(buf),
                 "S:DUMP %s XACTUAL=%ld XTARGET=%ld VACTUAL=%ld VMAX=%ld",
                 name, (long)xactual, (long)xtarget, (long)vactual, (long)vmax);
        SerialUSB.println(buf);
        snprintf(buf, sizeof(buf),
                 "S:DUMP %s VSTOP_L=%ld VSTOP_R=%ld REFCONF=0x%08lX STEP_CONF=0x%08lX",
                 name, (long)vstopL, (long)vstopR,
                 (unsigned long)refConf, (unsigned long)stepConf);
        SerialUSB.println(buf);
        snprintf(buf, sizeof(buf),
                 "S:DUMP %s isMoving=%d state=%d softLimEn=%d needReenable=%d",
                 name, (int)axis->isMoving(), (int)axis->getCurrentState(),
                 (int)axis->isSoftLimitsEnabled(), 0);
        SerialUSB.println(buf);
      }
      SerialUSB.println("S:DUMPREGS:END");
      return;
    }

    // S:SET_HOMING_VEL <axisName> <vel_mm_per_s>
    // 诊断用：运行时设 homingVelocityMM 不重烧 firmware
    // 例：S:SET_HOMING_VEL Y 5.0
    if (command.startsWith("S:SET_HOMING_VEL")) {
      String rest = command.substring(16);
      rest.trim();
      int sp = rest.indexOf(' ');
      if (sp < 0) {
        SerialUSB.println("S:SET_HOMING_VEL:ERR:missing_args");
        return;
      }
      String axisName = rest.substring(0, sp);
      String velStr = rest.substring(sp + 1);
      axisName.trim();
      velStr.trim();
      float vel = velStr.toFloat();
      bool found = false;
      for (uint8_t i = 0; i < axisManager.getAxisCount(); i++) {
        Axis *axis = axisManager.getAxis(i);
        if (!axis) continue;
        if (axisName != String(axis->getAxisName())) continue;
        axis->getMutableConfig().homingVelocityMM = vel;
        char buf[80];
        snprintf(buf, sizeof(buf), "S:SET_HOMING_VEL:OK:%s=%.3f", axis->getAxisName(), vel);
        SerialUSB.println(buf);
        found = true;
        break;
      }
      if (!found) {
        SerialUSB.print("S:SET_HOMING_VEL:ERR:axis_not_found:");
        SerialUSB.println(axisName);
      }
      return;
    }

    // 处理其他调试命令
    DEBUG_PRINT("Serial:TO_AXISMGR:");
    DEBUG_PRINTLN(command);  // 调试点 - 发往AxisManager

    bool success = axisManager.processCommand(command);
    if (!success) {
      sendDebugInfo("Command processing failed: %s", command.c_str());
    }
  }
}

void SerialProtocolHandler::processSerialStandardCommands() {
  if (checkForCommand()) {
    const byte *data = getCommandData();
    byte command = data[1];

    switch (command) {
    case Commands::MOVE_X:
      commandProcessor.handleMoveX(data);
      break;

    case Commands::MOVE_Y:
      commandProcessor.handleMoveY(data);
      break;

    case Commands::MOVE_Z:
      commandProcessor.handleMoveZ(data);
      break;

    case Commands::MOVE_THETA:
      commandProcessor.handleMoveTheta(data);
      break;

    case Commands::MOVE_W:
      commandProcessor.handleMoveW(data);
      break;

    case Commands::MOVE_W2:
      commandProcessor.handleMoveW2(data);
      break;

    case Commands::HOME_OR_ZERO:
      commandProcessor.handleHomeOrZero(data);
      break;

    case Commands::MOVETO_X:
      commandProcessor.handleMoveToX(data);
      break;

    case Commands::MOVETO_Y:
      commandProcessor.handleMoveToY(data);
      break;

    case Commands::MOVETO_Z:
      commandProcessor.handleMoveToZ(data);
      break;

    case Commands::SET_LIM:
      commandProcessor.handleSetLim(data);
      break;

    case Commands::TURN_ON_ILLUMINATION:
      commandProcessor.handleTurnOnIllumination(data);
      break;

    case Commands::TURN_OFF_ILLUMINATION:
      commandProcessor.handleTurnOffIllumination(data);
      break;

    case Commands::SET_ILLUMINATION:
      commandProcessor.handleSetIllumination(data);
      break;

    case Commands::SET_ILLUMINATION_LED_MATRIX:
      commandProcessor.handleSetIlluminationLEDMatrix(data);
      break;

    case Commands::ACK_JOYSTICK_BUTTON_PRESSED:
      commandProcessor.handleAckJoystickButtonPressed(data);
      break;

    case Commands::ANALOG_WRITE_ONBOARD_DAC:
      commandProcessor.handleAnalogWriteOnboardDAC(data);
      break;

    case Commands::SET_DAC80508_REFDIV_GAIN:
      commandProcessor.handleSetDAC80508RefDivGain(data);
      break;

    case Commands::SET_ILLUMINATION_INTENSITY_FACTOR:
      commandProcessor.handleSetIlluminationIntensityFactor(data);
      break;

    case Commands::SET_TRIGGER_MODE:
      commandProcessor.handleSetTriggerMode(data);
      break;

    case Commands::SET_PORT_INTENSITY:
      commandProcessor.handleSetPortIntensity(data);
      break;

    case Commands::TURN_ON_PORT:
      commandProcessor.handleTurnOnPort(data);
      break;

    case Commands::TURN_OFF_PORT:
      commandProcessor.handleTurnOffPort(data);
      break;

    case Commands::SET_PORT_ILLUMINATION:
      commandProcessor.handleSetPortIllumination(data);
      break;

    case Commands::SET_MULTI_PORT_MASK:
      commandProcessor.handleSetMultiPortMask(data);
      break;

    case Commands::TURN_OFF_ALL_PORTS:
      commandProcessor.handleTurnOffAllPorts(data);
      break;

    case Commands::SET_WATCHDOG_TIMEOUT:
      commandProcessor.handleSetWatchdogTimeout(data);
      break;

    case Commands::SET_PIN_LEVEL:
      commandProcessor.handleSetPinLevel(data);
      break;

    case Commands::HEARTBEAT:
      commandProcessor.handleHeartbeat(data);
      break;

    case Commands::MOVETO_W:
      commandProcessor.handleMoveToW(data);
      break;

    case Commands::SET_LIM_SWITCH_POLARITY:
      commandProcessor.handleSetLimSwitchPolarity(data);
      break;

    case Commands::CONFIGURE_STEPPER_DRIVER:
      commandProcessor.handleConfigureStepperDriver(data);
      break;

    case Commands::SET_MAX_VELOCITY_ACCELERATION:
      commandProcessor.handleSetMaxVelocityAcceleration(data);
      break;

    case Commands::SET_LEAD_SCREW_PITCH:
      commandProcessor.handleSetLeadScrewPitch(data);
      break;

    case Commands::SET_OFFSET_VELOCITY:
      commandProcessor.handleSetOffsetVelocity(data);
      break;

    case Commands::CONFIGURE_STAGE_PID:
      commandProcessor.handleConfigureStagePID(data);
      break;

    case Commands::ENABLE_STAGE_PID:
      commandProcessor.handleEnableStagePID(data);
      break;

    case Commands::DISABLE_STAGE_PID:
      commandProcessor.handleDisableStagePID(data);
      break;

    case Commands::SET_HOME_SAFETY_MERGIN:
      commandProcessor.handleSetHomeSafetyMargin(data);
      break;

    case Commands::SET_PID_ARGUMENTS:
      commandProcessor.handleSetPIDArguments(data);
      break;

    case Commands::SEND_HARDWARE_TRIGGER:
      commandProcessor.handleSendHardwareTrigger(data);
      break;

    case Commands::SET_STROBE_DELAY:
      commandProcessor.handleSetStrobeDelay(data);
      break;

    case Commands::SET_AXIS_DISABLE_ENABLE:
      commandProcessor.handleSetAxisDisableEnable(data);
      break;

    case Commands::INITFILTERWHEEL:
      commandProcessor.handleInitFilterWheel(data);
      break;

    case Commands::INITFILTERWHEEL_W2:
      commandProcessor.handleInitFilterWheelW2(data);
      break;

    case Commands::INITIALIZE:
      commandProcessor.handleInitialize(data);
      break;

    case Commands::RESET:
      commandProcessor.handleReset(data);
      break;

    default:
      break;
    }
  }
}
