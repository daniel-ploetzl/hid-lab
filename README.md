# hid-lab: USB HID Behaviour Simulation Lab (Raspberry Pi Pico 2 / Pico 2 W)

## Overview

This project implements a composite USB Human Interface Device (HID) behaviour simulator on Raspberry Pi Pico 2 and Pico 2 W. Wi-Fi is available on the Pico 2 W only.
It generates time-driven, non-deterministic keyboard and mouse activity for research and educational analysis of:

- USB HID behaviour modelling
- host-side classification and heuristic detection
- embedded scheduling under non-blocking timing constraints

---

## Engineering properties

- **Cooperative non-blocking scheduling:** one main loop with no RTOS, threads or sleeping, so USB and Wi-Fi are serviced on every pass
- **Single-writer mouse report:** only `mouse_flush()` transmits, so movement or scrolling cannot clear a held button
- **Checked HID sends:** a state machine advances only once its report is queued, so a busy endpoint cannot lose a release
- **Build-time flash layout checks:** the build fails if the linker region, board flash size or sector size disagrees with the reserved sector layout
- **Validated credential record:** Wi-Fi settings live in their own flash sector as a versioned record with a CRC-32 and an invalid record means no join
- **Image sizes:** 22 KB (Pico 2) and 301 KB (Pico 2 W) as `.bin`. The `.uf2` files are about twice that, 44 KB and 603 KB, because each 512-byte UF2 block carries 256 bytes of firmware

---

## Architecture

### Firmware (device-side)

- `src/main.c`
  Cooperative scheduler:
  - runs TinyUSB device task (`tud_task()`)
  - drives the active then idle re-activation cycle
  - dispatches behaviour subsystems

- `src/input_tasks.c`
  Behaviour engine implementing independent state machines for HID actions.

- `src/reactivation.c`
  Re-activation timer: the active then idle duty cycle that repeats for ever.

- `src/wifi_manager.c` (Pico 2 W only)
  Non-blocking station state machine: connect, monitor link, reconnect with backoff.

- `src/usb_descriptors.c`
  USB descriptor definitions for composite HID device:
  - keyboard interface
  - mouse interface
  - report ID mapping

### Shared configuration

- `inc/flash_config.h`
  Flash layout definitions and configuration read/validation logic.

### Host utility

- `src/write_config.c`
  Host-side tool that generates a UF2 payload to write the run time or Wi-Fi credentials into a reserved flash sector.

---

## Execution Model

The firmware is based on a **cooperative, non-preemptive scheduling loop**:

- `tud_task()` must be called continuously for USB stack progress
- `wifi_manager_task()` services the radio through `cyw43_arch_poll()` from the same loop, with no background thread or interrupt
- `reactivation_update()` advances the active then idle cycle and signals the start of each active window
- `input_tasks_run(active)` executes all HID behaviour logic
- timing is derived from a monotonic millisecond clock:
  `to_ms_since_boot(get_absolute_time())`

### Re-activation cycle

Activity repeats on a fixed cycle rather than running once. The device is active for the configured window, idle for the period, then active again with no end.

- the active window length is the stored `hours` value
- the idle period is read from the flash config in seconds, from 1 second to 1 week, and defaults to 1 week
- the first window opens at boot
- each window begins with a one-shot edge, reserved for a login sequence that is not yet implemented
- the timer keeps each phase well under the 49.7-day clock wrap, so the rollover does not disturb the cadence

### Constraint model

- No blocking delays (`sleep`, busy-wait loops) in behaviour logic
- Elapsed-time checks use wrap-safe modular subtraction on timestamps. Each re-activation phase is bounded well under the 49.7-day clock wrap, so the cycle keeps its cadence across a rollover
- USB stack responsiveness is preserved at all times

---

## Behaviour Model

Input generation is implemented as independent time-based state machines:

- mouse movement bursts
- mouse click events
- keyboard key presses
- typing bursts
- scroll events
- window switching sequences (ALT+TAB)

### Timing model

- intervals are bounded random values (uniform distribution unless otherwise stated)
- randomness is sourced from `pico_rand`, a PRNG seeded from several entropy sources on the RP2350, the hardware TRNG among them
- each subsystem operates independently (no global synchronisation)

### Report model

A HID report carries the full current state of the device, so overlapping subsystems must not each transmit their own report.

- the mouse uses a single-writer flush: `click_task` owns the button (level state) whilst `move_task` and `scroll_task` write transient deltas, and only `mouse_flush()` transmits, so a movement or scroll cannot clear a held button
- the keyboard subsystems transmit directly, one activity at a time in practice given the interval spacing
- every send checks its return value and advances its state machine only once the report is queued, so a busy endpoint never drops one half of a press or release pair

---

## USB HID Model

The device is implemented as a **composite HID interface with report IDs**:

- Report ID 1: keyboard
- Report ID 2: mouse

### Descriptor constraints

- Control endpoint size: 64 bytes (`CFG_TUD_ENDPOINT0_SIZE`)
- HID interrupt buffer size: 16 bytes (`CFG_TUD_HID_EP_BUFSIZE`)
- Polling interval: 5 ms (HID `bInterval`)

---

## Flash Layout and Safety

Pico 2 and Pico 2 W flash map (4 MB):

- Firmware region: `0x10000000 .. 0x103FDFFF` (`0x003FE000` bytes)
- Reserved credentials sector: `0x103FE000 .. 0x103FEFFF`
- Reserved config sector: `0x103FF000 .. 0x103FFFFF` (last 4 KB sector)

### Safety invariant

The firmware image must end before the reserved sectors. If it overlapped them, the firmware would read its own bytes as settings and fall back to `CONFIG_DEFAULT_HOURS` and no Wi-Fi. Writing settings would overwrite part of the firmware.

Enforced at build time:

- the `PICO_FLASH_SIZE_BYTES` CMake variable limits the linker's `FLASH` region to `0x003FE000` bytes, so an oversized image fails to link
- `static_assert`s in `src/main.c` fail the build unless the linker region ends exactly at `CREDS_FLASH_ADDR` and `flash_config.h` matches the board's flash size and sector size
- `_Static_assert`s in `inc/flash_config.h` fail both the firmware and host builds if `pico_config_t` or `wifi_creds_t` changes size
- `PICO_BOARD` defaults to `pico2_w` and CMake configuration fails for any non-RP2350 platform

The checks exist because the layout is defined in two places, `CMakeLists.txt` and `inc/flash_config.h`. Without them, either side could change and the build would still succeed.

Limits of this protection:

- the reduced size applies to the linker only. C code still sees the board's real size in `PICO_FLASH_SIZE_BYTES` and must: the SDK derives the flash device size from it and needs a power of two and its bounds check would otherwise reject the reserved sectors
- writing only to the reserved sectors is a convention, not a restriction. `write_config` is the only writer in this repository, but nothing stops another UF2 or a flash API call from changing them

---

## Wi-Fi

Built when the board has a CYW43 radio. On a Pico 2 the same API compiles to inline stubs.

- `wifi_manager_task()` runs from the main loop and never blocks, so a network failure cannot delay USB or HID behaviour
- a join that fails, or finds no DHCP lease within 30 s, retries after 2 s, doubling to 60 s. A wrong password retries at 60 s
- DHCP and DNS are provided by lwIP, which is configured for UDP only in `inc/lwipopts.h`
- the status LED is on the radio chip, so it needs the radio to start. Solid means online or no network configured, blinking means not online yet (including joined with no DHCP lease) and off means the run has finished

### Credentials

Credentials are not built into the firmware. Connect the board in BOOTSEL mode and store them in its credentials sector:

```bash
./write_config --wifi wpa2 "your-network"
```

The tool asks for the password or reads it from standard input, so it does not reach the shell history or process list. Use `wpa3` for WPA3-Personal. There is no automatic fallback between the two.

The sector survives firmware updates, so this is needed once per board. Without a valid record the firmware makes no connection attempt. To remove the credentials from a board:

```bash
./write_config --wifi-clear
```

The password is stored in plaintext in the board's flash, so anyone with physical access can read it. Use a dedicated network.

---

## Configuration System

Flash layout (12 bytes total):

```c
typedef struct {
    uint32_t magic;
    uint32_t hours;
    uint32_t period_s;
} pico_config_t;
```

### Validation rules:

- `magic == 0xCAFEF00E`
- `hours` in `[1, 24]`, the length of each active window in the re-activation cycle
- `period_s` in `[1, 604800]`, the idle gap in seconds, up to 1 week

Each field falls back to its default if the record is invalid.

### Fallback behaviour:

If validation fails (erased flash, corruption or invalid values):

  - firmware uses `CONFIG_DEFAULT_HOURS`
  - system behaviour remains deterministic

### Design note

  The magic value is a **struct validity marker**, not a security mechanism.
  No cryptographic integrity protection is (yet) implemented.

---

## Host Configuration Writer

### Build

```bash
gcc src/write_config.c -Iinc -o write_config
```

### Usage

```bash
./write_config <hours> [period]
./write_config --wifi <wpa2|wpa3> <ssid>
./write_config --wifi-clear
```

### Output

Generates a UF2 payload that writes one record into its reserved flash sector. The config write sets the run window and the re-activation period together, because they share a sector. The credentials live in a separate sector and are changed on their own. `period` accepts seconds or an integer with an `s`, `m`, `h`, `d` or `w` suffix, for example `7d`, and defaults to 1 week when left out.

### Credential record

`wifi_creds_t` in `inc/flash_config.h` holds a magic value, version, security mode, SSID, password and CRC-32. The firmware makes no connection attempt unless every field is valid. The CRC detects corruption only, it does not protect the password.

---

## Build System

### Requirements

- Raspberry Pi Pico SDK (>= 2.0.0)
- CMake >= 3.13
- ARM GCC toolchain

### Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Use one build directory per board. For a Pico 2 without Wi-Fi:

```bash
mkdir build-pico2 && cd build-pico2
cmake .. -DPICO_BOARD=pico2
make -j$(nproc)
```

`PICO_BOARD` defaults to `pico2_w`. An existing build directory keeps the board it was configured with, so a build directory created for another board fails until it is removed.

### Outputs

- .uf2 firmware image
- .elf executable
- .bin raw binary

---

## Verification

Device behaviour is observed host-side on Linux.

- `sudo evtest` on the keyboard and mouse event nodes shows the raw event stream. The invariant: every key or button down has a matching up before the next unrelated event and ALT is released cleanly after each ALT+TAB.
- `sudo usbhid-dump -m 2e8a:0003 -e stream` shows raw reports. During a click hold the mouse button field stays set whilst only the delta bytes vary.

---

## Known issues

| Severity | Location | Issue |
|---|---|---|
| Low | `src/usb_descriptors.c` | Product ID `0x0003` belongs to the RP2040 USB boot mode. |
| Risk | `src/wifi_manager.c` | Worst-case main loop time with Wi-Fi has not been measured against the 5 ms HID interval. |
| Risk | `inc/flash_config.h` | The Wi-Fi password is stored in plaintext in the credentials sector, so physical access to the board reveals it. |
| Low | `src/input_tasks.c` | After a long idle, each behaviour whose timer has already elapsed fires once on the first active pass, a brief catch-up at the start of a window. |

---

## Disclaimer

This project is intended for embedded systems research and educational analysis of USB HID behaviour and host-side detection mechanisms.
