# CareLoop Firmware (v1_develop)

Zephyr-based firmware for the CareLoop wearable. This branch is the
fresh start for v1 development and is under active construction.


## Sensors

- PPG: AFE with Osram7074 optical front-end. (**NOT IN USE**)
- Skin temperature: TMP117.
- Accelerometer/Gyroscope: BMI270.


## Hardware bring-up tests

Every assembled board is verified by one firmware image containing the whole
bring-up suite, driven by Zephyr Twister over SWD. `scripts/hil` wraps it.

The board exposes no UART pads, so the console is SEGGER RTT riding the SWD
lines. Twister reaches it through `scripts/rtt_console.py`, wired up as a
`serial_pty` in `hardware-map.yml`.

### Once per board, before anything else

```sh
./scripts/hil.sh unlock
```

Writes `UICR.APPROTECT = 0x5A`. Late nRF52840 revisions read an *erased*
APPROTECT as protected, so J-Link mass-erases on every connect: the flash
verifies, then silently disappears, and the board looks bricked. The write
persists, so this is never repeated for that board.

### Running the suite

```sh
./scripts/hil.sh smoke
```

Builds, flashes, runs all tests on hardware and writes reports. A good run
ends with:

```text
1/1 careloop_v2/nrf52840  bringup.careloop  PASSED
14 of 14 executed test cases passed (100.00%)
```

Covered: MCU identity and reset state, I2C bus and device presence, TMP117,
BMI270 (including gravity magnitude, which a chip-ID check cannot prove), and
the nPM1300 rails and battery voltage.

### Reading the result

The summary says what failed; the log says why. Everything the board printed -
every `printk` in the tests plus the ztest PASS/FAIL lines - is captured:

```sh
./scripts/hil.sh log            # what the board printed   (default)
./scripts/hil.sh log build      # compiler output
./scripts/hil.sh log device     # flashing and runner output
./scripts/hil.sh log twister    # Twister's own log
```

The tests print their measurements rather than only asserting on them, so a
passing run is still worth reading:

```text
  part 0x52840  package 0x2004  ram 256KB
  tmp117   0x48  64/64 ACK
  accel 402 -203 9771 mm/s2
  battery 3.745 V
  pmic die 32.452 C
```

JUnit and JSON reports for CI land in `build/twister-out/twister.{xml,json}`.

### Diagnostics

The suite identifies a broken domain; these answer why. Each builds, flashes
and streams RTT live until Ctrl-C - use them while physically probing the
board.

```sh
./scripts/hil.sh run boot          # CPU alive at all - heartbeat only
./scripts/hil.sh run i2cscan       # which addresses ACK on the bus
./scripts/hil.sh run tmp117stress  # hammer one device, catch intermittents

./scripts/hil.sh console           # watch an already-flashed board
./scripts/hil.sh probes            # list attached J-Links
```

### Adding a test

Add a `ZTEST(careloop_bringup, test_foo)` to a file in
`test/bringup/careloop/src/`, listing new files in that directory's
`CMakeLists.txt`. There is no Twister config to touch. Two rules earned the
hard way:

- Sensors have warm-up time. The suite completes in about 90 ms while the
  TMP117's first conversion takes ~1 s, so poll with a timeout instead of
  fetching once.
- Probe presence repeatedly, not once. The existing checks probe 64 times
  because a single probe passes a board with an intermittent sensor.

### Troubleshooting

Only one process may hold the J-Link. Most odd failures are a stale one, so
try `pkill -9 -f -i jlink` first.

| Symptom | Fix |
| --- | --- |
| `Timeout during flashing` | Another process holds the probe |
| Flash verifies, board still dead | `./scripts/hil unlock` was never run |
| `unrecognized platform` | `careloop_v2.yaml` must say `identifier: careloop_v2/nrf52840` |
| No RTT output | Stale probe holder; retry after `pkill` |
| `Build failure` | A real error - Twister builds with `-Werror`; see `build.log` |

The toolchain is discovered at `/opt/nordic/ncs` (NCS v3.4.0). Override with
the `NCS`, `NCS_VERSION`, `TOOLCHAIN`, `BOARD` or `PROBE` environment
variables.


## Notes

- All generated output belongs under `build/`, which is git-ignored.

