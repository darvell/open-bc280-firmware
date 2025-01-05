/*
 * Cooperative Time-Sliced Scheduler
 *
 * Simple deterministic scheduler for periodic tasks.
 * No preemption, no dynamic allocation, just fixed-interval callbacks.
 *
 * Features:
 *   - Fixed slot array (compile-time capacity)
 *   - Priority-based: lower slot_id runs first when multiple tasks are due
 *   - Interval-based timing with last_run tracking
 *   - Optional suspend/resume per slot
 *   - Execution time tracking for debugging
 *
 * Typical usage in main loop:
 *   while (1) {
 *       process_events();           // Drain event queues
 *       scheduler_tick(get_ms());   // Run due tasks
 *       __WFI();                    // Low power wait
 *   }
 */

#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Predefined scheduler slots - ordered by priority (lower = higher priority)
 */
#define SCHED_SLOT_MOTOR_MAIN  0   /* 10ms - motor command processing */
#define SCHED_SLOT_POWER       1   /* 50ms - power management */
#define SCHED_SLOT_BLE         2   /* 100ms - BLE communication */
#define SCHED_SLOT_UI          3   /* 200ms - UI refresh */
#define SCHED_SLOT_TELEMETRY   4   /* 500ms - trip stats update */
#define SCHED_SLOT_MAX         8   /* Maximum number of slots */

/*
 * Scheduler callback function signature
 *
 * Args:
 *   ctx - User context pointer (from registration)
 *   now_ms - Current time in milliseconds
 */
typedef void (*scheduler_fn)(void *ctx, uint32_t now_ms);

/*
 * Scheduler slot state
 */
typedef struct {
    scheduler_fn callback;      /* Task callback function */
    void        *ctx;           /* User context pointer */
    uint16_t     interval_ms;   /* Run interval in milliseconds */
    uint32_t     last_run_ms;   /* Last execution timestamp */
    uint32_t     max_exec_us;   /* Maximum execution time (microseconds) */
    bool         registered;    /* Slot is active */
    bool         suspended;     /* Slot is suspended */
    bool         first_run;     /* True until first execution */
} scheduler_slot_t;

/*
 * Initialize the scheduler
 *
 * Must be called before any other scheduler functions.
 */
void scheduler_init(void);

/*
 * Register a task in a specific slot
 *
 * Args:
 *   slot_id - Slot identifier (0-SCHED_SLOT_MAX-1)
 *   interval_ms - Run interval in milliseconds (0 = run every tick)
 *   callback - Task callback function
 *   ctx - User context pointer (passed to callback)
 *
 * Returns: true if registered, false if slot_id invalid or already registered
 */
bool scheduler_register(uint8_t slot_id, uint16_t interval_ms,
                        scheduler_fn callback, void *ctx);

/*
 * Unregister a task from a slot
 *
 * Args:
 *   slot_id - Slot identifier
 *
 * Returns: true if unregistered, false if slot_id invalid or not registered
 */
bool scheduler_unregister(uint8_t slot_id);

/*
 * Suspend a registered task
 *
 * Suspended tasks are skipped during tick() but remain registered.
 *
 * Args:
 *   slot_id - Slot identifier
 *
 * Returns: true if suspended, false if slot_id invalid or not registered
 */
bool scheduler_suspend(uint8_t slot_id);

/*
 * Resume a suspended task
 *
 * Args:
 *   slot_id - Slot identifier
 *
 * Returns: true if resumed, false if slot_id invalid or not registered
 */
bool scheduler_resume(uint8_t slot_id);

/*
 * Check if a slot is registered
 *
 * Args:
 *   slot_id - Slot identifier
 *
 * Returns: true if registered, false otherwise
 */
bool scheduler_is_registered(uint8_t slot_id);

/*
 * Check if a slot is suspended
 *
 * Args:
 *   slot_id - Slot identifier
 *
 * Returns: true if suspended, false otherwise
 */
bool scheduler_is_suspended(uint8_t slot_id);

/*
 * Main scheduler tick - call from main loop
 *
 * Runs all tasks that are:
 *   1. Registered
 *   2. Not suspended
 *   3. Due (interval_ms elapsed since last_run_ms)
 *
 * Tasks are run in priority order (slot 0 first).
 *
 * Args:
 *   now_ms - Current time in milliseconds
 *
 * Returns: Number of tasks executed
 */
uint8_t scheduler_tick(uint32_t now_ms);

/*
 * Run all pending tasks regardless of time
 *
 * Useful for testing or forced execution. Ignores interval timing.
 *
 * Returns: Number of tasks executed
 */
uint8_t scheduler_run_pending(void);

/*
 * Get maximum execution time for a slot
 *
 * Args:
 *   slot_id - Slot identifier
 *
 * Returns: Maximum execution time in microseconds, 0 if slot invalid
 *
 * Note: Requires microsecond timer support in platform layer
 */
uint32_t scheduler_get_max_exec_time(uint8_t slot_id);

/*
 * Reset maximum execution time for a slot
 *
 * Args:
 *   slot_id - Slot identifier
 */
void scheduler_reset_max_exec_time(uint8_t slot_id);

#endif /* KERNEL_SCHEDULER_H */
