# CareLoop Firmware (v1_develop)

Zephyr-based firmware for the CareLoop wearable. This branch is the
fresh start for v1 development and is under active construction.


## Sensors

- PPG: AFE with Osram7074 optical front-end. (**NOT IN USE**)
- Skin temperature: TMP117.
- Accelerometer/Gyroscope: BMI270.


## Hardware bring-up tests

Every assembled board is verified by one firmware image containing the whole
bring-up suite, driven by Zephyr Twister over SWD. `scripts/bringup.sh` wraps it.

The board exposes no UART pads, so the console is SEGGER RTT riding the SWD
lines. Twister reaches it through `scripts/rtt_console.py`, wired up as a
`serial_pty` in `hardware-map.yml`.

### Once per board, before anything else

```sh
printf 'connect\nr\nh\nw4 0x10001208, 0xFFFFFF5A\nr\nq\n' |
  JLinkExe -nogui 1 -device nRF52840_xxAA -if SWD -speed 4000
```

Writes `UICR.APPROTECT = 0x5A`. Late nRF52840 revisions read an *erased*
APPROTECT as protected, so J-Link mass-erases on every connect: the flash
verifies, then silently disappears, and the board looks bricked. The write
persists, so this is never repeated for that board.

### Running the suite

```sh
./scripts/bringup.sh suite
```

Builds, flashes, runs all tests on hardware and writes reports. A good run
ends with:

```text
5 of 5 executed test configurations passed (100.00%)
71 of 71 executed test cases passed (100.00%)
3 selected test cases not executed: 3 skipped.
```

The three skips are deliberate, not a problem: `hal_motion_f0_any_motion_is_manual`,
`hal_fifo_90_events_are_manual` and `hal_leds_60_visual_check_is_manual` mark
coverage that no assertion can reach, so they appear in the report rather than
only in a comment. **A run reporting fewer skips than that means those markers
were lost.**

Covered in two layers. The `careloop_<n>_*` suites drive the Zephyr drivers
directly and localise a fault to the bus, the part or its configuration: MCU
identity and reset state, clocks, I2C bus and device presence, TMP117, BMI270
(including gravity magnitude, which a chip-ID check cannot prove), the nPM1300
rails and battery voltage, and the LED driver path.

The `careloop_hal_*` suites drive `src/hal` - the code the product actually
ships - and verify the contracts its headers state: error codes, init ordering,
"untouched on failure" guarantees and units. A failure in one layer but not the
other is the most useful signal this suite produces. `careloop_hal_0_uninit`
runs first by design, because every HAL latches an `initialized` flag with no
way back, so the `NOT_INITIALIZED` paths are reachable exactly once per boot.

Two standalone streamers exist for what assertions cannot judge:
`./scripts/bringup.sh skin_temperature` prints a temperature per conversion, and
`./scripts/bringup.sh motion` streams accel/gyro with the dominant gravity axis
named - the only way to catch swapped axes or a wrong gyro scale factor, since
every gyro check in the suite is taken at rest and so sees bias only.

The LED tests are the one place in this suite where a pass is not proof. An
LED has no readback, so they verify only that the register writes reach the
PMIC - they cannot see light. On the first board they ran against, all of them
passed while none of the three LEDs lit: `+3V3` reaches the anodes only through
jumper **J3 (`JP_3V3`)**, and J3 was open. BUCK2 senses its own output upstream
of that jumper, so the PMIC reported the rail healthy throughout. Bridging J3
fixed it. `test_leds_host_control` walks D4, D3 then D2 with a visible dwell so
an operator can check - if the LEDs are dark, look at J3 first.

### Reading the result

The summary says what failed; the log says why. Everything the board printed -
every `printk` in the tests plus the ztest PASS/FAIL lines - is captured:

Twister buries these under a path containing the absolute source path, so
find them rather than spelling it out. `handler.log` is what the board
printed; `build.log`, `device.log` and `twister.log` are the other three.

```sh
find build/twister-out -name handler.log -exec cat {} +
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
./scripts/bringup.sh boot      # CPU alive at all - heartbeat only
./scripts/bringup.sh LEDs      # cycle D4/D3/D2, ending 12 s solid - watch the board
./scripts/bringup.sh ble       # advertise the product BLE stack for a phone
./scripts/bringup.sh clocks    # HFXO startup time and LFXO/HFXO ratio
```

Run it with no argument to list what actually exists - the names come from the
directories under `test/bringup`, not from a list kept in step with them.

Extra arguments go to `west build`:

```sh
./scripts/bringup.sh clocks -- -DCONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y
```

With two boards attached, set `PROBE` to the J-Link serial number, or the
runner picks one itself and may flash the other board.

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

### Debugging

Source-level debugging over SWD with Zephyr thread awareness: start
`./scripts/debug.sh` and attach on port 2331. Full walkthrough, for both the
command line and VS Code, in [docs/debugging.md](docs/debugging.md).

### Troubleshooting

Only one process may hold the J-Link. Most odd failures are a stale one, so try
`pkill -9 -f rtt_console.py; pkill -9 -f -i jlink` first. Both patterns are
needed - `rtt_console.py` holds the probe but its command line contains no
"jlink", so the second command alone never matches it.

| Symptom | Fix |
| --- | --- |
| `Timeout during flashing` | Another process holds the probe |
| Flash verifies, board still dead | APPROTECT never opened; recipe at top of `scripts/bringup.sh` |
| `unrecognized platform` | `careloop.yaml` must say `identifier: careloop/nrf52840` |
| No RTT output | Stale probe holder; retry after `pkill` |
| `Build failure` | A real error - Twister builds with `-Werror`; see `build.log` |
| Debugger shows only `Thread 57005` | Wrong ELF, or an image without `CONFIG_DEBUG_THREAD_INFO`; see [docs/debugging.md](docs/debugging.md) |

The toolchain is discovered at `/opt/nordic/ncs` (NCS v3.4.0). Override with
the `NCS`, `NCS_VERSION`, `TOOLCHAIN`, `BOARD` or `PROBE` environment
variables.


## Notes

- All generated output belongs under `build/`, which is git-ignored.

