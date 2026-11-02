#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Boards without a radio get inline stubs, so callers need no conditionals.
 * The status LED lives here because the Pico 2 W wires it to the radio chip.
 */
#if HID_LAB_WIFI

void wifi_manager_init(void);
void wifi_manager_task(void);
bool wifi_manager_is_up(void);
bool status_led_state(bool active, uint32_t now);
void status_led_write(bool on);

#else

#include "bsp/board.h"

static inline void wifi_manager_init(void) {}
static inline void wifi_manager_task(void) {}
static inline bool wifi_manager_is_up(void) { return false; }
static inline bool status_led_state(bool active, uint32_t now) { (void)now; return active; }
static inline void status_led_write(bool on) { board_led_write(on); }

#endif

#endif
