/*
 * Unit Tests for Button FSM
 *
 * Tests button gesture recognition:
 *   - Short press detection (<800ms)
 *   - Long press detection (≥800ms)
 *   - Combo detection (multiple simultaneous buttons)
 *   - Hold-repeat timing (1200ms start, 200ms interval)
 *   - GPIO debouncing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/kernel/event.h"
#include "src/input/gpio_sampler.h"
#include "src/input/button_fsm.h"

static int tests_passed = 0;
static int tests_failed = 0;

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

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    Expected %d == %d\n    at %s:%d\n", \
               (int)(a), (int)(b), __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EVENT_TYPE(evt, expected_type) do { \
    ASSERT_EQ((evt).type, (expected_type)); \
} while(0)

/* ================================================================
 * GPIO Sampler Tests
 * ================================================================ */

/*
 * Test: Sampler initializes to zero
 */
TEST(sampler_init)
{
    gpio_sampler_t sampler;
    gpio_sampler_init(&sampler);

    ASSERT_EQ(sampler.stable, 0);
    ASSERT_EQ(sampler.index, 0);
}

/*
 * Test: Stable signal passes through immediately (after 4 samples)
 */
TEST(sampler_stable_signal)
{
    gpio_sampler_t sampler;
    gpio_sampler_init(&sampler);

    /* Feed same value 4 times */
    uint8_t result = 0;
    for (int i = 0; i < 4; i++) {
        result = gpio_sampler_tick(&sampler, 0x01);  /* UP button */
    }

    /* After 4 samples, should be stable */
    ASSERT_EQ(result, 0x01);
}

/*
 * Test: Glitch is filtered out (single bad sample)
 */
TEST(sampler_filters_glitch)
{
    gpio_sampler_t sampler;
    gpio_sampler_init(&sampler);

    /* Establish stable state (button pressed) */
    for (int i = 0; i < 4; i++) {
        gpio_sampler_tick(&sampler, 0x01);
    }

    /* Single glitch (button released) */
    gpio_sampler_tick(&sampler, 0x00);

    /* Button pressed again */
    for (int i = 0; i < 3; i++) {
        gpio_sampler_tick(&sampler, 0x01);
    }

    /* Should still read as pressed (3 out of 4 samples) */
    ASSERT_EQ(sampler.stable, 0x01);
}

/*
 * Test: Multi-bit debouncing works independently
 */
TEST(sampler_multi_bit)
{
    gpio_sampler_t sampler;
    gpio_sampler_init(&sampler);

    /* Press UP and DOWN together */
    for (int i = 0; i < 4; i++) {
        gpio_sampler_tick(&sampler, 0x03);  /* UP | DOWN */
    }

    ASSERT_EQ(sampler.stable, 0x03);
}

/* ================================================================
 * Button FSM Tests - Short Press
 * ================================================================ */

/*
 * Test: Short press detection (< 800ms)
 */
TEST(fsm_short_press)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    /* Press UP button */
    button_fsm_update(&fsm, BTN_MASK_UP, now);

    /* Hold for 500ms (less than threshold) */
    now += 500;
    button_fsm_update(&fsm, BTN_MASK_UP, now);

    /* Release */
    now += 10;
    button_fsm_update(&fsm, 0x00, now);

    /* Should get short press event */
    event_t evt;
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_SHORT_UP);

    /* No more events */
    ASSERT_TRUE(!button_fsm_poll_event(&fsm, &evt));
}

/*
 * Test: Short press for each button
 */
TEST(fsm_short_press_all_buttons)
{
    const struct {
        uint8_t mask;
        uint8_t expected_event;
    } buttons[] = {
        { BTN_MASK_UP,    EVT_BTN_SHORT_UP },
        { BTN_MASK_DOWN,  EVT_BTN_SHORT_DOWN },
        { BTN_MASK_MENU,  EVT_BTN_SHORT_MENU },
        { BTN_MASK_POWER, EVT_BTN_SHORT_POWER },
    };

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        button_fsm_t fsm;
        button_fsm_init(&fsm);

        uint32_t now = 1000;

        /* Press and release quickly */
        button_fsm_update(&fsm, buttons[i].mask, now);
        now += 100;
        button_fsm_update(&fsm, 0x00, now);

        event_t evt;
        ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
        ASSERT_EVENT_TYPE(evt, buttons[i].expected_event);
    }
}

/* ================================================================
 * Button FSM Tests - Long Press
 * ================================================================ */

/*
 * Test: Long press detection (≥ 800ms)
 */
TEST(fsm_long_press)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    /* Press UP button */
    button_fsm_update(&fsm, BTN_MASK_UP, now);

    /* Hold for 799ms (just before threshold) */
    now += 799;
    button_fsm_update(&fsm, BTN_MASK_UP, now);

    /* No event yet */
    event_t evt;
    ASSERT_TRUE(!button_fsm_poll_event(&fsm, &evt));

    /* Cross threshold */
    now += 1;  /* now at 800ms */
    button_fsm_update(&fsm, BTN_MASK_UP, now);

    /* Should get long press event */
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_LONG_UP);

    /* Release - should NOT generate short press */
    now += 100;
    button_fsm_update(&fsm, 0x00, now);

    ASSERT_TRUE(!button_fsm_poll_event(&fsm, &evt));
}

/*
 * Test: Long press for all buttons
 */
TEST(fsm_long_press_all_buttons)
{
    const struct {
        uint8_t mask;
        uint8_t expected_event;
    } buttons[] = {
        { BTN_MASK_UP,    EVT_BTN_LONG_UP },
        { BTN_MASK_DOWN,  EVT_BTN_LONG_DOWN },
        { BTN_MASK_MENU,  EVT_BTN_LONG_MENU },
        { BTN_MASK_POWER, EVT_BTN_LONG_POWER },
    };

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        button_fsm_t fsm;
        button_fsm_init(&fsm);

        uint32_t now = 1000;

        /* Press and hold past threshold */
        button_fsm_update(&fsm, buttons[i].mask, now);
        now += 800;
        button_fsm_update(&fsm, buttons[i].mask, now);

        event_t evt;
        ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
        ASSERT_EVENT_TYPE(evt, buttons[i].expected_event);
    }
}

/* ================================================================
 * Button FSM Tests - Combo Press
 * ================================================================ */

/*
 * Test: UP+DOWN combo detection (walk assist)
 */
TEST(fsm_combo_up_down)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    /* Press both UP and DOWN */
    button_fsm_update(&fsm, BTN_MASK_UP | BTN_MASK_DOWN, now);

    /* Hold past long threshold */
    now += 800;
    button_fsm_update(&fsm, BTN_MASK_UP | BTN_MASK_DOWN, now);

    /* Should get combo event, not individual long presses */
    event_t evt;
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_COMBO_UP_DOWN);

    /* No more events */
    ASSERT_TRUE(!button_fsm_poll_event(&fsm, &evt));
}

/*
 * Test: UP+MENU combo
 */
TEST(fsm_combo_up_menu)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    button_fsm_update(&fsm, BTN_MASK_UP | BTN_MASK_MENU, now);
    now += 800;
    button_fsm_update(&fsm, BTN_MASK_UP | BTN_MASK_MENU, now);

    event_t evt;
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_COMBO_UP_MENU);
}

/*
 * Test: DOWN+MENU combo
 */
TEST(fsm_combo_down_menu)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    button_fsm_update(&fsm, BTN_MASK_DOWN | BTN_MASK_MENU, now);
    now += 800;
    button_fsm_update(&fsm, BTN_MASK_DOWN | BTN_MASK_MENU, now);

    event_t evt;
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_COMBO_DOWN_MENU);
}

/*
 * Test: Short combo press (released before long threshold)
 */
TEST(fsm_combo_short)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    /* Press combo */
    button_fsm_update(&fsm, BTN_MASK_UP | BTN_MASK_DOWN, now);

    /* Release quickly (before long threshold) */
    now += 500;
    button_fsm_update(&fsm, 0x00, now);

    /* Should still get combo event on release */
    event_t evt;
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_COMBO_UP_DOWN);
}

/* ================================================================
 * Button FSM Tests - Hold Repeat
 * ================================================================ */

/*
 * Test: Repeat starts after 1200ms
 */
TEST(fsm_repeat_start_timing)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    /* Press UP */
    button_fsm_update(&fsm, BTN_MASK_UP, now);

    /* Long press at 800ms */
    now += 800;
    button_fsm_update(&fsm, BTN_MASK_UP, now);

    event_t evt;
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_LONG_UP);

    /* Just before repeat start (1199ms total) */
    now += 399;
    button_fsm_update(&fsm, BTN_MASK_UP, now);
    ASSERT_TRUE(!button_fsm_poll_event(&fsm, &evt));

    /* At repeat start (1200ms total) */
    now += 1;
    button_fsm_update(&fsm, BTN_MASK_UP, now);
    /* Repeat event generated */

    /* No event yet (repeat requires another update) */
    ASSERT_TRUE(!button_fsm_poll_event(&fsm, &evt));
}

/*
 * Test: Repeat interval is 200ms
 */
TEST(fsm_repeat_interval)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    /* Press and hold to start repeat */
    button_fsm_update(&fsm, BTN_MASK_UP, now);
    now += 1200;
    button_fsm_update(&fsm, BTN_MASK_UP, now);

    event_t evt;
    /* Drain long press event */
    button_fsm_poll_event(&fsm, &evt);

    /* First repeat at 1200ms */
    now += 200;
    button_fsm_update(&fsm, BTN_MASK_UP, now);
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_REPEAT_UP);

    /* Second repeat at 1400ms */
    now += 200;
    button_fsm_update(&fsm, BTN_MASK_UP, now);
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_REPEAT_UP);

    /* Third repeat at 1600ms */
    now += 200;
    button_fsm_update(&fsm, BTN_MASK_UP, now);
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &evt));
    ASSERT_EVENT_TYPE(evt, EVT_BTN_REPEAT_UP);
}

/*
 * Test: Only UP and DOWN support repeat
 */
TEST(fsm_repeat_only_up_down)
{
    button_fsm_t fsm;
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    /* Try with MENU (should not repeat) */
    button_fsm_update(&fsm, BTN_MASK_MENU, now);
    now += 1200;
    button_fsm_update(&fsm, BTN_MASK_MENU, now);

    event_t evt;
    button_fsm_poll_event(&fsm, &evt);  /* Drain long press */

    /* Wait past repeat interval */
    now += 400;
    button_fsm_update(&fsm, BTN_MASK_MENU, now);

    /* Should not generate repeat */
    ASSERT_TRUE(!button_fsm_poll_event(&fsm, &evt));
}

/* ================================================================
 * Integration Tests
 * ================================================================ */

/*
 * Test: Full pipeline - debounce → FSM
 */
TEST(integration_full_pipeline)
{
    gpio_sampler_t sampler;
    button_fsm_t fsm;

    gpio_sampler_init(&sampler);
    button_fsm_init(&fsm);

    uint32_t now = 1000;

    /* Simulate noisy button press (UP button, bit 0) */
    uint8_t debounced;

    /* 4 samples to establish pressed state */
    debounced = gpio_sampler_tick(&sampler, 0x01);  /* First sample */
    debounced = gpio_sampler_tick(&sampler, 0x01);
    debounced = gpio_sampler_tick(&sampler, 0x00);  /* Glitch */
    debounced = gpio_sampler_tick(&sampler, 0x01);

    /* Should still read as pressed (3 out of 4) */
    ASSERT_EQ(debounced, 0x01);

    /* Feed to FSM */
    button_fsm_update(&fsm, debounced, now);

    /* Hold for short press duration */
    now += 500;
    button_fsm_update(&fsm, debounced, now);

    /* Release */
    debounced = gpio_sampler_tick(&sampler, 0x00);
    debounced = gpio_sampler_tick(&sampler, 0x00);
    debounced = gpio_sampler_tick(&sampler, 0x00);
    debounced = gpio_sampler_tick(&sampler, 0x00);

    now += 20;
    button_fsm_update(&fsm, debounced, now);

    /* Get button event */
    event_t btn_evt;
    ASSERT_TRUE(button_fsm_poll_event(&fsm, &btn_evt));
    ASSERT_EVENT_TYPE(btn_evt, EVT_BTN_SHORT_UP);

}

/* ================================================================
 * Main Test Runner
 * ================================================================ */

int main(void)
{
    printf("\nButton FSM Unit Tests\n");
    printf("=====================\n\n");

    printf("GPIO Sampler Tests:\n");
    RUN_TEST(sampler_init);
    RUN_TEST(sampler_stable_signal);
    RUN_TEST(sampler_filters_glitch);
    RUN_TEST(sampler_multi_bit);

    printf("\nButton FSM - Short Press:\n");
    RUN_TEST(fsm_short_press);
    RUN_TEST(fsm_short_press_all_buttons);

    printf("\nButton FSM - Long Press:\n");
    RUN_TEST(fsm_long_press);
    RUN_TEST(fsm_long_press_all_buttons);

    printf("\nButton FSM - Combo Press:\n");
    RUN_TEST(fsm_combo_up_down);
    RUN_TEST(fsm_combo_up_menu);
    RUN_TEST(fsm_combo_down_menu);
    RUN_TEST(fsm_combo_short);

    printf("\nButton FSM - Hold Repeat:\n");
    RUN_TEST(fsm_repeat_start_timing);
    RUN_TEST(fsm_repeat_interval);
    RUN_TEST(fsm_repeat_only_up_down);

    printf("\nIntegration Tests:\n");
    RUN_TEST(integration_full_pipeline);

    printf("\n");
    printf("=====================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=====================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
