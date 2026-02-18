#include <stdio.h>
#include <stdint.h>

#include "battery_soc.h"

static int g_failures = 0;

static void assert_eq_u8(uint8_t got, uint8_t want, const char *msg)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL: %s (got=%u want=%u)\n", msg, got, want);
        g_failures++;
    }
}

static void test_fixed_points_48v(void)
{
    assert_eq_u8(battery_soc_pct_from_mv(53800u, 48u), 100u, "48V 53.8V -> 100%");
    assert_eq_u8(battery_soc_pct_from_mv(51400u, 48u), 90u, "48V 51.4V -> 90%");
    assert_eq_u8(battery_soc_pct_from_mv(50100u, 48u), 75u, "48V 50.1V -> 75%");
    assert_eq_u8(battery_soc_pct_from_mv(42000u, 48u), 0u, "48V 42.0V -> 0%");
}

static void test_fixed_points_36v(void)
{
    assert_eq_u8(battery_soc_pct_from_mv(40800u, 36u), 100u, "36V 40.8V -> 100%");
    assert_eq_u8(battery_soc_pct_from_mv(39500u, 36u), 90u, "36V 39.5V -> 90%");
    assert_eq_u8(battery_soc_pct_from_mv(31500u, 36u), 0u, "36V 31.5V -> 0%");
}

static void test_infer_nominal_curve(void)
{
    /* Inference selects 36V curve for mid-30V pack. */
    assert_eq_u8(battery_soc_pct_from_mv(40800u, 0u), 100u, "infer 40.8V -> 100%");
    assert_eq_u8(battery_soc_pct_from_mv(31500u, 0u), 0u, "infer 31.5V -> 0%");

    /* Inference selects 48V curve for >= 42V pack. */
    assert_eq_u8(battery_soc_pct_from_mv(53800u, 0u), 100u, "infer 53.8V -> 100%");
    assert_eq_u8(battery_soc_pct_from_mv(42000u, 0u), 0u, "infer 42.0V -> 0%");
}

int main(void)
{
    test_fixed_points_48v();
    test_fixed_points_36v();
    test_infer_nominal_curve();

    if (g_failures)
    {
        fprintf(stderr, "Host tests failed: %d\n", g_failures);
        return 1;
    }
    printf("Host tests PASS\n");
    return 0;
}

