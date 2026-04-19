/*
 * HID Behaviour Simulation Lab (RP2350 / Pico 2)
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
#include "tusb.h"
#include "input_tasks.h"
#include "flash_config.h"

static inline uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

int main(void)
{
    board_init();
    tusb_init();

    uint32_t duration_ms = config_read_hours() * 3600UL * 1000UL;
    uint32_t start = now_ms();

    input_tasks_init();
	board_led_write(true);

    while (1)
    {
        tud_task();
        bool active = (now_ms() - start) < duration_ms;
		board_led_write(active);
        input_tasks_run(active);
    }
}
