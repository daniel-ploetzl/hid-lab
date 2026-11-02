/*
 * HID Behaviour Simulation Lab (RP2350 / Pico 2 and Pico 2 W)
 *
 *   Simulate realistic human input patterns via USB HID to analyse
 *   system behaviour, detection mechanisms and activity heuristics.
 *
 * CORE IDEAS :
 *   - time-based state machines per activity
 *   - randomised intervals to avoid deterministic patterns
 *   - separation of USB layer and behaviour logic
 *
 * USE CASES :
 *   - endpoint monitoring validation
 *   - idle detection bypass testing
 *   - behavioural analysis of input streams
 */

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/flash.h"
#include "bsp/board.h"
#include "tusb.h"
#include "input_tasks.h"
#include "wifi_manager.h"
#include "flash_config.h"
#include <assert.h>

/* ───────── Flash reservation checks  ───────── */
static_assert(CONFIG_FLASH_ADDR - CONFIG_FLASH_XIP_BASE == HID_LAB_LINK_FLASH_BYTES,
              "linker FLASH region must end exactly at the config sector");
static_assert(CONFIG_FLASH_TOTAL_BYTES == PICO_FLASH_SIZE_BYTES,
              "flash_config.h flash size must match the board");
static_assert(CONFIG_SECTOR_SIZE_BYTES == FLASH_SECTOR_SIZE,
              "flash_config.h sector size must match the SDK");

static inline uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/* On the Pico 2 W each LED write is a bus transaction to the radio chip. */
static void led_update(bool on)
{
    static bool led_on;
    static bool led_known;

    if (led_known && led_on == on)
        return;
    status_led_write(on);
    led_on = on;
    led_known = true;
}

int main(void)
{
    board_init();
    wifi_manager_init();
    tusb_init();

    uint32_t duration_ms = config_read_hours() * 3600UL * 1000UL;
    uint32_t start = now_ms();

    input_tasks_init();
	led_update(true);

    while (1)
    {
        tud_task();
        wifi_manager_task();
        uint32_t now = now_ms();
        bool active = (now - start) < duration_ms;
        led_update(status_led_state(active, now));
        input_tasks_run(active);
    }
}
