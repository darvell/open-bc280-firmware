/*
 * Motor Command Processing Implementation
 *
 * Main loop handler for motor events from ISR queue.
 * Parses Shengyi DWG22 protocol responses and updates application state.
 */

#include "motor_cmd.h"
#include "motor_isr.h"
#include "shengyi.h"
#include "app_data.h"
#include "../power/power.h"

#include <string.h>

#ifndef HOST_TEST
#include "../../platform/time.h"
#else
/* Host test stubs */
extern volatile uint32_t g_ms;
#endif

/*
 * Module state
 */
static struct {
    motor_status_cache_t status;    /* Last received status */
    motor_cmd_stats_t stats;        /* Event processing stats */

    /* Current command state */
    uint8_t assist_level;           /* Current assist level */
    bool light_on;                  /* Headlight state */
    bool walk_active;               /* Walk assist state */
    bool speed_over;                /* Speed limit exceeded */
    bool cmd_dirty;                 /* Command needs update */
} g_motor_cmd;

/*
 * Forward declarations
 */
static void motor_cmd_update_command(void);
static uint8_t motor_cmd_get_current_assist_mapped(void);

/*
 * Initialize motor command processor
 */
void motor_cmd_init(void)
{
    memset(&g_motor_cmd, 0, sizeof(g_motor_cmd));
    g_motor_cmd.status.valid = false;
    g_motor_cmd.cmd_dirty = true;  /* Force initial command send */
}

/*
 * Process a motor event from the queue
 */
void motor_cmd_process(const event_t *evt)
{
    if (!evt || !EVENT_IS_MOTOR(*evt))
        return;

    g_motor_cmd.stats.events_processed++;
    g_motor_cmd.stats.last_event_ms = evt->timestamp;

    switch (evt->type) {
        case EVT_MOTOR_STATE: {
            /* Motor status update - parse the frame */
            g_motor_cmd.stats.state_updates++;

            /* The payload contains the opcode */
            uint8_t opcode = (uint8_t)(evt->payload16 & 0xFF);

            if (opcode == SHENGYI_OPCODE_STATUS) {
                /* Update motor state from g_inputs (populated by main.c motor handler) */
                g_motor.rpm = g_inputs.cadence_rpm;
                g_motor.speed_dmph = g_inputs.speed_dmph;
                g_motor.torque_raw = g_inputs.torque_raw;
                /* g_motor.soc_pct and g_motor.err are set elsewhere */
                g_motor.last_ms = evt->timestamp;

                /* Update cache */
                g_motor_cmd.status.rpm = g_inputs.cadence_rpm;
                g_motor_cmd.status.speed_dmph = g_inputs.speed_dmph;
                g_motor_cmd.status.torque_raw = g_inputs.torque_raw;
                g_motor_cmd.status.power_w = g_inputs.power_w;
                g_motor_cmd.status.battery_dV = g_inputs.battery_dV;
                g_motor_cmd.status.battery_dA = g_inputs.battery_dA;
                g_motor_cmd.status.ctrl_temp_dC = g_inputs.ctrl_temp_dC;
                g_motor_cmd.status.soc_pct = g_motor.soc_pct;
                g_motor_cmd.status.err = g_motor.err;
                g_motor_cmd.status.assist_level = g_outputs.virtual_gear;
                g_motor_cmd.status.last_update_ms = evt->timestamp;
                g_motor_cmd.status.valid = true;
            }
            break;
        }

        case EVT_MOTOR_ERROR: {
            /* Protocol error */
            g_motor_cmd.stats.errors++;
            uint8_t error_code = (uint8_t)(evt->payload16 & 0xFF);

            /* Update motor error state */
            g_motor.err = error_code;
            break;
        }

        case EVT_MOTOR_READY: {
            /* Motor controller came online */
            g_motor_cmd.status.valid = true;
            /* Force command update */
            g_motor_cmd.cmd_dirty = true;
            motor_cmd_update_command();
            break;
        }

        case EVT_MOTOR_TIMEOUT: {
            /* Communication timeout */
            g_motor_cmd.stats.timeouts++;

            /* Mark motor state as stale */
            if (evt->timestamp - g_motor.last_ms > 500) {
                /* Motor offline */
                g_motor_cmd.status.valid = false;
            }
            break;
        }

        default:
            break;
    }
}

/*
 * Set assist level
 */
void motor_cmd_set_assist(uint8_t level)
{
    if (level > 9)
        level = 9;

    if (g_motor_cmd.assist_level != level) {
        g_motor_cmd.assist_level = level;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
}

/*
 * Set headlight state
 */
void motor_cmd_set_light(bool on)
{
    if (g_motor_cmd.light_on != on) {
        g_motor_cmd.light_on = on;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
}

/*
 * Set walk assist state
 */
void motor_cmd_set_walk(bool active)
{
    if (g_motor_cmd.walk_active != active) {
        g_motor_cmd.walk_active = active;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
}

/*
 * Set speed limit flag
 */
void motor_cmd_set_speed_over(bool over)
{
    if (g_motor_cmd.speed_over != over) {
        g_motor_cmd.speed_over = over;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
}

/*
 * Get last received motor status
 */
void motor_cmd_get_status(motor_status_cache_t *status)
{
    if (status) {
        *status = g_motor_cmd.status;
    }
}

/*
 * Check if motor communication is healthy
 */
bool motor_cmd_is_alive(uint32_t now_ms)
{
    if (!g_motor_cmd.status.valid)
        return false;

    uint32_t age = now_ms - g_motor_cmd.status.last_update_ms;
    return age < 500;  /* 500ms timeout */
}

/*
 * Get command subsystem statistics
 */
void motor_cmd_get_stats(motor_cmd_stats_t *stats)
{
    if (stats) {
        *stats = g_motor_cmd.stats;
    }
}

/*
 * Update command if dirty
 */
static void motor_cmd_update_command(void)
{
    if (!g_motor_cmd.cmd_dirty)
        return;

    /* Get mapped assist level from Shengyi DWG22 module */
    uint8_t mapped_level = motor_cmd_get_current_assist_mapped();

    /* Queue command to ISR */
    motor_isr_queue_cmd(
        mapped_level,
        g_motor_cmd.light_on,
        g_motor_cmd.walk_active,
        g_motor_cmd.speed_over
    );

    g_motor_cmd.cmd_dirty = false;
}

/*
 * Get current assist level mapped to OEM protocol
 */
static uint8_t motor_cmd_get_current_assist_mapped(void)
{
    return shengyi_assist_level_mapped();
}
