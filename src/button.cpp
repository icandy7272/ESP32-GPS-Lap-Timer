// ============================================================
// Button debounce module — ESP32-S3 GPS Lap Timer
// ============================================================

#include <Arduino.h>

#include "button.h"
#include "button_profile.h"
#include "pins.h"
#include "types.h"

// --- Module-private state ---

static QueueHandle_t s_btn_session_q = nullptr;
static QueueHandle_t s_btn_display_q = nullptr;
static size_t        s_button_count  = 0;

// Per-button debounce tracking

typedef struct {
    int      pin;
    uint8_t  button_id;       // ButtonId
    bool     stable_state;    // debounced state (true = HIGH)
    bool     last_raw_state;
    uint32_t last_change_ms;  // millis() of last raw state change
    uint32_t press_start_ms;  // millis() when button went LOW (pressed)
    bool     long_sent;       // long press already dispatched
    bool     is_latching;     // true for self-locking switch
} ButtonState;

static ButtonState s_buttons[2];

// --- Constants ---

static constexpr uint32_t DEBOUNCE_MS     = 50;
static constexpr uint32_t LONG_PRESS_MS   = 2000;
static constexpr uint32_t POLL_INTERVAL_MS = 10;

// --- Forward declarations ---

static void init_button_state(ButtonState* bs, int pin, uint8_t id,
                              bool latching);
static void register_button(int pin, uint8_t id);
static void poll_button(ButtonState* bs);
static void dispatch_event(uint8_t button_id, uint8_t event_type);
static void handle_latching_toggle(ButtonState* bs);
static void handle_momentary_press(ButtonState* bs, bool new_stable);

// --- Public API ---

void button_init(QueueHandle_t btn_session_q, QueueHandle_t btn_display_q)
{
    s_btn_session_q = btn_session_q;
    s_btn_display_q = btn_display_q;
    s_button_count  = 0;

    register_button(PIN_BTN_RECORD, BUTTON_RECORD);
    register_button(PIN_BTN_SECTOR, BUTTON_SECTOR);
}

void button_task(void* param)
{
    (void)param;

    for (;;) {
        for (size_t i = 0; i < s_button_count; i++) {
            poll_button(&s_buttons[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

// --- Private helpers ---

static void init_button_state(ButtonState* bs, int pin, uint8_t id,
                              bool latching)
{
    bs->pin            = pin;
    bs->button_id      = id;
    bs->stable_state   = (digitalRead(pin) == HIGH);
    bs->last_raw_state = bs->stable_state;
    bs->last_change_ms = millis();
    bs->press_start_ms = 0;
    bs->long_sent      = false;
    bs->is_latching    = latching;
}

static void register_button(int pin, uint8_t id)
{
    if (!button_is_connected(id) || s_button_count >= 2) {
        return;
    }

    pinMode(pin, INPUT_PULLUP);
    init_button_state(&s_buttons[s_button_count], pin, id,
                      button_is_latching(id));
    s_button_count++;
}

static void poll_button(ButtonState* bs)
{
    bool raw = (digitalRead(bs->pin) == HIGH);
    uint32_t now = millis();

    // Detect raw state change, restart debounce timer
    if (raw != bs->last_raw_state) {
        bs->last_raw_state = raw;
        bs->last_change_ms = now;
        return;
    }

    // Wait for debounce period
    if ((now - bs->last_change_ms) < DEBOUNCE_MS) {
        return;
    }

    // State is stable — check for transition
    if (raw == bs->stable_state) {
        // No transition; check for long press on momentary button
        if (!bs->is_latching && !bs->stable_state && !bs->long_sent) {
            if (bs->press_start_ms > 0 &&
                (now - bs->press_start_ms) >= LONG_PRESS_MS) {
                dispatch_event(bs->button_id, BUTTON_LONG_PRESS);
                bs->long_sent = true;
            }
        }
        return;
    }

    // Transition detected
    bool new_stable = raw;
    bs->stable_state = new_stable;

    if (bs->is_latching) {
        handle_latching_toggle(bs);
    } else {
        handle_momentary_press(bs, new_stable);
    }
}

static void handle_latching_toggle(ButtonState* bs)
{
    // Latching switch: both LOW->HIGH and HIGH->LOW are toggle events.
    // Each physical toggle sends a short press.
    dispatch_event(bs->button_id, BUTTON_SHORT_PRESS);
}

static void handle_momentary_press(ButtonState* bs, bool new_stable)
{
    if (!new_stable) {
        // HIGH -> LOW: button pressed
        bs->press_start_ms = millis();
        bs->long_sent      = false;
    } else {
        // LOW -> HIGH: button released
        if (!bs->long_sent && bs->press_start_ms > 0) {
            dispatch_event(bs->button_id, BUTTON_SHORT_PRESS);
        }
        bs->press_start_ms = 0;
        bs->long_sent      = false;
    }
}

static void dispatch_event(uint8_t button_id, uint8_t event_type)
{
    ButtonEvent ev;
    ev.button_id  = button_id;
    ev.event_type = event_type;

    // Fan-out: send to both queues, non-blocking
    xQueueSend(s_btn_session_q, &ev, 0);
    xQueueSend(s_btn_display_q, &ev, 0);
}
