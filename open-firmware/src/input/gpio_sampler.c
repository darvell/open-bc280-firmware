/*
 * GPIO Sampler Implementation
 *
 * Debounce filtering using majority voting over 4 samples.
 * Designed for 5ms tick rate (~20ms debounce window).
 */

#include "gpio_sampler.h"

/*
 * Initialize sampler state
 */
void gpio_sampler_init(gpio_sampler_t *sampler)
{
    for (uint8_t i = 0; i < 4; i++) {
        sampler->history[i] = 0;
    }
    sampler->index = 0;
    sampler->stable = 0;
}

/*
 * Process one GPIO sample with majority voting
 *
 * Each button bit is debounced independently. A bit is considered
 * stable if at least 3 of the last 4 samples agree.
 */
uint8_t gpio_sampler_tick(gpio_sampler_t *sampler, uint8_t raw_gpio)
{
    /* Store new sample */
    sampler->history[sampler->index] = raw_gpio & 0x0F;  /* Mask to 4 bits */
    sampler->index = (sampler->index + 1) & 0x03;  /* Wrap 0-3 */

    /* Majority vote per bit */
    uint8_t debounced = 0;

    for (uint8_t bit = 0; bit < 4; bit++) {
        uint8_t mask = (uint8_t)(1u << bit);
        uint8_t count = 0;

        /* Count how many samples have this bit set */
        for (uint8_t i = 0; i < 4; i++) {
            if (sampler->history[i] & mask) {
                count++;
            }
        }

        /* Require 3 out of 4 samples to agree */
        if (count >= 3) {
            debounced |= mask;
        }
    }

    sampler->stable = debounced;
    return debounced;
}
