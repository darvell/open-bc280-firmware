/*
 * Cooperative Time-Sliced Scheduler Implementation
 */

#include "scheduler.h"
#include <string.h>

/*
 * Platform-specific microsecond timer (optional for exec time tracking)
 * For HOST_TEST builds, we'll use a stub
 */
#ifdef HOST_TEST
static inline uint32_t get_us(void) { return 0; }
#else
/* In real firmware, implement this in platform layer */
static inline uint32_t get_us(void) {
    /* Exec time tracking not wired on target; return 0. */
    return 0;
}
#endif

/*
 * Scheduler state
 */
static struct {
    scheduler_slot_t slots[SCHED_SLOT_MAX];
    bool initialized;
} sched;

/*
 * Initialize the scheduler
 */
void scheduler_init(void)
{
    memset(&sched, 0, sizeof(sched));
    sched.initialized = true;
}

/*
 * Register a task in a specific slot
 */
bool scheduler_register(uint8_t slot_id, uint16_t interval_ms,
                        scheduler_fn callback, void *ctx)
{
    if (!sched.initialized || slot_id >= SCHED_SLOT_MAX || !callback) {
        return false;
    }

    scheduler_slot_t *slot = &sched.slots[slot_id];

    if (slot->registered) {
        return false;  /* Slot already registered */
    }

    slot->callback = callback;
    slot->ctx = ctx;
    slot->interval_ms = interval_ms;
    slot->last_run_ms = 0;
    slot->max_exec_us = 0;
    slot->registered = true;
    slot->suspended = false;
    slot->first_run = true;

    return true;
}

/*
 * Unregister a task from a slot
 */
bool scheduler_unregister(uint8_t slot_id)
{
    if (!sched.initialized || slot_id >= SCHED_SLOT_MAX) {
        return false;
    }

    scheduler_slot_t *slot = &sched.slots[slot_id];

    if (!slot->registered) {
        return false;
    }

    memset(slot, 0, sizeof(scheduler_slot_t));
    return true;
}

/*
 * Suspend a registered task
 */
bool scheduler_suspend(uint8_t slot_id)
{
    if (!sched.initialized || slot_id >= SCHED_SLOT_MAX) {
        return false;
    }

    scheduler_slot_t *slot = &sched.slots[slot_id];

    if (!slot->registered) {
        return false;
    }

    slot->suspended = true;
    return true;
}

/*
 * Resume a suspended task
 */
bool scheduler_resume(uint8_t slot_id)
{
    if (!sched.initialized || slot_id >= SCHED_SLOT_MAX) {
        return false;
    }

    scheduler_slot_t *slot = &sched.slots[slot_id];

    if (!slot->registered) {
        return false;
    }

    slot->suspended = false;
    return true;
}

/*
 * Check if a slot is registered
 */
bool scheduler_is_registered(uint8_t slot_id)
{
    if (!sched.initialized || slot_id >= SCHED_SLOT_MAX) {
        return false;
    }

    return sched.slots[slot_id].registered;
}

/*
 * Check if a slot is suspended
 */
bool scheduler_is_suspended(uint8_t slot_id)
{
    if (!sched.initialized || slot_id >= SCHED_SLOT_MAX) {
        return false;
    }

    return sched.slots[slot_id].suspended;
}

/*
 * Main scheduler tick
 */
uint8_t scheduler_tick(uint32_t now_ms)
{
    if (!sched.initialized) {
        return 0;
    }

    uint8_t tasks_run = 0;

    /* Iterate in priority order (slot 0 first) */
    for (uint8_t slot_id = 0; slot_id < SCHED_SLOT_MAX; slot_id++) {
        scheduler_slot_t *slot = &sched.slots[slot_id];

        /* Skip if not registered or suspended */
        if (!slot->registered || slot->suspended) {
            continue;
        }

        /* Check if interval has elapsed */
        bool should_run = false;

        if (slot->first_run) {
            /* Always run on first tick */
            should_run = true;
        } else {
            /* Check if interval has elapsed since last run */
            uint32_t elapsed_ms = now_ms - slot->last_run_ms;
            should_run = (elapsed_ms >= slot->interval_ms);
        }

        if (should_run) {
            /* Track execution time */
            uint32_t start_us = get_us();

            /* Execute callback */
            slot->callback(slot->ctx, now_ms);

            /* Update max execution time */
            uint32_t exec_us = get_us() - start_us;
            if (exec_us > slot->max_exec_us) {
                slot->max_exec_us = exec_us;
            }

            /* Update last run time */
            slot->last_run_ms = now_ms;
            slot->first_run = false;
            tasks_run++;
        }
    }

    return tasks_run;
}

/*
 * Run all pending tasks regardless of time
 */
uint8_t scheduler_run_pending(void)
{
    if (!sched.initialized) {
        return 0;
    }

    uint8_t tasks_run = 0;

    /* Iterate in priority order (slot 0 first) */
    for (uint8_t slot_id = 0; slot_id < SCHED_SLOT_MAX; slot_id++) {
        scheduler_slot_t *slot = &sched.slots[slot_id];

        /* Skip if not registered or suspended */
        if (!slot->registered || slot->suspended) {
            continue;
        }

        /* Track execution time */
        uint32_t start_us = get_us();

        /* Execute callback with timestamp 0 */
        slot->callback(slot->ctx, 0);

        /* Update max execution time */
        uint32_t exec_us = get_us() - start_us;
        if (exec_us > slot->max_exec_us) {
            slot->max_exec_us = exec_us;
        }

        tasks_run++;
    }

    return tasks_run;
}

/*
 * Get maximum execution time for a slot
 */
uint32_t scheduler_get_max_exec_time(uint8_t slot_id)
{
    if (!sched.initialized || slot_id >= SCHED_SLOT_MAX) {
        return 0;
    }

    return sched.slots[slot_id].max_exec_us;
}

/*
 * Reset maximum execution time for a slot
 */
void scheduler_reset_max_exec_time(uint8_t slot_id)
{
    if (!sched.initialized || slot_id >= SCHED_SLOT_MAX) {
        return;
    }

    sched.slots[slot_id].max_exec_us = 0;
}
