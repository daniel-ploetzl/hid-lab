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
 * RESEARCH FOCUS :
 *   - host-side detection of synthetic input
 *   - HID behaviour modelling
 *   - behavioural analysis of input streams
 */

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/flash.h"
#include "bsp/board.h"
#include "tusb.h"
#include "input_tasks.h"
#include "reactivation.h"
#include "wifi_manager.h"
#include "flash_config.h"
#include "hid_assert.h"
#include <assert.h>

/* ───────── Flash reservation checks  ───────── */
static_assert(CREDS_FLASH_ADDR - CONFIG_FLASH_XIP_BASE == HID_LAB_LINK_FLASH_BYTES,
              "linker FLASH region must end exactly at the credentials sector");
static_assert(CONFIG_FLASH_TOTAL_BYTES == PICO_FLASH_SIZE_BYTES,
              "flash_config.h flash size must match the board");
static_assert(CONFIG_SECTOR_SIZE_BYTES == FLASH_SECTOR_SIZE,
              "flash_config.h sector size must match the SDK");

#define MS_PER_HOUR 3600000UL

static inline uint32_t now_ms(void)
{
    /* The lead is 0 in normal builds. A test build sets it so the counter wraps
     * within minutes, to confirm the cycle survives the 49.7 day rollover. */
    return to_ms_since_boot(get_absolute_time()) + HID_LAB_TEST_WRAP_LEAD_MS;
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
    /* Test builds halt here, before USB starts, to prove assertions survive Release. */
    HID_ASSERT(!HID_LAB_TEST_ASSERT);
    wifi_manager_init();
    tusb_init();

    uint32_t active_ms = config_read_hours() * MS_PER_HOUR;
    HID_ASSERT(active_ms >= CONFIG_HOURS_MIN * MS_PER_HOUR);
    HID_ASSERT(active_ms <= CONFIG_HOURS_MAX * MS_PER_HOUR);

    /* The idle period is read from the config sector in seconds. The maximum of
     * one week is 604,800,000 ms, which fits in 32 bits and stays under the
     * timer's phase ceiling. */
    uint32_t period_ms = config_read_period_s() * 1000UL;
    HID_ASSERT(period_ms >= CONFIG_PERIOD_MIN_S * 1000UL);
    HID_ASSERT(period_ms <= CONFIG_PERIOD_MAX_S * 1000UL);

    /* Active for the configured window, idle for the period, then repeat with no
     * end. reactivation_update() keeps this wrap-safe, so no latch is needed. */
    reactivation_t react;
    reactivation_init(&react, active_ms, period_ms, now_ms());

    input_tasks_init();
	led_update(true);

    while (1)
    {
        tud_task();
        wifi_manager_task();
        uint32_t now = now_ms();
        if (reactivation_update(&react, now))
        {
            /* Activation edge, once per window. The login sequence, type
             * username, Return, password, Return, will run here once the
             * credential store is in place. */
        }
        bool active = reactivation_is_active(&react);
        led_update(status_led_state(active, now));
        input_tasks_run(active);
    }
}
