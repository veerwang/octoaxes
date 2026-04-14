# Squid++（双相机）硬件配置

Teensy 4.1 引脚定义、74HC154 译码器片选映射、MCP23S17_1 扩展 IO 分配。

来源：`squid++（双相机）配置.xlsx`

---

## 1. Teensy 4.1 引脚定义

| 序号 | Pin | 功能定义 | 功能描述 |
|-----:|----:|---|---|
| 1  | 0  | Power good      | 电源电压监控 |
| 2  | 1  | TRIGGER_OUT1    | |
| 3  | 2  | TRIGGER_IN1     | |
| 4  | 3  | TRIGGER_OUT2    | |
| 5  | 4  | TRIGGER_IN2     | 相机2_触发 |
| 6  | 5  | RESERVED        | 相机2_等待触发 |
| 7  | 6  | CAM_TRI_READY2  | 相机1_触发 |
| 8  | 7  | CAM_TRI_READY1  | 相机1_等待触发 |
| 9  | 8  | CAM_TRI_OUT2    | |
| 10 | 9  | CAM_TRI_OUT1    | |
| 11 | 10 | TTL8            | TTL输出 |
| 12 | 11 | SPI1_MOSI       | SPI1 通讯 |
| 13 | 12 | SPI1_MISO       | SPI1 通讯 |
| 14 | 13 | SPI1_SCK        | SPI1 通讯 |
| 15 | 14 | IIC_WP          | IIC 通讯 |
| 16 | 15 | CAM_TRIGGER5    | |
| 17 | 16 | RX2             | |
| 18 | 17 | TX2             | |
| 19 | 18 | IIC_SDA         | IIC 通讯 |
| 20 | 19 | IIC_SCL         | IIC 通讯 |
| 21 | 20 | TX-JOYSTICK     | 手控盒 通讯 |
| 22 | 21 | RX-JOYSTICK     | 手控盒 通讯 |
| 23 | 22 | CAM_TRIGGER4    | |
| 24 | 23 | CAM_TRIGGER3    | |
| 25 | 24 | TTL7            | TTL输出 |
| 26 | 25 | TTL6            | TTL输出 |
| 27 | 26 | SPI2_MOSI       | 矩阵灯 通讯 |
| 28 | 27 | SPI2_SCK        | 矩阵灯 通讯 |
| 29 | 28 | TTL5            | TTL输出 |
| 30 | 29 | TTL4            | TTL输出 |
| 31 | 30 | TTL3            | TTL输出 |
| 32 | 31 | TTL2            | TTL输出 |
| 33 | 32 | TTL1            | TTL输出 |
| 34 | 33 | 74HC154_A0      | |
| 35 | 34 | 74HC154_A1      | |
| 36 | 35 | 74HC154_A2      | |
| 37 | 36 | 74HC154_A3      | |
| 38 | 37 | TEENSY_CLK      | |
| 39 | 38 | INTERLOCK       | |
| 40 | 39 | CAM_TRIGGER8    | |
| 41 | 40 | CAM_TRIGGER7    | |
| 42 | 41 | CAM_TRIGGER6    | |
| 43 | 42 | GND             | |
| 44 | 43 | 3.3V            | |
| 45 | 44 | VIN             | |
| 46 | 45 | GND             | |
| 47 | 46 | 3.3V OUT        | |
| 48 | 47 | GND             | |

---

## 2. IO 控制

| IO控制： | SPI1控制： |
|---|---|
| Y0. SPI_CS_MCP23S17_1 | MCP23S17  SPI1片选信号 |
| Y1. SPI_CS_DAC80508_2 | |
| Y2. SPI_CS_DAC80508_1 | 8LED模拟信号输出  SPI1片选信号 |
| Y3. SPI_CS_R          | |
| Y4. SPI_CS_T          | 滤光转盘 F2 SPI1片选信号 |
| Y5. SPI_CS_F2         | |
| Y6. SPI_CS_Z2         | 滤光转盘 F1 SPI1片选信号 |
| Y7. SPI_CS_F1         | |
| Y8. SPI_CS_Z1         | Z轴  SPI1片选信号 |
| Y9. SPI_CS_Y          | Y轴  SPI1片选信号 |
| Y10. SPI_CS_X         | X轴  SPI1片选信号 |
| Y11. EXPAND_NSCS1     | |
| Y12. SPI_CS_DAC80508_4 | |
| Y13. SPI_CS_MCP23S17_2 | |
| Y14. SPI_CS_MCP23S17_3 | |
| Y15. SPI_CS_MCP23S17_4 | |

---

## 3. 扩展 IO

| 扩展IO | 扩展功能描述 |
|---|---|
| GPA0:INTR_Y    | Y轴 |
| GPA1:TARGET_Y  | |
| GPA2:INTR_X    | X轴 |
| GPA3:TARGET_X  | |
| GPA4:INTR_F1   | |
| GPA5:TARGET_F1 | |
| GPA6:INTR_Z1   | Z轴 |
| GPA7:TARGET_Z1 | |
| GPB0:INTR_R    | |
| GPB1:TARGET_R  | |
| GPB2:INTR_T    | F2轴 |
| GPB3:TARGET_T  | |
| GPB4:INTR_F2   | |
| GPB5:TARGET_F2 | |
| GPB6:INTR_Z2   | F1轴 |
| GPB7:TARGET_Z2 | |
