#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sim_shengyi.h"
#include "sim_shengyi_bus.h"
#include "sim_shengyi_frame.h"
#include "sim_mcu.h"

static int g_failures = 0;

static void assert_true(int cond, const char *msg)
{
    if (!cond)
    {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failures++;
    }
}


static void assert_eq_u8(uint8_t got, uint8_t want, const char *msg)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL: %s (got=%u want=%u)\n", msg, got, want);
        g_failures++;
    }
}

static void assert_eq_i32(int got, int want, const char *msg)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL: %s (got=%d want=%d)\n", msg, got, want);
        g_failures++;
    }
}

static void test_frame_0x52_decode(void)
{
    sim_shengyi_t ts;
    sim_shengyi_init(&ts);

    ts.v_mps = 5.0;
    ts.batt_v = 48.5;
    ts.batt_a = 6.0;
    ts.err = 34;
    ts.wheel_radius_m = 0.34;

    uint8_t frame[32];
    size_t len = sim_shengyi_build_frame_0x52(&ts, frame, sizeof(frame));
    assert_true(len >= 13, "0x52 frame length");

    double speed_kph_x10 = 0.0;
    int current_mA = 0;
    uint8_t batt_q = 0;
    uint8_t err = 0;
    int ok = sim_shengyi_decode_frame_0x52(frame, len, &ts, &speed_kph_x10, &current_mA, &batt_q, &err);
    assert_true(ok, "0x52 decode ok");

    assert_true(speed_kph_x10 >= 0.0, "0x52 speed non-negative");

    uint32_t current_mA_raw = (uint32_t)fmax(0.0, (double)sim_shengyi_batt_dA(&ts) * 100.0);
    uint8_t b1 = sim_shengyi_current_raw_from_mA(current_mA_raw);
    int expected_current = (int)sim_shengyi_current_mA_from_raw(b1);
    assert_eq_i32(current_mA, expected_current, "0x52 current scaling");

    uint8_t expected_batt = (uint8_t)((sim_shengyi_batt_dV(&ts) * 100) / 1000);
    assert_eq_u8(batt_q, expected_batt & 0x3F, "0x52 battery quantized");
    assert_eq_u8(err, ts.err, "0x52 error code");

    double wheel_mm = ts.wheel_radius_m * 2.0 * 3.14159 * 1000.0;
    uint16_t expected_speed_kph_x10 = (uint16_t)(ts.v_mps * 3.6 * 10.0 + 0.5);
    uint16_t expected_speed_raw = sim_shengyi_speed_raw_from_kph_x10(expected_speed_kph_x10,
                                                                      (uint16_t)(wheel_mm + 0.5));
    uint16_t speed_raw = (uint16_t)((uint16_t)frame[6] << 8) | frame[7];
    assert_eq_i32(speed_raw, expected_speed_raw, "0x52 speed raw encoding");
}

static void test_frame_0xC2_build(void)
{
    uint8_t frame[16];
    size_t len = sim_shengyi_build_frame_0xC2(frame, sizeof(frame));
    assert_true(len >= 8, "0xC2 frame length");
    assert_eq_u8(frame[0], SHENGYI_FRAME_START, "0xC2 SOF");
    assert_eq_u8(frame[1], SHENGYI_FRAME_SECOND, "0xC2 magic");
    assert_eq_u8(frame[2], 0xC2, "0xC2 cmd");
    assert_eq_u8(frame[3], 0, "0xC2 payload len");
}

static void test_frame_0xC3_roundtrip(void)
{
    sim_shengyi_c3_t in = {0};
    in.screen_brightness_level = 3;
    in.auto_poweroff_minutes = 10;
    in.batt_nominal_voltage_V = 48;
    in.config_profile_id = 2;
    in.lights_enabled = 1;
    in.max_assist_level = 5;
    in.gear_setting = 3;
    in.motor_enable_flag = 1;
    in.brake_flag = 0;
    in.speed_mode = 2;
    in.display_setting = 4;
    in.batt_voltage_threshold_mV = 42000;
    in.batt_current_limit_mA = 15000;
    in.speed_limit_kph_x10 = 250;
    in.wheel_size_x10 = 240;
    in.param_0281 = 9;
    in.motor_status_timeout_s = 3;
    in.param_027E = 7;
    in.units_mode = 1;
    in.flag_026F = 0;
    in.wheel_circumference_mm = 1914;
    in.param_0234 = 11;
    in.param_0270 = 12;
    in.param_0271 = 13;
    in.param_0267 = 14;
    in.param_0272 = 15;
    in.param_0273 = 16;
    in.param_0274 = 17;
    in.param_0275 = 18;
    in.param_0262 = 19;
    in.motor_current_mA_reported = 4321;
    in.motor_power_W_reported = 678;
    in.param_0235 = 22;
    in.param_021C = 0x1234;
    in.param_0238 = 0x4567;
    in.param_0230 = 0x89AB;
    in.param_023A = 33;
    in.param_023B = 34;
    in.param_023C = 35;

    uint8_t frame[96];
    size_t len = sim_shengyi_build_frame_0xC3(&in, frame, sizeof(frame));
    assert_true(len >= 55, "0xC3 frame length");
    assert_eq_u8(frame[2], 0xC3, "0xC3 cmd");
    assert_eq_u8(frame[3], 47, "0xC3 payload len");

    sim_shengyi_c3_t out = {0};
    int ok = sim_shengyi_decode_frame_0xC3(frame, len, &out);
    assert_true(ok, "0xC3 decode ok");
    assert_eq_u8(out.screen_brightness_level, in.screen_brightness_level, "0xC3 brightness");
    assert_eq_u8(out.auto_poweroff_minutes, in.auto_poweroff_minutes, "0xC3 auto-off");
    assert_eq_u8(out.batt_nominal_voltage_V, in.batt_nominal_voltage_V, "0xC3 nominal V");
    assert_eq_u8(out.max_assist_level, in.max_assist_level, "0xC3 max assist");
    assert_eq_u8(out.gear_setting, in.gear_setting, "0xC3 gear");
    assert_eq_u8(out.speed_mode, in.speed_mode, "0xC3 speed mode");
    assert_eq_u8(out.display_setting, in.display_setting, "0xC3 display setting");
    assert_eq_i32(out.batt_voltage_threshold_mV, in.batt_voltage_threshold_mV, "0xC3 batt threshold");
    assert_eq_i32(out.speed_limit_kph_x10, in.speed_limit_kph_x10, "0xC3 speed limit");
    assert_eq_i32(out.wheel_circumference_mm, in.wheel_circumference_mm, "0xC3 wheel circ");
    assert_eq_i32(out.motor_current_mA_reported, in.motor_current_mA_reported, "0xC3 current");
    assert_eq_i32(out.motor_power_W_reported, in.motor_power_W_reported, "0xC3 power");
    assert_eq_i32(out.param_021C, in.param_021C, "0xC3 param_021C");
    assert_eq_i32(out.param_0238, in.param_0238, "0xC3 param_0238");
    assert_eq_i32(out.param_0230, in.param_0230, "0xC3 param_0230");
    assert_eq_u8(out.param_023A, in.param_023A, "0xC3 param_023A");
    assert_eq_u8(out.param_023B, in.param_023B, "0xC3 param_023B");
    assert_eq_u8(out.param_023C, in.param_023C, "0xC3 param_023C");
}

static void test_frame_0x52_req_roundtrip(void)
{
    sim_shengyi_cmd52_req_t in = {0};
    in.assist_level_mapped = 7;
    in.headlight_enabled = 1;
    in.battery_low = 1;
    in.walk_assist_active = 0;
    in.speed_over_limit = 1;

    uint8_t frame[32];
    size_t len = sim_shengyi_build_frame_0x52_req(&in, frame, sizeof(frame));
    assert_true(len >= 10, "0x52 req frame length");

    sim_shengyi_cmd52_req_t out = {0};
    int ok = sim_shengyi_decode_frame_0x52_req(frame, len, &out);
    assert_true(ok, "0x52 req decode ok");
    assert_eq_u8(out.assist_level_mapped, in.assist_level_mapped, "0x52 req assist");
    assert_eq_u8(out.headlight_enabled, in.headlight_enabled, "0x52 req headlight");
    assert_eq_u8(out.battery_low, in.battery_low, "0x52 req batt low");
    assert_eq_u8(out.walk_assist_active, in.walk_assist_active, "0x52 req walk");
    assert_eq_u8(out.speed_over_limit, in.speed_over_limit, "0x52 req speed limit");
}

static void test_frame_0x53_decode(void)
{
    sim_shengyi_t ts;
    sim_shengyi_init(&ts);
    ts.assist_level = 3;

    uint8_t frame[32];
    size_t len = sim_shengyi_build_frame_0x53(&ts, frame, sizeof(frame));
    assert_true(len >= 14, "0x53 frame length");

    sim_shengyi_cmd53_t out = {0};
    int ok = sim_shengyi_decode_frame_0x53(frame, len, &out);
    assert_true(ok, "0x53 decode ok");
    assert_eq_u8(out.max_assist_level, 5, "0x53 max assist");
    assert_eq_u8(out.lights_enabled, 0, "0x53 lights");
    assert_eq_u8(out.gear_setting, ts.assist_level, "0x53 gear");
    assert_eq_u8(out.motor_enable_flag, 1, "0x53 motor enable");
    assert_eq_u8(out.brake_flag, 0, "0x53 brake");
    assert_eq_u8(out.speed_mode, 1, "0x53 speed mode");
    assert_eq_u8(out.display_setting, 1, "0x53 display setting");
    assert_eq_i32(out.batt_current_limit_mA, 15000, "0x53 current limit");
    assert_eq_i32(out.speed_limit_kph_x10, 250, "0x53 speed limit");
    assert_eq_u8(out.wheel_size_code, 4, "0x53 wheel size");
}

static void test_frame_0xC0_decode(void)
{
    uint8_t payload[56] = {0};
    payload[0] = 4;
    payload[1] = 8;
    payload[2] = 25;
    payload[3] = 12;
    payload[4] = 31;
    payload[5] = 23;
    payload[6] = 45;
    payload[7] = 48;
    payload[8] = 2;
    payload[9] = 1;
    payload[10] = 6;
    payload[11] = 3;
    payload[12] = 1;
    payload[13] = 0;
    payload[14] = 2;
    payload[15] = 4;
    payload[16] = 0xA4;
    payload[17] = 0x10;
    payload[18] = 15;
    payload[19] = 25;
    payload[20] = 4;
    payload[21] = 9;
    payload[22] = 6;
    payload[23] = 7;
    payload[24] = 1;
    payload[25] = 0;
    payload[26] = 0x07;
    payload[27] = 0x7A;
    payload[28] = 11;
    payload[29] = 12;
    payload[30] = 13;
    payload[31] = 14;
    payload[32] = 15;
    payload[33] = 16;
    payload[34] = 17;
    payload[35] = 18;
    payload[36] = 19;
    payload[37] = 0x12;
    payload[38] = 0x34;
    payload[39] = 0x00;
    payload[40] = 0x78;
    payload[41] = 55;
    payload[42] = 22;
    payload[43] = 0x01;
    payload[44] = 0x02;
    payload[45] = 0x03;
    payload[46] = 0x04;
    payload[47] = 0x05;
    payload[48] = 0x06;
    payload[49] = 0x33;
    payload[50] = 0x34;
    payload[51] = 0x35;

    uint8_t frame[80];
    size_t len = shengyi_frame_build(0xC0, payload, (uint8_t)sizeof(payload), frame, sizeof(frame));

    sim_shengyi_c0_t out = {0};
    int ok = sim_shengyi_decode_frame_0xC0(frame, len, &out);
    assert_true(ok, "0xC0 decode ok");
    assert_eq_u8(out.screen_brightness_level, payload[0], "0xC0 brightness");
    assert_eq_u8(out.auto_poweroff_minutes, payload[1], "0xC0 auto-off");
    assert_eq_u8(out.batt_nominal_voltage_V, payload[7], "0xC0 nominal V");
    assert_eq_u8(out.max_assist_level, payload[10], "0xC0 max assist");
    assert_eq_i32(out.batt_voltage_threshold_mV, 0xA410, "0xC0 batt threshold");
    assert_eq_i32(out.speed_limit_kph_x10, 250, "0xC0 speed limit");
    assert_eq_i32(out.wheel_circumference_mm, 0x077A, "0xC0 wheel circ");
    assert_eq_i32(out.motor_current_mA_reported, 0x1234, "0xC0 current");
    assert_eq_i32(out.motor_power_W_reported, 0x0078, "0xC0 power");
    assert_eq_u8(out.motor_temp_C, payload[41], "0xC0 temp");
    assert_eq_i32(out.param_021C, 0x0102, "0xC0 param_021C");
    assert_eq_i32(out.param_0238, 0x0304, "0xC0 param_0238");
    assert_eq_i32(out.param_0230, 0x0506, "0xC0 param_0230");
    assert_eq_u8(out.param_023A, payload[49], "0xC0 param_023A");
}

static void test_frame_misc_decodes(void)
{
    uint8_t frame[80];
    size_t len = 0;

    /* 0xAA */
    len = 0;
    uint8_t payload_aa[1] = {0x7E};
    len = shengyi_frame_build(0xAA, payload_aa, 1u, frame, sizeof(frame));
    sim_shengyi_cmdAA_t aa = {0};
    assert_true(sim_shengyi_decode_frame_0xAA(frame, len, &aa), "0xAA decode ok");
    assert_eq_u8(aa.display_mode_assist_raw, 0x7E, "0xAA value");

    /* 0xAB */
    len = 0;
    uint8_t payload_ab[2] = {1, 3};
    len = shengyi_frame_build(0xAB, payload_ab, 2u, frame, sizeof(frame));
    sim_shengyi_cmdAB_t ab = {0};
    assert_true(sim_shengyi_decode_frame_0xAB(frame, len, &ab), "0xAB decode ok");
    assert_eq_u8(ab.enable, 1, "0xAB enable");
    assert_eq_u8(ab.mode, 3, "0xAB mode");

    /* 0xAC */
    len = 0;
    uint8_t payload_ac[1] = {1};
    len = shengyi_frame_build(0xAC, payload_ac, 1u, frame, sizeof(frame));
    sim_shengyi_cmdAC_t ac = {0};
    assert_true(sim_shengyi_decode_frame_0xAC(frame, len, &ac), "0xAC decode ok");
    assert_eq_u8(ac.request_calibrate, 1, "0xAC flag");

    /* 0xA7 */
    len = 0;
    uint8_t payload_a7[6] = {2, 0x11, 0x22, 0x33, 0x44, 1};
    len = shengyi_frame_build(0xA7, payload_a7, 6u, frame, sizeof(frame));
    sim_shengyi_cmdA7_t a7 = {0};
    assert_true(sim_shengyi_decode_frame_0xA7(frame, len, &a7), "0xA7 decode ok");
    assert_eq_u8(a7.slot, 2, "0xA7 slot");
    assert_eq_u8(a7.data[3], 0x44, "0xA7 data");
    assert_eq_u8(a7.reinit_ble, 1, "0xA7 reinit");

    /* 0xA8 */
    len = 0;
    uint8_t payload_a8[5] = {3, 3, 0xAA, 0xBB, 0xCC};
    len = shengyi_frame_build(0xA8, payload_a8, 5u, frame, sizeof(frame));
    sim_shengyi_cmdA8_t a8 = {0};
    assert_true(sim_shengyi_decode_frame_0xA8(frame, len, &a8), "0xA8 decode ok");
    assert_eq_u8(a8.slot, 3, "0xA8 slot");
    assert_eq_u8(a8.data_len, 3, "0xA8 len");
    assert_eq_u8(a8.data[2], 0xCC, "0xA8 data");

    /* 0xA9 */
    len = 0;
    uint8_t payload_a9[1] = {4};
    len = shengyi_frame_build(0xA9, payload_a9, 1u, frame, sizeof(frame));
    sim_shengyi_cmdA9_t a9 = {0};
    assert_true(sim_shengyi_decode_frame_0xA9_req(frame, len, &a9), "0xA9 decode ok");
    assert_eq_u8(a9.slot, 4, "0xA9 slot");

    /* 0xB0 */
    sim_shengyi_cmdB0_t b0 = {{0}};
    for (uint8_t i = 0; i < 12; ++i)
        b0.bytes[i] = (uint8_t)(i + 1);
    len = sim_shengyi_build_frame_0xB0(&b0, frame, sizeof(frame));
    assert_true(len >= 18, "0xB0 frame length");
    sim_shengyi_cmdB0_t b0_out = {{0}};
    assert_true(sim_shengyi_decode_frame_0xB0(frame, len, &b0_out), "0xB0 decode ok");
    assert_eq_u8(b0_out.bytes[11], 12, "0xB0 payload");
}

static void test_status14_roundtrip(void)
{
    sim_shengyi_status14_t in = {0};
    in.frame_type = 1;
    in.frame_counter = 1;
    in.profile_type = 3;
    in.power_level = 12;
    in.status_flags = 0xA5;
    in.display_setting = 4;
    in.wheel_size_x10 = 240;
    in.batt_current_raw = 33;
    in.batt_voltage_raw = 44;
    in.controller_temp_raw = 55;
    in.speed_limit_kph = 25;
    in.batt_current_limit_a = 15;
    in.batt_voltage_threshold_div100 = 420;
    in.status2 = 9;

    uint8_t frame[32];
    size_t len = sim_shengyi_build_status14(&in, frame, sizeof(frame));
    assert_true(len == 20, "status14 length");

    sim_shengyi_status14_t out = {0};
    int ok = sim_shengyi_decode_status14(frame, len, &out);
    assert_true(ok, "status14 decode ok");
    assert_eq_u8(out.frame_type, in.frame_type, "status14 type");
    assert_eq_u8(out.profile_type, in.profile_type, "status14 profile");
    assert_eq_u8(out.power_level, in.power_level, "status14 power");
    assert_eq_u8(out.status_flags, in.status_flags, "status14 flags");
    assert_eq_u8(out.display_setting, in.display_setting, "status14 display");
    assert_eq_i32(out.wheel_size_x10, in.wheel_size_x10, "status14 wheel");
    assert_eq_u8(out.batt_current_raw, in.batt_current_raw, "status14 current");
    assert_eq_u8(out.batt_voltage_raw, in.batt_voltage_raw, "status14 voltage");
    assert_eq_u8(out.controller_temp_raw, in.controller_temp_raw, "status14 temp");
    assert_eq_u8(out.speed_limit_kph, in.speed_limit_kph, "status14 speed limit");
    assert_eq_u8(out.batt_current_limit_a, in.batt_current_limit_a, "status14 current limit");
    assert_eq_i32(out.batt_voltage_threshold_div100, in.batt_voltage_threshold_div100, "status14 batt threshold");
    assert_eq_u8(out.status2, in.status2, "status14 status2");
}

static void test_mcu_mmio(void)
{
    sim_mcu_t *m = sim_mcu_create();
    assert_true(m != NULL, "mcu create");
    if (!m)
        return;

    sim_mcu_write32(m, 0x4001380Cu, 0x2000);
    sim_mcu_write32(m, 0x40013804u, 0x55);
    uint8_t tx[8] = {0};
    size_t n = sim_mcu_uart_pop_tx(m, 0, tx, sizeof(tx));
    assert_true(n == 1, "mcu uart tx");
    assert_eq_u8(tx[0], 0x55, "mcu uart tx val");

    uint8_t rxv = 0xA5;
    sim_mcu_uart_push_rx(m, 1, &rxv, 1);
    uint32_t dr = sim_mcu_read32(m, 0x40004404u);
    assert_eq_i32(dr, 0xA5, "mcu uart rx");

    sim_mcu_gpio_set_input(m, 'B', 3, 1);
    uint32_t idr = sim_mcu_read32(m, 0x40010C08u);
    assert_true((idr & (1u << 3)) != 0, "mcu gpio idr");

    sim_mcu_adc_set_channel(m, 0, 1234);
    sim_mcu_write32(m, 0x40012408u, (1u << 22));
    uint32_t adc = sim_mcu_read32(m, 0x4001244Cu);
    assert_eq_i32(adc, 1234, "mcu adc");

    sim_mcu_write32(m, 0x40021024u, (1u << 24));
    uint32_t csr = sim_mcu_read32(m, 0x40021024u);
    assert_true((csr & 0xFE000000u) == 0, "mcu rcc csr clear");

    sim_mcu_destroy(m);
}

int main(void)
{
    test_frame_0x52_decode();
    test_frame_0x52_req_roundtrip();
    test_frame_0x53_decode();
    test_frame_0xC2_build();
    test_frame_0xC3_roundtrip();
    test_frame_0xC0_decode();
    test_frame_misc_decodes();
    test_status14_roundtrip();
    test_mcu_mmio();

    if (g_failures)
    {
        fprintf(stderr, "Shengyi DWG22 bus tests failed: %d\n", g_failures);
        return 1;
    }
    printf("Shengyi DWG22 bus tests PASS\n");
    return 0;
}
