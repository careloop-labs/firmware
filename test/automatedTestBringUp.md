//TODO: idea for the end stadium, of how tests should process and run


LEVEL 0 — Electrical safety
    ↓
No shorts
Rail resistance sane
Current-limited supply
    ↓

LEVEL 1 — MCU alive
    ↓
J-Link connects
SWD works
nRF52840 identified
Flash/read/reset works
    ↓

LEVEL 2 — Board infrastructure
    ↓
Clocks
GPIO
I2C
SPI
PMIC
external flash
    ↓

LEVEL 3 — Sensors
    ↓
IMU
temperature
PPG AFE
EDA
environment
    ↓

LEVEL 4 — Functional
    ↓
PPG samples
IMU motion response
temperature response
flash logging
    ↓

LEVEL 5 — System
    ↓
all sensors simultaneously
timestamps
logging
BLE
sleep/wakeup
    ↓

LEVEL 6 — Characterization
    ↓
power
24h stability
packet loss
sampling jitter
sensor quality