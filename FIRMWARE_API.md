# Octoaxes Firmware Serial Protocol API

Host-side reference for implementing a PC control application (host software)
against the Octoaxes motion-control firmware.

- **Target hardware**: Teensy 4.1 with TMC4361A motion controllers and
  TMC2660 / TMC2240 stepper drivers
- **Firmware variants**: `firmware/octoaxes` (mainline) and
  `firmware/octoaxesplus` (squid++ dual-camera). Both implement the same
  command set; the differences are limited and are called out explicitly.
- **Source of truth**: `firmware/<variant>/serial.{h,cpp}`,
  `firmware/<variant>/commandprocessor.cpp`, `firmware/<variant>/config.h`
  (namespace `Commands`), `software/common/define.py`.

> This document describes the **binary** serial protocol. The firmware also
> accepts ASCII debug commands for diagnostics (see
> [ASCII debug commands](#ascii-debug-commands)); they are optional for host
> implementations.

---

## 1. Overview

```
┌──────────────────┐   USB CDC serial, 115200 baud   ┌──────────────────┐
│  Host software   │ ◄──────────────────────────────► │   Teensy 4.1    │
│  (PC)            │   8-byte command packets         │    firmware     │
│                  │   24/40-byte periodic frames     │                 │
└──────────────────┘                                  └────────┬────────┘
                                                              │ SPI
                                                     ┌────────┴────────┐
                                                     │ TMC4361A × 6   │
                                                     │ + drivers      │
                                                     └─────────────────┘
```

The host sends **8-byte command packets**. The firmware does **not** send an
immediate per-command acknowledgement. Instead it continuously broadcasts
**position frames** every 10 ms on the same serial port; the status byte in
these frames tells the host whether any axis is still busy. This is the
legacy Squid protocol model, preserved for drop-in compatibility.

---

## 2. Firmware variants and axis layout

### 2.1 octoaxes (mainline)

| Protocol axis code | Axis name | icID | Type |
|---|---|---|---|
| 0 | X | 0 | Linear stage |
| 1 | Y | 1 | Linear stage |
| 2 | Z | 2 | Linear stage |
| 5 | W | 3 | Filter wheel |
| 6 | W2 | 4 | Filter wheel (second wheel, board optional) |
| 7 | Turret | 5 | Objective turret (4 positions, board optional) |

### 2.2 octoaxesplus (squid++ dual camera)

| Protocol axis code | Axis name | icID | Type |
|---|---|---|---|
| 0 | X | 0 | Linear stage |
| 1 | Y | 1 | Linear stage |
| 2 | Z | 2 | Linear stage |
| 5 | **W1** | 3 | Filter wheel (responds to protocol code 5 via `W → W1` fallback) |
| 6 | W2 | 4 | Filter wheel |
| 7 | Turret | 5 | Objective turret |

### 2.3 Shared conventions

- **Commands are routed by axis *name***, never by icID. Sending `MOVE_X`
  always moves the physical X motor, regardless of internal indexing.
- On octoaxesplus, every command that takes protocol axis code `5` ("W")
  resolves to the **W1** filter wheel automatically.
- **Optional boards** (W2, Turret): if the board is not installed, the axis
  object is deleted at boot and all commands targeting it become silent
  no-ops; the periodic frames report status `COMPLETED` immediately. Use
  the `S:HWINFO` debug command to enumerate which axes are actually present.

---

## 3. Transport layer

| Parameter | Value |
|---|---|
| Interface | USB CDC (Teensy 4.1 `SerialUSB`) |
| Baud rate | 115200 |
| Data format | 8N1 (USB CDC ignores baud, but the host should still configure it) |
| Command framing | Fixed 8-byte packets, CRC-validated (no start/stop bytes) |
| Firmware boot behavior | Waits at most 300 ms for the USB host to connect, then runs standalone; no "engine start" handshake is required |

The firmware parses serial input as a stream of exactly 8 bytes at a time.
When 8 bytes have accumulated it validates the CRC and executes the command.
If the CRC fails the receive buffer is flushed and the next periodic frame
reports `STATUS_CRC_ERROR`.

---

## 4. Packet formats

### 4.1 Command packet (host → firmware), 8 bytes

| Byte | Field | Description |
|---|---|---|
| 0 | `cmd_id` | Host-chosen echo token (any value). Echoed back in periodic frames on octoaxes (see §4.3). Not a command code. |
| 1 | command | Command code (see §8). |
| 2..6 | parameters | 5 parameter bytes, layout per command. |
| 7 | CRC-8-CCITT | CRC over bytes 0..6. |

Big-endian (most-significant byte first) is used for every multi-byte
integer. Signed values are two's-complement.

### 4.2 Response model

There is **no immediate per-command response packet**. Completion is
reported through the firmware's continuous position broadcast:

1. Every **10 ms** the firmware sends a position frame (the "heartbeat").
2. When **all axes stop** (motion-complete falling edge), one extra frame is
   sent immediately, so a waiting host is woken within ~1 ms instead of up to
   10 ms.
3. The frame's status byte is `COMPLETED` when no axis is moving or homing,
   `IN_PROGRESS` otherwise, and `CRC_ERROR` if the last received command
   packet failed its checksum.

### 4.3 Position frame, octoaxes (24 bytes)

| Byte | Field | Description |
|---|---|---|
| 0 | `cmd_id` | Echo of the **last received command's** byte[0] |
| 1 | status | 0 = COMPLETED, 1 = IN_PROGRESS, 2 = CRC_ERROR |
| 2..5 | X position | int32 big-endian, microsteps |
| 6..9 | Y position | int32 big-endian, microsteps |
| 10..13 | Z position | int32 big-endian, microsteps |
| 14..17 | W position | int32 big-endian, microsteps |
| 18 | status bits | bit0 = joystick button pressed (1 = pressed) |
| 19..21 | reserved | 0 |
| 22 | firmware version | high nibble = major, low nibble = minor (currently `0x17` = v1.7) |
| 23 | CRC-8-CCITT | CRC over bytes 0..22 |

### 4.4 Position frame, octoaxesplus (40 bytes)

The octoaxesplus periodic broadcast is a 40-byte extended packet carrying all
axis positions indexed by icID:

| Byte | Field | Description |
|---|---|---|
| 0 | `0xFD` | fixed marker (extended position packet; **no cmd_id echo**) |
| 1 | status | 0 = COMPLETED, 1 = IN_PROGRESS, 2 = CRC_ERROR |
| 2..33 | positions | 8 × int32 big-endian microsteps, indexed by icID (X=0, Y=1, Z=2, W1=3, W2=4, Turret=5; 6..7 always 0) |
| 34 | status bits | bit0 = joystick button pressed |
| 35..37 | reserved | 0 |
| 38 | firmware version | high nibble = major, low nibble = minor (currently `0x17` = v1.7) |
| 39 | CRC-8-CCITT | CRC over bytes 0..38 |

> **octoaxes vs octoaxesplus**: on octoaxes, frame byte[0] echoes your
> command's `cmd_id`, so a host can correlate frames to commands. On
> octoaxesplus byte[0] is always `0xFD` — the host must serialize commands
> (one outstanding at a time) and treat any `COMPLETED` frame as the
> completion signal. An effective pattern is to wait ~25 ms after sending a
> command before the next one (matching proven biforst behavior), or simply
> wait for a `COMPLETED` frame.

---

## 5. CRC-8-CCITT

Poly `0x07`, init `0x00`, no final XOR, table-driven.

Python reference implementation:

```python
CRC8_TABLE = [
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
    0xFA, 0xFD, 0xF4, 0xF3,
]

def crc8ccitt(data: bytes) -> int:
    val = 0
    for b in data:
        val = CRC8_TABLE[val ^ b]
    return val
```

To build a command packet:

```python
def build_command(cmd_id: int, command: int, params: bytes) -> bytes:
    pkt = bytes([cmd_id & 0xFF, command & 0xFF]) + params  # params: 5 bytes
    assert len(pkt) == 7
    return pkt + bytes([crc8ccitt(pkt)])
```

The CRC for octoaxes 24-byte frames covers bytes 0..22; for octoaxesplus
40-byte frames it covers bytes 0..38.

---

## 6. Units and data conventions

| Quantity | Representation | Notes |
|---|---|---|
| Position (reported) | int32 big-endian, **microsteps**, signed | `XACTUAL` (open loop) or `ENC_POS` (when the encoder is enabled) |
| Relative move | int32 big-endian, microsteps, signed | + = positive direction |
| Absolute target | int32 big-endian, microsteps, signed | |
| Soft limits | int32, microsteps | Set via `SET_LIM` |
| Velocity | uint16 × 0.01 → mm/s | e.g. `1000` = 10.00 mm/s |
| Acceleration | uint16 × 0.1 → mm/s² | e.g. `800` = 80.0 mm/s² |
| Lead-screw pitch | uint16 × 0.001 → mm | e.g. `2540` = 2.540 mm |
| Home safety margin | uint16 × 0.001 → mm | |
| Motor current | uint16, mA | peak current |
| Hold current | uint8 0..255 → ratio = value/255 | |
| Microstepping | uint8, 1..256 | 0 → 1; values > 128 → 256 |
| DAC intensity | uint16, 0..65535 | scaled by global intensity factor for illumination commands |
| Light intensity (TTL ports) | uint16, 0..65535 | |
| Timeouts / trigger timing | uint32 big-endian, µs or ms per command | |
| Joystick button | bit0 of frame status byte | 1 = pressed |

mm ↔ microsteps conversion is done by the host using the axis microstepping
and lead-screw pitch:

```
microsteps = mm / pitch_mm × microstepping × 200   (per full revolution)
```

---

## 7. Protocol axis codes

Used in `data[2]` of `HOME_OR_ZERO`, `SET_LIM_SWITCH_POLARITY`,
`CONFIGURE_STEPPER_DRIVER`, `SET_MAX_VELOCITY_ACCELERATION`,
`SET_LEAD_SCREW_PITCH`, `CONFIGURE_STAGE_PID`, `ENABLE_STAGE_PID`,
`DISABLE_STAGE_PID`, `SET_HOME_SAFETY_MERGIN`, `SET_PID_ARGUMENTS`,
`SET_AXIS_DISABLE_ENABLE`.

| Code | Axis |
|---|---|
| 0 | X |
| 1 | Y |
| 2 | Z |
| 4 | XY (combined — homing/zero only) |
| 5 | W (octoaxes) / W1 (octoaxesplus) |
| 6 | W2 |
| 7 | Turret |

---

## 8. Command reference

Notation: `u16`/`u32`/`i32` = unsigned/signed big-endian integer;
`u8` = single byte. `data[N]` refers to byte N of the command packet.

### 8.1 Motion commands

#### `0` MOVE_X · `1` MOVE_Y · `2` MOVE_Z · `4` MOVE_W · `19` MOVE_W2 · `44` MOVE_TURRET — relative move

| data[2..5] | i32 relative position, microsteps |
|---|---|
| data[6] | unused |

Moves the axis by a relative amount (delta relative to the chip position
when the command arrives, matching legacy Squid). Commands are routed by
axis name; `MOVE_W` on octoaxesplus targets W1. The request is rejected (no
motion) only while the axis is homing; a delta that would cross a soft limit
is clamped to the boundary instead (see `MOVETO_*` below).

#### `6` MOVETO_X · `7` MOVETO_Y · `8` MOVETO_Z · `18` MOVETO_W · `43` MOVETO_W2 (octoaxesplus only) · `45` MOVETO_TURRET — absolute move

| data[2..5] | i32 absolute target, microsteps |
|---|---|
| data[6] | unused |

Moves the axis so that its position equals the target. Soft limits are
enforced by **direction-aware clamping**: a target beyond a limit is clamped
to the limit boundary (with a small margin) so the motor stops at the
boundary — if the axis is already at/beyond the limit, the target is clamped
to the current position and the command completes as a no-op. This clamping
(not rejection) is how the firmware enforces `SET_LIM` values, matching
legacy Squid behavior. Commands are rejected only while the axis is homing.
`MOVETO_W2` (43) exists only in the octoaxesplus firmware.

#### `5` HOME_OR_ZERO — homing / zero

| data[2] | protocol axis code (0/1/2/4/5/6/7; 4 = X+Y combined) |
|---|---|
| data[3] | mode: 0 = home toward +, 1 = home toward −, 2 = zero only |
| data[4..6] | unused |

- **mode 2 (zero)**: sets the current position to 0; no motion. With
  `data[2]=4` zeros both X and Y.
- **mode 0/1 (homing)**: runs the homing routine for the axis (search the
  home limit switch at reduced speed, back off, set position 0). The search
  direction is derived from `data[3]`, overriding the firmware default —
  send the mode matching the physical mounting of your unit.
- For filter wheels and the turret, homing establishes the reference for
  absolute slot positioning. Commands issued while homing is in progress are
  rejected by the firmware — wait for status `COMPLETED` first.

### 8.2 Limits

#### `9` SET_LIM — set a soft limit

| data[2] | limit code (see table) |
|---|---|
| data[3..6] | i32 limit value, microsteps |

| Code | Limit |
|---|---|
| 0 | X positive |
| 1 | X negative |
| 2 | Y positive |
| 3 | Y negative |
| 4 | Z positive |
| 5 | Z negative |

Soft limits are enforced by the firmware on absolute moves and relative
moves.

#### `20` SET_LIM_SWITCH_POLARITY — set limit-switch polarity

| data[2] | protocol axis code |
|---|---|
| data[3] | 0 = active-low, 1 = active-high, 2 = disabled (ignored) |
| data[4..6] | unused |

Sets both left and right switch polarities for the axis. On axes whose
polarity is software-configurable in the chip (Z on the current hardware)
the value is written into the TMC4361A `REFERENCE_CONF` register; on other
axes it only updates the software model. Send the correct value for your
sensor variant before homing.

#### `28` SET_HOME_SAFETY_MERGIN — set home safety margin

| data[2] | protocol axis code |
|---|---|
| data[3..4] | u16 margin × 0.001 → mm |
| data[5..6] | unused |

Distance the axis backs off from the limit switch after homing.

### 8.3 Driver and motion configuration

#### `21` CONFIGURE_STEPPER_DRIVER — motor current / microstepping

| data[2] | protocol axis code |
|---|---|
| data[3] | microstepping: 1..256 (0 → 1, values > 128 → 256) |
| data[4..5] | u16 peak current, mA |
| data[6] | u8 hold-current ratio = value/255 |

Configures the stepper driver (TMC2660 or TMC2240, auto-detected per axis).
Applies immediately.

#### `22` SET_MAX_VELOCITY_ACCELERATION — motion profile

| data[2] | protocol axis code |
|---|---|
| data[3..4] | u16 velocity × 0.01 → mm/s |
| data[5..6] | u16 acceleration × 0.1 → mm/s² |

#### `23` SET_LEAD_SCREW_PITCH — lead-screw pitch

| data[2] | protocol axis code |
|---|---|
| data[3..4] | u16 pitch × 0.001 → mm |
| data[5..6] | unused |

Sets the axis pitch used for mm↔microstep conversions. This is required for
correct closed-loop operation and for homing velocity scaling — configure
every axis at startup.

#### `24` SET_OFFSET_VELOCITY — offset velocity (legacy/diagnostic)

| data[2] | axis: 0 = X, 1 = Y |
|---|---|
| data[3..6] | i32 velocity, µm/s |
| data[7] | (CRC) |

Only stored when offset velocity is enabled in the firmware build
(`enable_offset_velocity`, off by default). Intended for the joystick loop;
most hosts can ignore this command.

#### `32` SET_AXIS_DISABLE_ENABLE — enable / disable an axis

| data[2] | protocol axis code |
|---|---|
| data[3] | 0 = disable (motor current off), 1 = enable |

### 8.4 Encoder / closed-loop (PID) control

Closed-loop mode is per axis and off by default. Typical use: configure →
optionally tune → enable.

#### `25` CONFIGURE_STAGE_PID — encoder configuration

| data[2] | protocol axis code |
|---|---|
| data[3] | flip_direction: 0/1 |
| data[4..5] | u16 transitions per revolution |
| data[6] | unused |

Configures the axis encoder (counts per revolution, direction flip). This
also switches position reporting for the axis from `XACTUAL` to `ENC_POS`.
Enabling encoder input alone does **not** start closed-loop control; that is
`26`/`27`.

#### `26` ENABLE_STAGE_PID — enable closed-loop PID for an axis

| data[2] | protocol axis code |
|---|---|

#### `27` DISABLE_STAGE_PID — disable closed-loop PID for an axis

| data[2] | protocol axis code |
|---|---|

#### `29` SET_PID_ARGUMENTS — PID gains

| data[2] | protocol axis code |
|---|---|
| data[3..4] | u16 P |
| data[5] | u8 I |
| data[6] | u8 D |

> **Caution**: wrong PID gains on an encoder-equipped axis can produce
> positive feedback (runaway). Tune with the axis unloaded and within its
> travel.

### 8.5 Illumination — legacy single-source API

The "legacy" API works on a single global `illumination_source`; source
codes are listed in §9.

#### `10` TURN_ON_ILLUMINATION — turn on the current source

No parameters. Turns on whatever source was last selected with `12`
(`data[2]` of command 12 is deliberately ignored by this command to stay
legacy-Squid compatible).

#### `11` TURN_OFF_ILLUMINATION — turn off the current source

No parameters.

#### `12` SET_ILLUMINATION — select source and intensity

| data[2] | light-source code (see §9.1) |
|---|---|
| data[3..4] | u16 intensity 0..65535 |
| data[5..6] | unused |

Stores the source and writes the intensity to the matching DAC channel,
scaled by the global intensity factor (see `17`). If the light is currently
on, the output is updated immediately.

#### `13` SET_ILLUMINATION_LED_MATRIX — LED-matrix pattern and color

| data[2] | pattern code 0..8 (see §9.1) |
|---|---|
| data[3] | red 0..255 |
| data[4] | green 0..255 |
| data[5] | blue 0..255 |

Sets the 128-pixel APA102 matrix. Colors are scaled internally to the
matrix's 0..100 intensity range.

#### `17` SET_ILLUMINATION_INTENSITY_FACTOR — global intensity factor

| data[2] | factor × 100 (e.g. 60 → 0.60) |
|---|---|
| data[3..6] | unused |

Global multiplier applied to illumination intensities (default 0.60). Applies
to `12` and to the multi-port `34` intensity writes.

#### `14` ACK_JOYSTICK_BUTTON_PRESSED — acknowledge joystick button

No parameters. Clears the joystick-button flag. The firmware auto-clears the
flag anyway after 1000 ms if not acknowledged; this command lets the host
clear it immediately.

#### `15` ANALOG_WRITE_ONBOARD_DAC — direct DAC write

| data[2] | DAC channel 0..7 |
|---|---|
| data[3..4] | u16 value 0..65535 |
| data[5..6] | unused |

Writes the DAC80508 output register directly (no intensity-factor scaling).
Valid channels are 0..7; out-of-range values are rejected.

#### `16` SET_DAC80508_REFDIV_GAIN — DAC gain configuration

| data[2] | ref-divider code |
|---|---|
| data[3] | gain code (bit per channel; default 0x80 = channels 0..6 gain 1, channel 7 gain 2) |
| data[4..6] | unused |

### 8.6 Illumination — multi-port API

The multi-port API addresses illumination outputs by **port index** directly
instead of legacy source codes.

| Port index | Source code | octoaxes | octoaxesplus |
|---|---|---|---|
| 0 | D1 (11) | GPIO pin 5, DAC ch 0 | pin 32, DAC ch 0 |
| 1 | D2 (12) | pin 4, DAC ch 1 | pin 31, DAC ch 1 |
| 2 | D3 (14) | pin 22, DAC ch 2 | pin 30, DAC ch 2 |
| 3 | D4 (13) | pin 3, DAC ch 3 | pin 29, DAC ch 3 |
| 4 | D5 (15) | pin 23, DAC ch 4 | pin 28, DAC ch 4 |
| 5 | D6 (16) | — | pin 25, DAC ch 5 |
| 6 | D7 (17) | — | pin 24, DAC ch 6 |
| 7 | D8 (18) | — | pin 10, DAC ch 7 |

> Note the legacy source-code ordering quirk: D3 = code 14, D4 = code 13
> (out of order), inherited from the legacy Squid protocol.

#### `34` SET_PORT_INTENSITY — set one port's DAC intensity

| data[2] | port index |
|---|---|
| data[3..4] | u16 intensity 0..65535 (scaled by the global factor) |
| data[5..6] | unused |

#### `35` TURN_ON_PORT — turn on one port

| data[2] | port index |
|---|---|

Gated by the laser interlock: the GPIO stays low while the interlock is open.

#### `36` TURN_OFF_PORT — turn off one port

| data[2] | port index |
|---|---|

#### `37` SET_PORT_ILLUMINATION — set intensity and state in one call

| data[2] | port index |
|---|---|
| data[3..4] | u16 intensity |
| data[5] | 0 = off, nonzero = on |

#### `38` SET_MULTI_PORT_MASK — set many ports at once

| data[2..3] | u16 `port_mask`: bit i selects port i |
|---|---|
| data[4..5] | u16 `on_mask`: bit i = turn port i on (0 = off) |
| data[6] | unused |

Only the ports with a set bit in `port_mask` are touched.

#### `39` TURN_OFF_ALL_PORTS — turn everything off

No parameters. Turns off all ports and the LED matrix. This is also what the
serial watchdog (see §11) invokes on communication loss.

### 8.7 Camera hardware trigger / strobe

See §10 for timing semantics.

#### `33` SET_TRIGGER_MODE — trigger mode

| data[2] | 0 = NORMAL (fixed 50 µs pulse), 1 = LEVEL (strobe_delay + on_time pulse) |
|---|---|

#### `30` SEND_HARDWARE_TRIGGER — fire a camera trigger

| data[2] | bits 0..3 = camera channel (0..3 on octoaxes, 0..7 on octoaxesplus); bit 7 = enable strobe illumination |
|---|---|
| data[3..6] | u32 illumination on-time, µs |
| data[7] | (CRC) |

Pulls the trigger pin **low** (negative pulse) for the channel. With
bit7 set, the firmware strobe timer also turns the illumination on
`strobe_delay` µs after the trigger, holds it for `on-time` µs, then turns
it off, synchronously within the ISR for on-times ≤ 30 ms.

The command is **dropped** if the same channel is still mid-pulse
(re-entrancy guard): in LEVEL mode while the pin is held low, or while a
strobe is pending/active.

#### `31` SET_STROBE_DELAY — strobe delay per channel

| data[2] | camera channel |
|---|---|
| data[3..6] | u32 delay, µs |
| data[7] | (CRC) |

Delay between the trigger falling edge and illumination turn-on.

### 8.8 Safety, watchdog, misc

#### `40` SET_WATCHDOG_TIMEOUT — serial watchdog

| data[2..5] | u32 timeout, ms (0 = default 5000; clamped to max 1 hour) |
|---|---|

Enables the communication watchdog. The watchdog timer is reset by **any
packet with a valid CRC** (so `42` HEARTBEAT is technically redundant). If
no valid packet arrives within the timeout, the firmware turns off all
illumination and the LED matrix once — protection against an unattended
laser when the host has crashed or the cable is unplugged. Recommended
default: enable at startup with 5000 ms.

#### `42` HEARTBEAT — heartbeat

No parameters. No-op: the watchdog is reset by any valid packet. Exists for
legacy software compatibility.

#### `41` SET_PIN_LEVEL — raw GPIO write (diagnostic)

| data[2] | Teensy GPIO pin number |
|---|---|
| data[3] | 0 = LOW, 1 = HIGH |
| data[4..6] | unused |

For bring-up only. The pin is forced to OUTPUT on the first write.

### 8.9 Initialization and reset

#### `253` INITFILTERWHEEL · `252` INITFILTERWHEEL_W2 — filter-wheel init

No parameters. **No-ops** in current firmware: the filter wheels are already
configured at boot, and actual homing is triggered by `HOME_OR_ZERO`
(mode 0/1, axis 5/6). Kept for legacy-Squid software compatibility.

#### `254` INITIALIZE — full re-initialization

No parameters. Equivalent to a power-cycle of the motion subsystem:
TMC4361A chips are soft-reset and reconfigured, the C++ axis state machines
are reset, DAC settings are restored, and the trigger system returns to
NORMAL. All axes stop. Send this once at host startup before configuring
axes.

#### `255` RESET — reset motion and trigger state

No parameters. Stops all axes and resets the trigger system (pins back to
HIGH, strobes cleared, mode NORMAL) without reinitializing the chips.

### 8.10 Command summary table

| Code | Name | Parameters (data[2..6]) |
|---|---|---|
| 0 | MOVE_X | i32 relative microsteps |
| 1 | MOVE_Y | i32 relative microsteps |
| 2 | MOVE_Z | i32 relative microsteps |
| 3 | MOVE_THETA | *not implemented (no-op)* |
| 4 | MOVE_W | i32 relative microsteps |
| 5 | HOME_OR_ZERO | u8 axis, u8 mode (0/1/2) |
| 6 | MOVETO_X | i32 absolute microsteps |
| 7 | MOVETO_Y | i32 absolute microsteps |
| 8 | MOVETO_Z | i32 absolute microsteps |
| 9 | SET_LIM | u8 limit code, i32 value µsteps |
| 10 | TURN_ON_ILLUMINATION | — |
| 11 | TURN_OFF_ILLUMINATION | — |
| 12 | SET_ILLUMINATION | u8 source, u16 intensity |
| 13 | SET_ILLUMINATION_LED_MATRIX | u8 pattern, u8 R, u8 G, u8 B |
| 14 | ACK_JOYSTICK_BUTTON_PRESSED | — |
| 15 | ANALOG_WRITE_ONBOARD_DAC | u8 channel, u16 value |
| 16 | SET_DAC80508_REFDIV_GAIN | u8 div, u8 gains |
| 17 | SET_ILLUMINATION_INTENSITY_FACTOR | u8 factor × 100 |
| 18 | MOVETO_W | i32 absolute microsteps |
| 19 | MOVE_W2 | i32 relative microsteps |
| 20 | SET_LIM_SWITCH_POLARITY | u8 axis, u8 polarity |
| 21 | CONFIGURE_STEPPER_DRIVER | u8 axis, u8 µsteps, u16 mA, u8 hold |
| 22 | SET_MAX_VELOCITY_ACCELERATION | u8 axis, u16 vel×100, u16 acc×10 |
| 23 | SET_LEAD_SCREW_PITCH | u8 axis, u16 pitch×1000 |
| 24 | SET_OFFSET_VELOCITY | u8 axis, i32 µm/s |
| 25 | CONFIGURE_STAGE_PID | u8 axis, u8 flip, u16 tpr |
| 26 | ENABLE_STAGE_PID | u8 axis |
| 27 | DISABLE_STAGE_PID | u8 axis |
| 28 | SET_HOME_SAFETY_MERGIN | u8 axis, u16 margin×1000 |
| 29 | SET_PID_ARGUMENTS | u8 axis, u16 P, u8 I, u8 D |
| 30 | SEND_HARDWARE_TRIGGER | u8 ch\|bit7 strobe, u32 on-time µs |
| 31 | SET_STROBE_DELAY | u8 channel, u32 delay µs |
| 32 | SET_AXIS_DISABLE_ENABLE | u8 axis, u8 0/1 |
| 33 | SET_TRIGGER_MODE | u8 mode (0/1) |
| 34 | SET_PORT_INTENSITY | u8 port, u16 intensity |
| 35 | TURN_ON_PORT | u8 port |
| 36 | TURN_OFF_PORT | u8 port |
| 37 | SET_PORT_ILLUMINATION | u8 port, u16 intensity, u8 on/off |
| 38 | SET_MULTI_PORT_MASK | u16 port_mask, u16 on_mask |
| 39 | TURN_OFF_ALL_PORTS | — |
| 40 | SET_WATCHDOG_TIMEOUT | u32 timeout ms |
| 41 | SET_PIN_LEVEL | u8 pin, u8 level |
| 42 | HEARTBEAT | — |
| 43 | MOVETO_W2 *(octoaxesplus only)* | i32 absolute microsteps |
| 44 | MOVE_TURRET | i32 relative microsteps |
| 45 | MOVETO_TURRET | i32 absolute microsteps |
| 252 | INITFILTERWHEEL_W2 | — (no-op) |
| 253 | INITFILTERWHEEL | — (no-op) |
| 254 | INITIALIZE | — |
| 255 | RESET | — |

---

## 9. Light-source codes

### 9.1 Legacy illumination source codes (`data[2]` of command 12)

| Code | Meaning |
|---|---|
| 0 | LED matrix: full field |
| 1 | LED matrix: left half |
| 2 | LED matrix: right half |
| 3 | LED matrix: left blue / right red |
| 4 | LED matrix: low-NA ring |
| 5 | LED matrix: left dot |
| 6 | LED matrix: right dot |
| 7 | LED matrix: top half |
| 8 | LED matrix: bottom half |
| 11 | TTL port D1 |
| 12 | TTL port D2 |
| 14 | TTL port D3 *(code out of order — legacy)* |
| 13 | TTL port D4 *(code out of order — legacy)* |
| 15 | TTL port D5 |
| 16 | TTL port D6 *(octoaxesplus only)* |
| 17 | TTL port D7 *(octoaxesplus only)* |
| 18 | TTL port D8 *(octoaxesplus only)* |
| 20 | External FET (legacy placeholder, no action) |

### 9.2 Laser interlock

The illumination outputs are gated by a hardware interlock input
(octoaxes: pin 2; octoaxesplus: pin 38; **LOW = safe**). While the interlock
is open, TTL ports are forced low in the main loop and `TURN_ON_PORT`
refuses to raise them. If your system has no interlock, the firmware must be
built with `-DDISABLE_LASER_INTERLOCK` for illumination to work.

---

## 10. Hardware trigger and strobe timing

Trigger outputs are active-low (idle HIGH, pulse = LOW). Per-channel
parameters are `strobe_delay_us` and `illumination_on_time_us`.

### NORMAL mode (`SET_TRIGGER_MODE` 0)

- The trigger pin is pulled LOW for a **fixed 50 µs** and restored by the
  main loop.
- Strobe illumination (if bit7 set on `SEND_HARDWARE_TRIGGER`) still runs
  independently of the 50 µs pin pulse.

### LEVEL mode (`SET_TRIGGER_MODE` 1)

- The trigger pin stays LOW for `strobe_delay + illumination_on_time`
  (both µs), i.e. the pulse width is programmable.

### Strobe illumination (bit7 of command 30)

A 100 µs `IntervalTimer` ISR drives the illumination timing:

- `on_time ≤ 30 ms`: **synchronous** — delay `strobe_delay` µs, turn the
  light on, hold for `on_time` µs (interrupt-disabled, µs-precise), turn off.
- `on_time > 30 ms`: **asynchronous** — turn on after `strobe_delay` µs,
  turn off after `strobe_delay + on_time` µs (light stays on between strobe
  ISR ticks).

The illumination source used by the strobe is **latched at strobe start**,
so switching channels mid-acquisition cannot leave a laser on. `RESET` /
`INITIALIZE` clean up any mid-strobe light.

> This ISR intentionally blocks briefly (µs-level timing precision is a hard
> requirement for short exposures); see the firmware history for the
> wontfix-by-design decision.

---

## 11. Watchdog

See command `40`. Default timeout 5000 ms, maximum 1 hour, `0` selects the
default. Any packet with a valid CRC resets the timer. On timeout the
firmware calls `turn_off_all_ports()` once (single-shot).

---

## 12. Versioning

| Channel | Value |
|---|---|
| Frame byte[22] (24-byte) / byte[38] (40-byte) | major << 4 \| minor — currently `0x17` (v1.7) |
| `S:VERSION` (ASCII debug) | internal build number: 106 (octoaxes), 119 (octoaxesplus) |

---

## 13. ASCII debug commands

ASCII commands are sent with the two-byte header `0x55 0xAA` followed by the
command text and a newline (e.g. `\x55\xAA"S:VERSION\n"`). Replies are
plain-text lines ending with a newline (no header). The `S:*` handlers
listed below reply unconditionally, including in production firmware builds
(without `DEBUG`) where general debug logging is compiled out. `S:Engine
Start` is the exception — its reply is debug-build only. These commands are
diagnostic aids; do not depend on them for production control.

| Command | Reply (abridged) | Purpose |
|---|---|---|
| `S:VERSION` | `S:VERSION:106` | firmware build number (always answered, even without the debug build) |
| `S:Engine Start` | `System already running...` | legacy compatibility no-op |
| `S:HWINFO` | `S:HWINFO:X:TMC4361A+TMC2240` per axis + `S:HWINFO:END` | enumerate present axes and driver types |
| `S:ENCPOS` | `S:ENCPOS:<axis>:enc=.. xactual=.. dev=..` per axis + `S:ENCPOS:END` | encoder vs XACTUAL comparison |
| `S:DUMPREGS [axis]` | `S:DUMP <axis> STATUS=.. XACTUAL=.. ...` + `S:DUMPREGS:END` | TMC4361A register dump for on-site diagnosis |
| `S:DUMP_TOFF [axis]` | `S:TOFF <axis> driver=.. coverTOFF=.. shadowTOFF=.. match=Y/N` + `S:DUMP_TOFF:END` | TMC2240 enable/disable diagnosis |
| `S:SET_HOMING_VEL <axis> <mm/s>` | `S:SET_HOMING_VEL:OK:<axis>=<vel>` | change homing speed at runtime |
| `S:JOYSTICK_STATS` | `legacy=N crc_ok=N crc_fail=N` | hand-controller link statistics |

Other ASCII commands are dispatched to axis objects for internal testing and
may change between firmware versions — do not use them in host software.

---

## 14. Recommended host workflow

### 14.1 Startup sequence

```
1. Open the serial port (115200, 8N1).
2. [optional] send S:VERSION, verify a reply; send S:HWINFO to enumerate axes.
3. Send INITIALIZE (254) — clean state (chips soft-reset, all axes stopped).
4. Enable the watchdog: SET_WATCHDOG_TIMEOUT (40) with 5000 ms.
5. Per axis:
   - SET_LEAD_SCREW_PITCH (23)
   - CONFIGURE_STEPPER_DRIVER (21) — microstepping, peak current, hold ratio
   - SET_MAX_VELOCITY_ACCELERATION (22)
   - SET_LIM (9) for X/Y/Z soft limits
   - SET_LIM_SWITCH_POLARITY (20) if the sensor variant differs from boot defaults
   - [closed loop only] CONFIGURE_STAGE_PID (25) → SET_PID_ARGUMENTS (29) → ENABLE_STAGE_PID (26)
6. Home the axes: HOME_OR_ZERO (5), mode 0/1 — wait for COMPLETED.
   Filter wheels / turret must be homed before absolute positioning is valid.
7. Move with MOVETO_* / MOVE_*.
```

### 14.2 Wait-for-completion pattern

Because the firmware answers through the periodic frames, the host should
read frames continuously and wait for the completion condition:

```python
def wait_idle(ser, timeout_s=10.0):
    """Read frames until status == COMPLETED or timeout."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        frame = ser.read(24)              # or 40 on octoaxesplus
        if len(frame) < 24:
            continue
        if crc8ccitt(frame[:23]) != frame[23]:
            continue
        status = frame[1]
        if status == 0:                   # COMPLETED
            return frame
        if status == 2:                   # CRC_ERROR — resend the last command
            raise IOError("last command packet was rejected (CRC)")
    raise TimeoutError("operation timed out")
```

Rules of thumb:

- Send one command at a time and wait for `COMPLETED` before the next
  (motion commands are serialized anyway; homing rejects overlapping moves).
- On octoaxesplus, do not rely on cmd_id correlation (frames always use
  `0xFD`); on octoaxes, you may additionally filter frames by the echoed
  `cmd_id`.
- Treat `CRC_ERROR` as "the last command was not executed — resend it".
- A `COMPLETED` frame with the expected final position is the success
  criterion for moves; a move clamped by soft limits still completes with
  `COMPLETED`.

---

## 15. Behavior notes and compatibility

- **Legacy Squid drop-in**: the binary protocol (command codes, 8-byte
  packet layout, 24-byte frames, CRC) is byte-compatible with the legacy
  Squid firmware; the octoaxes firmware was verified as a drop-in
  replacement driven by the legacy Squid host software.
- **Direction conventions**: physical direction of a positive move matches
  the legacy Squid convention; firmware-internal compensation (mirror-assembled
  hardware) is invisible to the host. If you physically re-mount an axis,
  re-verify homing direction (`HOME_OR_ZERO` mode 0 vs 1).
- **Units**: all reported positions are microsteps; convert to mm with the
  pitch/microstepping you configured via commands 21/23.
- **Interlock**: illumination TTL ports cannot be turned on while the
  interlock is open; trigger/strobe illumination is also gated.
- **octoaxesplus specifics**: 8 trigger channels (pins 6/4/23/22/15/41/40/39),
  8 illumination ports (D1..D8), 40-byte broadcast frames, `MOVETO_W2` (43)
  exists here but not in the octoaxes mainline firmware.
