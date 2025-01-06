#include "sim_shengyi_bus.h"
#include "sim_shengyi_frame.h"
#include "util/byteorder.h"

#include <math.h>

static int decode_header(const uint8_t *buf, size_t len, uint8_t cmd, uint8_t payload_len_min,
                         const uint8_t **payload_out);

size_t sim_shengyi_build_frame_0x52(const sim_shengyi_t *s, uint8_t *out, size_t cap)
{
    if (!s)
        return 0;
    uint8_t payload[5];
    int batt_mv = sim_shengyi_batt_dV(s) * 100;
    uint8_t batt_q = sim_shengyi_batt_voltage_raw_from_mV((uint32_t)fmax(0.0, batt_mv));
    payload[0] = (uint8_t)(batt_q & 0x3Fu);
    uint32_t current_mA = (uint32_t)fmax(0.0, (double)sim_shengyi_batt_dA(s) * 100.0);
    uint8_t b1 = sim_shengyi_current_raw_from_mA(current_mA);
    payload[1] = b1;

    double wheel_mm_f = s->wheel_radius_m * 2.0 * 3.14159 * 1000.0;
    if (wheel_mm_f < 1.0)
        wheel_mm_f = 1.0;
    uint16_t wheel_mm = (uint16_t)(wheel_mm_f + 0.5);
    uint16_t speed_kph_x10 = (uint16_t)fmax(0.0, (s->v_mps * 3.6 * 10.0) + 0.5);
    uint16_t speed_raw = sim_shengyi_speed_raw_from_kph_x10(speed_kph_x10, wheel_mm);
    store_be16(&payload[2], speed_raw);
    payload[4] = s->err;

    return sim_shengyi_build_frame(0x52, payload, (uint8_t)sizeof(payload), out, cap);
}

int sim_shengyi_decode_frame_0x52(const uint8_t *buf, size_t len, const sim_shengyi_t *s,
                                    double *out_speed_kph_x10, int *out_current_mA,
                                    uint8_t *out_batt_v, uint8_t *out_err)
{
    const uint8_t *p = NULL;
    if (!s || !decode_header(buf, len, 0x52, 5, &p))
        return 0;
    if (buf[3] != 5)
        return 0;

    uint8_t b0 = p[0];
    uint8_t b1 = p[1];
    uint16_t speed_raw = load_be16(&p[2]);
    double current_mA = sim_shengyi_current_mA_from_raw(b1);
    double wheel_mm = s->wheel_radius_m * 2.0 * 3.14159 * 1000.0;
    double speed_kph_x10 = sim_shengyi_speed_kph_x10_from_raw(speed_raw, (uint16_t)(wheel_mm + 0.5));

    if (out_speed_kph_x10)
        *out_speed_kph_x10 = speed_kph_x10;
    if (out_current_mA)
        *out_current_mA = (int)current_mA;
    if (out_batt_v)
        *out_batt_v = (uint8_t)(b0 & 0x3Fu);
    if (out_err)
        *out_err = p[4];
    return 1;
}

size_t sim_shengyi_build_frame_0x53(const sim_shengyi_t *s, uint8_t *out, size_t cap)
{
    if (!s)
        return 0;
    uint8_t payload[7];
    uint8_t max_assist = 5;
    uint8_t lights_enabled = 0;
    uint8_t gear_setting = s->assist_level;
    uint8_t motor_enable = 1;
    uint8_t brake_flag = 0;
    uint8_t speed_mode = 1;
    uint8_t display_setting = 1;
    uint16_t batt_threshold_mV = 42000;
    uint16_t batt_current_limit_ma = 15000;
    uint16_t speed_limit_kph_x10 = 250;
    uint8_t wheel_size_code = 4;

    payload[0] = (uint8_t)(max_assist & 0x3Fu);
    if (!lights_enabled)
        payload[0] |= 0x40u;
    payload[1] = gear_setting;
    payload[2] = (uint8_t)(display_setting & 0x0Fu);
    payload[2] |= (uint8_t)((speed_mode & 0x03u) << 4);
    if (brake_flag)
        payload[2] |= 0x40u;
    if (motor_enable)
        payload[2] |= 0x80u;

    {
        int n420 = (int)(batt_threshold_mV / 100);
        uint8_t b3 = 0;
        uint8_t b4 = 0;
        if (n420 > 0x106)
        {
            if (n420 >= 0x170)
            {
                b4 = (uint8_t)((b4 & 0x3Fu) - 64);
                if (n420 >= 420)
                    b3 = (uint8_t)(n420 + 92);
                else
                {
                    b3 = (uint8_t)(-92 - n420);
                    b3 |= 0x80u;
                }
            }
            else
            {
                b4 = (uint8_t)((b4 & 0x3Fu) + 0x80u);
                if (n420 > 314)
                    b3 = (uint8_t)(n420 - 59);
                else
                {
                    b3 = (uint8_t)(59 - n420);
                    b3 |= 0x80u;
                }
            }
        }
        else
        {
            b4 = (uint8_t)((b4 & 0x3Fu) + 64);
            if (n420 >= 210)
                b3 = (uint8_t)(n420 + 46);
            else
            {
                b3 = (uint8_t)(-46 - n420);
                b3 |= 0x80u;
            }
        }
        b4 = (uint8_t)((b4 & 0xC0u) | (uint8_t)((2 * (batt_current_limit_ma / 1000)) & 0x3Fu));
        payload[3] = b3;
        payload[4] = b4;
    }

    payload[5] = 2;
    payload[6] = (uint8_t)(wheel_size_code & 0x07u);
    payload[6] |= (uint8_t)(8u * (uint8_t)(((speed_limit_kph_x10 / 10u) - 10u) & 0x1Fu));

    return sim_shengyi_build_frame(0x53, payload, (uint8_t)sizeof(payload), out, cap);
}

size_t sim_shengyi_build_frame_0x52_req(const sim_shengyi_cmd52_req_t *req, uint8_t *out, size_t cap)
{
    if (!req)
        return 0;
    return shengyi_build_frame_0x52_req(req->assist_level_mapped,
                                        req->headlight_enabled,
                                        req->walk_assist_active,
                                        req->speed_over_limit,
                                        out,
                                        cap);
}

int sim_shengyi_decode_frame_0x52_req(const uint8_t *buf, size_t len, sim_shengyi_cmd52_req_t *out)
{
    const uint8_t *p = NULL;
    if (!buf || !out || !decode_header(buf, len, 0x52, 2, &p))
        return 0;
    if (buf[3] != 2)
        return 0;
    out->assist_level_mapped = p[0];
    uint8_t flags = p[1];
    out->headlight_enabled = (flags >> 7) & 1u;
    out->walk_assist_active = (flags >> 5) & 1u;
    out->speed_over_limit = flags & 1u;
    return 1;
}

int sim_shengyi_decode_frame_0x53(const uint8_t *buf, size_t len, sim_shengyi_cmd53_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0x53, 7, &p))
        return 0;
    if (buf[3] != 7)
        return 0;
    out->max_assist_level = p[0] & 0x3Fu;
    out->lights_enabled = (p[0] & 0x40u) ? 0u : 1u;
    out->gear_setting = p[1];
    out->display_setting = p[2] & 0x0Fu;
    out->speed_mode = (p[2] >> 4) & 0x03u;
    out->brake_flag = (p[2] >> 6) & 1u;
    out->motor_enable_flag = (p[2] >> 7) & 1u;
    out->batt_threshold_b3 = p[3];
    out->batt_threshold_b4 = p[4];
    out->batt_current_limit_mA = ((uint16_t)(p[4] & 0x3Fu) / 2u) * 1000u;
    out->wheel_size_code = p[6] & 0x07u;
    out->speed_limit_kph_x10 = (uint16_t)(((p[6] >> 3) & 0x1Fu) + 10u) * 10u;
    return 1;
}

size_t sim_shengyi_build_frame_0xC2(uint8_t *out, size_t cap)
{
    return sim_shengyi_build_frame(0xC2, NULL, 0, out, cap);
}

size_t sim_shengyi_build_frame_0xC3(const sim_shengyi_c3_t *s, uint8_t *out, size_t cap)
{
    if (!s || !out || cap < 80)
        return 0;
    uint8_t payload[47];
    payload[0] = s->screen_brightness_level;
    payload[1] = s->auto_poweroff_minutes;
    payload[2] = s->batt_nominal_voltage_V;
    payload[3] = s->config_profile_id;
    payload[4] = s->lights_enabled;
    payload[5] = s->max_assist_level;
    payload[6] = s->gear_setting;
    payload[7] = s->motor_enable_flag;
    payload[8] = s->brake_flag;
    payload[9] = s->speed_mode;
    payload[10] = s->display_setting;
    store_be16(&payload[11], s->batt_voltage_threshold_mV);
    payload[13] = (uint8_t)(s->batt_current_limit_mA / 1000);
    payload[14] = (uint8_t)(s->speed_limit_kph_x10 / 10);
    payload[15] = sim_shengyi_wheel_code_from_x10(s->wheel_size_x10);
    payload[16] = s->param_0281;
    payload[17] = s->motor_status_timeout_s;
    payload[18] = s->param_027E;
    payload[19] = s->units_mode ? 1u : 0u;
    payload[20] = s->flag_026F ? 1u : 0u;
    store_be16(&payload[21], s->wheel_circumference_mm);
    payload[23] = s->param_0234;
    payload[24] = s->param_0270;
    payload[25] = s->param_0271;
    payload[26] = s->param_0267;
    payload[27] = s->param_0272;
    payload[28] = s->param_0273;
    payload[29] = s->param_0274;
    payload[30] = s->param_0275;
    payload[31] = s->param_0262;
    store_be16(&payload[32], s->motor_current_mA_reported);
    store_be16(&payload[34], s->motor_power_W_reported);
    payload[36] = 1;
    payload[37] = s->param_0235;
    store_be16(&payload[38], s->param_021C);
    store_be16(&payload[40], s->param_0238);
    store_be16(&payload[42], s->param_0230);
    payload[44] = s->param_023A;
    payload[45] = s->param_023B;
    payload[46] = s->param_023C;

    return sim_shengyi_build_frame(0xC3, payload, (uint8_t)sizeof(payload), out, cap);
}

int sim_shengyi_decode_frame_0xC3(const uint8_t *buf, size_t len, sim_shengyi_c3_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xC3, 47, &p))
        return 0;
    out->screen_brightness_level = p[0];
    out->auto_poweroff_minutes = p[1];
    out->batt_nominal_voltage_V = p[2];
    out->config_profile_id = p[3];
    out->lights_enabled = p[4];
    out->max_assist_level = p[5];
    out->gear_setting = p[6];
    out->motor_enable_flag = p[7];
    out->brake_flag = p[8];
    out->speed_mode = p[9];
    out->display_setting = p[10];
    out->batt_voltage_threshold_mV = load_be16(&p[11]);
    out->batt_current_limit_mA = (uint16_t)p[13] * 1000u;
    out->speed_limit_kph_x10 = (uint16_t)p[14] * 10u;
    out->wheel_size_code = p[15];
    sim_shengyi_wheel_from_code(p[15], &out->wheel_size_x10, &out->wheel_circumference_mm);
    out->param_0281 = p[16];
    out->motor_status_timeout_s = p[17];
    out->param_027E = p[18];
    out->units_mode = p[19];
    out->flag_026F = p[20];
    out->wheel_circumference_mm = load_be16(&p[21]);
    out->param_0234 = p[23];
    out->param_0270 = p[24];
    out->param_0271 = p[25];
    out->param_0267 = p[26];
    out->param_0272 = p[27];
    out->param_0273 = p[28];
    out->param_0274 = p[29];
    out->param_0275 = p[30];
    out->param_0262 = p[31];
    out->motor_current_mA_reported = load_be16(&p[32]);
    out->motor_power_W_reported = load_be16(&p[34]);
    out->param_0235 = p[37];
    out->param_021C = load_be16(&p[38]);
    out->param_0238 = load_be16(&p[40]);
    out->param_0230 = load_be16(&p[42]);
    out->param_023A = p[44];
    out->param_023B = p[45];
    out->param_023C = p[46];
    return 1;
}

int sim_shengyi_decode_frame_0xC0(const uint8_t *buf, size_t len, sim_shengyi_c0_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xC0, 56, &p))
        return 0;
    out->screen_brightness_level = p[0];
    out->auto_poweroff_minutes = p[1];
    out->datetime_year = (uint16_t)(p[2] + 2000u);
    out->datetime_month = p[3];
    out->datetime_day = p[4];
    out->datetime_hour = p[5];
    out->datetime_minute = p[6];
    out->batt_nominal_voltage_V = p[7];
    out->config_profile_id = p[8];
    out->lights_enabled = p[9];
    out->max_assist_level = p[10];
    out->gear_setting = p[11];
    out->motor_enable_flag = p[12];
    out->brake_flag = p[13];
    out->speed_mode = p[14];
    out->display_setting = p[15];
    out->batt_voltage_threshold_mV = load_be16(&p[16]);
    out->batt_current_limit_mA = (uint16_t)p[18] * 1000u;
    out->speed_limit_kph_x10 = (uint16_t)p[19] * 10u;
    out->wheel_size_code = p[20];
    sim_shengyi_wheel_from_code(p[20], &out->wheel_size_x10, &out->wheel_circumference_mm);
    out->param_0281 = p[21];
    out->motor_status_timeout_ms = (p[22] >= 5u) ? (uint32_t)p[22] * 1000u : 0u;
    out->param_027E = p[23];
    out->units_mode = p[24] != 0;
    out->flag_026F = p[25] != 0;
    out->wheel_circumference_mm = load_be16(&p[26]);
    out->param_0234 = p[28];
    out->param_0270 = p[29];
    out->param_0271 = p[30];
    out->param_0267 = p[31];
    out->param_0272 = p[32];
    out->param_0273 = p[33];
    out->param_0274 = p[34];
    out->param_0275 = p[35];
    out->param_0262 = p[36];
    out->motor_current_mA_reported = load_be16(&p[37]);
    out->motor_power_W_reported = load_be16(&p[39]);
    out->motor_temp_C = p[41];
    out->param_0235 = p[42];
    out->param_021C = load_be16(&p[43]);
    out->param_0238 = load_be16(&p[45]);
    out->param_0230 = load_be16(&p[47]);
    out->param_023A = p[49];
    out->param_023B = p[50];
    out->param_023C = p[51];
    return 1;
}

static int decode_header(const uint8_t *buf, size_t len, uint8_t cmd, uint8_t payload_len_min,
                         const uint8_t **payload_out)
{
    return shengyi_frame_validate(buf, len, cmd, payload_len_min, payload_out);
}

int sim_shengyi_decode_frame_0xA6_req(const uint8_t *buf, size_t len)
{
    return decode_header(buf, len, 0xA6, 0, NULL);
}

int sim_shengyi_decode_frame_0xA7(const uint8_t *buf, size_t len, sim_shengyi_cmdA7_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xA7, 6, &p))
        return 0;
    out->slot = p[0];
    out->data[0] = p[1];
    out->data[1] = p[2];
    out->data[2] = p[3];
    out->data[3] = p[4];
    out->reinit_ble = p[5];
    return 1;
}

int sim_shengyi_decode_frame_0xA8(const uint8_t *buf, size_t len, sim_shengyi_cmdA8_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xA8, 2, &p))
        return 0;
    out->slot = p[0];
    out->data_len = p[1];
    if (out->data_len > 64)
        return 0;
    if ((size_t)(2u + out->data_len) > (size_t)buf[3])
        return 0;
    for (uint8_t i = 0; i < out->data_len; ++i)
        out->data[i] = p[2 + i];
    return 1;
}

int sim_shengyi_decode_frame_0xA9_req(const uint8_t *buf, size_t len, sim_shengyi_cmdA9_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xA9, 1, &p))
        return 0;
    out->slot = p[0];
    return 1;
}

int sim_shengyi_decode_frame_0xAA(const uint8_t *buf, size_t len, sim_shengyi_cmdAA_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xAA, 1, &p))
        return 0;
    out->display_mode_assist_raw = p[0];
    return 1;
}

int sim_shengyi_decode_frame_0xAB(const uint8_t *buf, size_t len, sim_shengyi_cmdAB_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xAB, 1, &p))
        return 0;
    out->enable = p[0] != 0;
    out->mode = (buf[3] >= 2) ? p[1] : 0;
    return 1;
}

int sim_shengyi_decode_frame_0xAC(const uint8_t *buf, size_t len, sim_shengyi_cmdAC_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xAC, 1, &p))
        return 0;
    out->request_calibrate = p[0];
    return 1;
}

size_t sim_shengyi_build_frame_0xB0(const sim_shengyi_cmdB0_t *s, uint8_t *out, size_t cap)
{
    if (!s)
        return 0;
    return sim_shengyi_build_frame(0xB0, s->bytes, 12, out, cap);
}

int sim_shengyi_decode_frame_0xB0(const uint8_t *buf, size_t len, sim_shengyi_cmdB0_t *out)
{
    const uint8_t *p = NULL;
    if (!out || !decode_header(buf, len, 0xB0, 12, &p))
        return 0;
    for (size_t i = 0; i < 12; ++i)
        out->bytes[i] = p[i];
    return 1;
}

static uint8_t checksum_xor(const uint8_t *buf, size_t len)
{
    uint8_t x = 0;
    for (size_t i = 0; i < len; ++i)
        x ^= buf[i];
    return x;
}

size_t sim_shengyi_build_status14(const sim_shengyi_status14_t *s, uint8_t *out, size_t cap)
{
    if (!s || !out || cap < 24)
        return 0;
    uint8_t data[19];
    data[0] = s->frame_type;
    data[1] = 0x14;
    data[2] = s->frame_counter;
    data[3] = s->profile_type;
    data[4] = s->power_level;
    data[5] = s->status_flags;
    data[6] = s->display_setting;
    store_be16(&data[7], s->wheel_size_x10);
    data[9] = s->batt_current_raw;
    data[10] = s->batt_voltage_raw;
    data[11] = s->controller_temp_raw;
    data[12] = s->speed_limit_kph;
    data[13] = s->batt_current_limit_a;
    store_be16(&data[14], s->batt_voltage_threshold_div100);
    data[16] = 0;
    data[17] = 0;
    data[18] = s->status2;
    uint8_t cks = checksum_xor(data, sizeof(data));
    size_t len = 0;
    for (size_t i = 0; i < sizeof(data); ++i)
        out[len++] = data[i];
    out[len++] = cks;
    return len;
}

int sim_shengyi_decode_status14(const uint8_t *buf, size_t len, sim_shengyi_status14_t *out)
{
    if (!buf || !out || len < 20)
        return 0;
    if (buf[1] != 0x14)
        return 0;
    uint8_t expect = checksum_xor(buf, 19);
    if (buf[19] != expect)
        return 0;
    out->frame_type = buf[0];
    out->frame_counter = buf[2];
    out->profile_type = buf[3];
    out->power_level = buf[4];
    out->status_flags = buf[5];
    out->display_setting = buf[6];
    out->wheel_size_x10 = load_be16(&buf[7]);
    out->batt_current_raw = buf[9];
    out->batt_voltage_raw = buf[10];
    out->controller_temp_raw = buf[11];
    out->speed_limit_kph = buf[12];
    out->batt_current_limit_a = buf[13];
    out->batt_voltage_threshold_div100 = load_be16(&buf[14]);
    out->status2 = buf[18];
    return 1;
}
