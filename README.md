# hid-lab — USB HID Behaviour Simulation Lab (Raspberry Pi Pico 2 / Pico 2 W)

## Overview

This project implements a composite USB Human Interface Device (HID) behaviour simulator on Raspberry Pi Pico 2 and Pico 2 W. Wi-Fi is available on the Pico 2 W only.
It generates time-driven, non-deterministic keyboard and mouse activity for research and educational analysis of:

- USB HID behaviour modelling
- host-side classification and heuristic detection
- embedded scheduling under non-blocking timing constraints

---

## Architecture

### Firmware (device-side)

- `src/main.c`
  Cooperative scheduler:
  - runs TinyUSB device task (`tud_task()`)
  - enforces runtime-duration limits
  - dispatches behaviour subsystems

- `src/input_tasks.c`
  Behaviour engine implementing independent state machines for HID actions.

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
  Host-side tool that generates a UF2 payload to write configuration into a reserved flash sector.

---

## Execution Model

The firmware is based on a **cooperative, non-preemptive scheduling loop**:

- `tud_task()` must be called continuously for USB stack progress
- `wifi_manager_task()` services the radio through `cyw43_arch_poll()` from the same loop, with no background thread or interrupt
- `input_tasks_run(active)` executes all HID behaviour logic
- timing is derived from a monotonic millisecond clock:
  `to_ms_since_boot(get_absolute_time())`

### Constraint model

- No blocking delays (`sleep`, busy-wait loops) in behaviour logic
- All timing is implemented using delta comparisons on timestamps
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

- Firmware region: `0x10000000 .. 0x103FEFFF` (`0x003FF000` bytes)
- Reserved config sector: `0x103FF000 .. 0x103FFFFF` (last 4 KB sector)

### Safety invariant

The firmware image must end before the config sector. If it overlapped, `config_read_hours()` would read firmware bytes and silently fall back to `CONFIG_DEFAULT_HOURS`, and writing a config would overwrite part of the firmware.

Enforced at build time:

- the `PICO_FLASH_SIZE_BYTES` CMake variable limits the linker's `FLASH` region to `0x003FF000` bytes, so an oversized image fails to link
- `static_assert`s in `src/main.c` fail the build unless the linker region ends exactly at `CONFIG_FLASH_ADDR` and `flash_config.h` matches the board's flash size and sector size
- a `_Static_assert` in `inc/flash_config.h` fails both the firmware and host builds if `pico_config_t` stops being the 8 bytes `write_config` writes
- `PICO_BOARD` defaults to `pico2_w`, and CMake configuration fails for any non-RP2350 platform

The checks exist because the layout is defined in two places, `CMakeLists.txt` and `inc/flash_config.h`. Without them, either side could change and the build would still succeed.

Limits of this protection:

- the reduced size applies to the linker only. C code still sees the board's real size in `PICO_FLASH_SIZE_BYTES`, and must: the SDK derives the flash device size from it and needs a power of two, and its bounds check would otherwise reject the config sector
- writing only to `CONFIG_FLASH_ADDR` is a convention, not a restriction. `write_config` is the only writer in this repository, but nothing stops another UF2 or a flash API call from changing that sector

---

## Wi-Fi

Built when the board has a CYW43 radio; on a Pico 2 the same API compiles to inline stubs.

- `wifi_manager_task()` runs from the main loop and never blocks, so a network failure cannot delay USB or HID behaviour
- a join that fails, or finds no DHCP lease within 30 s, retries after 2 s, doubling to 60 s; a wrong password retries at 60 s
- DHCP and DNS are provided by lwIP, which is configured for UDP only in `inc/lwipopts.h`
- the status LED is on the radio chip, so it needs the radio to start. Solid means online or no network configured, blinking means not online yet (including joined with no DHCP lease), and off means the run has finished

### Credentials

```bash
cp inc/wifi_secrets.example.h inc/wifi_secrets.h
```

`inc/wifi_secrets.h` is gitignored. Without it the firmware makes no connection attempt. `WIFI_AUTH` selects the security mode and defaults to WPA2 when unset; set it to `CYW43_AUTH_WPA3_SAE_AES_PSK` for WPA3-Personal. There is no automatic fallback between the two. The password is stored in plaintext in the firmware image and UF2, so use a dedicated network and do not share those files.

---

## Configuration System

Flash layout (8 bytes total):

```c
typedef struct {
    uint32_t magic;
    uint32_t hours;
} pico_config_t;
```

### Validation rules:

- `magic == 0xCAFEF00D`
- `hours` in `[1, 24]`

### Fallback behaviour:

If validation fails (erased flash, corruption or invalid values):

  - firmware uses `CONFIG_DEFAULT_HOURS`
  - system behaviour remains deterministic

### Design note

  The magic value is a **struct validity marker**, not a security mechanism.
  No cryptographic integrity protection is implemented.

---

## Host Configuration Writer

### Build

```bash
gcc src/write_config.c -Iinc -o write_config
```

### Usage

```bash
./write_config <hours>
```

### Output

Generates a UF2 payload that writes configuration data into the reserved flash sector.

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

- `sudo evtest` on the keyboard and mouse event nodes shows the raw event stream. The invariant: every key or button down has a matching up before the next unrelated event, and ALT is released cleanly after each ALT+TAB.
- `sudo usbhid-dump -m 2e8a:0003 -e stream` shows raw reports. During a click hold the mouse button field stays set whilst only the delta bytes vary.

---

## Disclaimer

This project is intended for embedded systems research and educational analysis of USB HID behaviour and host-side detection mechanisms.
