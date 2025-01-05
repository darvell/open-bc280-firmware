#ifndef SIM_SHENGYI_H
#define SIM_SHENGYI_H

#include <stdint.h>

typedef struct {
    /* environment/physics */
    double mass_kg;
    double crr;
    double cda;
    double grade;
    double wind_mps;

    /* drivetrain */
    double wheel_radius_m;
    double eff;
    uint8_t assist_level;

    /* state */
    uint32_t t_ms;
    double v_mps;
    double cadence_rpm;
    double rider_power_w;
    double motor_power_w;
    double batt_v;
    double batt_a;
    double temp_c;
    uint8_t soc_pct;
    uint16_t torque_raw;
    uint8_t err;
} sim_shengyi_t;

void sim_shengyi_init(sim_shengyi_t *s);
void sim_shengyi_step(sim_shengyi_t *s, uint32_t dt_ms);

uint16_t sim_shengyi_speed_dmph(const sim_shengyi_t *s);
uint16_t sim_shengyi_cadence_rpm(const sim_shengyi_t *s);
uint16_t sim_shengyi_power_w(const sim_shengyi_t *s);
int16_t sim_shengyi_batt_dV(const sim_shengyi_t *s);
int16_t sim_shengyi_batt_dA(const sim_shengyi_t *s);

#endif
