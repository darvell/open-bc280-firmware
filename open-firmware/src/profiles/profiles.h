#ifndef PROFILES_H
#define PROFILES_H

#include <stdint.h>

#include "src/config/config.h"
#include "src/core/core.h"

typedef struct {
    uint8_t  id;
    uint16_t cap_power_w;      /* commanded power clamp */
    uint16_t cap_current_dA;   /* commanded current clamp */
    uint16_t cap_speed_dmph;   /* optional speed cap (0 = none) */
} assist_profile_t;

typedef struct {
    uint8_t count;
    fxp_point_t pts[ASSIST_CURVE_MAX_POINTS];
} assist_curve_t;

typedef struct {
    assist_curve_t speed_curve;   /* x=speed_dmph, y=power_w */
    assist_curve_t cadence_curve; /* x=cadence_rpm, y=Q15 multiplier */
} assist_curve_profile_t;

extern const assist_profile_t g_profiles[PROFILE_COUNT];
extern const assist_curve_profile_t g_assist_curves[PROFILE_COUNT];

/* Active profile selection (defined in main.c). */
extern uint8_t g_active_profile_id;
int set_active_profile(uint8_t id, int persist);

#endif /* PROFILES_H */
