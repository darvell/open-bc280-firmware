/*
 * Unit Tests for Cooperative Scheduler
 *
 * Tests cover:
 *   - Initialization
 *   - Slot registration/unregistration
 *   - Interval timing (tasks run at correct intervals)
 *   - Priority ordering (lower slot_id runs first)
 *   - Suspend/resume functionality
 *   - Edge cases (invalid slots, double registration, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define HOST_TEST 1
#include "src/kernel/scheduler.h"

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

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    Expected %d == %d\n    at %s:%d\n", \
               (int)(a), (int)(b), __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * Test callback infrastructure
 */
static int callback_count[SCHED_SLOT_MAX];
static uint32_t callback_last_time[SCHED_SLOT_MAX];

static void reset_callback_tracking(void)
{
    memset(callback_count, 0, sizeof(callback_count));
    memset(callback_last_time, 0, sizeof(callback_last_time));
}

static void test_callback_0(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    callback_count[0]++;
    callback_last_time[0] = now_ms;
}

static void test_callback_1(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    callback_count[1]++;
    callback_last_time[1] = now_ms;
}

static void test_callback_2(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    callback_count[2]++;
    callback_last_time[2] = now_ms;
}


static void test_callback_with_ctx(void *ctx, uint32_t now_ms)
{
    (void)now_ms;
    int *counter = (int *)ctx;
    (*counter)++;
}

/*
 * Test: Initialization
 */
TEST(init)
{
    scheduler_init();

    /* All slots should be unregistered */
    for (uint8_t i = 0; i < SCHED_SLOT_MAX; i++) {
        ASSERT_FALSE(scheduler_is_registered(i));
        ASSERT_FALSE(scheduler_is_suspended(i));
    }
}

/*
 * Test: Basic registration
 */
TEST(register_single_slot)
{
    scheduler_init();
    reset_callback_tracking();

    ASSERT_TRUE(scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL));
    ASSERT_TRUE(scheduler_is_registered(SCHED_SLOT_MOTOR_MAIN));
    ASSERT_FALSE(scheduler_is_suspended(SCHED_SLOT_MOTOR_MAIN));
}

/*
 * Test: Register multiple slots
 */
TEST(register_multiple_slots)
{
    scheduler_init();
    reset_callback_tracking();

    ASSERT_TRUE(scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL));
    ASSERT_TRUE(scheduler_register(SCHED_SLOT_POWER, 50, test_callback_1, NULL));
    ASSERT_TRUE(scheduler_register(SCHED_SLOT_UI, 200, test_callback_2, NULL));

    ASSERT_TRUE(scheduler_is_registered(SCHED_SLOT_MOTOR_MAIN));
    ASSERT_TRUE(scheduler_is_registered(SCHED_SLOT_POWER));
    ASSERT_TRUE(scheduler_is_registered(SCHED_SLOT_UI));
}

/*
 * Test: Double registration fails
 */
TEST(register_double_fails)
{
    scheduler_init();
    reset_callback_tracking();

    ASSERT_TRUE(scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL));
    ASSERT_FALSE(scheduler_register(SCHED_SLOT_MOTOR_MAIN, 20, test_callback_1, NULL));
}

/*
 * Test: Invalid slot_id fails
 */
TEST(register_invalid_slot)
{
    scheduler_init();
    reset_callback_tracking();

    ASSERT_FALSE(scheduler_register(SCHED_SLOT_MAX, 10, test_callback_0, NULL));
    ASSERT_FALSE(scheduler_register(255, 10, test_callback_0, NULL));
}

/*
 * Test: NULL callback fails
 */
TEST(register_null_callback)
{
    scheduler_init();

    ASSERT_FALSE(scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, NULL, NULL));
}

/*
 * Test: Unregister slot
 */
TEST(unregister_slot)
{
    scheduler_init();
    reset_callback_tracking();

    ASSERT_TRUE(scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL));
    ASSERT_TRUE(scheduler_is_registered(SCHED_SLOT_MOTOR_MAIN));

    ASSERT_TRUE(scheduler_unregister(SCHED_SLOT_MOTOR_MAIN));
    ASSERT_FALSE(scheduler_is_registered(SCHED_SLOT_MOTOR_MAIN));

    /* Should be able to re-register after unregister */
    ASSERT_TRUE(scheduler_register(SCHED_SLOT_MOTOR_MAIN, 20, test_callback_1, NULL));
}

/*
 * Test: Unregister invalid slot
 */
TEST(unregister_invalid)
{
    scheduler_init();

    ASSERT_FALSE(scheduler_unregister(SCHED_SLOT_MAX));
    ASSERT_FALSE(scheduler_unregister(255));
}

/*
 * Test: Single task runs on first tick
 */
TEST(tick_runs_task_initially)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);

    uint8_t tasks_run = scheduler_tick(100);

    ASSERT_EQ(tasks_run, 1);
    ASSERT_EQ(callback_count[0], 1);
    ASSERT_EQ(callback_last_time[0], 100);
}

/*
 * Test: Task respects interval timing
 */
TEST(tick_respects_interval)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);

    /* First run at t=0 */
    scheduler_tick(0);
    ASSERT_EQ(callback_count[0], 1);

    /* Too early at t=5 */
    scheduler_tick(5);
    ASSERT_EQ(callback_count[0], 1);

    /* Should run at t=10 */
    scheduler_tick(10);
    ASSERT_EQ(callback_count[0], 2);

    /* Too early at t=15 */
    scheduler_tick(15);
    ASSERT_EQ(callback_count[0], 2);

    /* Should run at t=20 */
    scheduler_tick(20);
    ASSERT_EQ(callback_count[0], 3);
}

/*
 * Test: Multiple tasks with different intervals
 */
TEST(tick_multiple_intervals)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);  /* 10ms */
    scheduler_register(SCHED_SLOT_POWER, 50, test_callback_1, NULL);       /* 50ms */
    scheduler_register(SCHED_SLOT_UI, 100, test_callback_2, NULL);         /* 100ms */

    /* t=0: all three run (initial) */
    uint8_t tasks = scheduler_tick(0);
    ASSERT_EQ(tasks, 3);
    ASSERT_EQ(callback_count[0], 1);
    ASSERT_EQ(callback_count[1], 1);
    ASSERT_EQ(callback_count[2], 1);

    /* t=10: only slot 0 runs */
    tasks = scheduler_tick(10);
    ASSERT_EQ(tasks, 1);
    ASSERT_EQ(callback_count[0], 2);
    ASSERT_EQ(callback_count[1], 1);
    ASSERT_EQ(callback_count[2], 1);

    /* t=50: slots 0 and 1 run */
    tasks = scheduler_tick(50);
    ASSERT_EQ(tasks, 2);
    ASSERT_EQ(callback_count[0], 3);  /* 0->10->50 (runs when interval elapsed) */
    ASSERT_EQ(callback_count[1], 2);  /* 0->50 */
    ASSERT_EQ(callback_count[2], 1);

    /* t=100: all run */
    tasks = scheduler_tick(100);
    ASSERT_EQ(tasks, 3);
    ASSERT_EQ(callback_count[0], 4);  /* 0->10->50->100 */
    ASSERT_EQ(callback_count[1], 3);  /* 0->50->100 */
    ASSERT_EQ(callback_count[2], 2);  /* 0->100 */
}

/*
 * Test: Priority ordering - lower slot_id runs first
 */
static int execution_order[SCHED_SLOT_MAX];
static int execution_index;

static void priority_callback_0(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    (void)now_ms;
    execution_order[execution_index++] = 0;
}

static void priority_callback_1(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    (void)now_ms;
    execution_order[execution_index++] = 1;
}

static void priority_callback_2(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    (void)now_ms;
    execution_order[execution_index++] = 2;
}

static void priority_callback_3(void *ctx, uint32_t now_ms)
{
    (void)ctx;
    (void)now_ms;
    execution_order[execution_index++] = 3;
}

TEST(tick_priority_ordering)
{
    scheduler_init();
    execution_index = 0;
    memset(execution_order, -1, sizeof(execution_order));

    /* Register in non-sequential order */
    scheduler_register(SCHED_SLOT_UI, 10, priority_callback_3, NULL);
    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, priority_callback_0, NULL);
    scheduler_register(SCHED_SLOT_BLE, 10, priority_callback_2, NULL);
    scheduler_register(SCHED_SLOT_POWER, 10, priority_callback_1, NULL);

    scheduler_tick(0);

    /* Should execute in slot_id order: 0, 1, 2, 3 */
    ASSERT_EQ(execution_index, 4);
    ASSERT_EQ(execution_order[0], 0);
    ASSERT_EQ(execution_order[1], 1);
    ASSERT_EQ(execution_order[2], 2);
    ASSERT_EQ(execution_order[3], 3);
}

/*
 * Test: Suspend a task
 */
TEST(suspend_task)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);

    /* Run once */
    scheduler_tick(0);
    ASSERT_EQ(callback_count[0], 1);

    /* Suspend */
    ASSERT_TRUE(scheduler_suspend(SCHED_SLOT_MOTOR_MAIN));
    ASSERT_TRUE(scheduler_is_suspended(SCHED_SLOT_MOTOR_MAIN));

    /* Should not run when suspended */
    scheduler_tick(10);
    ASSERT_EQ(callback_count[0], 1);

    scheduler_tick(20);
    ASSERT_EQ(callback_count[0], 1);
}

/*
 * Test: Resume a suspended task
 */
TEST(resume_task)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);
    scheduler_tick(0);
    ASSERT_EQ(callback_count[0], 1);

    /* Suspend and verify no execution */
    scheduler_suspend(SCHED_SLOT_MOTOR_MAIN);
    scheduler_tick(10);
    ASSERT_EQ(callback_count[0], 1);

    /* Resume */
    ASSERT_TRUE(scheduler_resume(SCHED_SLOT_MOTOR_MAIN));
    ASSERT_FALSE(scheduler_is_suspended(SCHED_SLOT_MOTOR_MAIN));

    /* Should run again */
    scheduler_tick(20);
    ASSERT_EQ(callback_count[0], 2);
}

/*
 * Test: Suspend/resume invalid slot
 */
TEST(suspend_resume_invalid)
{
    scheduler_init();

    ASSERT_FALSE(scheduler_suspend(SCHED_SLOT_MAX));
    ASSERT_FALSE(scheduler_resume(SCHED_SLOT_MAX));
}

/*
 * Test: Suspend/resume unregistered slot
 */
TEST(suspend_resume_unregistered)
{
    scheduler_init();

    ASSERT_FALSE(scheduler_suspend(SCHED_SLOT_MOTOR_MAIN));
    ASSERT_FALSE(scheduler_resume(SCHED_SLOT_MOTOR_MAIN));
}

/*
 * Test: Context pointer is passed to callback
 */
TEST(callback_receives_context)
{
    scheduler_init();

    int my_counter = 0;
    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_with_ctx, &my_counter);

    scheduler_tick(0);
    ASSERT_EQ(my_counter, 1);

    scheduler_tick(10);
    ASSERT_EQ(my_counter, 2);
}

/*
 * Test: Zero interval runs every tick
 */
TEST(zero_interval_every_tick)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 0, test_callback_0, NULL);

    scheduler_tick(0);
    ASSERT_EQ(callback_count[0], 1);

    scheduler_tick(1);
    ASSERT_EQ(callback_count[0], 2);

    scheduler_tick(2);
    ASSERT_EQ(callback_count[0], 3);
}

/*
 * Test: run_pending executes all registered tasks
 */
TEST(run_pending_all_tasks)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);
    scheduler_register(SCHED_SLOT_POWER, 50, test_callback_1, NULL);
    scheduler_register(SCHED_SLOT_UI, 200, test_callback_2, NULL);

    uint8_t tasks = scheduler_run_pending();

    ASSERT_EQ(tasks, 3);
    ASSERT_EQ(callback_count[0], 1);
    ASSERT_EQ(callback_count[1], 1);
    ASSERT_EQ(callback_count[2], 1);
}

/*
 * Test: run_pending skips suspended tasks
 */
TEST(run_pending_skips_suspended)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);
    scheduler_register(SCHED_SLOT_POWER, 50, test_callback_1, NULL);
    scheduler_suspend(SCHED_SLOT_POWER);

    uint8_t tasks = scheduler_run_pending();

    ASSERT_EQ(tasks, 1);
    ASSERT_EQ(callback_count[0], 1);
    ASSERT_EQ(callback_count[1], 0);
}

/*
 * Test: Tick returns 0 when no tasks registered
 */
TEST(tick_no_tasks)
{
    scheduler_init();

    uint8_t tasks = scheduler_tick(0);
    ASSERT_EQ(tasks, 0);

    tasks = scheduler_tick(100);
    ASSERT_EQ(tasks, 0);
}

/*
 * Test: Tick returns 0 when all tasks suspended
 */
TEST(tick_all_suspended)
{
    scheduler_init();
    reset_callback_tracking();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);
    scheduler_register(SCHED_SLOT_POWER, 50, test_callback_1, NULL);
    scheduler_suspend(SCHED_SLOT_MOTOR_MAIN);
    scheduler_suspend(SCHED_SLOT_POWER);

    uint8_t tasks = scheduler_tick(0);
    ASSERT_EQ(tasks, 0);
    ASSERT_EQ(callback_count[0], 0);
    ASSERT_EQ(callback_count[1], 0);
}

/*
 * Test: Max slot count
 */
TEST(max_slot_count)
{
    scheduler_init();
    reset_callback_tracking();

    /* Should be able to register up to SCHED_SLOT_MAX */
    ASSERT_EQ(SCHED_SLOT_MAX, 8);

    for (uint8_t i = 0; i < SCHED_SLOT_MAX; i++) {
        ASSERT_TRUE(scheduler_register(i, 10, test_callback_0, NULL));
    }
}

/*
 * Test: get/reset max execution time
 */
TEST(max_exec_time_tracking)
{
    scheduler_init();

    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, test_callback_0, NULL);

    /* Initial max exec time is 0 */
    ASSERT_EQ(scheduler_get_max_exec_time(SCHED_SLOT_MOTOR_MAIN), 0);

    /* Run task (exec time tracking requires platform support, will be 0 in tests) */
    scheduler_tick(0);

    /* Reset should work */
    scheduler_reset_max_exec_time(SCHED_SLOT_MOTOR_MAIN);
    ASSERT_EQ(scheduler_get_max_exec_time(SCHED_SLOT_MOTOR_MAIN), 0);
}

/*
 * Test: Invalid slot_id for max exec time
 */
TEST(max_exec_time_invalid_slot)
{
    scheduler_init();

    ASSERT_EQ(scheduler_get_max_exec_time(SCHED_SLOT_MAX), 0);
    ASSERT_EQ(scheduler_get_max_exec_time(255), 0);

    /* Should not crash */
    scheduler_reset_max_exec_time(SCHED_SLOT_MAX);
    scheduler_reset_max_exec_time(255);
}

int main(void)
{
    printf("Scheduler Tests\n");
    printf("===============\n");

    RUN_TEST(init);
    RUN_TEST(register_single_slot);
    RUN_TEST(register_multiple_slots);
    RUN_TEST(register_double_fails);
    RUN_TEST(register_invalid_slot);
    RUN_TEST(register_null_callback);
    RUN_TEST(unregister_slot);
    RUN_TEST(unregister_invalid);
    RUN_TEST(tick_runs_task_initially);
    RUN_TEST(tick_respects_interval);
    RUN_TEST(tick_multiple_intervals);
    RUN_TEST(tick_priority_ordering);
    RUN_TEST(suspend_task);
    RUN_TEST(resume_task);
    RUN_TEST(suspend_resume_invalid);
    RUN_TEST(suspend_resume_unregistered);
    RUN_TEST(callback_receives_context);
    RUN_TEST(zero_interval_every_tick);
    RUN_TEST(run_pending_all_tasks);
    RUN_TEST(run_pending_skips_suspended);
    RUN_TEST(tick_no_tasks);
    RUN_TEST(tick_all_suspended);
    RUN_TEST(max_slot_count);
    RUN_TEST(max_exec_time_tracking);
    RUN_TEST(max_exec_time_invalid_slot);

    printf("\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
