#include "sim_shengyi_frame.h"

#include <math.h>

typedef struct {
    uint16_t size_x10;
    uint16_t circumference_mm;
} sim_shengyi_wheel_info_t;

static const sim_shengyi_wheel_info_t k_sim_shengyi_wheels[] = {
    {160, 1276},
    {180, 1436},
    {200, 1595},
    {220, 1755},
    {240, 1914},
    {260, 2074},
    {275, 2193},
    {290, 2313},
};

size_t sim_shengyi_build_frame(uint8_t cmd, const uint8_t *payload, uint8_t payload_len,
                               uint8_t *out, size_t cap)
{
    return shengyi_frame_build(cmd, payload, payload_len, out, cap);
}

uint8_t sim_shengyi_batt_voltage_raw_from_mV(uint32_t batt_mV)
{
    uint32_t q = batt_mV / 1000u;
    if (q > 63u)
        q = 63u;
    return (uint8_t)q;
}

uint8_t sim_shengyi_current_raw_from_mA(uint32_t current_mA)
{
    double raw = ((double)current_mA * 3.0) / 99.9;
    raw = fmax(1.0, fmin(250.0, raw));
    return (uint8_t)(raw + 0.5);
}

uint32_t sim_shengyi_current_mA_from_raw(uint8_t current_raw)
{
    double current_mA = ((double)current_raw * 99.9) / 3.0;
    if (current_mA < 0.0)
        current_mA = 0.0;
    return (uint32_t)(current_mA + 0.5);
}

uint16_t sim_shengyi_speed_raw_from_kph_x10(uint16_t speed_kph_x10, uint16_t wheel_mm)
{
    if (speed_kph_x10 == 0 || wheel_mm == 0)
        return 0;
    double raw = ((double)wheel_mm * 36.0) / (double)speed_kph_x10;
    raw = fmax(1.0, fmin(3500.0, raw));
    return (uint16_t)(raw + 0.5);
}

double sim_shengyi_speed_kph_x10_from_raw(uint16_t speed_raw, uint16_t wheel_mm)
{
    if (speed_raw == 0 || wheel_mm == 0)
        return 0.0;
    return ((double)wheel_mm * 36.0) / (double)speed_raw;
}

uint8_t sim_shengyi_wheel_code_from_x10(uint16_t wheel_size_x10)
{
    for (uint8_t i = 0; i < (uint8_t)(sizeof(k_sim_shengyi_wheels) / sizeof(k_sim_shengyi_wheels[0])); ++i)
    {
        if (k_sim_shengyi_wheels[i].size_x10 == wheel_size_x10)
            return i;
    }
    return 0;
}

void sim_shengyi_wheel_from_code(uint8_t code, uint16_t *wheel_size_x10, uint16_t *wheel_circumference_mm)
{
    if (wheel_size_x10)
        *wheel_size_x10 = 0;
    if (wheel_circumference_mm)
        *wheel_circumference_mm = 0;
    if (code >= (uint8_t)(sizeof(k_sim_shengyi_wheels) / sizeof(k_sim_shengyi_wheels[0])))
        return;
    if (wheel_size_x10)
        *wheel_size_x10 = k_sim_shengyi_wheels[code].size_x10;
    if (wheel_circumference_mm)
        *wheel_circumference_mm = k_sim_shengyi_wheels[code].circumference_mm;
}

uint16_t sim_shengyi_wheel_circumference_mm_from_code(uint8_t code)
{
    if (code >= (uint8_t)(sizeof(k_sim_shengyi_wheels) / sizeof(k_sim_shengyi_wheels[0])))
        return 0;
    return k_sim_shengyi_wheels[code].circumference_mm;
}
