#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "comm_proto.h"
#include "core.h"
#include "core/math_util.h"

static int g_failures = 0;

static void assert_eq_i32(int32_t got, int32_t want, const char *msg)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL: %s (got=%ld want=%ld)\n", msg, (long)got, (long)want);
        g_failures++;
    }
}

static void assert_eq_u16(uint16_t got, uint16_t want, const char *msg)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL: %s (got=%u want=%u)\n", msg, got, want);
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

static void test_fxp_helpers(void)
{
    assert_eq_i32(fxp_millivolts_to_decivolts(12345), 123, "mv->dV rounding");
    assert_eq_i32(fxp_milliamps_to_deciamperes(155), 2, "mA->dA rounding");
    assert_eq_i32(fxp_watts_from_mv_ma(50000, 1000), 50, "watts from mv/ma");

    /* 1.0 m/s (x1000) should be about 2.236 mph => ~22 deci-mph (rounded). */
    assert_eq_i32(fxp_mps1000_to_dmph(1000), 2237, "mps->deci-mph scaling");

    {
        fxp_point_t pts[] = {{0, 0}, {10, 100}, {20, 200}};
        assert_eq_i32(fxp_interp_linear(-5, pts, 3), 0, "interp clamp low");
        assert_eq_i32(fxp_interp_linear(25, pts, 3), 200, "interp clamp high");
        assert_eq_i32(fxp_interp_linear(5, pts, 3), 50, "interp mid");
    }
}

static void test_ringbuf_minmax(void)
{
    ringbuf_i16_t rb;
    int16_t storage[8];
    uint16_t min_idx[8];
    uint16_t max_idx[8];
    ringbuf_i16_summary_t s;

    memset(storage, 0, sizeof(storage));
    ringbuf_i16_init(&rb, storage, 8, min_idx, max_idx);
    ringbuf_i16_reset(&rb);

    for (int i = 1; i <= 8; ++i)
        ringbuf_i16_push(&rb, (int16_t)i);
    ringbuf_i16_summary(&rb, &s);
    assert_eq_u16(s.count, 8, "ringbuf count");
    assert_eq_u16(s.min, 1, "ringbuf min");
    assert_eq_u16(s.max, 8, "ringbuf max");
    assert_eq_u16(s.latest, 8, "ringbuf latest");

    ringbuf_i16_push(&rb, 9);
    ringbuf_i16_summary(&rb, &s);
    assert_eq_u16(s.count, 8, "ringbuf count after wrap");
    assert_eq_u16(s.min, 2, "ringbuf min after wrap");
    assert_eq_u16(s.max, 9, "ringbuf max after wrap");
    assert_eq_u16(s.latest, 9, "ringbuf latest after wrap");
}

static void test_comm_checksum(void)
{
    uint8_t frame1[] = {COMM_SOF, 0x01u, 0x00u};
    uint8_t want1 = 0xABu;
    assert_eq_u16(checksum(frame1, sizeof(frame1)), want1, "checksum empty payload");

    uint8_t frame2[] = {COMM_SOF, 0x10u, 0x02u, 0xAAu, 0x55u};
    uint8_t want2 = 0x47u;
    assert_eq_u16(checksum(frame2, sizeof(frame2)), want2, "checksum payload");
}

static void test_comm_state_frame_v1(void)
{
    comm_state_frame_t state = {
        .ms = 0x11223344u,
        .speed_dmph = 0x5566u,
        .cadence_rpm = 0x7788u,
        .power_w = 0x99AAu,
        .batt_dV = 0x1234,
        .batt_dA = 0x2345,
        .ctrl_temp_dC = 0x3456,
        .assist_mode = 0x5Au,
        .profile_id = 0x6Bu,
        .virtual_gear = 0x7Cu,
        .flags = 0x03u,
    };
    uint8_t out[COMM_STATE_FRAME_V1_LEN];
    uint8_t len = comm_state_frame_build_v1(out, (uint8_t)sizeof(out), &state);
    assert_eq_u16(len, COMM_STATE_FRAME_V1_LEN, "state frame len");
    assert_eq_u8(out[0], 1u, "state frame version");
    assert_eq_u8(out[1], COMM_STATE_FRAME_V1_LEN, "state frame payload size");
    assert_eq_u8(out[2], 0x11u, "state frame ms[31:24]");
    assert_eq_u8(out[5], 0x44u, "state frame ms[7:0]");
    assert_eq_u8(out[6], 0x55u, "state frame speed msb");
    assert_eq_u8(out[7], 0x66u, "state frame speed lsb");
    assert_eq_u8(out[10], 0x99u, "state frame power msb");
    assert_eq_u8(out[11], 0xAAu, "state frame power lsb");
    assert_eq_u8(out[12], 0x12u, "state frame batt_dV msb");
    assert_eq_u8(out[13], 0x34u, "state frame batt_dV lsb");
    assert_eq_u8(out[18], 0x5Au, "state frame assist");
    assert_eq_u8(out[19], 0x6Bu, "state frame profile");
    assert_eq_u8(out[20], 0x7Cu, "state frame vgear");
    assert_eq_u8(out[21], 0x03u, "state frame flags");
}

static void test_clamp_helpers(void)
{
    assert_eq_u16(clamp_q15(0u, 10u, 20u), 10u, "clamp_q15 low");
    assert_eq_u16(clamp_q15(25u, 10u, 20u), 20u, "clamp_q15 high");
    assert_eq_u16(clamp_q15(15u, 10u, 20u), 15u, "clamp_q15 mid");
}

int main(void)
{
    test_fxp_helpers();
    test_ringbuf_minmax();
    test_comm_checksum();
    test_comm_state_frame_v1();
    test_clamp_helpers();

    if (g_failures)
    {
        fprintf(stderr, "Host tests failed: %d\n", g_failures);
        return 1;
    }
    printf("Host tests PASS\n");
    return 0;
}
