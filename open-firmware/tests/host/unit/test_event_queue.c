/*
 * Unit Tests for Lock-Free SPSC Event Queue
 *
 * Tests cover:
 *   - Basic push/pop operations
 *   - Empty/full conditions
 *   - Wrap-around behavior
 *   - Drain functionality
 *   - Event creation helpers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/kernel/event.h"
#include "src/kernel/event_queue.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-40s ", #name); \
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

/*
 * Test: Queue starts empty
 */
TEST(queue_init_empty)
{
    event_queue_t q;
    event_queue_init(&q);

    ASSERT_TRUE(event_queue_empty(&q));
    ASSERT_TRUE(!event_queue_full(&q));
    ASSERT_EQ(event_queue_count(&q), 0);
}

/*
 * Test: Single push and pop
 */
TEST(single_push_pop)
{
    event_queue_t q;
    event_queue_init(&q);

    event_t evt_in = event_simple(EVT_BTN_SHORT_UP, 1000);
    ASSERT_TRUE(event_queue_push(&q, &evt_in));

    ASSERT_TRUE(!event_queue_empty(&q));
    ASSERT_EQ(event_queue_count(&q), 1);

    event_t evt_out;
    ASSERT_TRUE(event_queue_pop(&q, &evt_out));

    ASSERT_EQ(evt_out.type, EVT_BTN_SHORT_UP);
    ASSERT_EQ(evt_out.timestamp, 1000);
    ASSERT_TRUE(event_queue_empty(&q));
}

/*
 * Test: Pop from empty queue returns false
 */
TEST(pop_empty_fails)
{
    event_queue_t q;
    event_queue_init(&q);

    event_t evt;
    ASSERT_TRUE(!event_queue_pop(&q, &evt));
}

/*
 * Test: Fill queue to capacity
 */
TEST(fill_to_capacity)
{
    event_queue_t q;
    event_queue_init(&q);

    /* Queue holds CAPACITY-1 elements (one slot wasted for full detection) */
    for (uint16_t i = 0; i < EVENT_QUEUE_CAPACITY - 1; i++) {
        event_t evt = event_create(EVT_BTN_SHORT_UP, i, i * 100);
        ASSERT_TRUE(event_queue_push(&q, &evt));
    }

    ASSERT_TRUE(event_queue_full(&q));
    ASSERT_EQ(event_queue_count(&q), EVENT_QUEUE_CAPACITY - 1);

    /* One more push should fail */
    event_t evt_extra = event_simple(EVT_BTN_SHORT_DOWN, 9999);
    ASSERT_TRUE(!event_queue_push(&q, &evt_extra));
}

/*
 * Test: FIFO ordering
 */
TEST(fifo_order)
{
    event_queue_t q;
    event_queue_init(&q);

    /* Push 5 events */
    for (uint16_t i = 0; i < 5; i++) {
        event_t evt = event_create(EVT_BTN_SHORT_UP + i, i, i * 100);
        ASSERT_TRUE(event_queue_push(&q, &evt));
    }

    /* Pop and verify order */
    for (uint16_t i = 0; i < 5; i++) {
        event_t evt;
        ASSERT_TRUE(event_queue_pop(&q, &evt));
        ASSERT_EQ(evt.type, EVT_BTN_SHORT_UP + i);
        ASSERT_EQ(evt.payload16, i);
        ASSERT_EQ(evt.timestamp, i * 100);
    }

    ASSERT_TRUE(event_queue_empty(&q));
}

/*
 * Test: Wrap-around behavior
 */
TEST(wrap_around)
{
    event_queue_t q;
    event_queue_init(&q);

    /* Fill and drain multiple times to exercise wrap-around */
    for (int cycle = 0; cycle < 3; cycle++) {
        /* Fill half the queue */
        for (uint16_t i = 0; i < EVENT_QUEUE_CAPACITY / 2; i++) {
            event_t evt = event_create(EVT_MOTOR_STATE, (uint16_t)(cycle * 100 + i), 0);
            ASSERT_TRUE(event_queue_push(&q, &evt));
        }

        /* Drain half */
        for (uint16_t i = 0; i < EVENT_QUEUE_CAPACITY / 2; i++) {
            event_t evt;
            ASSERT_TRUE(event_queue_pop(&q, &evt));
            ASSERT_EQ(evt.payload16, cycle * 100 + i);
        }
    }

    ASSERT_TRUE(event_queue_empty(&q));
}

/*
 * Test: Interleaved push/pop
 */
TEST(interleaved_push_pop)
{
    event_queue_t q;
    event_queue_init(&q);

    uint16_t push_count = 0;
    uint16_t pop_count = 0;

    /* Simulate producer/consumer with different rates */
    for (int i = 0; i < 100; i++) {
        /* Push 3 */
        for (int j = 0; j < 3; j++) {
            event_t evt = event_create(EVT_BTN_SHORT_UP, push_count++, 0);
            if (event_queue_push(&q, &evt)) {
                /* OK */
            } else {
                push_count--;  /* Queue was full */
            }
        }

        /* Pop 2 */
        for (int j = 0; j < 2; j++) {
            event_t evt;
            if (event_queue_pop(&q, &evt)) {
                ASSERT_EQ(evt.payload16, pop_count);
                pop_count++;
            }
        }
    }

    /* Drain remaining */
    event_t evt;
    while (event_queue_pop(&q, &evt)) {
        ASSERT_EQ(evt.payload16, pop_count);
        pop_count++;
    }

    ASSERT_EQ(push_count, pop_count);
}

/*
 * Test: Event category macros
 */
TEST(event_categories)
{
    event_t btn_evt = event_simple(EVT_BTN_SHORT_UP, 0);
    ASSERT_TRUE(EVENT_IS_BUTTON(btn_evt));
    ASSERT_TRUE(!EVENT_IS_MOTOR(btn_evt));
    ASSERT_TRUE(!EVENT_IS_CONTROL(btn_evt));
    ASSERT_TRUE(!EVENT_IS_UI(btn_evt));
    ASSERT_EQ(EVENT_CATEGORY(btn_evt), EVT_CAT_BUTTON);

    event_t motor_evt = event_simple(EVT_MOTOR_STATE, 0);
    ASSERT_TRUE(EVENT_IS_MOTOR(motor_evt));
    ASSERT_TRUE(!EVENT_IS_BUTTON(motor_evt));
    ASSERT_EQ(EVENT_CATEGORY(motor_evt), EVT_CAT_MOTOR);

    event_t ctrl_evt = event_simple(CMD_CTRL_GEAR_UP, 0);
    ASSERT_TRUE(EVENT_IS_CONTROL(ctrl_evt));
    ASSERT_EQ(EVENT_CATEGORY(ctrl_evt), EVT_CAT_CONTROL);

    event_t ui_evt = event_simple(CMD_UI_PAGE_NEXT, 0);
    ASSERT_TRUE(EVENT_IS_UI(ui_evt));
    ASSERT_EQ(EVENT_CATEGORY(ui_evt), EVT_CAT_UI);
}

/*
 * Test: Drain function
 */
static uint16_t drain_count;
static uint16_t drain_sum;

static void drain_handler(const event_t *evt, void *ctx)
{
    (void)ctx;
    drain_count++;
    drain_sum += evt->payload16;
}

TEST(drain_all)
{
    event_queue_t q;
    event_queue_init(&q);

    /* Push 10 events with payload 0-9 */
    for (uint16_t i = 0; i < 10; i++) {
        event_t evt = event_create(EVT_BTN_SHORT_UP, i, 0);
        ASSERT_TRUE(event_queue_push(&q, &evt));
    }

    drain_count = 0;
    drain_sum = 0;

    uint16_t drained = event_queue_drain(&q, drain_handler, NULL);

    ASSERT_EQ(drained, 10);
    ASSERT_EQ(drain_count, 10);
    ASSERT_EQ(drain_sum, 45);  /* 0+1+2+...+9 = 45 */
    ASSERT_TRUE(event_queue_empty(&q));
}

/*
 * Test: Event struct size
 */
TEST(event_size)
{
    ASSERT_EQ(sizeof(event_t), 8);
}

/*
 * Test: Queue capacity
 */
TEST(queue_capacity)
{
    /* Verify capacity is power of 2 */
    ASSERT_EQ(EVENT_QUEUE_CAPACITY & (EVENT_QUEUE_CAPACITY - 1), 0);
    ASSERT_EQ(EVENT_QUEUE_CAPACITY, 32);
}

int main(void)
{
    printf("Event Queue Tests\n");
    printf("=================\n");

    RUN_TEST(queue_init_empty);
    RUN_TEST(single_push_pop);
    RUN_TEST(pop_empty_fails);
    RUN_TEST(fill_to_capacity);
    RUN_TEST(fifo_order);
    RUN_TEST(wrap_around);
    RUN_TEST(interleaved_push_pop);
    RUN_TEST(event_categories);
    RUN_TEST(drain_all);
    RUN_TEST(event_size);
    RUN_TEST(queue_capacity);

    printf("\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
