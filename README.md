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
