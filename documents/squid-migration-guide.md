# Squid → Octoaxes 命令层移植指南

**创建日期**: 2026-02-26
**参考代码**:
- 旧架构: `/home/hds/github.com/veerwang/lihongquan/Squid/firmware/controller/`
- 新架构: `/home/hds/gitee.com/octoaxes/firmware/octoaxes/`
- 驱动逻辑文档: `squid_controller_driver_logic.md`

---

## 1. 架构对比

| 维度 | 旧架构 (Squid) | 新架构 (Octoaxes) |
|------|--------------|-----------------|
| 分发方式 | `cmd_map[256]` 函数指针数组 | `switch-case` → `CommandProcessor` 方法 |
| 轴访问 | `tmc4361[x]`（全局数组，x=1/y=0 互换）| `axisManager.findAxisByName("X")` |
| 底层调用 | `tmc4361A_moveTo()` 直接传微步 | `axis->moveAxis()` / `axis->moveToPosition()` |
| 全局状态 | `mcu_cmd_execution_in_progress` 等完整 | **尚未实现** |
| 响应机制 | 10ms 周期 `send_position_update()` | `sendResponse()` 存在但**未调用** |
| 滤光轮初始化 | 命令触发（延迟初始化）| 需保持同样机制 |

### 旧架构文件结构

```
src/
├── commands/
│   ├── commands.cpp        # init_callbacks() + 系统/PID/DAC/触发命令
│   ├── stage_commands.cpp  # 运动/限位/归位/配置命令
│   └── light_commands.cpp  # 照明命令
├── operations.cpp          # 主循环状态机（homing/check/limit）
├── serial_communication.cpp # 串口收发
├── init.cpp                # setup() 各子系统初始化
├── globals.cpp             # 全局变量定义
└── tmc/                    # TMC4361A 旧版 API
```

### 新架构文件结构

```
firmware/octoaxes/
├── commandprocessor.cpp    # 命令实现（大部分为 NOT_IMPLEMENTED 骨架）
├── commandprocessor.h
├── serial.cpp              # 串口收发 + switch-case 分发
├── axesmrg.cpp             # AxisManager（轴注册/查找/命令转发）
├── axis.cpp / stepaxis.cpp / filterwheel.cpp  # Axis 类层次
└── tmc/motion/MotorControl.cpp  # 新版 TMC API
```

---

## 2. 协议规范（双方一致，不需修改）

### 命令包（8 字节）

```
byte[0]: 命令 ID（序列号）
byte[1]: 命令码
byte[2]: 参数1（轴标识或端口号）
byte[3]: 参数2 高字节
byte[4]: 参数2 低字节
byte[5]: 参数3
byte[6]: 保留
byte[7]: CRC-8-CCITT（对 byte[0..6] 计算）
```

### 响应包（24 字节）

```
byte[0]:     命令 ID
byte[1]:     执行状态（0=完成, 1=进行中, 2=CRC错误, 3=无效命令, 4=执行错误）
byte[2-5]:   X 轴位置（32位大端序）
byte[6-9]:   Y 轴位置
byte[10-13]: Z 轴位置
byte[14-17]: W 轴位置
byte[18]:    状态位（bit0=摇杆按钮）
byte[22]:    固件版本
byte[23]:    CRC-8-CCITT
```

---

## 3. 轴映射（关键！）

### 协议轴号（`data[2]`）

```
AXIS_X  = 0
AXIS_Y  = 1
AXIS_Z  = 2
AXES_XY = 4  （联合归位专用）
AXIS_W  = 5
AXIS_W2 = 6
```

### 旧架构内部索引（X/Y 因硬件接线互换）

```
x = 1  （协议 AXIS_X=0 → 内部索引 1）
y = 0  （协议 AXIS_Y=1 → 内部索引 0）
z = 2
w = 3
w2 = 4
```

### 新架构处理方式

新架构用轴名称字符串，**不引入数字互换**，更可读：

```cpp
// 建议在 CommandProcessor 或 serial.cpp 中实现此映射
const char* protocolAxisToName(uint8_t protocolAxis) {
    switch (protocolAxis) {
        case 0: return "X";
        case 1: return "Y";
        case 2: return "Z";
        case 5: return "W";
        case 6: return "W2";
        default: return nullptr;
    }
}
```

---

## 4. 已发现的 Bug（移植前必须修复）

### Bug 1：单位错误（严重）

**位置**: `commandprocessor.cpp:80,93,104`

```cpp
// 错误！协议传的是微步，不是 mm×1000
float obsolute_position_float = float(obsolute_position) / 1000.0;
axis->moveToPosition(obsolute_position_float);
```

旧架构直接传微步：
```cpp
// stage_commands.cpp:73 — 正确做法
X_commanded_target_position = absolute_position;  // 直接微步
tmc4361A_moveTo(&tmc4361[x], X_commanded_target_position);
```

**修复**: 确认 `Axis::moveToPosition()` 的接口语义，或统一改用微步接口。

### Bug 2：handleHomeOrZero 轴映射错误

**位置**: `commandprocessor.cpp:70`

```cpp
// 错误！data[2] 是协议轴值（0/1/2/5/6），不是内部数组索引
Axis *axis = axisManager.getAxis(data[2]);
```

`getAxis(5)` 会返回第 5 个注册轴，而非 W 轴（协议值 5）。

**修复**: 改用 `findAxisByName(protocolAxisToName(data[2]))`。

另外 `axis->startHoming()` 缺少方向参数（HOME_NEGATIVE=1 / HOME_POSITIVE=0 / HOME_OR_ZERO_ZERO=2）。

### Bug 3：串口分发缺失命令

**位置**: `serial.cpp` 的 `processSerialStandardCommands()` switch

缺少：
- `INITFILTERWHEEL_W2 (252)`
- `MOVE_W2 (19)`
- `MOVETO_W2`（旧架构有，但新架构未定义命令码）
- `SET_TRIGGER_MODE (33)`

---

## 5. 缺失的全局状态（需补充）

旧架构有完整的运动状态跟踪，新架构尚未实现。需在合适位置（建议 `SerialProtocolHandler` 或独立结构体）添加：

```cpp
// 全局运动状态
bool mcu_cmd_execution_in_progress;        // 任意轴在运动中

// 各轴运动状态
bool X_commanded_movement_in_progress;
bool Y_commanded_movement_in_progress;
bool Z_commanded_movement_in_progress;
bool W_commanded_movement_in_progress;
bool W2_commanded_movement_in_progress;

long X_commanded_target_position;
long Y_commanded_target_position;
long Z_commanded_target_position;
long W_commanded_target_position;
long W2_commanded_target_position;

int X_direction, Y_direction, Z_direction, W_direction, W2_direction;

// 归位状态（每轴）
bool is_homing_X, is_homing_Y, is_homing_Z, is_homing_W, is_homing_W2;
bool is_preparing_for_homing_X, ..._Y, ..._Z, ..._W, ..._W2;
bool home_X_found, home_Y_found, home_Z_found, home_W_found, home_W2_found;
int  homing_direction_X, ..._Y, ..._Z, ..._W, ..._W2;
bool is_homing_XY;
```

---

## 6. 响应机制决策

旧架构：10ms 周期 `send_position_update()`，在 `loop()` 中持续调用，无论是否有命令。

新架构：`sendResponse()` 已实现但未被调用。

**待决定**：上位机期望哪种模式？
- **方案 A**（推荐，与旧兼容）：保留周期上报，上位机轮询状态
- **方案 B**：每条命令立即返回，命令完成时再发 COMPLETED

---

## 7. 移植顺序（建议）

### 第 0 步：决定响应机制（见第 6 节）

### 第 1 步：修复已知 Bug

1. 修复单位问题（MOVETO 系列）
2. 修复 handleHomeOrZero 轴映射
3. 补全 switch 中缺失的 3 条命令

### 第 2 步：添加全局状态变量

### 第 3 步：实现各命令（按优先级）

#### 优先级 1：系统命令（无运动风险）

| 命令 | 码 | 旧架构位置 | 核心逻辑 |
|------|----|-----------|---------|
| `RESET` | 255 | `commands.cpp:261` | 清空所有运动/归位状态标志 |
| `INITIALIZE` | 254 | `commands.cpp:211` | 重新初始化 TMC IC + 限位 + DAC，重置 trigger_mode |

#### 优先级 2：基础运动命令

| 命令 | 码 | 旧架构位置 | 核心逻辑 |
|------|----|-----------|---------|
| `MOVE_X/Y/Z` | 0/1/2 | `stage_commands.cpp:3` | 相对位移，X/Y 需钳位 |
| `MOVE_W/W2` | 4/19 | `stage_commands.cpp:59` | 相对位移，需 enable 检查 |
| `MOVETO_X/Y/Z` | 6/7/8 | `stage_commands.cpp:69` | 绝对位置 |
| `MOVETO_W` | 18 | `stage_commands.cpp:106` | 需 enable 检查 |

**注意**：移动命令须设置 `X_commanded_movement_in_progress = true` 和 `mcu_cmd_execution_in_progress = true`。

#### 优先级 3：运动配置命令

| 命令 | 码 | 旧架构位置 | 核心逻辑 |
|------|----|-----------|---------|
| `SET_LIM` | 9 | `stage_commands.cpp:120` | 设虚拟限位并 enable |
| `SET_MAX_VELOCITY_ACCELERATION` | 22 | `stage_commands.cpp:338` | 速度×100，加速度×10 |
| `SET_LEAD_SCREW_PITCH` | 23 | `stage_commands.cpp:392` | 螺距×1000 |
| `CONFIGURE_STEPPER_DRIVER` | 21 | `stage_commands.cpp:261` | 微步/电流/保持电流 |
| `SET_LIM_SWITCH_POLARITY` | 20 | `stage_commands.cpp:169` | 限位极性 |
| `SET_HOME_SAFETY_MERGIN` | 28 | `stage_commands.cpp:200` | 安全裕量 mm×1000 |
| `SET_OFFSET_VELOCITY` | 24 | `stage_commands.cpp:789` | 偏移速度×10⁶ |
| `SET_AXIS_DISABLE_ENABLE` | 32 | `commands.cpp:191` | 使能/禁用驱动 |

#### 优先级 4：Homing 状态机

| 命令 | 码 | 旧架构位置 | 核心逻辑 |
|------|----|-----------|---------|
| `HOME_OR_ZERO` | 5 | `stage_commands.cpp:434` | 三阶段状态机 |
| `INITFILTERWHEEL` | 253 | `commands.cpp:179` | 延迟初始化 W 轴 |
| `INITFILTERWHEEL_W2` | 252 | `commands.cpp:185` | 延迟初始化 W2 轴 |

**Homing 三阶段**（需迁移到新架构 loop 中）：

```
prepare_homing_*()  — 若已在限位上，先反向脱离
check_homing_*()    — 向目标方向移动，等待限位事件，读 X_LATCH_RD
finalize_homing_*() — 移到 latch 位置，设零，恢复 PID
```

#### 优先级 5：PID / 编码器（可选）

| 命令 | 码 | 旧架构位置 |
|------|----|-----------|
| `CONFIGURE_STAGE_PID` | 25 | `commands.cpp:107` |
| `ENABLE_STAGE_PID` | 26 | `commands.cpp:133` |
| `DISABLE_STAGE_PID` | 27 | `commands.cpp:142` |
| `SET_PID_ARGUMENTS` | 29 | `stage_commands.cpp:247` |

#### 优先级 6：照明系统

| 命令 | 码 | 旧架构位置 |
|------|----|-----------|
| `TURN_ON_ILLUMINATION` | 10 | `light_commands.cpp` |
| `TURN_OFF_ILLUMINATION` | 11 | `light_commands.cpp` |
| `SET_ILLUMINATION` | 12 | `light_commands.cpp` |
| `SET_ILLUMINATION_LED_MATRIX` | 13 | `light_commands.cpp` |
| `SET_ILLUMINATION_INTENSITY_FACTOR` | 17 | `light_commands.cpp` |
| `SET_PORT_INTENSITY` | 34 | `light_commands.cpp` |
| `TURN_ON_PORT` | 35 | `light_commands.cpp` |
| `TURN_OFF_PORT` | 36 | `light_commands.cpp` |
| `SET_PORT_ILLUMINATION` | 37 | `light_commands.cpp` |
| `SET_MULTI_PORT_MASK` | 38 | `light_commands.cpp` |
| `TURN_OFF_ALL_PORTS` | 39 | `light_commands.cpp` |

#### 优先级 7：相机触发 / DAC / IO

| 命令 | 码 | 旧架构位置 |
|------|----|-----------|
| `SEND_HARDWARE_TRIGGER` | 30 | `commands.cpp:85` |
| `SET_STROBE_DELAY` | 31 | `commands.cpp:80` |
| `SET_TRIGGER_MODE` | 33 | `commands.cpp:205` |
| `ANALOG_WRITE_ONBOARD_DAC` | 15 | `commands.cpp:66` |
| `SET_DAC80508_REFDIV_GAIN` | 16 | `commands.cpp:73` |
| `SET_PIN_LEVEL` | 41 | `commands.cpp:100` |
| `ACK_JOYSTICK_BUTTON_PRESSED` | 14 | `commands.cpp:61` |

---

## 8. 移植注意事项

### 8.1 MOVE 与 MOVETO 的区别

- `MOVE_*`：相对位移，X/Y 轴需钳位到 `[NEG_LIMIT, POS_LIMIT]`；W/W2 无限位钳位
- `MOVETO_*`：绝对位置；Z 轴移动需同步更新 `focusPosition`

### 8.2 滤光轮特殊性

- W/W2 需要 `enable_filterwheel` / `enable_filterwheel_w2` 标志为 true 才能操作
- 初始化通过 `INITFILTERWHEEL(253)` / `INITFILTERWHEEL_W2(252)` 命令触发
- Homing 完成阶段用 `setCurrentPosition(0)` 直接设零，不需要移回 latch 位置

### 8.3 Homing 与限位

- Homing 开始时需禁用虚拟限位开关（`disableVirtualLimitSwitch`）
- Homing 开始时若已在限位上，先反向移动脱离（prepare 阶段）
- 检测到限位事件后读 `X_LATCH_RD` 寄存器获取精确触发位置
- XY 联合归位：`AXES_XY=4`，byte[3]=X 方向，byte[4]=Y 方向

### 8.4 电流计算公式

```c
current_scale = (RMS_CURRENT_mA / 1000.0) * R_sense / 0.2298
```

各轴 R_sense：
- X/Y: `R_sense_xy = 0.22Ω`
- Z: `R_sense_z = 0.43Ω`
- W/W2: `R_sense_w = 0.105Ω`

### 8.5 CONFIGURE_STEPPER_DRIVER 微步处理

```
input 0   → 实际微步 1
input 1-128 → 原值
input >128  → 256
```

---

## 9. 参考代码路径速查

| 功能 | 旧架构文件 | 关键函数/行 |
|------|-----------|------------|
| 命令注册 | `src/commands/commands.cpp` | `init_callbacks()` L1-53 |
| 运动命令 | `src/commands/stage_commands.cpp` | L1-807 |
| 照明命令 | `src/commands/light_commands.cpp` | — |
| 归位状态机 | `src/operations.cpp` | `prepare/check/finalize_homing_*` |
| 串口协议 | `src/serial_communication.cpp` | — |
| 全局变量 | `src/globals.cpp` / `globals.h` | — |
| TMC 初始化 | `src/init.cpp` | `init_stages()` |
| 轴映射 | `src/global_defs.cpp` | `protocol_axis_to_internal()` |
