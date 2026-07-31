#include "boot_button.h"

#include <stddef.h>

void boot_button_state_init(
    boot_button_state_t *state, bool pressed, uint32_t now_ms,
    uint32_t debounce_ms, uint32_t long_press_ms) {
    if (!state) return;
    *state = (boot_button_state_t){
        .debounce_ms = debounce_ms,
        .long_press_ms = long_press_ms,
        .raw_changed_ms = now_ms,
        .pressed_ms = now_ms,
        .raw_pressed = pressed,
        .stable_pressed = pressed,
    };
}

boot_button_event_t boot_button_state_update(
    boot_button_state_t *state, bool pressed, uint32_t now_ms) {
    if (!state) return BOOT_BUTTON_EVENT_NONE;

    if (pressed != state->raw_pressed) {
        state->raw_pressed = pressed;
        state->raw_changed_ms = now_ms;
    }

    if (state->stable_pressed != state->raw_pressed &&
        now_ms - state->raw_changed_ms >= state->debounce_ms) {
        state->stable_pressed = state->raw_pressed;
        if (state->stable_pressed) {
            state->pressed_ms = now_ms;
            state->long_press_sent = false;
        } else if (!state->long_press_sent) {
            return BOOT_BUTTON_EVENT_SHORT_PRESS;
        }
    }

    if (state->stable_pressed && state->raw_pressed &&
        !state->long_press_sent &&
        now_ms - state->pressed_ms >= state->long_press_ms) {
        state->long_press_sent = true;
        return BOOT_BUTTON_EVENT_LONG_PRESS;
    }
    return BOOT_BUTTON_EVENT_NONE;
}
