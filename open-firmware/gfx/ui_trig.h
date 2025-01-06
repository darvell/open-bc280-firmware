#ifndef OPEN_FIRMWARE_UI_TRIG_H
#define OPEN_FIRMWARE_UI_TRIG_H

#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
} ui_vec2_i16_t;

/*
 * Tiny trig helpers for UI vector rendering.
 *
 * Coordinate system:
 * - +X right
 * - +Y down
 * - degrees increase clockwise (CW)
 *
 * Returned vectors are unit-length in Q15 (~32767 == 1.0).
 */
int16_t ui_trig_sin_q15(int16_t deg_cw);
int16_t ui_trig_cos_q15(int16_t deg_cw);
ui_vec2_i16_t ui_trig_unit_deg_cw_q15(int16_t deg_cw);

#endif

