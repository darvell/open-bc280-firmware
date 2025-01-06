#ifndef OEM_BUTTONS_H
#define OEM_BUTTONS_H

#include <stdint.h>

#include "input.h"

/* OEM button wiring bits (GPIOC IDR, active-low). */
#define OEM_BTN_UP       (1u << 0)
#define OEM_BTN_POWER    (1u << 1)
#define OEM_BTN_DOWN     (1u << 2)
#define OEM_BTN_MENU     (1u << 3)
#define OEM_BTN_LIGHT    (1u << 4)
#define OEM_BTN_VIRTUAL  (1u << 5)
#define OEM_BTN_MASK     0x1Fu
#define OEM_BTN_MASK_ALL 0x3Fu

/* Light button maps to the headlight toggle input. */
#define HEADLIGHT_BUTTON_MASK CRUISE_BUTTON_MASK

static inline uint8_t oem_buttons_map_raw(uint8_t raw, uint8_t *virtual_pressed)
{
    uint8_t pressed = (uint8_t)(~raw) & OEM_BTN_MASK_ALL;
    uint8_t virtual_on = (pressed & OEM_BTN_VIRTUAL) ? 1u : 0u;
    if (virtual_pressed)
        *virtual_pressed = virtual_on;
    pressed &= OEM_BTN_MASK;

    uint8_t out = 0;
    if (pressed & OEM_BTN_UP)
        out |= BUTTON_GEAR_UP_MASK;
    if (pressed & OEM_BTN_DOWN)
        out |= BUTTON_GEAR_DOWN_MASK;
    if ((pressed & OEM_BTN_UP) && (pressed & OEM_BTN_DOWN))
        out |= WALK_BUTTON_MASK;
    if (pressed & OEM_BTN_POWER)
        out |= UI_PAGE_BUTTON_POWER;
    if (pressed & OEM_BTN_MENU)
        out |= UI_PAGE_BUTTON_RAW;
    if (pressed & OEM_BTN_LIGHT)
        out |= HEADLIGHT_BUTTON_MASK;
    return out;
}

#endif /* OEM_BUTTONS_H */
