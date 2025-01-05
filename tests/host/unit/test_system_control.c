/*
 * Unit Tests for System Control helpers.
 */

#include <stdio.h>
#include <stdint.h>

#include "src/system_control.h"
#include "src/input/input.h"

static int tests_passed = 0;
static int tests_failed = 0;

static uint8_t g_bootloader_flag_calls = 0;
uint8_t g_request_soft_reboot = 0;

void spi_flash_set_bootloader_mode_flag(void)
{
    g_bootloader_flag_calls++;
}

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s ", #name); \
    test_##name(); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

static void reset_state(void)
{
    g_request_soft_reboot = 0;
    g_bootloader_flag_calls = 0;
}

TEST(recovery_not_requested_without_combo)
{
    reset_state();
    request_bootloader_recovery(0);
    ASSERT_TRUE(g_request_soft_reboot == 0);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_not_requested_with_menu_only)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW);
    ASSERT_TRUE(g_request_soft_reboot == 0);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_not_requested_with_menu_gear_combo)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | BUTTON_GEAR_UP_MASK);
    ASSERT_TRUE(g_request_soft_reboot == 0);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_not_requested_with_power_only)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_POWER);
    ASSERT_TRUE(g_request_soft_reboot == 0);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_not_requested_with_power_gear_combo)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_POWER | BUTTON_GEAR_DOWN_MASK);
    ASSERT_TRUE(g_request_soft_reboot == 0);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_requested_on_menu_power_combo)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
    ASSERT_TRUE(g_request_soft_reboot == 1u);
    ASSERT_TRUE(g_bootloader_flag_calls == 1u);
}

TEST(recovery_does_not_repeat_when_pending)
{
    reset_state();
    g_request_soft_reboot = 1u;
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
    ASSERT_TRUE(g_request_soft_reboot == 1u);
    ASSERT_TRUE(g_bootloader_flag_calls == 0u);
}

TEST(recovery_overrides_app_reboot_request)
{
    reset_state();
    g_request_soft_reboot = 2u;
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
    ASSERT_TRUE(g_request_soft_reboot == 1u);
    ASSERT_TRUE(g_bootloader_flag_calls == 1u);
}

TEST(recovery_accepts_combo_with_extra_buttons)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER | BUTTON_GEAR_UP_MASK);
    ASSERT_TRUE(g_request_soft_reboot == 1u);
    ASSERT_TRUE(g_bootloader_flag_calls == 1u);
}

int main(void)
{
    printf("\nSystem Control Unit Tests\n");
    printf("=========================\n\n");

    RUN_TEST(recovery_not_requested_without_combo);
    RUN_TEST(recovery_not_requested_with_menu_only);
    RUN_TEST(recovery_not_requested_with_menu_gear_combo);
    RUN_TEST(recovery_not_requested_with_power_only);
    RUN_TEST(recovery_not_requested_with_power_gear_combo);
    RUN_TEST(recovery_requested_on_menu_power_combo);
    RUN_TEST(recovery_does_not_repeat_when_pending);
    RUN_TEST(recovery_overrides_app_reboot_request);
    RUN_TEST(recovery_accepts_combo_with_extra_buttons);

    printf("\n");
    printf("=========================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=========================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
