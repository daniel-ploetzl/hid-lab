# hid-lab

USB Human Interface Device (HID) behaviour simulation lab for Raspberry Pi Pico 2.

## Overview

This project implements a composite USB HID device (keyboard + mouse)
that generates realistic, non-deterministic user input patterns.

Designed for analysing system behaviour, detection mechanisms
and activity heuristics.

## Architecture

- `main.c`            : scheduling and behaviour orchestration
- `usb_descriptors.c` : HID descriptor definitions
- `flash_config.h`    : persistent runtime configuration
- `write_config.c`    : host-side UF2 configuration tool

## Behaviour Model

- time-based state machines per activity
- independent scheduling for:
  - mouse movement
  - clicks
  - key presses
  - typing bursts
  - scrolling
  - window switching
- randomised intervals and durations

## Key Concepts

- USB HID protocol (TinyUSB)
- composite HID devices (report IDs)
- embedded timing and scheduling
- non-deterministic input generation

## Build

Requires Raspberry Pi Pico SDK.

```bash
mkdir build && cd build
cmake .. -DPICO_BOARD=pico2
make -j$(nproc)
```

## Flash layout and safety

The project uses a fixed flash partition for Pico 2 (4MB external flash):

- Firmware range (linked and used by `hid_lab.uf2`): `0x10000000 .. 0x103FEFFF` (`0x003FF000` bytes)
- Reserved config sector (written by `write_config`): `0x103FF000 .. 0x103FFFFF` (last 4KB sector)

Why this is safe:

- `CMakeLists.txt` sets `PICO_FLASH_SIZE_BYTES` to `0x003FF000`, so firmware outputs cannot grow into the last sector.
- `write_config.c` writes only the reserved last sector.

## Config format and meaning

The config sector stores 8 bytes at the start of the sector:

- `[0..3]` magic `0xCAFEF00D` (marks data as valid config)
- `[4..7]` runtime hours (`1..24`)

`CONFIG_MAGIC` exists so firmware can distinguish valid config from erased/random flash content.
If config is invalid or missing, firmware uses `CONFIG_DEFAULT_HOURS`.

## Host config writer

Build:

```bash
gcc src/write_config.c -o write_config
```

Use:

```bash
write_config <hours>
```

This writes `pico_hid_cfg.uf2` to `/media/$USER/RP2350/`.
