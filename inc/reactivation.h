/*
 * Re-activation timer
 *
 *   Drives a repeating duty cycle from the monotonic millisecond clock. The
 *   device is active for a window, idle for a period, then active again. No
 *   wall clock and no network are needed, so this runs with or without Wi-Fi.
 *
 *   The active window begins with a one-shot edge, which is where the login
 *   sequence will run once credentials are in place.
 */

#ifndef REACTIVATION_H
#define REACTIVATION_H

#include <stdint.h>
#include <stdbool.h>

/* Default idle gap between active windows. One hour is the first test value.
 * Raise to a day (86400000) then a week (604800000) once the cadence is
 * confirmed. Moving this into the flash config sector is a later step. */
#ifndef REACT_PERIOD_MS
#define REACT_PERIOD_MS (60u * 60u * 1000u)
#endif

/* Largest phase the wrap-safe comparison allows, kept well under the 49.7 day
 * (2^32 ms) counter wrap. One week fits with wide margin. */
#define REACT_MAX_PHASE_MS (14u * 24u * 60u * 60u * 1000u)

typedef enum { REACT_IDLE, REACT_ACTIVE } react_phase_t;

typedef struct {
    uint32_t      active_ms;    /* length of each active window */
    uint32_t      period_ms;    /* idle gap between active windows */
    uint32_t      phase_start;  /* now_ms at the start of the current phase */
    react_phase_t phase;
} reactivation_t;

/* Starts so the first reactivation_update() opens an active window and reports
 * the activation edge, so the device acts at boot rather than after one idle
 * period. active_ms and period_ms must be within (0, REACT_MAX_PHASE_MS]. */
void reactivation_init(reactivation_t *r, uint32_t active_ms,
                       uint32_t period_ms, uint32_t now);

/* Advances the cycle. Returns true only on the single call where an active
 * window begins, which is where the login sequence should run. */
bool reactivation_update(reactivation_t *r, uint32_t now);

static inline bool reactivation_is_active(const reactivation_t *r)
{
    return r->phase == REACT_ACTIVE;
}

#endif
