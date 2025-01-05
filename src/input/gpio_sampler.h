/*
 * GPIO Sampler - Debounce filtering for button inputs
 *
 * Runs in ISR context (TIM2 @ 5ms). Filters raw GPIO to stable button state.
 * Uses majority voting over 4 samples (~20ms) for reliable debouncing.
 *
 * Button mapping (bits 0-3):
 *   Bit 0: UP button
 *   Bit 1: DOWN button
 *   Bit 2: MENU button
 *   Bit 3: POWER button
 */

#ifndef INPUT_GPIO_SAMPLER_H
#define INPUT_GPIO_SAMPLER_H

#include <stdint.h>

/*
 * GPIO sampler state
 *
 * Maintains 4-sample history for debouncing.
 * Each bit tracks one button across time.
 */
typedef struct {
    uint8_t history[4];  /* Last 4 raw samples */
    uint8_t index;       /* Current write position (0-3) */
    uint8_t stable;      /* Last stable debounced state */
} gpio_sampler_t;

/*
 * Initialize sampler to idle state
 */
void gpio_sampler_init(gpio_sampler_t *sampler);

/*
 * Process one raw GPIO sample (ISR context)
 *
 * Called every 5ms from TIM2 ISR. Returns debounced button state
 * using majority voting (3 out of 4 samples must agree).
 *
 * Arguments:
 *   sampler - Sampler state
 *   raw_gpio - Raw button bits (0-3: UP, DOWN, MENU, POWER)
 *
 * Returns:
 *   Debounced button state (bits 0-3)
 */
uint8_t gpio_sampler_tick(gpio_sampler_t *sampler, uint8_t raw_gpio);

#endif /* INPUT_GPIO_SAMPLER_H */
