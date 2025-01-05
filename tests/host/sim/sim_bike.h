#ifndef SIM_BIKE_H
#define SIM_BIKE_H

#include <stdint.h>

typedef struct {
    uint32_t t_ms;
    uint16_t speed_dmph;
    uint16_t rpm;
    uint16_t cadence_rpm;
    uint16_t torque_raw;
    uint16_t power_w;
    int16_t batt_dV;
    int16_t batt_dA;
    uint8_t soc_pct;
    uint8_t err;
} sim_bike_t;

void sim_bike_init(sim_bike_t *b);
void sim_bike_step(sim_bike_t *b, uint32_t dt_ms);

#endif
