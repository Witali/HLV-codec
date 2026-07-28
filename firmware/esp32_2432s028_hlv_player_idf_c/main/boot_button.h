#ifndef HLV_BOOT_BUTTON_H
#define HLV_BOOT_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOOT_BUTTON_EVENT_NONE,
    BOOT_BUTTON_EVENT_SHORT_PRESS,
    BOOT_BUTTON_EVENT_LONG_PRESS,
} boot_button_event_t;

typedef struct {
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    uint32_t raw_changed_ms;
    uint32_t pressed_ms;
    bool raw_pressed;
    bool stable_pressed;
    bool long_press_sent;
} boot_button_state_t;

void boot_button_state_init(
    boot_button_state_t *state, bool pressed, uint32_t now_ms,
    uint32_t debounce_ms, uint32_t long_press_ms);

boot_button_event_t boot_button_state_update(
    boot_button_state_t *state, bool pressed, uint32_t now_ms);

#endif
