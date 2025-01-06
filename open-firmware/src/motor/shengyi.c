/*
 * Shengyi DWG22 Motor Protocol (custom variant)
 *
 * Handles communication with the Shengyi DWG22 motor controller via UART2.
 * Implements the 0x52 status request frame and OEM assist level mapping.
 */

#include "shengyi.h"
#include "../control/control.h"
#include "app_data.h"
#include "../../drivers/uart.h"
#include "../../platform/hw.h"

/* Module state */
static uint8_t g_shengyi_req_pending;
static uint8_t g_shengyi_req_force;
static uint8_t g_shengyi_last_assist;
static uint8_t g_shengyi_last_flags;

/* -------------------------------------------------------------
 * Build flags byte from current state
 * ------------------------------------------------------------- */
uint8_t shengyi_build_flags(void)
{
    uint8_t flags = 0;
    if (g_headlight_enabled)
        flags |= 0x80u;
    if (g_walk_state == WALK_STATE_ACTIVE)
        flags |= 0x20u;
    if (g_effective_cap_speed_dmph && g_inputs.speed_dmph > g_effective_cap_speed_dmph)
        flags |= 0x01u;
    return flags;
}

/* -------------------------------------------------------------
 * Send a 0x52 request frame
 * ------------------------------------------------------------- */
void shengyi_send_0x52_req(uint8_t assist_level_mapped,
                              uint8_t headlight_enabled,
                              uint8_t walk_assist_active,
                              uint8_t speed_over_limit)
{
    uint8_t frame[14];
    size_t len = shengyi_build_frame_0x52_req(assist_level_mapped,
                                                headlight_enabled,
                                                walk_assist_active,
                                                speed_over_limit,
                                                frame,
                                                sizeof(frame));
    if (len)
        uart_write(UART2_BASE, frame, len);
}

/* -------------------------------------------------------------
 * Request update to motor
 * ------------------------------------------------------------- */
void shengyi_request_update(uint8_t force)
{
    uint8_t assist = shengyi_assist_level_mapped();
    uint8_t flags = shengyi_build_flags();
    if (force || assist != g_shengyi_last_assist || flags != g_shengyi_last_flags)
    {
        g_shengyi_req_pending = 1u;
        if (force)
            g_shengyi_req_force = 1u;
    }
}

/* -------------------------------------------------------------
 * Periodic send tick
 * ------------------------------------------------------------- */
void shengyi_periodic_send_tick(void)
{
    if (!g_shengyi_req_pending && !g_shengyi_req_force)
        return;
    uint8_t assist = shengyi_assist_level_mapped();
    uint8_t flags = shengyi_build_flags();
    uint8_t walk = (flags & 0x20u) ? 1u : 0u;
    uint8_t speed_over = (flags & 0x01u) ? 1u : 0u;
    shengyi_send_0x52_req(assist, g_headlight_enabled, walk, speed_over);
    g_shengyi_last_assist = assist;
    g_shengyi_last_flags = flags;
    g_shengyi_req_pending = 0u;
    g_shengyi_req_force = 0u;
}

/* -------------------------------------------------------------
 * OEM assist level mapping
 *
 * The Shengyi DWG22 variant only supports certain assist level counts
 * (1, 3, 5, 6, 9). This maps virtual gear count to the closest.
 * ------------------------------------------------------------- */
uint8_t shengyi_assist_oem_max(uint8_t count)
{
    static const uint8_t opts[] = {1u, 3u, 5u, 6u, 9u};
    uint8_t best = opts[0];
    uint8_t best_diff = (count > best) ? (uint8_t)(count - best) : (uint8_t)(best - count);
    for (size_t i = 1; i < (sizeof(opts) / sizeof(opts[0])); ++i)
    {
        uint8_t v = opts[i];
        uint8_t diff = (count > v) ? (uint8_t)(count - v) : (uint8_t)(v - count);
        if (diff < best_diff || (diff == best_diff && v > best))
        {
            best = v;
            best_diff = diff;
        }
    }
    return best;
}

uint8_t shengyi_assist_oem_lut(uint8_t max, uint8_t idx)
{
    switch (max)
    {
        case 1:
        {
            static const uint8_t lut[] = {0u, 0x66u, 0x32u};
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        case 3:
        {
            static const uint8_t lut[] = {0u, 0x66u, 0x8Cu, 0xB3u, 0x32u};
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        case 5:
        {
            static const uint8_t lut[] = {0u, 0x66u, 0x8Cu, 0xB3u, 0xD9u, 0xFFu, 0x32u};
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        case 6:
        {
            static const uint8_t lut[] = {0u, 0x66u, 0x84u, 0xA2u, 0xC0u, 0xDEu, 0xFFu, 0x32u};
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        case 9:
        {
            static const uint8_t lut[] = {
                0u, 0x66u, 0x79u, 0x89u, 0x9Cu, 0xAFu, 0xC2u, 0xD5u, 0xE8u, 0xFFu, 0x32u
            };
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        default:
            break;
    }
    return 0u;
}

uint8_t shengyi_assist_level_mapped(void)
{
    uint8_t max = shengyi_assist_oem_max(g_vgears.count);
    uint8_t idx = g_active_vgear;
    if (idx == 0u)
        idx = 1u;
    if (idx > max)
        idx = max;
    if (g_walk_state == WALK_STATE_ACTIVE)
        idx = (uint8_t)(max + 1u);
    return shengyi_assist_oem_lut(max, idx);
}

/* -------------------------------------------------------------
 * Init/tick stubs (full implementation in main.c for now)
 * ------------------------------------------------------------- */
void shengyi_init(void)
{
    g_shengyi_req_pending = 0;
    g_shengyi_req_force = 0;
    g_shengyi_last_assist = 0;
    g_shengyi_last_flags = 0;
}

void shengyi_tick(void)
{
    shengyi_periodic_send_tick();
}
