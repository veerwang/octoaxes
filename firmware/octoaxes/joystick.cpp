#include "joystick.h"
#include "axesmrg.h"
#include "build_opt.h"
#include "config.h"
#include "def_octopi_80120.h"
#include "serial.h"
#include "trigger.h"
#include "tmc/motion/MotorControl.h"
#include "tmc/ic/TMC4361A/TMC4361A.h"
#include <PacketSerial.h>

// =============================================================================
// 外部变量
// =============================================================================

// 偏移速度（定义在 commandprocessor.cpp）
extern float offset_velocity_x;
extern float offset_velocity_y;

// =============================================================================
// 内部常量
// =============================================================================

static const unsigned long JOYSTICK_UPDATE_INTERVAL_US = 30000; // 30ms

// =============================================================================
// 内部状态
// =============================================================================

static PacketSerial joystickSerial;

// 缓存的轴指针和 icID（启动时查找一次）
static Axis *axisX = nullptr;
static Axis *axisY = nullptr;
static Axis *axisZ = nullptr;
static uint8_t icID_X = 0;
static uint8_t icID_Y = 0;
static uint8_t icID_Z = 0;

// 摇杆数据（由 PacketSerial 回调写入，主循环读取）
static volatile int16_t joystick_delta_x = 0;
static volatile int16_t joystick_delta_y = 0;
static volatile bool flag_read_joystick = false; // 收到新包时置 true，处理后清除

// 焦点轮状态
static int32_t focusPosition = 0;
static volatile int32_t focusWheelDelta = 0;  // 回调中累计的增量
static int32_t focusWheelPos = 0;             // 上一次绝对编码器位置
static bool firstJoystickPacket = true;       // 首包标志（仅记录基准）
static bool focusPositionSynced = false;      // focusPosition 是否已与实际位置同步

// 周期计时器
static elapsedMicros joystickTimer;

// 协议帧统计计数（S:JOYSTICK_STATS 读取）
// byte[9] == 0 → legacy 包（老 joystick，不带 CRC）
// byte[9] != 0 → 新 joystick，校验 CRC-8-CCITT(buffer[0..8])，0x00 映射为 0x01
static uint32_t joystick_legacy_count = 0;
static uint32_t joystick_crc_ok_count = 0;
static uint32_t joystick_crc_fail_count = 0;

// =============================================================================
// PacketSerial 回调：解析手控盒 10 字节消息
// =============================================================================

static void onJoystickPacketReceived(const uint8_t *buffer, size_t size) {
  if (size != 10)
    return;

  // CRC 兼容性闸门：byte[9]==0 视为 legacy（老 joystick），非 0 校验 CRC
  uint8_t recv_crc = buffer[9];
  if (recv_crc == 0x00) {
    joystick_legacy_count++;
  } else {
    uint8_t calc = serialProtocol.crc8ccitt(const_cast<byte *>(buffer), 9);
    if (calc == 0x00) calc = 0x01; // 与 joystick 侧映射规则一致
    if (calc != recv_crc) {
      joystick_crc_fail_count++;
      return; // CRC 失配丢包
    }
    joystick_crc_ok_count++;
  }

  // bytes[0-3]: 焦点轮绝对编码器位置 (int32 BE)
  int32_t focusWheelNew = (int32_t)((uint32_t)buffer[0] << 24 |
                                     (uint32_t)buffer[1] << 16 |
                                     (uint32_t)buffer[2] << 8  |
                                     (uint32_t)buffer[3]);
  if (firstJoystickPacket) {
    // 首包仅记录基准，不产生运动
    focusWheelPos = focusWheelNew;
    firstJoystickPacket = false;
  } else {
    int32_t pkt_delta = (focusWheelNew - focusWheelPos) * JOYSTICK_SIGN_Z;
    if (pkt_delta != 0) {
      DEBUG_PRINT("[FOCUS] pkt_delta=");
      DEBUG_PRINT(pkt_delta);
      DEBUG_PRINT(" focusWheelNew=");
      DEBUG_PRINTLN(focusWheelNew);
    }
    focusWheelDelta += pkt_delta;
    focusWheelPos = focusWheelNew;
  }

  // bytes[4-5]: X 摇杆 (int16 BE)
  joystick_delta_x = (int16_t)((uint16_t)buffer[4] << 8 | (uint16_t)buffer[5]);
  joystick_delta_x *= JOYSTICK_SIGN_X;

  // bytes[6-7]: Y 摇杆 (int16 BE)
  joystick_delta_y = (int16_t)((uint16_t)buffer[6] << 8 | (uint16_t)buffer[7]);
  joystick_delta_y *= JOYSTICK_SIGN_Y;

  // byte[8]: 按钮
  if (buffer[8] != 0) {
    joystick_button_pressed = true;
    joystick_button_pressed_timestamp = millis();
  }

  flag_read_joystick = true;
}

// =============================================================================
// XY 轴摇杆速度控制
// =============================================================================

static void check_joystick() {
  // X 轴
  if (axisX && !axisX->isMoving() && !axisX->isHomingInProgress()) {
    int16_t delta = joystick_delta_x;
    if (delta != 0) {
      float velocity = offset_velocity_x +
                        (float(delta) / 32768.0f) *
                        AxisConstDefinition::MAX_VELOCITY_X_mm;
      int32_t velInternal = motor_velocityMMToInternal(icID_X, velocity);
      motor_setVelocityInternal(icID_X, velInternal);
    } else {
      if (enable_offset_velocity)
        motor_setVelocityInternal(icID_X,
            motor_velocityMMToInternal(icID_X, offset_velocity_x));
      else
        motor_stop(icID_X);
    }
  }

  // Y 轴
  if (axisY && !axisY->isMoving() && !axisY->isHomingInProgress()) {
    int16_t delta = joystick_delta_y;
    if (delta != 0) {
      float velocity = offset_velocity_y +
                        (float(delta) / 32768.0f) *
                        AxisConstDefinition::MAX_VELOCITY_Y_mm;
      int32_t velInternal = motor_velocityMMToInternal(icID_Y, velocity);
      motor_setVelocityInternal(icID_Y, velInternal);
    } else {
      if (enable_offset_velocity)
        motor_setVelocityInternal(icID_Y,
            motor_velocityMMToInternal(icID_Y, offset_velocity_y));
      else
        motor_stop(icID_Y);
    }
  }
}

// =============================================================================
// Z 轴焦点轮控制
// =============================================================================

static void do_focus_control() {
  if (!axisZ || axisZ->isHomingInProgress())
    return;

  // 读取并清零累计增量
  noInterrupts();
  int32_t delta = focusWheelDelta;
  focusWheelDelta = 0;
  interrupts();

  if (delta == 0)
    return;

  // 首次使用时从实际位置同步，避免 init 时位置过期（homing 前后不一致）
  if (!focusPositionSynced) {
    focusPosition = motor_getPositionMicrosteps(icID_Z);
    focusPositionSynced = true;
  }

  focusPosition += delta;

  // 软限位钳位：仅在软限位已启用时生效（上位机 SET_LIMITS 后才有有效值）
  if (axisZ->isSoftLimitsEnabled()) {
    int32_t lowerLimit = (int32_t)tmc4361A_readRegister(icID_Z, TMC4361A_VIRT_STOP_LEFT);
    int32_t upperLimit = (int32_t)tmc4361A_readRegister(icID_Z, TMC4361A_VIRT_STOP_RIGHT);
    if (focusPosition < lowerLimit)
      focusPosition = lowerLimit;
    if (focusPosition > upperLimit)
      focusPosition = upperLimit;
  }

  [[maybe_unused]] int32_t xactual_before = motor_getPositionMicrosteps(icID_Z);
  DEBUG_PRINT("[FOCUS] do_focus delta=");
  DEBUG_PRINT(delta);
  DEBUG_PRINT(" target=");
  DEBUG_PRINT(focusPosition);
  DEBUG_PRINT(" xactual_before=");
  DEBUG_PRINTLN(xactual_before);

  motor_moveToMicrosteps(icID_Z, focusPosition);
}

// =============================================================================
// 公开 API
// =============================================================================

void joystick_init() {
  // 初始化 Serial5 @ 115200bps
  Serial5.begin(115200);
  joystickSerial.setStream(&Serial5);
  joystickSerial.setPacketHandler(&onJoystickPacketReceived);

  // 缓存轴指针和 icID
  axisX = axisManager.findAxisByName("X");
  axisY = axisManager.findAxisByName("Y");
  axisZ = axisManager.findAxisByName("Z");

  if (axisX) icID_X = axisX->getIcID();
  if (axisY) icID_Y = axisY->getIcID();
  if (axisZ) {
    icID_Z = axisZ->getIcID();
    // 初始化焦点位置为 Z 轴当前位置
    focusPosition = motor_getPositionMicrosteps(icID_Z);
  }

  joystickTimer = 0;

  DEBUG_PRINTLN("Joystick system initialized");
}

void joystick_update() {
  // 接收 PacketSerial 数据
  joystickSerial.update();

  // XY 摇杆：仅在收到新数据包时处理（与 Squid flag_read_joystick 一致）
  if (flag_read_joystick) {
    if (joystickTimer >= JOYSTICK_UPDATE_INTERVAL_US) {
      joystickTimer -= JOYSTICK_UPDATE_INTERVAL_US;
      check_joystick();
    }
    flag_read_joystick = false;
  }

  // Z 焦点轮：每次 loop 无条件运行（与 Squid 一致，在 flag_read_joystick 外面）
  do_focus_control();
}

void joystick_print_stats() {
  // 用 SerialUSB.println 直发（不走 DEBUG_PRINTLN），确保生产 env (teensy41)
  // 下也能查询计数器；对齐 S:HWINFO / S:VERSION 同款 pattern
  char buf[96];
  snprintf(buf, sizeof(buf),
           "JOYSTICK_STATS legacy=%lu crc_ok=%lu crc_fail=%lu",
           (unsigned long)joystick_legacy_count,
           (unsigned long)joystick_crc_ok_count,
           (unsigned long)joystick_crc_fail_count);
  SerialUSB.println(buf);
}
