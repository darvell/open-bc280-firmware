#ifndef OPEN_FIRMWARE_APP_DATA_H
#define OPEN_FIRMWARE_APP_DATA_H

#include <stdint.h>

typedef struct motor_state_t {
    uint16_t rpm;
    uint16_t torque_raw;
    uint16_t speed_dmph; /* deci-mph */
    uint8_t  soc_pct;
    uint8_t  err;
    uint32_t last_ms;
} motor_state_t;

typedef struct debug_inputs_t {
    uint16_t speed_dmph;
    uint16_t cadence_rpm;
    uint16_t torque_raw;
    uint16_t power_w;      /* optional sample power for trip stats */
    int16_t  battery_dV;   /* battery voltage in 0.1 V */
    int16_t  battery_dA;   /* battery current in 0.1 A (signed) */
    int16_t  ctrl_temp_dC; /* controller temp in 0.1 C */
    uint8_t  throttle_pct;
    uint8_t  brake;
    uint8_t  buttons;
    uint32_t last_ms;
} debug_inputs_t;

typedef struct debug_outputs_t {
    uint8_t  assist_mode;
    uint8_t  profile_id;
    uint8_t  virtual_gear;
    uint8_t  cruise_state;
    uint16_t cmd_power_w;
    uint16_t cmd_current_dA;
    uint32_t last_ms;
} debug_outputs_t;

extern motor_state_t g_motor;
extern debug_inputs_t g_inputs;
extern debug_outputs_t g_outputs;

#endif

