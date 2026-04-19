/*
 * HID behaviour engine for USB input simulation.
 *
 * - Internal state machines per activity
 * - Randomised timing using pico_rand
 * - Uses TinyUSB HID API
 */

#include "input_tasks.h"
#include "pico/time.h"
#include "pico/rand.h"
#include "tusb.h"

/* ───────── HID IDs ───────── */
#define REPORT_ID_KEYBOARD  1
#define REPORT_ID_MOUSE     2

/* ───────── Timing ───────── */

#define MOVE_INTERVAL_MIN_MS    8000UL
#define MOVE_INTERVAL_MAX_MS   20000UL

#define CLICK_INTERVAL_MIN_MS  30000UL
#define CLICK_INTERVAL_MAX_MS  90000UL
#define CLICK_HOLD_MIN_MS         80UL
#define CLICK_HOLD_MAX_MS        180UL

#define KEY_INTERVAL_MIN_MS    20000UL
#define KEY_INTERVAL_MAX_MS    60000UL
#define KEY_HOLD_MIN_MS           40UL
#define KEY_HOLD_MAX_MS          120UL

#define TEXT_INTERVAL_MIN_MS   120000UL
#define TEXT_INTERVAL_MAX_MS   300000UL
#define TEXT_BURST_KEYS_MIN         3UL
#define TEXT_BURST_KEYS_MAX         8UL
#define TEXT_KEY_HOLD_MIN_MS       40UL
#define TEXT_KEY_HOLD_MAX_MS      120UL
#define TEXT_KEY_GAP_MIN_MS        45UL
#define TEXT_KEY_GAP_MAX_MS       140UL

#define SCROLL_INTERVAL_MIN_MS 60000UL
#define SCROLL_INTERVAL_MAX_MS 180000UL
#define SCROLL_BURST_STEPS_MIN      3UL
#define SCROLL_BURST_STEPS_MAX     10UL
#define SCROLL_STEP_GAP_MIN_MS     50UL
#define SCROLL_STEP_GAP_MAX_MS    150UL

#define WINDOW_INTERVAL_MIN_MS 180000UL
#define WINDOW_INTERVAL_MAX_MS 420000UL
#define WINDOW_BURST_SWITCHES_MIN   1UL
#define WINDOW_BURST_SWITCHES_MAX   3UL
#define WINDOW_HOLD_MIN_MS         70UL
#define WINDOW_HOLD_MAX_MS        110UL
#define WINDOW_GAP_MIN_MS          90UL
#define WINDOW_GAP_MAX_MS         170UL

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

/* ───────── Global state ───────── */
static bool active = true;
static bool released_all = false;


/* ───────── Move ───────── */
static uint32_t move_next;

static void move_init(void)
{
    move_next = now_ms() + rand_range(MOVE_INTERVAL_MIN_MS, MOVE_INTERVAL_MAX_MS);
}

static void move_task(void)
{
    if (now_ms() < move_next || !tud_hid_ready())
        return;

    int8_t dx = rand_range_i32(-4, 4);
    int8_t dy = rand_range_i32(-4, 4);

    hid_mouse_report_t r = {0, dx, dy, 0, 0};
    tud_hid_report(REPORT_ID_MOUSE, &r, sizeof(r));

    move_next = now_ms() + rand_range(MOVE_INTERVAL_MIN_MS, MOVE_INTERVAL_MAX_MS);
}


/* ───────── Click ───────── */
typedef enum { C_IDLE, C_DOWN } cphase_t;

static struct {
    cphase_t p;
    uint32_t next, rel;
} click;

static void click_init(void)
{
    click.p = C_IDLE;
    click.next = now_ms() + rand_range(CLICK_INTERVAL_MIN_MS, CLICK_INTERVAL_MAX_MS);
}

static void click_task(void)
{
    uint32_t now = now_ms();

    if (click.p == C_IDLE && now >= click.next && tud_hid_ready())
	{
        hid_mouse_report_t r = {MOUSE_BUTTON_LEFT,0,0,0,0};
        tud_hid_report(REPORT_ID_MOUSE, &r, sizeof(r));
        click.rel = now + rand_range(CLICK_HOLD_MIN_MS, CLICK_HOLD_MAX_MS);
        click.p = C_DOWN;
    }
    else if (click.p == C_DOWN && now >= click.rel && tud_hid_ready())
	{
        hid_mouse_report_t r = {0};
        tud_hid_report(REPORT_ID_MOUSE, &r, sizeof(r));
        click.next = now + rand_range(CLICK_INTERVAL_MIN_MS, CLICK_INTERVAL_MAX_MS);
        click.p = C_IDLE;
    }
}

/* ───────── Key ───────── */
static const uint8_t safe_keys[] = {
    HID_KEY_SHIFT_LEFT, HID_KEY_CONTROL_LEFT,
    HID_KEY_ALT_LEFT,
    HID_KEY_ARROW_LEFT, HID_KEY_ARROW_RIGHT,
};

typedef enum { K_IDLE, K_DOWN } kphase_t;

static struct {
    kphase_t p;
    uint32_t next, rel;
    uint8_t key;
} key;

static void key_init(void)
{
    key.p = K_IDLE;
    key.next = now_ms() + rand_range(KEY_INTERVAL_MIN_MS, KEY_INTERVAL_MAX_MS);
}

static void key_task(void)
{
    uint32_t now = now_ms();

    if (key.p == K_IDLE && now >= key.next && tud_hid_ready())
	{
        key.key = safe_keys[get_rand_32() % (sizeof(safe_keys)/sizeof(safe_keys[0]))];
        uint8_t kc[6] = { key.key };
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, kc);
        key.rel = now + rand_range(KEY_HOLD_MIN_MS, KEY_HOLD_MAX_MS);
        key.p = K_DOWN;
    }
    else if (key.p == K_DOWN && now >= key.rel && tud_hid_ready())
	{
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        key.next = now + rand_range(KEY_INTERVAL_MIN_MS, KEY_INTERVAL_MAX_MS);
        key.p = K_IDLE;
    }
}

/* ───────── Stop ───────── */
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
}

void input_tasks_run(bool is_active)
{
    active = is_active;

    if (!active)
	{
        input_tasks_stop();
        return;
    }

    move_task();
    click_task();
    key_task();
}

void input_tasks_stop(void)
{
    release_all_inputs_once();
}
