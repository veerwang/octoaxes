# Octoaxes

A multi-axis microscope motion control system developed on the SQUID microscope platform. Supports 7-axis precision motion control, suitable for automated microscopy imaging applications.

## Features

- **7-axis motion control**: X, Y, Z stages, filter wheels (W, E4), objective changer (E1), extended Z axis (E3)
- **Precision positioning**: Supports 256x microstepping for sub-micron positioning accuracy
- **Automatic homing**: Each axis supports independent homing, used together with limit switches
- **Real-time status monitoring**: Displays the position and status of each axis in real time through a PyQt5 graphical interface
- **Hardware triggering**: Supports external trigger signals and illumination system control

## System Architecture

```
┌─────────────────┐     USB/Serial     ┌─────────────────┐
│   PC software   │ ◄───────────────► │   Teensy 4.1    │
│    (PyQt5)      │     115200 baud    │    firmware     │
└─────────────────┘                    └────────┬────────┘
                                                │ SPI
                                       ┌────────┴────────┐
                                       │   TMC4361A x7   │
                                       │  motion ctrl    │
                                       └────────┬────────┘
                                                │
                                       ┌────────┴────────┐
                                       │   TMC2660 x7    │
                                       │ stepper drivers │
                                       └─────────────────┘
```

## Directory Structure

```
octoaxes/
├── firmware/                 # Embedded firmware
│   └── octoaxes/
│       ├── octoaxes.ino     # Arduino main program
│       ├── platformio.ini   # PlatformIO configuration file
│       ├── config.h         # System and axis configuration
│       ├── axis.h/.cpp      # Axis base class
│       ├── stepaxis.h/.cpp  # Stepper motor axis implementation
│       ├── filterwheel.h/.cpp   # Filter wheel control
│       ├── objectives.h/.cpp    # Objective changer
│       ├── axesmrg.h/.cpp   # Axis manager
│       ├── commandprocessor.h/.cpp  # Command processing
│       ├── serial.h/.cpp    # Serial communication
│       └── TMC4361A*.h/.cpp # TMC4361A driver library
│
├── software/                 # PC control software
│   ├── main.py              # Program entry point
│   ├── define.py            # Command definitions
│   ├── gui/                 # Graphical interface
│   │   ├── main_window.py   # Main window
│   │   └── widgets.py       # UI components
│   ├── hardware/            # Hardware communication
│   │   ├── serial_thread.py # Serial thread
│   │   └── axis_manager.py  # Axis state management
│   └── utils/               # Utility modules
│       ├── constants.py     # Constant definitions
│       └── helpers.py       # Helper functions
│
└── documents/               # Documentation
```

## Hardware Requirements

- **Controller**: Teensy 4.1 development board
- **Motor drivers**: TMC4361A + TMC2660 combination (one set per axis)
- **Stepper motors**: Stepper motors matched to each axis
- **Limit switches**: A limit switch on each axis for homing

## Development Environment Setup

### 1. Install PlatformIO

PlatformIO is a cross-platform embedded development tool that supports several installation methods:

#### Option 1: VS Code extension (recommended)

1. Install [Visual Studio Code](https://code.visualstudio.com/)
2. Open VS Code and go to the Extensions Marketplace (Ctrl+Shift+X)
3. Search for "PlatformIO IDE" and install it
4. Restart VS Code and wait for the PlatformIO core components to install automatically

#### Option 2: Command-line installation (PlatformIO Core)

```bash
# Install with pip
pip install platformio

# Or use the installer script (Linux/macOS)
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py

# Verify installation
pio --version
```

#### Option 3: Use a package manager

```bash
# macOS (Homebrew)
brew install platformio

# Arch Linux
yay -S platformio

# Other Linux distributions
pip install platformio
```

### 2. Install Teensy Support

The first time you build a Teensy project, PlatformIO automatically downloads the required toolchain and libraries. To install manually:

```bash
# Install Teensy platform support
pio platform install teensy

# Install the Teensy upload tool (Linux requires udev rules)
# Download and install Teensy Loader: https://www.pjrc.com/teensy/loader.html
```

**Note for Linux users**: You need to add udev rules to allow non-root users to access the Teensy:

```bash
# Download the udev rules
curl -fsSL https://www.pjrc.com/teensy/00-teensy.rules | sudo tee /etc/udev/rules.d/00-teensy.rules

# Reload the rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### 3. Set Up the Python Environment

```bash
# Create a virtual environment (recommended)
python -m venv venv

# Activate the virtual environment
# Linux/macOS:
source venv/bin/activate
# Windows:
venv\Scripts\activate

# Install dependencies
pip install PyQt5 pyserial crc
```

## Firmware Build and Upload

### Build Environments

The project provides several build environments for different scenarios:

| Environment | Command | Purpose | CPU Frequency | Characteristics |
|------|------|------|---------|------|
| `teensy41` | `pio run` | Production | 600 MHz | Optimized for size, debug info removed |
| `teensy41_debug` | `pio run -e teensy41_debug` | Debugging | 600 MHz | Full debug symbols, logging |
| `teensy41_dev` | `pio run -e teensy41_dev` | Development | 600 MHz | Strict warning checks |
| `teensy41_fast` | `pio run -e teensy41_fast` | High performance | 720 MHz | Overclocked, maximum optimization |

### Building the Firmware

```bash
# Enter the firmware directory
cd firmware/octoaxes

# Build the production version (default)
pio run

# Build a specific environment
pio run -e teensy41_debug    # Debug version
pio run -e teensy41_dev      # Development version (strict warnings)
pio run -e teensy41_fast     # High-performance version

# Clean build artifacts
pio run --target clean
```

### Uploading the Firmware

```bash
# Upload to the Teensy (using the default environment)
pio run --target upload

# Upload the firmware for a specific environment
pio run -e teensy41_debug --target upload

# Build and upload (in one step)
pio run -t upload
```

### Serial Monitor

```bash
# Open the serial monitor (baud rate 2000000)
pio device monitor

# Specify the serial port
pio device monitor --port /dev/ttyACM0    # Linux
pio device monitor --port COM3            # Windows

# Show timestamps
pio device monitor --filter time

# Also log to a file (enabled by default in the debug environment)
pio device monitor --filter log2file
```

### Build Options

The following build options can be enabled in `platformio.ini`:

```ini
build_flags =
    -D DISABLE_LASER_INTERLOCK  ; Disable the laser safety interlock (laser-free systems only)
    -D SPI_DEBUG                ; Enable SPI debug output
    -D MOTION_PROFILE_DEBUG     ; Enable motion profile debugging
```

## Running the Control Software

### Launching the Software

```bash
cd software
python main.py
```

### Basic Workflow

1. Connect the Teensy 4.1 to the PC (via USB)
2. Launch the software and select the correct serial port from the drop-down list
3. Click **Connect** to establish the connection
4. Click **Engine Start** to initialize the motor drivers
5. Perform a **Homing** operation on the axes that need it
6. Use the control panel to move positions

### FAQ

**Q: Serial port not found?**
- Linux: Check whether the user is in the `dialout` group: `sudo usermod -aG dialout $USER`
- Windows: Install the Teensy driver
- Check that the USB cable supports data transfer

**Q: Upload fails?**
- Press the reset button on the Teensy and try again
- Check that the Teensy Loader is installed
- Linux users: verify that the udev rules are correctly configured

**Q: Build errors?**
- Run `pio pkg update` to update dependencies
- Check your PlatformIO version: `pio upgrade`

## Axis Configuration

| Axis | Name | Purpose | Max Speed | Lead Screw Pitch | Microsteps |
|---|------|------|---------|------|------|
| X | X axis | Horizontal motion | 25 mm/s | 2.54 mm | 256 |
| Y | Y axis | Horizontal motion | 25 mm/s | 2.54 mm | 256 |
| Z | Z axis | Vertical motion | 3 mm/s | 0.3 mm | 256 |
| W | Filter wheel 1 | Filter switching | - | 100 mm | 64 |
| E1 | Objective changer | Objective selection | - | 1 mm | 64 |
| E3 | Extended Z axis | Auxiliary Z axis | 3 mm/s | 0.3 mm | 256 |
| E4 | Filter wheel 2 | Filter switching | - | 100 mm | 64 |

## Main Commands

| Command | Description |
|------|------|
| `MOVE_X/Y/Z` | Relative position move |
| `MOVETO_X/Y/Z` | Absolute position move |
| `HOME_X/Y/Z` | Homing operation |
| `STOP` | Stop all motion |
| `GET_DATA` | Get axis status data |
| `VERSION` | Get firmware version |

## License

MIT License

## Author

kevin.wang
