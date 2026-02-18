#include <stdio.h>
#include <stdint.h>

#include "motor_stx02.h"

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

static void assert_eq_u16(uint16_t got, uint16_t want, const char *msg)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL: %s (got=%u want=%u)\n", msg, got, want);
        g_failures++;
    }
}

static void assert_eq_i16(int16_t got, int16_t want, const char *msg)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL: %s (got=%d want=%d)\n", msg, got, want);
        g_failures++;
    }
}

static uint8_t xor8(const uint8_t *p, size_t n)
{
    uint8_t x = 0;
    for (size_t i = 0; i < n; ++i)
        x ^= p[i];
    return x;
}

static void build_cmd1(uint8_t *frame,
                       uint8_t flags,
                       uint16_t raw_current_be,
                       uint16_t period_ms,
                       uint8_t soc)
{
    /* [0]=SOF, [1]=LEN, [2]=CMD, [3..12]=payload, [13]=XOR */
    frame[0] = 0x02u;
    frame[1] = 14u;
    frame[2] = 1u;
    frame[3] = flags;     /* p[0] */
    frame[4] = 0x00u;     /* p[1] reserved */
    frame[5] = (uint8_t)(raw_current_be >> 8); /* p[2] */
    frame[6] = (uint8_t)(raw_current_be & 0xFFu); /* p[3] */
    frame[7] = 0x00u;     /* p[4] reserved */
    frame[8] = (uint8_t)(period_ms >> 8); /* p[5] */
    frame[9] = (uint8_t)(period_ms & 0xFFu); /* p[6] */
    frame[10] = soc;      /* p[7] */
    frame[11] = 0x00u;    /* p[8] reserved */
    frame[12] = 0x00u;    /* p[9] reserved */
    frame[13] = xor8(frame, 13);
}

static void test_cmd1_current_scale_deciA(void)
{
    uint8_t frame[14];
    /* raw=0x4000|123 => 12.3A => 123 dA */
    build_cmd1(frame, 0x00u, (uint16_t)(0x4000u | 123u), 500u, 87u);

    motor_stx02_cmd1_t out = {0};
    int ok = motor_stx02_decode_cmd1(frame, sizeof(frame), &out);
    assert_true(ok, "decode cmd1 (deciA scale)");
    assert_eq_i16(out.current_dA, 123, "cmd1 current_dA (deciA scale)");
    assert_eq_u16(out.period_ms, 500u, "cmd1 period_ms");
    assert_eq_u8(out.err_code, 0u, "cmd1 err=0");
    assert_eq_u8(out.soc_valid, 1u, "cmd1 soc_valid");
    assert_eq_u8(out.soc_pct, 87u, "cmd1 soc_pct");
}

static void test_cmd1_current_scale_amp(void)
{
    uint8_t frame[14];
    /* raw=25A => 250 dA */
    build_cmd1(frame, 0x00u, 25u, 1234u, 100u);

    motor_stx02_cmd1_t out = {0};
    int ok = motor_stx02_decode_cmd1(frame, sizeof(frame), &out);
    assert_true(ok, "decode cmd1 (A scale)");
    assert_eq_i16(out.current_dA, 250, "cmd1 current_dA (A scale)");
    assert_eq_u8(out.soc_valid, 1u, "cmd1 soc_valid (100%)");
}

static void test_cmd1_err_priority(void)
{
    uint8_t frame[14];
    /* flags bit1 and bit3 set: OEM priority returns err=2. */
    build_cmd1(frame, (uint8_t)((1u << 1) | (1u << 3)), 0u, 0u, 0u);

    motor_stx02_cmd1_t out = {0};
    int ok = motor_stx02_decode_cmd1(frame, sizeof(frame), &out);
    assert_true(ok, "decode cmd1 (err priority)");
    assert_eq_u8(out.err_code, 2u, "cmd1 err priority");
}

static void test_cmd1_soc_invalid(void)
{
    uint8_t frame[14];
    build_cmd1(frame, 0x00u, 0u, 0u, 200u);

    motor_stx02_cmd1_t out = {0};
    int ok = motor_stx02_decode_cmd1(frame, sizeof(frame), &out);
    assert_true(ok, "decode cmd1 (soc invalid)");
    assert_eq_u8(out.soc_valid, 0u, "cmd1 soc_valid=0 for >100");
}

static void test_cmd1_len_guard(void)
{
    uint8_t frame[14];
    build_cmd1(frame, 0x00u, 0u, 0u, 0u);
    frame[1] = 15u; /* claims more bytes than provided */

    motor_stx02_cmd1_t out = {0};
    int ok = motor_stx02_decode_cmd1(frame, sizeof(frame), &out);
    assert_true(!ok, "decode rejects exp_len > provided len");
}

int main(void)
{
    test_cmd1_current_scale_deciA();
    test_cmd1_current_scale_amp();
    test_cmd1_err_priority();
    test_cmd1_soc_invalid();
    test_cmd1_len_guard();

    if (g_failures)
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}

