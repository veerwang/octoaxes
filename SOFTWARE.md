# Octoaxes Host Software — Architecture & Main Logic

This document describes the **host PC software** (the `software/` directory of this
repository): its main-program logic, threading model, data flow, and how it
interacts with the firmware. It is written for developers who need to understand,
maintain, or extend the GUI, and for third parties who want to replicate its
behavior against the firmware.

For the wire protocol itself (command bytes, packet layouts, CRC, units), see
[`FIRMWARE_API.md`](FIRMWARE_API.md) — this document focuses on the host side.

> **Companion documents**
>
> - [`FIRMWARE_API.md`](FIRMWARE_API.md) — firmware command/protocol reference
> - [`CLAUDE.md`](CLAUDE.md) — project overview, firmware structure, cross-layer
>   axis-definition checklist
> - `documents/` — axis-definition analysis, protocol v2 (40-byte packet)
>   description, performance baselines

---

## 1. Overview

The host software is a **PyQt5 desktop application** that drives the Teensy 4.1
firmware over a USB serial link (115200 baud). Its job is to:

1. present the microscope's 7 (octoaxes) or 5 (octoaxesplus) axes as a GUI,
2. translate user actions (move / home / set limits / illumination) into the
   firmware's binary command protocol,
3. continuously ingest firmware status (10 ms periodic position broadcasts and
   ASCII status lines) and reflect it in the UI,
4. run diagnostics: an integration-test panel and a long-running Z-axis aging
   test.

The design is **data-driven and profile-based**: one shared code base
(`software/common/`) is parameterized by a per-profile `constants.py`
(`software/octoaxes/` vs `software/octoaxesplus/`). All axis knowledge — which
axes exist, their type, pitch, microstepping, motor current, encoder setup,
limit-switch polarity — lives in the profile constants, never in shared code.

```
┌─────────────────────────── software/ ───────────────────────────┐
│                                                                  │
│  software/octoaxes/main.py          software/octoaxesplus/main.py│
│  ├─ constants.py (7-axis profile)   ├─ constants.py (5-axis)     │
│  └─ run.bat                         └─ run.bat                   │
│                                                                  │
│  software/common/          ← shared, profile-agnostic            │
│  ├─ define.py              protocol enums & command maps         │
│  ├─ hardware/serial_thread.py    serial I/O thread               │
│  ├─ hardware/axis_manager.py     axis-state store                │
│  ├─ gui/main_window.py           main window & all logic         │
│  ├─ gui/widgets.py               AxisStatusDisplay / ControlPanel│
│  │                               / IlluminationPanel             │
│  ├─ gui/test_panel.py            Integration Test tab            │
│  ├─ utils/helpers.py             port scan, payload helpers      │
│  └─ tests/                       scripts (e.g. test_hwinfo.py)   │
└──────────────────────────────────────────────────────────────────┘
```

---

## 2. The profile mechanism

Both profiles share one entry-point file structure. `main.py` differs only in
its docstring; the logic is identical (`diff` between the two is only comments):

```python
HERE   = os.path.dirname(os.path.abspath(__file__))       # profile dir
COMMON = os.path.join(os.path.dirname(HERE), "common")    # ../common

sys.path.insert(0, COMMON)   # shared modules (gui/, hardware/, utils/, define.py)
sys.path.insert(0, HERE)     # profile dir wins (contains constants.py)

import constants as _profile_constants
import utils
sys.modules["utils.constants"] = _profile_constants   # alias for `from utils.constants import ...`
utils.constants = _profile_constants                  # also for `utils.constants.X` attribute access
```

Then `TeensyControlGUI` is instantiated and the Qt event loop starts. Because
`sys.modules["utils.constants"]` points at the profile's `constants.py`, every
shared module that does `from utils.constants import AXIS_CONFIG` automatically
sees the correct axis set for the running profile — **no shared code ever needs
to know which profile it is in**.

### Profile-safe rules (enforced by review, documented in CLAUDE.md)

| ✅ Allowed in `common/` | ❌ Forbidden in `common/` |
|---|---|
| Iterate `AXIS_CONFIG.keys()` / `.items()` | Hardcode axis-name lists (`["X","Y","Z","W","E1","E3","E4"]`) |
| Use `AXIS_CONFIG[axis]["index"]` (firmware icID) | Assume a fixed axis count |
| Use `AXIS_CONFIG[axis]["type"]`, `["movement_sign"]`, etc. | Put profile-specific business logic in common/ |
| List **all** profiles' axes in `define.py` maps (e.g. `AXIS_MOVE_CMD_MAP` contains W/E1/E3/E4 **and** W1/W2) | Import profile-specific constants in common/ |

`define.py` holds the protocol enums shared by both profiles: `CMD_SET`
(command bytes), `AXIS` (protocol axis values), `AXIS_MOVE_CMD_MAP`,
`AXIS_MOVETO_CMD_MAP`, `AXIS_LIMIT_CODE_MAP`, plus hardware constants
(`SQUID_FILTERWHEEL_OFFSET`, `OBJECTIVE_RATIO`, …).

---

## 3. Startup sequence

```
main()
├─ setup_exception_handler()            # global excepthook → log/msgbox
├─ QApplication + Arial-14 font
└─ TeensyControlGUI()
   ├─ build UI: 4 tabs (Motion / Illumination / Log / Integration Test)
   ├─ start timers:
   │    startup_timer  (500 ms interval; startup_launch stops it after first run)
   │    status_timer   (2000 ms, periodic)   → safe_refresh_all_axes()
   │                                   (only if "Auto Poll" is checked)
   └─ find_and_connect_teensy()
        ├─ helpers.find_teensy_port(): serial.tools.list_ports, matches
        │    "USB Serial" / "Teensy" in device description
        ├─ start SerialThread(port, 115200)
        └─ on connect signal → check_connection_status()
```

### Connection handshake (`check_connection_status`)

Once the port is open, a scripted burst of queries runs via `QTimer.singleShot`:

| delay after connect | action | wire traffic |
|---|---|---|
| +500 ms | `query_firmware_version` | ASCII `S:VERSION` |
| +800 ms | `query_hardware_info` | ASCII `S:HWINFO` |
| +1000 ms | `refresh_all_axes_status` | per axis: ASCII `<AXIS>:GET_DATA` (50 ms apart) |

`refresh_all_axes_status` sends `X:GET_DATA`, `Y:GET_DATA`, … for every
`AXIS_CONFIG` key under the `_query_mutex`. Note: **production firmware builds
return no ASCII replies** to these queries (see §5.2); the UI state comes from
the periodic binary broadcast instead. Debug builds do reply, which populates
the `AxisManager` faster.

### Startup launch (`startup_launch`)

Fires once, ~0.5 s after the connection is confirmed:

1. **`set_limits()`** — for the **current axis**, when its `type` is not
   `filter_wheel`/`objective` (i.e. step motors): send the limit-switch
   positions stored in the UI as binary `SET_LIM` (cmd 9). (Limits are also
   re-sent whenever the user switches axes — §7.1.)
2. **`_configure_actuators()`** — per axis: `SET_LEAD_SCREW_PITCH` (cmd 23),
   `CONFIGURE_STEPPER_DRIVER` (cmd 21), and — only where the config keys exist —
   `SET_LIM_SWITCH_POLARITY` (cmd 20) and `S:SET_HOMING_VEL`. See §6.
3. **`_configure_encoders()`** — per axis with `has_encoder=True`:
   `CONFIGURE_STAGE_PID` (cmd 25), plus `SET_PID_ARGUMENTS` (cmd 29) and
   `ENABLE_STAGE_PID` (cmd 26) when the axis runs closed-loop. See §6.

The application does **not** home axes at startup — homing is always an
explicit user action.

---

## 4. Communication layer: `SerialThread`

`software/common/hardware/serial_thread.py` is a `QThread` wrapping pyserial.
It owns all serial I/O and exposes Qt signals to the GUI thread.

### 4.1 Sending (TX pipeline)

Two command channels exist, distinguished by a `('S' | 'B')` type tag:

- **String (ASCII) commands** — debug/status queries. Sent as
  `b"\x55\xaa" + (command + "\n")` (two-byte header, text, newline).
- **Binary commands** — motion/illumination/configuration. The caller builds an
  8-byte array (cmd byte at `[1]`, parameters `[2..6]`); the sender then
  **overwrites** the packet on the wire:

  ```python
  data_to_send[0] = 0x02          # fixed binary prefix (firmware treats it as cmd_id and echoes it)
  data_to_send[-1] = crc(data_to_send[:-1])   # CRC-8-CCITT over bytes 0..6
  ```

  CRC is computed with the `crc` package:
  `CrcCalculator(Crc8.CCITT, table_based=True)` (poly 0x07), identical to the
  firmware implementation.

**Buffering & pacing.** Both entry points (`send_command`,
`send_binary_command`) first try a direct write under `_send_lock`; if the lock
is held, the command goes into a FIFO `deque` (capacity 100; overflow is
dropped with a log line). A `QTimer` **created on the main thread** (via the
`start_timer_signal` handshake — Qt timers must live in the thread that owns
the event loop) fires every **50 ms** and drains the FIFO one command per tick,
enforcing a global minimum 50 ms inter-command gap (`_last_send_time` check).
This pacing is deliberate: the firmware processes commands serially, and
bursts (e.g. `refresh_all_axes_status`) would otherwise collide with the
10 ms periodic broadcast.

Connection parameters: `serial.Serial(port, 115200, timeout=0.1,
write_timeout=0.5)`; up to **3 connection retries** with a 2 s wait between
them. A failed send marks the port closed and triggers a retry cycle.

### 4.2 Receiving (RX parsing)

`_read_data_loop` reads whatever is waiting (`in_waiting`) into a byte buffer
and runs `_parse_incoming`, which disambiguates three interleaved streams
**byte-wise, in priority order**:

| priority | packet | match | validate |
|---|---|---|---|
| 1 | 40-byte extended position broadcast | `buf[0] == 0xFD` | CRC over first 39 bytes |
| 2 | 24-byte command response / broadcast | — | CRC over first 23 bytes |
| 3 | ASCII line | first `\n` in buffer | utf-8 decode, strip |

Both binary lengths are CRC-validated before being accepted. If the buffer
grows beyond **4 × 24 = 96 bytes** without anything matching, the oldest byte
is dropped (anti-stall protection — a corrupted stream cannot block parsing
forever).

Matched packets are emitted via Qt signals (see below); remaining bytes stay
in the buffer for the next round.

### 4.3 Signals

| signal | payload | consumer |
|---|---|---|
| `binary_response` | `bytes` (24 or 40) | `handle_binary_response` → axis states (see §5.1) |
| `data_received` | `str` (ASCII line) | `handle_received_data` → version/HWINFO/DAC/axis status (see §5.2) |
| `error_occurred` | `str` | status label ("Connection Error" on keywords) |
| `debug_info` | `str` | Log tab as `[DEBUG]` lines |
| `start_timer_signal` | — | main thread creates the 50 ms send timer |

Thread safety: `QMutex`es guard send, read and the FIFO; the GUI serializes
application-level command bursts with its own `_query_mutex`
(`send_command` / `wait_until_idle` / refresh methods).

---

## 5. Runtime data flow: firmware → AxisManager → UI

### 5.1 Binary frames (`handle_binary_response`)

Called for every CRC-validated binary packet (~every 10 ms from the firmware's
periodic broadcast, plus on motion-complete falling edges). Two layouts:

**24-byte frame** (octoaxes mainline; also octoaxesplus command responses):

| offset | meaning |
|---|---|
| 0 | cmd_id (echo of the host's prefix, 0x02) |
| 1 | status: 0 = COMPLETED, 1 = IN_PROGRESS, 2 = CRC_ERROR |
| 2..5 | X position (int32 big-endian, microsteps) |
| 6..9 | Y position |
| 10..13 | Z position |
| 14..17 | W position |
| 18 | status bits (bit0 = joystick button) |
| 22 | firmware version (0x17 = v1.7) |
| 23 | CRC-8-CCITT of bytes 0..22 |

The host reads the **fixed slots** `X / Y / Z / W` directly.

**40-byte extended frame** (octoaxesplus only, cmd_id `0xFD`):

| offset | meaning |
|---|---|
| 0 | 0xFD |
| 1 | status byte |
| 2..33 | 8 × int32 positions (`struct.unpack('>8i', data[2:34])`) — **indexed by firmware icID** |
| … | version byte 38, CRC byte 39 |

The host maps icID → axis name via `AXIS_CONFIG[axis]["index"]`, which is why
the profile constants must match the firmware's icID assignment.

For every axis found, the host updates:

```
position_steps ← raw int32
position_mm    ← position_steps × AXIS_MM_PER_STEP[axis]
moving         ← "YES" if status == 1 else "NO"
state          ← "MOVING" if status == 1 else "IDLE"
```

…into `AxisManager` and the axis-status display, then re-renders the current
axis's position labels (µm for step motors, slot/angle for the objective
turret — §7.6). The position source is decided by the firmware (XACTUAL vs
ENC_POS when the encoder is enabled) — the host just displays what arrives.

### 5.2 ASCII lines (`handle_received_data`)

1. `S:VERSION:` → version label.
2. `S:HWINFO:<axis>:TMC4361A+<driver>` → stores `driver` in `AxisManager`.
3. `S:DAC_*` (bring-up responses) → printed raw to the Log.
4. Anything else is offered to `AxisManager.parse_axis_data`, which recognizes
   per-axis prefixes (`<AXIS>:`) and these content formats:

   | format | updates |
   |---|---|
   | `STATE:` | `state` |
   | `Current Position (mm):` | `position_mm` |
   | `Current Position (microsteps):` | `position_steps` |
   | `IS_MOVING:` | `moving` |
   | `IS_ENABLED:` | `enabled` |
   | `LIMIT_SWITCHES:` | `limits` |
   | `AXIS_STATUS:` (combined `Pos:… \| Moving:… \| Limits:…`) | all three |
   | `EMERGENCY:` | `state` = EMERGENCY |

5. Unrecognized lines go to the Log tab verbatim.

> **Production vs debug firmware:** with `ENABLE_DEBUG` off, the firmware does
> not answer ASCII queries, so `query_axis_status` just refreshes the UI from
> the `AxisManager` cache and the `GET_DATA` write is harmless. With
> `ENABLE_DEBUG` on, ASCII replies flow and enrich the state.

### 5.3 `AxisManager`

A plain dict store (`axis_status[axis] = {state, position_mm, position_steps,
moving, enabled, limits}`), initialized for exactly `AXIS_CONFIG.keys()`. It is
written from the binary/ASCII handlers and read by `wait_until_idle`, the
objective logic, and the W/Z tests. No locking is needed in practice: updates
arrive via queued Qt signals on the GUI thread.

---

## 6. The axis configuration model (`constants.py`)

Everything axis-specific is data. `AXIS_CONFIG` is a dict of
`axis_name → config-dict`; per-profile: octoaxes = X/Y/Z/W/E1/E3/E4,
octoaxesplus = X/Y/Z/W1/W2 (+Turret in both).

| field | meaning | example (octoaxes) |
|---|---|---|
| `display_name` | GUI label | `"X axis"` |
| `type` | `step_motor` \| `filter_wheel` \| `objective` | `"step_motor"` |
| `movement_sign` | ±1; physical direction multiplier | 1 / -1 |
| `index` | firmware icID; **40-byte slot index** (octoaxesplus) | plus: X=0, Y=1, Z=2, W1=3, W2=4, Turret=5; octoaxes: X=1, Y=0 — **historical values, unused** because the 24-byte frame reads fixed X/Y/Z/W slots |
| `has_limits` / `limits` | limit-switch presence & UI bounds | — |
| `has_encoder` | encoder present | — |
| `encoder_transitions_per_rev` | encoder lines for cmd 25 | 4000 (filter wheels) |
| `encoder_flip_direction` | encoder direction inversion | — |
| `pid_enabled` / `pid_p/i/d` | closed-loop control | Turret only |
| `actuator_screw_pitch_mm` | leadscrew pitch for cmd 23 | 1.0 |
| `actuator_microstepping` | µsteps/fullstep for cmd 21 | 256 |
| `actuator_motor_current_ma` | peak current for cmd 21 | 1000 |
| `actuator_motor_hold_ratio` | holding current (0..1) for cmd 21 | 0.25 |
| `switch_polarity` | limit-switch polarity (cmd 20) — **only the Z variant** has it | 1 |
| `homing_velocity_mm` | homing speed via `S:SET_HOMING_VEL` — **only the Z variant** | 2.0 |

Derived quantities:

```python
FULLSTEPS_PER_REV = 200
AXIS_MM_PER_STEP = { name: pitch / (200 × microstepping) ... }   # mm per µstep
FILTERWHEEL_DISTANCE = 0.125   # mm — 1 slot = 1/8 turn, legacy-Squid aligned
```

### The Z-axis variant mechanism

`Z_AXIS_VARIANT` (octoaxes: `"old"`) selects a hardware variant by **merging
overrides into the Z entry** via `**_Z_VARIANTS[Z_AXIS_VARIANT]`:

| variant | pitch (mm) | current (mA) | hold ratio |
|---|---|---|---|
| `"old"` | 0.3 | 500 | 0.5 |
| `"new"` | 1.0 | 1500 | 0.75 |

Because the variant dict also carries `switch_polarity` and
`homing_velocity_mm`, switching variants changes what the GUI sends at startup
(cmd 20 polarity, `S:SET_HOMING_VEL`) — **no firmware reflash needed**. The
absence of those keys on every other axis is exactly what the profile-safe
`config.get(...)` guards rely on (§7.2).

### Illumination metadata

`ILLUMINATION_PORTS` / `ILLUMINATION_DAC_CHANNELS` /
`ILLUMINATION_HAS_GAIN_SWITCH` / `ILLUMINATION_HAS_DAC_READBACK` drive the
Illumination tab's rendering (see §8). octoaxes: 5 TTL ports (D1–D5, pins
5/4/22/3/23), no DAC direct control; octoaxesplus: 8 ports (D1–D8), 8 DAC
channels, gain-switch + readback.

---

## 7. Core operations

### 7.1 Limits (`set_limits`)

UI values are in **µm**. Conversion: `µsteps = int(µm/1000 / mm_per_step) ×
movement_sign`; if the sign flips the ordering, low/high are swapped. Then two
`SET_LIM` (cmd 9) writes via `AXIS_LIMIT_CODE_MAP` — one for the positive code
(high bound), one for the negative code (low bound), int32 big-endian payload.

The firmware itself enforces limits by **clamping target positions**
(`clampTargetByDirection`); the host's role is transmitting the bounds. Limits
are re-sent whenever the user switches axes (`on_axis_changed`) and at startup.

### 7.2 Startup configuration (`_configure_actuators`, `_configure_encoders`)

Per axis (protocol axis value via `_AXIS_PROTOCOL`, which maps W1→W's code 5 —
the firmware falls back W→W1 — W2→6, Turret→7):

- **cmd 23** `SET_LEAD_SCREW_PITCH`: `data[2]=axis`, `data[3..4]=pitch×1000`
  (uint16 BE).
- **cmd 21** `CONFIGURE_STEPPER_DRIVER`: `data[2]=axis`,
  `data[3]=µstep encoding` (**1→0, ≥256→255**, else as-is — legacy-Squid
  convention), `data[4..5]=current mA` (uint16 BE), `data[6]=hold×255`.
- **cmd 20** `SET_LIM_SWITCH_POLARITY` — sent only when the axis config carries
  `switch_polarity` (currently = the Z variant). This is the "Z-variant
  software switch" landing point.
- **`S:SET_HOMING_VEL <axis> <vel>`** — sent only when the config carries
  `homing_velocity_mm` (also the Z variant). The firmware boot default is
  1 mm/s; the new-Z's long homing travel (~34.5 mm) needs 2 mm/s.

Encoder/PID configuration, only for `has_encoder` axes, in a **fixed order**:

1. (if `pid_enabled`) **cmd 29** `SET_PID_ARGUMENTS` — P uint16, I byte, D byte.
2. **cmd 25** `CONFIGURE_STAGE_PID` — `data[3]=flip`, `data[4..5]=tpr` uint16.
3. (if `pid_enabled`) **cmd 26** `ENABLE_STAGE_PID`.

Order matters: firmware `configureStagePID` writes the chip's PID registers
from the current `_pidState`, so `SET_PID_ARGUMENTS` must precede
`CONFIGURE_STAGE_PID`. Axes without `pid_enabled` get encoder *readout* only
(filter wheels on octoaxesplus); the Turret gets full closed-loop control.

### 7.3 Homing (`send_homing`)

- Protocol axis via `_AXIS_PROTOCOL` (X=0, Y=1, Z=2, W=5, W1→5, W2=6, Turret=7).
- `HOME_OR_ZERO` (cmd 5): `data[2]=axis`, `data[3]=home direction` —
  **0 = HOME_POSITIVE (toward +), 1 = HOME_NEGATIVE (toward −)** — derived from
  `movement_sign` (+1 → 1, −1 → 0), legacy-Squid-compatible.
- The objective Turret cancels any pending rest-disable and is **enabled first**
  (cmd 32) because it runs closed-loop.
- `wait_until_idle(15)` — polls `AxisManager` state == `"IDLE"` (15 s timeout).
- Post-homing behavior depends on axis **type**:
  - `filter_wheel` → **relative** move of `+SQUID_FILTERWHEEL_OFFSET × 1000` µm
    (0.008 mm — from the index flag to the center of slot 1,
    legacy-Squid-aligned).
  - `objective` → pure homing, no auto slot move ("Go to Slot 0" is a separate
    button); then schedule rest-disable (§7.6).
  - `step_motor` → `set_limits()` (the firmware disables virtual limits during
    homing, so they are re-asserted afterwards).

`wait_until_idle(timeout=10)` is the generic completion primitive: it polls the
state that the 10 ms binary broadcast keeps fresh. Motion is considered
complete when the firmware reports COMPLETED (falling-edge frame), which the
host renders as `IDLE`.

### 7.4 Relative / absolute moves

- `move_axis(forward/back)` — relative; distance from the UI in µm, sign
  applied, then `movement_sign` applied, then µm→µsteps
  (`int(distance/1000 / mm_per_step)`), cmd via `AXIS_MOVE_CMD_MAP`
  (MOVE_X=0, MOVE_Y=1, MOVE_Z=2, MOVE_W=4, MOVE_W2=19, MOVE_TURRET=44).
- `moveto_axis(pos_µm)` — absolute; same conversion, cmd via
  `AXIS_MOVETO_CMD_MAP` (MOVETO_X=6 …).
- `_move_step_axis_relative_usteps(axis, µsteps)` — raw-microstep relative
  move (currently uncalled; kept as the precision path for objective work:
  1 slot = 0.6875 mm, and the µm round-trip `int(687.5) = 687` would silently
  drop ~7 µsteps per slot).
- `_set_max_velocity_acceleration` — cmd 22: `vel×100` uint16,
  `acc×10` uint16 (connected to the ControlPanel's velocity/acceleration
  fields).

### 7.5 Filter wheels

`move_filterwheel(next/prev)` sends a relative move of **±125 µm**
(`FILTERWHEEL_DISTANCE = 0.125 mm`, 1 slot = 1/8 turn) using the generic
relative-move path — so on octoaxesplus the firmware falls back W→W1 and uses
dedicated W2 (code 6 / MOVE_W2 = 19). Homing then applies the legacy
`SQUID_FILTERWHEEL_OFFSET` (8 µm) offset.

### 7.6 Objective turret (closed-loop turret)

The Turret is an encoder + PID closed-loop axis (`type: "objective"`):

- **Slot moves are decomposed into single-slot steps.** A multi-slot `goto`
  becomes repeated `_objective_move_to_slot_single` calls — closed-loop moves
  of more than one slot stall and squeal on this hardware (measured
  2026-06-24: >1-slot closed-loop moves trigger in-place stall/squeal, 1-slot
  moves always work). Each single-slot step reads the slot's **calibrated
  angle** (from the four objective-angle line edits; fallback = slot × 90°),
  converts to an absolute motor position via `_objective_angle_to_position_um`
  (gear ratio `OBJECTIVE_RATIO = 132/48 = 2.75`; µsteps per turret rev =
  `OBJECTIVE_RATIO × 200 × microstepping`), cancels any pending rest-disable,
  re-enables the axis, sends an absolute **MOVETO**, then
  `wait_until_idle(65)` — 65 s is only a fallback ceiling above the firmware's
  60 s move-timeout for this axis; idle returns early. Every step
  re-schedules the rest-disable timer, so only the final step's timer fires.
- **Disable-at-rest:** after arriving, a non-blocking `QTimer` waits
  `rest_disable_delay_ms` (default 100 ms) then sends cmd 32
  (`SET_AXIS_DISABLE_ENABLE`, disable). This keeps the motor unpowered while
  the turret springs settle into the detent, and avoids heat buildup. Moves
  re-enable first.
- **Calibration persistence:** `_save_objective_calib` /
  `_load_objective_calib` store the angle↔slot calibration in
  `~/.octoaxes/objective_calib.json`.
- **Display:** `_render_objective_position` replaces the mm/µstep readouts with
  turret slot + objective angle (`objective_slot_angle` in `define.py`).

### 7.7 W encoder step-loss test (`run_w_test`)

Runs on a background `threading.Thread` (daemon). W is encoder-enabled, so the
broadcast reports ENC_POS — the test compares **encoder displacement vs
expected** per move to detect lost steps / mechanical slip:

- `_read_pos_settled`: polls the position until it stops changing (±3 µsteps,
  4 consecutive stable samples) before reading — `wait_until_idle` alone can
  return while the encoder is still settling, which would read a mid-move value
  and falsely flag step loss.
- **Stage 1:** homing → (Next ×7 → Prev ×7) × N rounds, one slot (±125 µm)
  at a time.
- **Stage 2:** large-gap jumps `+gap / -gap` for gap = 2..7 — high-speed
  accel/decel stress that exposes slip better than slow single slots.
- Per move: `|actual Δ| − |expected|` compared against
  `threshold = max(120, expected//8)`; exceeding it counts as a suspected lost
  step (logged with a marker).
- Final report: **net offset** (all moves net to zero, so ≈0 = no accumulation),
  **max deviation**, and **suspected-loss count**.

---

## 8. Illumination control

The Illumination tab (`IlluminationPanel`) is generated from the profile's
`ILLUMINATION_*` metadata (see §6): octoaxes renders 5 TTL ports and no DAC
controls; octoaxesplus renders 8 ports plus the DAC direct-control section.

Wire traffic behind each control:

| control | command | notes |
|---|---|---|
| Port on/off + intensity | **cmd 37** `SET_PORT_ILLUMINATION` (`port, intensity uint16 raw, on/off`) | atomic intensity + switch |
| Global intensity factor | **cmd 17** `SET_ILLUMINATION_INTENSITY_FACTOR` | scales DAC output |
| **Set Matrix** (LED matrix) | **cmd 13** `SET_ILLUMINATION_LED_MATRIX` (caches pattern+RGB) **then cmd 10** `TURN_ON_ILLUMINATION` | two-step since firmware 2026-05-10 made cmd 13 cache-only |
| Brightness slider while on | **cmd 13** only | firmware re-applies when already on; avoids re-lighting when off |
| **Clear** | **cmd 11** `TURN_OFF_ILLUMINATION` | true off |
| Turn Off All | **cmd 39** `TURN_OFF_ALL_PORTS` | also the watchdog's action |
| DAC direct set / gain / readback (squid++) | ASCII `S:DAC_SET`, `S:DAC_GAIN`, `S:DAC_READ_ALL`, `S:DAC_READ` | bypasses the intensity factor — raw register control for bring-up; replies logged raw |

Note: the firmware gates illumination on an **interlock** input (octoaxes:
pin 2 — the default `teensy41` build blocks D1–D5 until the interlock build is
flashed; octoaxesplus: pin 38). See `FIRMWARE_API.md` and CLAUDE.md for the
build-flag matrix.

---

## 9. Integration Test panel & Z aging test

`IntegrationTestPanel` (the 4th tab) lists per-test rows with PASS/FAIL status:

| id | test | runner |
|---|---|---|
| `firmware_version` | `S:VERSION` round-trip | `_test_firmware_version` |
| `hardware_info` | `S:HWINFO` round-trip | `_test_hardware_info` |
| `z_aging` | Z-axis aging test (**excluded from Run All**) | `_test_z_aging` |

`run_all_tests` runs the batch sequentially; responses are captured through
`on_response` (fed from `handle_received_data`). The aging test is deliberately
not part of the batch because it is long-running.

### ZAgingWorker

A `QThread` implementing an endless-until-stopped home-and-cycle soak test,
one round being:

```
HOME → dwell (0.5 s) → forward jogs ×26 → backward jogs ×25
```

(defaults: `rounds`, `step_um=1000.0`, `fwd=26`, `bwd=25`, `dwell=0.5`,
`home_timeout=70`). It is non-blocking and mirror the standalone
`z_aging_test.py` methodology:

- commands go through `serial_thread.send_binary_command` (thread-safe);
- **position/state come from the GUI's live 24-byte broadcast frames**
  (`axis_manager` Z `position_steps`) — no separate polling channel;
- per-step completion: expected single-step duration
  `step_mm/velocity + velocity/acceleration`, stability deadline
  `3 × expected + 0.5 s` — within which the position must stop changing;
- each jog's actual displacement is compared to nominal with tolerance
  `max(2000, nominal × 0.3)` µsteps; shortfall = lost steps → round failure.

`stop()` sets a flag checked between rounds; `closeEvent` in the main window
stops the worker (2 s wait) before closing the serial port.

---

## 10. Logging & debug

- The **Log tab** shows every sent ASCII command, every binary send
  (as a length log line), ASCII responses, firmware/error messages, and
  `[DEBUG]` lines from `SerialThread.debug_info`. Optionally saved to a file
  (checkbox) with millisecond timestamps.
- A **debug command line** lets the user send arbitrary ASCII commands
  (`S:…`) directly.
- Connection errors are filtered by keywords (`failed`, `closed`, `not open`,
  `disconnected`) and flip the status label to "Connection Error".
- `S:DAC_*` responses are printed verbatim for bring-up readability.

---

## 11. Key timings (quick reference)

| item | value |
|---|---|
| serial baud / read timeout / write timeout | 115200 / 0.1 s / 0.5 s |
| command FIFO capacity | 100 |
| global command send interval | 50 ms (1 command per tick) |
| firmware position broadcast period | 10 ms |
| status-timer poll | 2000 ms (only with Auto Poll) |
| startup query delays | +500 / +800 / +1000 ms (VERSION / HWINFO / GET_DATA) |
| `wait_until_idle` default timeout | 10 s (homing 15, filter-wheel homing 15, objective slot 65) |
| objective rest-disable delay | `rest_disable_delay_ms`, default 100 ms |
| Z aging: step / fwd / bwd / dwell | 1000 µm / 26 / 25 / 0.5 s |
| W test: settle tolerance | ±3 µsteps × 4 samples |

---

## 12. Extending the software

**Adding or modifying an axis** must touch six layers, two of them here:

1. `firmware/<project>/config.h` — HC154 channel enum + `Pins::*_AXIS_CS` (plus)
2. `firmware/<project>/tmc/hal/TMC_SPI.cpp` — `tmc_ic_configs[]`
3. `firmware/<project>/config.h` — `AxisConfigs::*_AXIS`
4. `firmware/<project>/<project>.ino` — `new Axis(...)` + `addAxis(...)`
5. `firmware/<project>/axesmrg.cpp::beginAll` — axisName → config branch
6. **`software/<profile>/constants.py` — `AXIS_CONFIG` entry** (plus
   illumination metadata when relevant)

After any `common/` change, re-verify **both** profiles import cleanly and
produce their expected axis sets (commands in CLAUDE.md). When adding commands,
keep `define.py::CMD_SET` in lockstep with the firmware `Commands` namespace,
and register new axes in the shared maps (`AXIS_MOVE_CMD_MAP` …) so neither
profile breaks.

The rule of thumb for shared code: **if it mentions an axis name or a count,
it is wrong** — derive from `AXIS_CONFIG`; keep profile-specific behavior in
the profile's `main.py` / `constants.py`; keep protocol knowledge in
`define.py`.
