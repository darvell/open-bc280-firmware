/*
 * Application Orchestration Layer
 *
 * High-level application initialization and main loop coordination.
 * Delegates to subsystems but owns the overall flow.
 *
 * Architecture:
 *   - app_main_loop() - Infinite main loop (never returns)
 *
 * Subsystems coordinated:
 *   - Hardware platform (clocks, NVIC, timebase)
 *   - Communication (UART, motor protocol, BLE)
 *   - Input (buttons, sensors)
 *   - Control (motor, power, drive modes)
 *   - UI (display, user interaction)
 *   - Storage (config, logs, crash dumps)
 *   - Telemetry (trip, graphs, events)
 *
 * Note: This layer is intentionally thin - it just sequences
 *       subsystem calls. Business logic lives in subsystems.
 */

#ifndef APP_H
#define APP_H

#include <stdint.h>

/*
 * Main application loop - never returns
 *
 * Implements the classic embedded main loop:
 *   while (1) {
 *       app_process_time();      // Advance time, handle reboot requests
 *       app_process_events();    // Drain UART RX, button events
 *       app_process_periodic();  // 1Hz status, streaming, logging
 *       app_update_ui();         // UI refresh at UI_TICK_MS rate
 *       app_housekeeping();      // Watchdog, sleep
 *   }
 *
 * Note: This function never returns. It runs the main loop forever.
 */
void app_main_loop(void) __attribute__((noreturn));

/*
 * Process time-critical operations
 *
 * Called first in main loop to:
 *   - Advance millisecond counter (platform_time_poll_1ms)
 *   - Handle reboot requests (soft reboot to bootloader/app)
 */
void app_process_time(void);

/*
 * Process all pending events
 *
 * Drains event queues from:
 *   - UART RX (protocol commands from host/BLE)
 *   - Button inputs (gestures from button_fsm)
 *   - Motor ISR events (via event queue)
 *
 * This is the event-driven part of the main loop.
 */
void app_process_events(void);

/*
 * Process periodic tasks
 *
 * Handles fixed-interval operations:
 *   - 1Hz status print (print_status)
 *   - Binary streaming (send_state_frame_bin)
 *   - Stream logging (stream_log_tick)
 *   - Graph updates (graph_tick)
 *   - Bus replay (bus_replay_tick)
 *   - Shengyi DWG22 periodic send (shengyi_periodic_send_tick)
 *
 * Note: Uses time-based guards (e.g., every 1000ms)
 */
void app_process_periodic(void);

/*
 * Update UI subsystem
 *
 * Rebuilds UI model and triggers UI refresh when due (every UI_TICK_MS).
 * This is relatively expensive so it's rate-limited.
 *
 * Operations:
 *   - Populate ui_model from global state
 *   - Call ui_tick() to render
 */
void app_update_ui(void);

/*
 * Housekeeping tasks
 *
 * Final main loop operations:
 *   - Watchdog management (watchdog_tick, feed)
 *   - Low power wait (wfi)
 */
void app_housekeeping(void);

#endif /* APP_H */
