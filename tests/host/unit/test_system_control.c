/*
 * Unit Tests for System Control helpers.
 */

#include <stdio.h>
#include <stdint.h>

#include "src/system_control.h"
#include "src/app_state.h"
#include "src/input/input.h"

static int tests_passed = 0;
static int tests_failed = 0;

static uint8_t g_bootloader_flag_calls = 0;
reboot_request_t g_request_soft_reboot = REBOOT_REQUEST_NONE;
static uint8_t g_key_level = 0xFFu;
static uint8_t g_key_set_calls = 0u;

void spi_flash_set_bootloader_mode_flag(void)
{
    g_bootloader_flag_calls++;
}

void platform_key_output_set(uint8_t on)
{
    g_key_level = on ? 1u : 0u;
    g_key_set_calls++;
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
    g_request_soft_reboot = REBOOT_REQUEST_NONE;
    g_bootloader_flag_calls = 0;
    g_key_level = 0xFFu;
    g_key_set_calls = 0u;
}

TEST(recovery_not_requested_without_combo)
{
    reset_state();
    request_bootloader_recovery(0);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_NONE);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_not_requested_with_menu_only)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_NONE);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_not_requested_with_menu_gear_combo)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | BUTTON_GEAR_UP_MASK);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_NONE);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_not_requested_with_power_only)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_POWER);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_NONE);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_not_requested_with_power_gear_combo)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_POWER | BUTTON_GEAR_DOWN_MASK);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_NONE);
    ASSERT_TRUE(g_bootloader_flag_calls == 0);
}

TEST(recovery_requested_on_menu_power_combo)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_BOOTLOADER);
    ASSERT_TRUE(g_bootloader_flag_calls == 1u);
}

TEST(recovery_does_not_repeat_when_pending)
{
    reset_state();
    g_request_soft_reboot = REBOOT_REQUEST_BOOTLOADER;
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_BOOTLOADER);
    ASSERT_TRUE(g_bootloader_flag_calls == 0u);
}

TEST(recovery_overrides_app_reboot_request)
{
    reset_state();
    g_request_soft_reboot = REBOOT_REQUEST_APP;
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_BOOTLOADER);
    ASSERT_TRUE(g_bootloader_flag_calls == 1u);
}

TEST(recovery_accepts_combo_with_extra_buttons)
{
    reset_state();
    request_bootloader_recovery(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER | BUTTON_GEAR_UP_MASK);
    ASSERT_TRUE(g_request_soft_reboot == REBOOT_REQUEST_BOOTLOADER);
    ASSERT_TRUE(g_bootloader_flag_calls == 1u);
}

TEST(key_seq_boot_delayed_raise)
{
    reset_state();
    system_control_key_sequencer_init(100u);
    ASSERT_TRUE(g_key_level == 0u);
    ASSERT_TRUE(g_key_set_calls == 1u);

    system_control_key_sequencer_tick(109u, 0u, 0u);
    ASSERT_TRUE(g_key_level == 0u);
    ASSERT_TRUE(g_key_set_calls == 1u);

    system_control_key_sequencer_tick(110u, 0u, 0u);
    ASSERT_TRUE(g_key_level == 1u);
    ASSERT_TRUE(g_key_set_calls == 2u);
}

TEST(key_seq_no_fault_pulse)
{
    reset_state();
    system_control_key_sequencer_init(0u);
    system_control_key_sequencer_tick(10u, 0u, 0u);
    ASSERT_TRUE(g_key_level == 1u);

    /* Fault changes must not deassert PB1 in runtime. */
    uint8_t calls = g_key_set_calls;
    system_control_key_sequencer_tick(50u, 1u, 0u);
    ASSERT_TRUE(g_key_level == 1u);
    ASSERT_TRUE(g_key_set_calls == calls);

    system_control_key_sequencer_tick(70u, 1u, 0u);
    system_control_key_sequencer_tick(80u, 0u, 0u);
    ASSERT_TRUE(g_key_level == 1u);
    ASSERT_TRUE(g_key_set_calls == calls);
}

TEST(key_seq_ignores_transitions_while_rebooting)
{
    reset_state();
    system_control_key_sequencer_init(0u);
    system_control_key_sequencer_tick(10u, 0u, 0u);
    ASSERT_TRUE(g_key_level == 1u);

    uint8_t calls = g_key_set_calls;
    system_control_key_sequencer_tick(20u, 1u, 1u);
    ASSERT_TRUE(g_key_level == 1u);
    ASSERT_TRUE(g_key_set_calls == calls);
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
    RUN_TEST(key_seq_boot_delayed_raise);
    RUN_TEST(key_seq_no_fault_pulse);
    RUN_TEST(key_seq_ignores_transitions_while_rebooting);

    printf("\n");
    printf("=========================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=========================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
