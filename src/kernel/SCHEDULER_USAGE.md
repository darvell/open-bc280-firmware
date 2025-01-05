# Scheduler Usage Guide

## Overview

The cooperative scheduler provides deterministic time-sliced task execution for the BC280 firmware. It replaces ad-hoc timing logic with structured periodic callbacks.

## Key Features

- **Fixed-slot scheduling**: 8 predefined slots, no dynamic allocation
- **Priority-based**: Lower slot IDs run first when multiple tasks are due
- **Interval timing**: Tasks run when their interval has elapsed since last execution
- **Suspend/resume**: Temporarily disable tasks without unregistering
- **Execution tracking**: Optional microsecond-level execution time monitoring

## Predefined Slots

```c
SCHED_SLOT_MOTOR_MAIN  0   // 10ms - motor command processing
SCHED_SLOT_POWER       1   // 50ms - power management
SCHED_SLOT_BLE         2   // 100ms - BLE communication
SCHED_SLOT_UI          3   // 200ms - UI refresh
SCHED_SLOT_TELEMETRY   4   // 500ms - trip stats update
SCHED_SLOT_MAX         8   // Maximum slots
```

Slots 5-7 are available for future use.

## Basic Usage

### 1. Initialize the Scheduler

Call once at startup, before registering any tasks:

```c
void main(void) {
    // Platform init...

    scheduler_init();

    // Register tasks...
}
```

### 2. Register Tasks

Each task is a callback function with this signature:

```c
typedef void (*scheduler_fn)(void *ctx, uint32_t now_ms);
```

Example task registration:

```c
// Motor processing task
static void motor_task(void *ctx, uint32_t now_ms) {
    (void)ctx;

    // Read motor controller
    motor_update();

    // Process control commands
    motor_apply_throttle();
}

// UI refresh task
static void ui_task(void *ctx, uint32_t now_ms) {
    (void)ctx;

    // Update display if state changed
    if (STATE_UI()->dirty) {
        ui_render();
    }
}

// Register tasks during init
void tasks_init(void) {
    scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, motor_task, NULL);
    scheduler_register(SCHED_SLOT_UI, 200, ui_task, NULL);
}
```

### 3. Main Loop Integration

Call `scheduler_tick()` from the main loop with current millisecond time:

```c
while (1) {
    uint32_t now_ms = HAL_GetTick();  // Or your platform's ms counter

    // Process events first (high priority)
    event_queue_drain(&g_button_events, handle_button_event, NULL);
    event_queue_drain(&g_motor_events, handle_motor_event, NULL);

    // Run scheduled tasks
    scheduler_tick(now_ms);

    // Low power wait for next interrupt
    __WFI();
}
```

## Advanced Usage

### Passing Context to Tasks

You can pass a context pointer to tasks:

```c
typedef struct {
    uint8_t counter;
    bool enabled;
} telemetry_ctx_t;

static telemetry_ctx_t telemetry_ctx = { .enabled = true };

static void telemetry_task(void *ctx, uint32_t now_ms) {
    telemetry_ctx_t *state = (telemetry_ctx_t *)ctx;

    if (state->enabled) {
        state->counter++;
        update_trip_stats();
    }
}

// Register with context
scheduler_register(SCHED_SLOT_TELEMETRY, 500,
                   telemetry_task, &telemetry_ctx);
```

### Suspend/Resume Tasks

Useful for power modes or disabling subsystems:

```c
void enter_low_power_mode(void) {
    // Suspend non-critical tasks
    scheduler_suspend(SCHED_SLOT_BLE);
    scheduler_suspend(SCHED_SLOT_TELEMETRY);

    // Motor and UI continue running
}

void exit_low_power_mode(void) {
    scheduler_resume(SCHED_SLOT_BLE);
    scheduler_resume(SCHED_SLOT_TELEMETRY);
}
```

### Dynamic Interval Changes

To change a task's interval, unregister and re-register:

```c
// Switch UI to high-refresh mode
scheduler_unregister(SCHED_SLOT_UI);
scheduler_register(SCHED_SLOT_UI, 50, ui_task, NULL);  // 50ms instead of 200ms
```

### Execution Time Monitoring

Track maximum execution time for debugging:

```c
void print_scheduler_stats(void) {
    printf("Motor task max exec: %lu us\n",
           scheduler_get_max_exec_time(SCHED_SLOT_MOTOR_MAIN));
    printf("UI task max exec: %lu us\n",
           scheduler_get_max_exec_time(SCHED_SLOT_UI));

    // Reset counters for next measurement period
    scheduler_reset_max_exec_time(SCHED_SLOT_MOTOR_MAIN);
    scheduler_reset_max_exec_time(SCHED_SLOT_UI);
}
```

Note: Execution time tracking requires implementing `get_us()` in the platform layer.

## Task Design Guidelines

### Keep Tasks Short

Tasks should complete in well under their interval time:

```c
// GOOD: Quick, focused task
static void power_task(void *ctx, uint32_t now_ms) {
    update_battery_voltage();
    check_thermal_limits();
    apply_power_limits();
}

// BAD: Long-running operation
static void bad_task(void *ctx, uint32_t now_ms) {
    for (int i = 0; i < 1000; i++) {
        complex_calculation();  // Blocks other tasks!
    }
}
```

If a task needs to do heavy work, break it into chunks across multiple invocations.

### Use State for Multi-Step Operations

```c
typedef enum {
    BLE_STATE_IDLE,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED
} ble_state_t;

static ble_state_t ble_state = BLE_STATE_IDLE;

static void ble_task(void *ctx, uint32_t now_ms) {
    switch (ble_state) {
        case BLE_STATE_IDLE:
            if (should_advertise()) {
                start_advertising();
                ble_state = BLE_STATE_ADVERTISING;
            }
            break;

        case BLE_STATE_ADVERTISING:
            if (connection_received()) {
                ble_state = BLE_STATE_CONNECTED;
            }
            break;

        case BLE_STATE_CONNECTED:
            process_ble_data();
            break;
    }
}
```

### Don't Block or Spin

```c
// BAD: Blocking delay
static void bad_task(void *ctx, uint32_t now_ms) {
    HAL_Delay(100);  // Blocks entire system!
}

// GOOD: State-based delay
static uint32_t last_action_ms = 0;
static void good_task(void *ctx, uint32_t now_ms) {
    if (now_ms - last_action_ms >= 100) {
        do_action();
        last_action_ms = now_ms;
    }
}
```

## Timing Behavior

### First Run

Tasks always run on the first `scheduler_tick()` call after registration:

```c
scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, motor_task, NULL);
scheduler_tick(0);     // motor_task runs immediately
scheduler_tick(5);     // motor_task skipped (interval not elapsed)
scheduler_tick(10);    // motor_task runs (10ms elapsed since t=0)
```

### Missed Intervals

The scheduler does not "catch up" missed intervals. If you call `tick()` late, tasks run once:

```c
scheduler_register(SCHED_SLOT_MOTOR_MAIN, 10, motor_task, NULL);
scheduler_tick(0);     // Runs, last_run=0
scheduler_tick(50);    // Runs ONCE (not 5 times), last_run=50
scheduler_tick(60);    // Runs, last_run=60
```

This is intentional - tasks should be designed to handle variable intervals gracefully.

### Zero Interval

A zero interval causes the task to run on every tick:

```c
scheduler_register(SCHED_SLOT_MOTOR_MAIN, 0, motor_task, NULL);
scheduler_tick(0);   // Runs
scheduler_tick(1);   // Runs
scheduler_tick(2);   // Runs
```

This is useful for tasks that should run as fast as possible.

## Integration with Event System

The scheduler complements the event queue system:

```c
while (1) {
    uint32_t now_ms = HAL_GetTick();

    // 1. Events: React to asynchronous inputs (buttons, motor updates)
    event_queue_drain(&g_button_events, handle_button_event, NULL);
    event_queue_drain(&g_motor_events, handle_motor_event, NULL);

    // 2. Scheduled tasks: Periodic background work
    scheduler_tick(now_ms);

    // 3. Idle: Wait for next interrupt
    __WFI();
}
```

Events are for reactive behavior, scheduler is for periodic tasks.

## Thread Safety

The scheduler is **not thread-safe**. All scheduler functions must be called from the same context (typically the main loop). Do not call scheduler functions from ISRs.

If you need to trigger actions from ISRs, use the event queue system instead.

## Memory Footprint

- Scheduler state: ~280 bytes (fixed)
- Per-slot overhead: ~32 bytes × 8 slots = 256 bytes
- Total: ~536 bytes

All memory is statically allocated - no heap usage.

## Performance

With 8 registered tasks, `scheduler_tick()` completes in:
- Best case (no tasks due): ~200 CPU cycles
- Typical case (1-2 tasks run): ~1000 CPU cycles + task execution time
- Worst case (all tasks run): ~8000 CPU cycles + task execution time

At 72 MHz (STM32F103), this is negligible overhead (<1% CPU).
