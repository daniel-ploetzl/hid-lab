# hid-lab — USB HID Behaviour Simulation Lab (Raspberry Pi Pico 2)

## Overview

This project implements a composite USB Human Interface Device (HID) behaviour simulator on Raspberry Pi Pico 2.
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

- `src/usb_descriptors.c`
  USB descriptor definitions for composite HID device:
  - keyboard interface
  - mouse interface
  - report ID mapping

### Shared configuration

- `inc/flash_config.h`
  Flash layout definitions and configuration read/validation logic.

### Host utility

- `src/write_config.
  Host-side tool that generates a UF2 payload to write configuration into a reserved flash sector.

---

## Execution Model

The firmware is based on a **cooperative, non-preemptive scheduling loop**:

- `tud_task()` must be called continuously for USB stack progress
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
- randomness sourced from RP2350 hardware True Random Number Generator (TRNG) via `pico_rand`
- each subsystem operates independently (no global synchronisation)

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

Pico 2 flash map (4 MB):

- Firmware region: `0x10000000 .. 0x103FEFFF` (`0x003FF000` bytes)
- Reserved config sector: `0x103FF000 .. 0x103FFFFF` (last 4 KB sector)

### Safety invariant:

- Firmware image is constrained to exclude the last flash sector
- `PICO_FLASH_SIZE_BYTES` is reduced to reserve the final 4 KB sector
- configuration writes are restricted to `CONFIG_FLASH_ADDR`

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
cmake .. -DPICO_BOARD=pico2
make -j$(nproc)
```

### Outputs

- .uf2 firmware image
- .elf executable
- .bin raw binary

---

## Disclaimer

This project is intended for embedded systems research and educational analysis of USB HID behaviour and host-side detection mechanisms.
