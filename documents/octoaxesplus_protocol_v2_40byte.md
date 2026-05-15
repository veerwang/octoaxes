# octoaxesplus 位置上报协议扩展 v2（24 → 40 字节）

> **日期**：2026-05-15
> **分支**：maxpro
> **状态**：方案文档（pending 实施）
> **依赖**：`documents/octoaxesplus_axis_definitions.md`、`documents/octoaxesplus_xyzw1w2_plan.md`

---

## 背景

octoaxes 主线时代 24 字节响应包设计针对 4 轴（X/Y/Z/W），与旧 Squid GUI
兼容。octoaxesplus 现已扩 5 轴（X/Y/Z/W1/W2），未来计划全 8 轴（含
F1/Z2/F2/R/T）。**24 字节包的位置字段不够 5+ 轴用**，导致：

- 实测：octoaxesplus 主固件下 GUI 显示 W1/W2 的 State 永远 "Unknown"
- 根因：24 字节包只 4 个位置 slot，没有 W2 字段；W1 字段被 firmware 当 "W" 找轴失败填 0

## 决策（2026-05-15 用户确认）

1. ✅ **octoaxes 主线保持 24 字节**，兼容旧 Squid GUI 不动
2. ✅ **octoaxesplus 改用 40 字节**，支持 8 轴位置 + 状态
3. ✅ **用 cmd_id 区分 24/40 字节包**：octoaxesplus 周期性位置广播用专用
   `cmd_id = 0xFD (253)`；GUI 收包时按首字节 cmd_id 判定长度
4. ⏳ **实施下个会话做**（本会话只写设计文档 commit）

---

## 新 40 字节包格式（octoaxesplus）

| 字节偏移 | 内容 | 类型 |
|---|---|---|
| `byte[0]` | **cmd_id = 0xFD (253)** ← 标识扩展位置包 | uint8 |
| `byte[1]` | 状态：0=COMPLETED, 1=IN_PROGRESS, 2=CRC_ERROR | uint8 |
| `byte[2-5]` | icID=0 位置（octoaxesplus = Y 轴） | int32 BE |
| `byte[6-9]` | icID=1 位置（X 轴） | int32 BE |
| `byte[10-13]` | icID=2 位置（Z 轴） | int32 BE |
| `byte[14-17]` | icID=3 位置（W1 滤光转盘） | int32 BE |
| `byte[18-21]` | icID=4 位置（W2 滤光转盘） | int32 BE |
| `byte[22-25]` | icID=5 位置（F2 预留） | int32 BE |
| `byte[26-29]` | icID=6 位置（R 预留） | int32 BE |
| `byte[30-33]` | icID=7 位置（T 预留） | int32 BE |
| `byte[34]` | 状态位（bit0 = 摇杆按钮） | uint8 |
| `byte[35-37]` | 保留 | uint24 |
| `byte[38]` | 固件版本（高半字节=major，低半字节=minor） | uint8 |
| `byte[39]` | CRC-8-CCITT of `byte[0..38]` | uint8 |

**关键设计**：
- **位置按 firmware icID 索引**（不按轴名），协议层与轴命名解耦
  - 未来 W1 改名 / 加 Z2 / 修改 axisName，协议字节布局不动
  - 上位机通过 `AXIS_CONFIG[axis_name]["index"]` 反查 icID → 字节 slot
- **未实例化的 icID**（5/6/7 当前 octoaxesplus 没 F2/R/T）：firmware 填 0
- **cmd_id = 0xFD** 是周期性扩展位置广播的固定标识；命令响应仍用各自 cmd_id
  + 24 字节格式（保持兼容性）—— **TBD：实施时再决定命令响应是否也升级**

## GUI 端解析逻辑

```python
def handle_binary_response(self, data: bytes):
    if len(data) < 24:
        return
    
    cmd_id = data[0]
    
    if cmd_id == 0xFD and len(data) >= 40:
        # 40 字节扩展位置包（octoaxesplus）
        positions_by_icid = struct.unpack('>8i', data[2:34])  # 8 个 int32
        for axis_name, cfg in AXIS_CONFIG.items():
            ic_id = cfg["index"]
            if 0 <= ic_id < 8:
                steps = positions_by_icid[ic_id]
                # update axis status...
    elif len(data) == 24:
        # 24 字节旧包（octoaxes 主线 / 旧 Squid 兼容）
        # 现有解析逻辑保持不动
        ...
```

## Firmware 端实施

### 1. `firmware/octoaxesplus/serial.h`
```cpp
static const int MSG_LENGTH = 40;        // 24 → 40
static const byte EXTENDED_POS_CMD_ID = 0xFD;  // 新增
```
`sendResponse` 签名重设计：接收 axis position 数组 + count，或仍接 8 个
int32（更接近现有签名习惯）。

### 2. `firmware/octoaxesplus/serial.cpp::send_position_update`

```cpp
void send_position_update() {
    ...
    int32_t positions[8] = {0};
    uint8_t count = axisManager.getAxisCount();
    for (uint8_t i = 0; i < count && i < 8; i++) {
        Axis *axis = axisManager.getAxis(i);
        if (axis) positions[i] = axis->getCurrentPositionMicrosteps();
    }
    sendExtendedResponse(EXTENDED_POS_CMD_ID, status, positions,
                         joystick_button_pressed);
}
```

新增 `sendExtendedResponse()`，按 40 字节布局填包。

### 3. 命令响应（普通命令的 ack）

**保留 24 字节 sendResponse() 不变**（命令响应可以仍是 24 字节，只是不带
W2 位置）—— 这样所有改动都集中在周期性广播路径，命令派发链路不动。

---

## 影响范围

| 文件 | 改动 | 类型 |
|---|---|---|
| `firmware/octoaxesplus/serial.h` | `MSG_LENGTH=40`、加 `EXTENDED_POS_CMD_ID`、新签名 | 协议结构 |
| `firmware/octoaxesplus/serial.cpp` | 加 `sendExtendedResponse`、改 `send_position_update` 走新路径 | 实施 |
| `software/common/gui/main_window.py` | `handle_binary_response` 加 cmd_id 分支：0xFD → 40 字节解析 + icID 索引派发 | 解析 |
| `software/common/hardware/serial_thread.py` | 可能需要按 cmd_id 切换 expected message length；视具体串口读逻辑而定 | 帧分隔 |
| `firmware/octoaxes/` | **完全不动** | — |
| `software/octoaxes/` | **完全不动** | — |

### 帧分隔注意

`serial_thread.py` 当前读 24 字节作为一包。如果改成支持 40 字节包，需要：
- 先读 byte[0] 判 cmd_id
- 如果 cmd_id == 0xFD → 再读 39 字节（共 40）
- 否则 → 再读 23 字节（共 24）

实施时这是最容易出 bug 的地方，要小心测试。

---

## 后续兼容性 / 演进

- 24 字节包仍可用作命令响应（保持兼容）
- 未来若 octoaxes 也想升级 → 改 octoaxes serial.cpp 同样支持新格式，但
  cmd_id 区分让 GUI 知道用哪种解析
- W2 位置如果不放 broadcast，可以通过 S:POS W2 ASCII 命令按需查询（备用方案）

## 验证计划（实施时）

1. 编译：octoaxes 不变，octoaxesplus 编译 SUCCESS
2. 烧 octoaxesplus 主固件，串口监听 0xFD 开头的 40 字节包
3. 用 Python 脚本 unpack 验证 8 个 int32 与 firmware getAxisCount 一致
4. GUI 解析：octoaxesplus profile 启动，所有 5 个轴的 State / Position 列
   都正确更新（W1 因 CLK 缺仍为 0，W2 显示正常位置）
5. octoaxes profile 启动，旧 24 字节包仍工作（X/Y/Z/W 显示）

## 实施任务清单

- [ ] firmware/octoaxesplus/serial.{h,cpp}：实施 40 字节 sendExtendedResponse
- [ ] software/common/hardware/serial_thread.py：按 cmd_id 切换帧长
- [ ] software/common/gui/main_window.py：handle_binary_response cmd_id 分支
- [ ] 编译验证（两工程）
- [ ] 烧写 + GUI 实测 W1/W2 状态显示
- [ ] 文档：更新 `documents/octoaxesplus_axis_definitions.md` 第 6 层、CLAUDE.md 位置上报协议章节
