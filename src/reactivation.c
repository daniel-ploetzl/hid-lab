/*
 * Re-activation timer. See inc/reactivation.h for the model.
 *
 *   All timing uses unsigned subtraction, which stays correct across the
 *   millisecond counter wrap as long as each phase is shorter than the wrap
 *   period. reactivation_init() enforces that bound.
 */

#include "reactivation.h"
#include "hid_assert.h"

void reactivation_init(reactivation_t *r, uint32_t active_ms,
                       uint32_t period_ms, uint32_t now)
{
    HID_ASSERT(r != NULL);
    HID_ASSERT(active_ms > 0u && active_ms <= REACT_MAX_PHASE_MS);
    HID_ASSERT(period_ms > 0u && period_ms <= REACT_MAX_PHASE_MS);

    r->active_ms   = active_ms;
    r->period_ms   = period_ms;
    r->phase       = REACT_IDLE;
    /* Back-date the idle phase by one period so the first update opens an
     * active window. Underflow here is intended and harmless, because the
     * delta in reactivation_update() is modular. */
    r->phase_start = now - period_ms;
}

bool reactivation_update(reactivation_t *r, uint32_t now)
{
    HID_ASSERT(r != NULL);

    uint32_t elapsed = now - r->phase_start;

    if (r->phase == REACT_ACTIVE)
    {
        if (elapsed >= r->active_ms)
        {
            r->phase       = REACT_IDLE;
            r->phase_start = now;
        }
        return false;
    }

    /* REACT_IDLE */
    if (elapsed >= r->period_ms)
    {
        r->phase       = REACT_ACTIVE;
        r->phase_start = now;
        return true;
    }
    return false;
}
