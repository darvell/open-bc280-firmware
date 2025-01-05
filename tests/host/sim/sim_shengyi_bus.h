#ifndef SIM_SHENGYI_BUS_H
#define SIM_SHENGYI_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "shengyi.h"
#include "sim_shengyi.h"
#include "sim_shengyi_frame.h"
size_t sim_shengyi_build_frame_0x52(const sim_shengyi_t *s, uint8_t *out, size_t cap);
int sim_shengyi_decode_frame_0x52(const uint8_t *buf, size_t len, const sim_shengyi_t *s,
                                    double *out_speed_kph_x10, int *out_current_mA,
                                    uint8_t *out_batt_v, uint8_t *out_err);
size_t sim_shengyi_build_frame_0x53(const sim_shengyi_t *s, uint8_t *out, size_t cap);

typedef struct {
    uint8_t assist_level_mapped;
    uint8_t headlight_enabled;
    uint8_t walk_assist_active;
    uint8_t battery_low;
    uint8_t speed_over_limit;
} sim_shengyi_cmd52_req_t;

typedef struct {
    uint8_t max_assist_level;
    uint8_t lights_enabled;
    uint8_t gear_setting;
    uint8_t motor_enable_flag;
    uint8_t brake_flag;
    uint8_t speed_mode;
    uint8_t display_setting;
    uint8_t batt_threshold_b3;
    uint8_t batt_threshold_b4;
    uint16_t batt_current_limit_mA;
    uint16_t speed_limit_kph_x10;
    uint8_t wheel_size_code;
} sim_shengyi_cmd53_t;

typedef struct {
    uint8_t display_mode_assist_raw;
} sim_shengyi_cmdAA_t;

typedef struct {
    uint8_t enable;
    uint8_t mode;
} sim_shengyi_cmdAB_t;

typedef struct {
    uint8_t request_calibrate;
} sim_shengyi_cmdAC_t;

typedef struct {
    uint8_t slot;
    uint8_t data_len;
    uint8_t data[64];
} sim_shengyi_cmdA8_t;

typedef struct {
    uint8_t slot;
    uint8_t data[4];
    uint8_t reinit_ble;
} sim_shengyi_cmdA7_t;

typedef struct {
    uint8_t slot;
} sim_shengyi_cmdA9_t;

typedef struct {
    uint8_t bytes[12];
} sim_shengyi_cmdB0_t;

typedef struct {
    uint8_t frame_type;
    uint8_t frame_counter;
    uint8_t profile_type;
    uint8_t power_level;
    uint8_t status_flags;
    uint8_t display_setting;
    uint16_t wheel_size_x10;
    uint8_t batt_current_raw;
    uint8_t batt_voltage_raw;
    uint8_t controller_temp_raw;
    uint8_t speed_limit_kph;
    uint8_t batt_current_limit_a;
    uint16_t batt_voltage_threshold_div100;
    uint8_t status2;
} sim_shengyi_status14_t;

typedef struct {
    uint8_t screen_brightness_level;
    uint8_t auto_poweroff_minutes;
    uint16_t datetime_year;
    uint8_t datetime_month;
    uint8_t datetime_day;
    uint8_t datetime_hour;
    uint8_t datetime_minute;
    uint8_t batt_nominal_voltage_V;
    uint8_t config_profile_id;
    uint8_t lights_enabled;
    uint8_t max_assist_level;
    uint8_t gear_setting;
    uint8_t motor_enable_flag;
    uint8_t brake_flag;
    uint8_t speed_mode;
    uint8_t display_setting;
    uint16_t batt_voltage_threshold_mV;
    uint16_t batt_current_limit_mA;
    uint16_t speed_limit_kph_x10;
    uint8_t wheel_size_code;
    uint16_t wheel_size_x10;
    uint16_t wheel_circumference_mm;
    uint8_t param_0281;
    uint32_t motor_status_timeout_ms;
    uint8_t param_027E;
    uint8_t units_mode;
    uint8_t flag_026F;
    uint8_t param_0234;
    uint8_t param_0270;
    uint8_t param_0271;
    uint8_t param_0267;
    uint8_t param_0272;
    uint8_t param_0273;
    uint8_t param_0274;
    uint8_t param_0275;
    uint8_t param_0262;
    uint16_t motor_current_mA_reported;
    uint16_t motor_power_W_reported;
    uint8_t motor_temp_C;
    uint8_t param_0235;
    uint16_t param_021C;
    uint16_t param_0238;
    uint16_t param_0230;
    uint8_t param_023A;
    uint8_t param_023B;
    uint8_t param_023C;
} sim_shengyi_c0_t;

typedef struct {
    uint8_t screen_brightness_level;
    uint8_t auto_poweroff_minutes;
    uint8_t batt_nominal_voltage_V;
    uint8_t config_profile_id;
    uint8_t lights_enabled;
    uint8_t max_assist_level;
    uint8_t gear_setting;
    uint8_t motor_enable_flag;
    uint8_t brake_flag;
    uint8_t speed_mode;
    uint8_t display_setting;
    uint16_t batt_voltage_threshold_mV;
    uint16_t batt_current_limit_mA;
    uint16_t speed_limit_kph_x10;
    uint16_t wheel_size_x10;
    uint8_t wheel_size_code;
    uint8_t param_0281;
    uint8_t motor_status_timeout_s;
    uint8_t param_027E;
    uint8_t units_mode;
    uint8_t flag_026F;
    uint16_t wheel_circumference_mm;
    uint8_t param_0234;
    uint8_t param_0270;
    uint8_t param_0271;
    uint8_t param_0267;
    uint8_t param_0272;
    uint8_t param_0273;
    uint8_t param_0274;
    uint8_t param_0275;
    uint8_t param_0262;
    uint16_t motor_current_mA_reported;
    uint16_t motor_power_W_reported;
    uint8_t param_0235;
    uint16_t param_021C;
    uint16_t param_0238;
    uint16_t param_0230;
    uint8_t param_023A;
    uint8_t param_023B;
    uint8_t param_023C;
} sim_shengyi_c3_t;

size_t sim_shengyi_build_frame_0x52_req(const sim_shengyi_cmd52_req_t *req, uint8_t *out, size_t cap);
int sim_shengyi_decode_frame_0x52_req(const uint8_t *buf, size_t len, sim_shengyi_cmd52_req_t *out);
int sim_shengyi_decode_frame_0x53(const uint8_t *buf, size_t len, sim_shengyi_cmd53_t *out);
int sim_shengyi_decode_frame_0xC0(const uint8_t *buf, size_t len, sim_shengyi_c0_t *out);
int sim_shengyi_decode_frame_0xA6_req(const uint8_t *buf, size_t len);
int sim_shengyi_decode_frame_0xA7(const uint8_t *buf, size_t len, sim_shengyi_cmdA7_t *out);
int sim_shengyi_decode_frame_0xA8(const uint8_t *buf, size_t len, sim_shengyi_cmdA8_t *out);
int sim_shengyi_decode_frame_0xA9_req(const uint8_t *buf, size_t len, sim_shengyi_cmdA9_t *out);
int sim_shengyi_decode_frame_0xAA(const uint8_t *buf, size_t len, sim_shengyi_cmdAA_t *out);
int sim_shengyi_decode_frame_0xAB(const uint8_t *buf, size_t len, sim_shengyi_cmdAB_t *out);
int sim_shengyi_decode_frame_0xAC(const uint8_t *buf, size_t len, sim_shengyi_cmdAC_t *out);
size_t sim_shengyi_build_frame_0xB0(const sim_shengyi_cmdB0_t *s, uint8_t *out, size_t cap);
int sim_shengyi_decode_frame_0xB0(const uint8_t *buf, size_t len, sim_shengyi_cmdB0_t *out);
size_t sim_shengyi_build_status14(const sim_shengyi_status14_t *s, uint8_t *out, size_t cap);
int sim_shengyi_decode_status14(const uint8_t *buf, size_t len, sim_shengyi_status14_t *out);
size_t sim_shengyi_build_frame_0xC2(uint8_t *out, size_t cap);
size_t sim_shengyi_build_frame_0xC3(const sim_shengyi_c3_t *s, uint8_t *out, size_t cap);
int sim_shengyi_decode_frame_0xC3(const uint8_t *buf, size_t len, sim_shengyi_c3_t *out);

#endif
