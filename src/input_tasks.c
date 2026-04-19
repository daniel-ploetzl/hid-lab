/*
 * HID behaviour engine for USB input simulation.
 *
 * - Internal state machines per activity (non-blocking)
 * - Randomised timing using RP2350 hardware TRNG (pico_rand)
 * - Uses TinyUSB HID API
 *
 * Activities:
 *   1. Mouse micro-movement
 *   2. Mouse left click
 *   3. Safe key press
 *   4. Text burst (printable keys)
 *   5. Scroll burst (mouse wheel)
 *   6. Window switch (ALT+TAB)
 */

#include "input_tasks.h"
#include "pico/time.h"
#include "pico/rand.h"
#include "bsp/board.h"
#include "tusb.h"

/* ───────── HID IDs ───────── */
#define REPORT_ID_KEYBOARD  1
#define REPORT_ID_MOUSE     2

/* ───────── Timing ───────── */
#define MOVE_INTERVAL_MIN_MS        8000UL
#define MOVE_INTERVAL_MAX_MS       20000UL

#define CLICK_INTERVAL_MIN_MS      30000UL
#define CLICK_INTERVAL_MAX_MS      90000UL
#define CLICK_HOLD_MIN_MS             80UL
#define CLICK_HOLD_MAX_MS            180UL

#define KEY_INTERVAL_MIN_MS        20000UL
#define KEY_INTERVAL_MAX_MS        60000UL
#define KEY_HOLD_MIN_MS               40UL
#define KEY_HOLD_MAX_MS              120UL

#define TEXT_INTERVAL_MIN_MS      120000UL
#define TEXT_INTERVAL_MAX_MS      300000UL
#define TEXT_BURST_KEYS_MIN             3UL
#define TEXT_BURST_KEYS_MAX             8UL
#define TEXT_KEY_HOLD_MIN_MS           40UL
#define TEXT_KEY_HOLD_MAX_MS          120UL
#define TEXT_KEY_GAP_MIN_MS            45UL
#define TEXT_KEY_GAP_MAX_MS           140UL

#define SCROLL_INTERVAL_MIN_MS     60000UL
#define SCROLL_INTERVAL_MAX_MS    180000UL
#define SCROLL_BURST_STEPS_MIN          3UL
#define SCROLL_BURST_STEPS_MAX         10UL
#define SCROLL_STEP_GAP_MIN_MS         50UL
#define SCROLL_STEP_GAP_MAX_MS        150UL

#define WINDOW_INTERVAL_MIN_MS    180000UL
#define WINDOW_INTERVAL_MAX_MS    420000UL
#define WINDOW_BURST_SWITCHES_MIN       1UL
#define WINDOW_BURST_SWITCHES_MAX       3UL
#define WINDOW_HOLD_MIN_MS             70UL
#define WINDOW_HOLD_MAX_MS            110UL
#define WINDOW_GAP_MIN_MS              90UL
#define WINDOW_GAP_MAX_MS             170UL

/* ───────── Utils ───────── */
static inline uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static uint32_t rand_range(uint32_t min, uint32_t max)
{
    return min + (get_rand_32() % (max - min + 1));
}

static int32_t rand_range_i32(int32_t min, int32_t max)
{
    uint32_t span = (uint32_t)(max - min + 1);
    return min + (int32_t)(get_rand_32() % span);
}

/* ───────── Stop flag ───────── */
static bool released_all = false;

/* ───────── Mouse movement ───────── */
static uint32_t move_next;

static void move_init(void)
{
    move_next = now_ms() + rand_range(MOVE_INTERVAL_MIN_MS, MOVE_INTERVAL_MAX_MS);
}

static void move_task(void)
{
    if (now_ms() < move_next || !tud_hid_ready())
        return;

    /* Non-zero on both axes guaranteed — zero movement has no idle-reset value */
    int8_t dx = (int8_t)(rand_range_i32(1, 4) * (get_rand_32() & 1 ? 1 : -1));
    int8_t dy = (int8_t)(rand_range_i32(1, 4) * (get_rand_32() & 1 ? 1 : -1));

    hid_mouse_report_t r = {0, dx, dy, 0, 0};
    tud_hid_report(REPORT_ID_MOUSE, &r, sizeof(r));

    move_next = now_ms() + rand_range(MOVE_INTERVAL_MIN_MS, MOVE_INTERVAL_MAX_MS);
}

/* ───────── Mouse left click ───────── */
typedef enum { C_IDLE, C_DOWN } cphase_t;

static struct {
    cphase_t p;
    uint32_t next, rel;
} click;

static void click_init(void)
{
    click.p    = C_IDLE;
    click.next = now_ms() + rand_range(CLICK_INTERVAL_MIN_MS, CLICK_INTERVAL_MAX_MS);
}

static void click_task(void)
{
    uint32_t now = now_ms();

    if (click.p == C_IDLE && now >= click.next && tud_hid_ready()) {
        hid_mouse_report_t r = {MOUSE_BUTTON_LEFT, 0, 0, 0, 0};
        tud_hid_report(REPORT_ID_MOUSE, &r, sizeof(r));
        click.rel = now + rand_range(CLICK_HOLD_MIN_MS, CLICK_HOLD_MAX_MS);
        click.p   = C_DOWN;
    }
    else if (click.p == C_DOWN && now >= click.rel && tud_hid_ready()) {
        hid_mouse_report_t r = {0};
        tud_hid_report(REPORT_ID_MOUSE, &r, sizeof(r));
        click.next = now + rand_range(CLICK_INTERVAL_MIN_MS, CLICK_INTERVAL_MAX_MS);
        click.p    = C_IDLE;
    }
}

/* ───────── Safe key press ───────── */
static const uint8_t safe_keys[] = {
    HID_KEY_SHIFT_LEFT, HID_KEY_CONTROL_LEFT,
    HID_KEY_ALT_LEFT,
    HID_KEY_ARROW_LEFT, HID_KEY_ARROW_RIGHT,
};
#define SAFE_KEY_COUNT (sizeof(safe_keys) / sizeof(safe_keys[0]))

typedef enum { K_IDLE, K_DOWN } kphase_t;

static struct {
    kphase_t p;
    uint32_t next, rel;
    uint8_t  key;
} key;

static void key_init(void)
{
    key.p    = K_IDLE;
    key.next = now_ms() + rand_range(KEY_INTERVAL_MIN_MS, KEY_INTERVAL_MAX_MS);
}

static void key_task(void)
{
    uint32_t now = now_ms();

    if (key.p == K_IDLE && now >= key.next && tud_hid_ready()) {
        key.key = safe_keys[get_rand_32() % SAFE_KEY_COUNT];
        uint8_t kc[6] = { key.key };
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, kc);
        key.rel = now + rand_range(KEY_HOLD_MIN_MS, KEY_HOLD_MAX_MS);
        key.p   = K_DOWN;
    }
    else if (key.p == K_DOWN && now >= key.rel && tud_hid_ready()) {
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        key.next = now + rand_range(KEY_INTERVAL_MIN_MS, KEY_INTERVAL_MAX_MS);
        key.p    = K_IDLE;
    }
}
/* ───────── Text burst  ───────── */
static const uint8_t text_keys[] = {
    HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F,
    HID_KEY_J, HID_KEY_K, HID_KEY_L,
    HID_KEY_SPACE,
};
#define TEXT_KEY_COUNT (sizeof(text_keys) / sizeof(text_keys[0]))

typedef enum { T_IDLE, T_KEY_DOWN, T_KEY_GAP } tphase_t;

static struct {
    tphase_t p;
    uint32_t next;        /* next burst start (T_IDLE)     */
    uint32_t deadline;    /* current phase deadline        */
    uint8_t  remaining;   /* keystrokes left in burst      */
} text;

static void text_init(void)
{
    text.p         = T_IDLE;
    text.remaining = 0;
    text.next      = now_ms() + rand_range(TEXT_INTERVAL_MIN_MS, TEXT_INTERVAL_MAX_MS);
}

static void text_task(void)
{
    uint32_t now = now_ms();

    switch (text.p) {

    case T_IDLE:
        if (now < text.next || !tud_hid_ready())
            return;
        text.remaining = (uint8_t)rand_range(TEXT_BURST_KEYS_MIN, TEXT_BURST_KEYS_MAX);
        /* fall through to send first key immediately */

    /* intentional fallthrough */
    case T_KEY_GAP:
        if (text.p == T_KEY_GAP && now < text.deadline)
            return;
        if (!tud_hid_ready())
            return;
        if (text.remaining == 0) {
            text.next = now + rand_range(TEXT_INTERVAL_MIN_MS, TEXT_INTERVAL_MAX_MS);
            text.p    = T_IDLE;
            return;
        }
        {
            uint8_t kc[6] = { text_keys[get_rand_32() % TEXT_KEY_COUNT] };
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, kc);
            text.deadline = now + rand_range(TEXT_KEY_HOLD_MIN_MS, TEXT_KEY_HOLD_MAX_MS);
            text.p = T_KEY_DOWN;
        }
        break;

    case T_KEY_DOWN:
        if (now < text.deadline || !tud_hid_ready())
            return;
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        text.remaining--;
        text.deadline = now + rand_range(TEXT_KEY_GAP_MIN_MS, TEXT_KEY_GAP_MAX_MS);
        text.p = T_KEY_GAP;
        break;
    }
}

/* ───────── Scroll burst  ───────── */
typedef enum { S_IDLE, S_STEP_ACTIVE, S_STEP_GAP } sphase_t;

static struct {
    sphase_t p;
    uint32_t next;
    uint32_t deadline;
    uint8_t  remaining;
} scroll;

static void scroll_init(void)
{
    scroll.p         = S_IDLE;
    scroll.remaining = 0;
    scroll.next      = now_ms() + rand_range(SCROLL_INTERVAL_MIN_MS, SCROLL_INTERVAL_MAX_MS);
}

static void scroll_task(void)
{
    uint32_t now = now_ms();

    switch (scroll.p) {

    case S_IDLE:
        if (now < scroll.next || !tud_hid_ready())
            return;
        scroll.remaining = (uint8_t)rand_range(SCROLL_BURST_STEPS_MIN, SCROLL_BURST_STEPS_MAX);
        /* fall through to send first tick immediately */

    /* intentional fallthrough */
    case S_STEP_GAP:
        if (scroll.p == S_STEP_GAP && now < scroll.deadline)
            return;
        if (!tud_hid_ready())
            return;
        if (scroll.remaining == 0) {
            scroll.next = now + rand_range(SCROLL_INTERVAL_MIN_MS, SCROLL_INTERVAL_MAX_MS);
            scroll.p    = S_IDLE;
            return;
        }
        {
            /* rand_range_i32 used — passing negative literals to rand_range
             * causes silent uint32_t promotion and garbage output           */
            int8_t wheel = (int8_t)rand_range_i32(-2, 2);
            hid_mouse_report_t r = {0, 0, 0, wheel, 0};
            tud_hid_report(REPORT_ID_MOUSE, &r, sizeof(r));
            scroll.remaining--;
            scroll.deadline = now + rand_range(SCROLL_STEP_GAP_MIN_MS, SCROLL_STEP_GAP_MAX_MS);
            scroll.p = S_STEP_GAP;
        }
        break;

    case S_STEP_ACTIVE:
        /* unused phase — collapses to gap immediately after send */
        break;
    }
}

/* ───────── Window switch  ───────── */
typedef enum { W_IDLE, W_KEY_DOWN, W_KEY_GAP } wphase_t;

static struct {
    wphase_t p;
    uint32_t next;
    uint32_t deadline;
    uint8_t  remaining;
} window;

static void window_init(void)
{
    window.p         = W_IDLE;
    window.remaining = 0;
    window.next      = now_ms() + rand_range(WINDOW_INTERVAL_MIN_MS, WINDOW_INTERVAL_MAX_MS);
}

static void window_task(void)
{
    uint32_t now = now_ms();

    switch (window.p) {

    case W_IDLE:
        if (now < window.next || !tud_hid_ready())
            return;
        window.remaining = (uint8_t)rand_range(WINDOW_BURST_SWITCHES_MIN,
                                               WINDOW_BURST_SWITCHES_MAX);
        /* fall through to send first ALT+TAB immediately */

    /* intentional fallthrough */
    case W_KEY_GAP:
        if (window.p == W_KEY_GAP && now < window.deadline)
            return;
        if (!tud_hid_ready())
            return;
        if (window.remaining == 0) {
            window.next = now + rand_range(WINDOW_INTERVAL_MIN_MS, WINDOW_INTERVAL_MAX_MS);
            window.p    = W_IDLE;
            return;
        }
        {
            uint8_t kc[6] = { HID_KEY_TAB };
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD,
                                    KEYBOARD_MODIFIER_LEFTALT, kc);
            window.deadline = now + rand_range(WINDOW_HOLD_MIN_MS, WINDOW_HOLD_MAX_MS);
            window.p = W_KEY_DOWN;
        }
        break;

    case W_KEY_DOWN:
        if (now < window.deadline || !tud_hid_ready())
            return;
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        window.remaining--;
        window.deadline = now + rand_range(WINDOW_GAP_MIN_MS, WINDOW_GAP_MAX_MS);
        window.p = W_KEY_GAP;
        break;
    }
}

/* ───────── Release all inputs  ───────── */
static void release_all_inputs_once(void)
{
    if (released_all || !tud_hid_ready())
        return;

    hid_mouse_report_t m = {0};
    tud_hid_report(REPORT_ID_MOUSE, &m, sizeof(m));
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);

    released_all = true;
}

/* ───────── Public API  ───────── */
void input_tasks_init(void)
{
    move_init();
    click_init();
    key_init();
    text_init();
    scroll_init();
    window_init();
}

void input_tasks_run(bool is_active)
{
    if (!is_active) {
        input_tasks_stop();
        return;
    }

    move_task();
    click_task();
    key_task();
    text_task();
    scroll_task();
    window_task();
}

void input_tasks_stop(void)
{
    release_all_inputs_once();
}
