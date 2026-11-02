/*
 * Wi-Fi station manager (Pico 2 W)
 *
 *   Joins the network and keeps it joined from the main loop. Uses the SDK's
 *   poll architecture: no thread or radio interrupt, so all lwIP and driver
 *   callbacks run inside cyw43_arch_poll() and need no locking.
 *
 *   Nothing here may block: the blocking connect helpers would stall
 *   tud_task(). DHCP and DNS are handled by lwIP once the link is up.
 */

#include "wifi_manager.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "hid_assert.h"
#include <stdint.h>

/* Untracked until credentials move to the config sector. Without it the radio
 * still starts for the LED, but no join is attempted. */
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""
#endif

/* Explicit on purpose: there is no fallback, so a WPA3 network can never be
 * silently downgraded. The driver's WPA3/WPA2 mode only configures WPA3 for a
 * station, so it would not reach a WPA2-only network either. */
#ifndef WIFI_AUTH
#define WIFI_AUTH CYW43_AUTH_WPA2_AES_PSK
#endif

#define JOIN_TIMEOUT_MS 30000u
#define BACKOFF_MIN_MS   2000u
#define BACKOFF_MAX_MS  60000u

typedef enum {
    WIFI_OFF,
    WIFI_JOINING,
    WIFI_UP,
    WIFI_BACKOFF
} wifi_state_t;

static wifi_state_t s_state = WIFI_OFF;
static bool         s_radio_ok;
static uint32_t     s_state_since_ms;
static uint32_t     s_backoff_ms = BACKOFF_MIN_MS;
static uint32_t     s_retry_delay_ms;

static inline uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static void set_state(wifi_state_t state, uint32_t now)
{
    s_state = state;
    s_state_since_ms = now;
}

static void schedule_retry(int status, uint32_t now)
{
    HID_ASSERT(s_radio_ok);

    /* A wrong password will not fix itself, so retry at the slowest rate. */
    s_retry_delay_ms = (status == CYW43_LINK_BADAUTH) ? BACKOFF_MAX_MS : s_backoff_ms;

    s_backoff_ms *= 2;
    if (s_backoff_ms > BACKOFF_MAX_MS)
        s_backoff_ms = BACKOFF_MAX_MS;
    HID_ASSERT(s_backoff_ms <= BACKOFF_MAX_MS);

    /* Clears a half-finished join, such as associated but no DHCP lease. */
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    set_state(WIFI_BACKOFF, now);
}

static void start_join(uint32_t now)
{
    int err = cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, WIFI_AUTH);
    if (err)
        schedule_retry(CYW43_LINK_FAIL, now);
    else
        set_state(WIFI_JOINING, now);
}

/* Blocks while the radio firmware loads, so call it before tusb_init().
 * On failure HID carries on; only the LED and networking are lost. */
void wifi_manager_init(void)
{
    if (cyw43_arch_init() != 0)
        return;

    s_radio_ok = true;
    cyw43_arch_enable_sta_mode();

    if (WIFI_SSID[0] != '\0')
        start_join(now_ms());
}

void wifi_manager_task(void)
{
    if (!s_radio_ok)
        return;

    cyw43_arch_poll();

    if (s_state == WIFI_OFF)
        return;

    uint32_t now = now_ms();
    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

    switch (s_state)
    {
        case WIFI_JOINING:
            if (status == CYW43_LINK_UP)
            {
                s_backoff_ms = BACKOFF_MIN_MS;
                set_state(WIFI_UP, now);
            }
            else if (status < 0 || now - s_state_since_ms >= JOIN_TIMEOUT_MS)
                schedule_retry(status, now);
            break;

        case WIFI_UP:
            if (status != CYW43_LINK_UP)
                schedule_retry(status, now);
            break;

        case WIFI_BACKOFF:
            if (now - s_state_since_ms >= s_retry_delay_ms)
                start_join(now);
            break;

        default:
            break;
    }
}

bool wifi_manager_is_up(void)
{
    return s_state == WIFI_UP;
}

/* Solid: online, or no network configured. Blinking: not online yet, which
 * includes joined without a DHCP lease. Off: run finished. */
bool status_led_state(bool active, uint32_t now)
{
    if (!active)
        return false;
    if (s_state == WIFI_OFF || s_state == WIFI_UP)
        return true;
    return (now / 500u) & 1u;
}

void status_led_write(bool on)
{
    if (s_radio_ok)
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}
