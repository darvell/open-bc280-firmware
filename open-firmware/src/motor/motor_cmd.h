/*
 * Motor Command Processing
 *
 * Main loop side of motor communication - processes motor events
 * from the ISR event queue and updates application state.
 *
 * Design:
 *   - Runs in main loop (not ISR)
 *   - Processes EVT_MOTOR_* events from event queue
 *   - Parses Shengyi DWG22 protocol responses
 *   - Updates motor state (g_motor, g_inputs from app_data.h)
 *   - Provides high-level command API for control subsystem
 *
 * Flow:
 *   ISR (motor_isr.c) → event_queue → motor_cmd_process() → state updates
 */

#ifndef MOTOR_CMD_H
#define MOTOR_CMD_H

#include <stdint.h>
#include <stdbool.h>
#include "../kernel/event.h"

/*
 * Initialize motor command processor
 *
 * Note: Call after motor_isr_init()
 */
void motor_cmd_init(void);

/*
 * Process a motor event from the queue
 *
 * Handles:
 *   EVT_MOTOR_STATE   - Parse response and update state
 *   EVT_MOTOR_ERROR   - Log error, increment error counter
 *   EVT_MOTOR_READY   - Motor controller came online
 *   EVT_MOTOR_TIMEOUT - Communication timeout
 *
 * Args:
 *   evt - Motor event to process
 *
 * Note: Call from main loop event dispatcher
 */
void motor_cmd_process(const event_t *evt);

/*
 * Set assist level (queue command to motor)
 *
 * Args:
 *   level - Virtual gear level (0 = off, 1-9 = assist)
 *
 * Note: Command will be sent at next ISR TX interval.
 *       Uses shengyi_assist_level_mapped() to convert to OEM levels.
 */
void motor_cmd_set_assist(uint8_t level);

/*
 * Set headlight state
 *
 * Args:
 *   on - true to turn light on, false for off
 */
void motor_cmd_set_light(bool on);

/*
 * Set walk assist state
 *
 * Args:
 *   active - true to activate walk assist, false to deactivate
 */
void motor_cmd_set_walk(bool active);

/*
 * Set speed limit flag
 *
 * Args:
 *   over - true if current speed exceeds configured limit
 */
void motor_cmd_set_speed_over(bool over);

/*
 * Get last received motor status (for diagnostics)
 */
typedef struct {
    uint16_t rpm;
    uint16_t speed_dmph;
    uint16_t torque_raw;
    uint16_t power_w;
    int16_t  battery_dV;
    int16_t  battery_dA;
    int16_t  ctrl_temp_dC;
    uint8_t  soc_pct;
    uint8_t  err;
    uint8_t  assist_level;
    uint32_t last_update_ms;
    bool     valid;
} motor_status_cache_t;

void motor_cmd_get_status(motor_status_cache_t *status);

/*
 * Check if motor communication is healthy
 *
 * Returns:
 *   true if we've received valid data within last 500ms
 */
bool motor_cmd_is_alive(uint32_t now_ms);

/*
 * Get command subsystem statistics
 */
typedef struct {
    uint32_t events_processed;   /* Total events handled */
    uint32_t state_updates;      /* EVT_MOTOR_STATE count */
    uint32_t errors;             /* EVT_MOTOR_ERROR count */
    uint32_t timeouts;           /* EVT_MOTOR_TIMEOUT count */
    uint32_t parse_errors;       /* Frame parse failures */
    uint32_t last_event_ms;      /* Last event timestamp */
} motor_cmd_stats_t;

void motor_cmd_get_stats(motor_cmd_stats_t *stats);

#endif /* MOTOR_CMD_H */
