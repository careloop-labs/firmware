# Debugging the CareLoop wearable

Source-level debugging over SWD, with Zephyr thread awareness.

This board is not the generic Zephyr experience in three ways, and each one
fails *quietly* — you get a plausible-looking wrong answer rather than an error.
They are covered below under [Gotchas](#gotchas). If something looks impossible,
read that section first.

---

## Start here

Two terminals. The first holds the probe:

```sh
./scripts/debug.sh
```

Wait for `Waiting for GDB connection...`. The second attaches:

```sh
/opt/nordic/ncs/toolchains/ccc010f809/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb \
    build/ble/zephyr/zephyr.elf
```

```gdb
(gdb) target remote localhost:2331
```

Two things about that GDB path: it is **not on `PATH`** — the NCS toolchain is
never exported into your shell — and it is `arm-zephyr-eabi-gdb`, not
`arm-none-eabi-gdb`. A system `arm-none-eabi-gdb` will attach and then
mis-decode Zephyr's DWARF.

Worth an alias:

```sh
alias zgdb=/opt/nordic/ncs/toolchains/ccc010f809/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb
```

Attaching **halts the core**. To let the board run again:

```gdb
(gdb) monitor go          # resume without a reset
(gdb) monitor reset       # reset, then `monitor go`
```

## From VS Code

`.vscode/launch.json` has three attach configurations. Start
`./scripts/debug.sh` in a terminal **first**, then press F5 and pick the one
matching the image on the board.

They use `servertype: "external"` deliberately: Cortex-Debug can spawn its own
J-Link server, but then the probe-release logic and the RTOS plugin path would
be duplicated in editor config — which is exactly what rotted in `debug.sh`,
where a hardcoded J-Link path pointed at a Linux directory and a version that
was not installed. One place owns that.

The configs also load `nrf52840.svd`, so `CLOCK`, `RADIO`, `TWIM` and friends
are readable as named registers in the sidebar rather than raw addresses.

---

## Match the ELF to what is flashed

**This is the single easiest mistake.** GDB reads symbols from the ELF you pass,
and the RTOS plugin needs those symbols to locate Zephyr's kernel structures. A
stale or wrong ELF does not produce an error — it produces a debug session that
looks broken:

```
  Id   Target Id                    Frame
* 1    Thread 57005 (Unknown thread)   0x00031b3c in postfix ()
#1  0x00039d92 in ?? ()
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```

`57005` is `0xDEAD` — the GDB server's placeholder when it has no thread
information. One thread and a corrupt backtrace means *wrong ELF*, not a
crashed board.

| Flashed via | ELF |
| --- | --- |
| `./scripts/bringup.sh ble` | `build/ble/zephyr/zephyr.elf` |
| `./scripts/bringup.sh <name>` | `build/<name>/zephyr/zephyr.elf` |
| product app (`./build.sh`) | `build/product-careloop/zephyr/zephyr.elf` |
| product app, debug build | `build/product-debug/zephyr/zephyr.elf` |
| Twister run | `build/twister-out/**/<scenario>/zephyr/zephyr.elf` |

---

## Thread awareness

A healthy `info threads` on this board looks like this:

```
  Id   Target Id                                           Frame
  3    Thread 536890248 (main NOT STARTED PRIO 0)          arch_swap (...)
  4    Thread 536890072 (idle UNKNOWN PRIO 15)             arch_cpu_idle (...)
  5    Thread 536889864 (MPSL Work PENDING PRIO 246)       arch_swap (...)
  6    Thread 536890424 (sysworkq PENDING PRIO 255)        arch_swap (...)
  7    Thread 536885720 (BT LW WQ PENDING PRIO 10)         arch_swap (...)
  8    Thread 536885928 (bt_tx_processor PENDING PRIO 255) arch_swap (...)
  9    Thread 536886136 (BT RX WQ PENDING PRIO 248)        arch_swap (...)
```

Switch with `thread 7`, then `bt` for that thread's stack.

If you see only `Thread 57005`, it is one of two things:

1. **Wrong ELF** — see the table above.
2. **An image built without `CONFIG_DEBUG_THREAD_INFO`** — which is the product
   app, by default.

### Debugging the product app

`prj.conf` deliberately sets neither `CONFIG_DEBUG_THREAD_INFO` nor
`CONFIG_DEBUG_OPTIMIZATIONS`: the shipping image stays `-Os` with no kernel
thread-info tables, because this is a battery wearable and a debugger is a bench
tool. Every app under `test/bringup/` sets both, which is why they "just work".

To debug the product app, build it with the overlay:

```sh
west build --no-sysbuild -b careloop/nrf52840 -d build/product-debug -p always . \
    -- -DEXTRA_CONF_FILE=debug.conf
```

`EXTRA_CONF_FILE` is the current mechanism; `OVERLAY_CONFIG` is the deprecated
spelling. See [`debug.conf`](../debug.conf) for what it turns on and why.

Without it you can still set breakpoints and read globals — you lose the thread
list, and locals read `<optimized out>`.

---

## Useful breakpoints

The BLE callbacks are where most questions get answered. All are file-static, so
break by function name:

```gdb
(gdb) break connected              # src/communication/ble/ble_connection.c
(gdb) break disconnected
(gdb) break security_changed       # `level` tells you if the link is encrypted
(gdb) break pairing_complete       # src/communication/ble/ble_security.c
(gdb) break pairing_failed
(gdb) break core_write             # a phone writing a Core characteristic
(gdb) continue
```

`security_changed` is the high-value one: its `level` argument is the difference
between an encrypted link and an unencrypted one that merely reported success.
Level 1 is *not* encrypted.

Connection state lives in a file-static in `ble_connection.c`:

```gdb
(gdb) print current_conn
(gdb) print *current_conn
```

Peripheral registers, when the SVD is not loaded (CLI):

```gdb
(gdb) x/1xw 0x40000418      # CLOCK->LFCLKSTAT
(gdb) x/1xw 0x4000040C      # CLOCK->HFCLKSTAT
```

---

## Logs and GDB at the same time

Only one host process may hold a J-Link, and `./scripts/debug.sh` kills
`rtt_console.py` on startup to guarantee it gets the probe. So the obvious
setup — RTT log in one window, stepping in another — does not work directly.

Three options:

1. **Logs only** — `./scripts/bringup.sh <name>`, no debugger.
2. **Debugger only** — `./scripts/debug.sh`, no log.
3. **Both, through the GDB server.** The J-Link GDB server exposes RTT on its
   own telnet port, so no second process touches the probe:

   ```sh
   nc localhost 19021
   ```

   This is the one to use while stepping. Note that no new output appears while
   the core is halted at a breakpoint — the CPU is not executing. Historically
   this channel has also stalled once the RTT up-buffer filled with nobody
   draining it; both the product image and the bring-up apps now use a buffer
   larger than the 1024-byte default, so drain it from the start of the run.

---

## Gotchas

**Attaching halts the core, and a halted core lies about the clocks.** Zephyr's
LFXO switch is software-driven in two stages, so a target halted early reads
`LFCLKSTAT` with `SRC=RC` — indistinguishable from a dead 32.768 kHz crystal.
This has caused a false diagnosis before. Let the board run ~1.5 s before
trusting any clock register, and never diagnose crystals from a halted target.

**Running `debug.sh` twice kills the first server.** It calls
`pkill -9 -f -i jlink` to release the probe, and that pattern matches
`JLinkGDBServerCL`. An active session in another window dies without warning.

**`pkill -f -i jlink` does not match `rtt_console.py`.** Its command line
contains no "jlink", so a console left over from an earlier session keeps the
probe and the next capture silently produces an empty log. Kill both:

```sh
pkill -9 -f rtt_console.py; pkill -9 -f -i jlink
```

**A board that flashes cleanly and stays dead is APPROTECT, not firmware.** Late
nRF52840 revisions read an erased `UICR.APPROTECT` as *protected*, so J-Link
mass-erases on every connect. The recipe is at the top of `scripts/bringup.sh`;
the write persists per board.

**`arch_cpu_atomic_idle` in the PC is the idle thread**, not a hang.

---

## Reference

| What | Where |
| --- | --- |
| GDB server | `./scripts/debug.sh`, port 2331 |
| GDB | `/opt/nordic/ncs/toolchains/ccc010f809/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb` |
| RTT via GDB server | `nc localhost 19021` |
| RTT standalone | `scripts/rtt_console.py --probe <sn>` |
| Debug overlay | [`debug.conf`](../debug.conf) |
| VS Code configs | `.vscode/launch.json` |
| Probe | J-Link EDU Mini V2, S/N `802003872` |

Override the toolchain location with `NCS`, `NCS_VERSION`, `TOOLCHAIN`, `BOARD`
or `PROBE`; override the GDB server binary with `JLINK_GDB`.
