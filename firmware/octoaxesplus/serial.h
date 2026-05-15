#ifndef SERIAL_PROTOCOL_HANDLER_H
#define SERIAL_PROTOCOL_HANDLER_H

#include <Arduino.h>

class SerialProtocolHandler {
public:
    SerialProtocolHandler();
    
    // 初始化串口通信
    void begin(long baudRate = 2000000, uint32_t timeout = 200);
    
    // 检查是否有新命令并处理
    bool checkForCommand();
    
    // 获取命令ID
    byte getCommandId() const { return cmd_id; }
    
    // 获取命令执行状态
    bool isCommandInProgress() const { return mcu_cmd_execution_in_progress; }
    
    // 获取校验和错误状态
    bool hasChecksumError() const { return checksum_error; }
    
    // 获取接收到的命令数据
    const byte* getCommandData() const { return buffer_rx; }
    
    // 发送响应消息（24 字节，命令响应用，保持旧 Squid 兼容）
    void sendResponse(byte cmd_id, byte status,
                      int32_t x_pos, int32_t y_pos, int32_t z_pos,
                      int32_t w_pos = 0,
                      bool joystick_button_pressed = false);

    // 发送扩展位置广播（40 字节，octoaxesplus 周期性上报用）
    // positions[] 必须包含 8 个 int32（按 firmware icID 索引）
    // cmd_id 固定使用 EXTENDED_POS_CMD_ID = 0xFD
    void sendExtendedResponse(byte status, const int32_t positions[8],
                              bool joystick_button_pressed = false);

    // 10ms 周期位置上报（在 loop() 中调用）
    void send_position_update();
    
    // 发送调试信息
    void sendDebugInfo(const char* format, ...);
    
    // 设置命令执行完成状态
    void setCommandInProgress(bool in_progress) { 
        mcu_cmd_execution_in_progress = in_progress; 
    }
    
    // 获取命令长度
    static int getCommandLength() { return CMD_LENGTH; }

    // 获取消息长度
    static int getMessageLength() { return MSG_LENGTH; }
    
    // 串口调试信息处理函数
    void processSerialCommands();
    void processSerialDebugCommands();
    void processSerialStandardCommands();
    // CRC校验函数
    uint8_t crc8ccitt(byte *data, uint8_t len);

private:
    static const int CMD_LENGTH = 8;
    static const int MSG_LENGTH = 24;            // 命令响应包长度（兼容旧 Squid）
    static const int EXTENDED_MSG_LENGTH = 40;   // 扩展位置广播包长度（octoaxesplus 8 轴）

    // 协议标识符
    static const byte DEBUG_PROTOCOL_HEADER_1 = 0x55;
    static const byte DEBUG_PROTOCOL_HEADER_2 = 0xAA;
    static const byte EXTENDED_POS_CMD_ID    = 0xFD;  // 扩展位置广播包 cmd_id
    
    byte buffer_rx[512];
    volatile int buffer_rx_ptr;
    byte cmd_id;
    bool mcu_cmd_execution_in_progress;
    bool checksum_error;
    elapsedMicros _us_since_last_pos_update;
    // 上一次 send_position_update 计算的 any_moving，用于检测下降沿（移动完成边缘）
    // 下降沿时立即额外发一帧 COMPLETED，省 0-10ms 心跳等待
    bool _last_any_moving = false;
    
    // 调试命令缓冲区
    String debugCommandBuffer;
};

extern SerialProtocolHandler serialProtocol;

#endif
