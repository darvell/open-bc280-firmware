#ifndef SIM_SHENGYI_FRAME_H
#define SIM_SHENGYI_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "shengyi.h"

size_t sim_shengyi_build_frame(uint8_t cmd, const uint8_t *payload, uint8_t payload_len,
                               uint8_t *out, size_t cap);

uint8_t sim_shengyi_batt_voltage_raw_from_mV(uint32_t batt_mV);
uint8_t sim_shengyi_current_raw_from_mA(uint32_t current_mA);
uint32_t sim_shengyi_current_mA_from_raw(uint8_t current_raw);
uint16_t sim_shengyi_speed_raw_from_kph_x10(uint16_t speed_kph_x10, uint16_t wheel_mm);
double sim_shengyi_speed_kph_x10_from_raw(uint16_t speed_raw, uint16_t wheel_mm);

uint8_t sim_shengyi_wheel_code_from_x10(uint16_t wheel_size_x10);
void sim_shengyi_wheel_from_code(uint8_t code, uint16_t *wheel_size_x10, uint16_t *wheel_circumference_mm);
uint16_t sim_shengyi_wheel_circumference_mm_from_code(uint8_t code);

#endif /* SIM_SHENGYI_FRAME_H */
