#include "bus.h"

#include "app_data.h"
#include "config/config.h"
#include "platform/time.h"
#include "src/app_state.h"
#include "storage/logs.h"

static bus_capture_record_t g_bus_capture[BUS_CAPTURE_CAPACITY];
static uint16_t g_bus_capture_count;
static uint16_t g_bus_capture_head;
static uint32_t g_bus_capture_seq;
static uint32_t g_bus_capture_last_ms;
static uint8_t g_bus_capture_enabled;
static uint8_t g_bus_inject_armed;
static uint8_t g_bus_inject_override;
static uint16_t g_bus_replay_rate_ms;
static uint8_t g_bus_replay_active;
static uint8_t g_bus_replay_offset;
static uint32_t g_bus_replay_next_ms;

static void bus_capture_append_internal(uint8_t bus_id, const uint8_t *data, uint8_t len,
                                        uint16_t dt_ms, uint8_t use_override);

void bus_capture_reset(void)
{
    g_bus_capture_count = 0;
    g_bus_capture_head = 0;
    g_bus_capture_seq = 1;
    g_bus_capture_last_ms = 0;
    g_bus_inject_armed = 0;
    g_bus_inject_override = 0;
    g_bus_replay_active = 0;
    g_bus_replay_offset = 0;
    g_bus_replay_next_ms = 0;
    g_bus_replay_rate_ms = BUS_REPLAY_RATE_MIN_MS;
    bus_ui_reset();
}

void bus_capture_set_enabled(uint8_t enable, uint8_t reset)
{
    g_bus_capture_enabled = enable ? 1u : 0u;
    if (reset)
        bus_capture_reset();
}

void bus_capture_append(uint8_t bus_id, const uint8_t *data, uint8_t len, uint16_t dt_ms)
{
    bus_capture_append_internal(bus_id, data, len, dt_ms, 0);
}

uint8_t bus_capture_get_enabled(void)
{
    return g_bus_capture_enabled ? 1u : 0u;
}

uint16_t bus_capture_get_count(void)
{
    return g_bus_capture_count;
}

void bus_capture_get_state(bus_capture_state_t *out)
{
    if (!out)
        return;
    out->enabled = g_bus_capture_enabled ? 1u : 0u;
    out->paused = 0;
    out->head = g_bus_capture_head;
    out->count = g_bus_capture_count;
    out->capacity = BUS_CAPTURE_CAPACITY;
    out->seq = g_bus_capture_seq;
    out->last_ms = g_bus_capture_last_ms;
}

int bus_capture_get_record(uint16_t offset, bus_capture_record_t *out)
{
    if (!out)
        return 0;
    if (offset >= g_bus_capture_count)
        return 0;
    uint16_t oldest = (g_bus_capture_count >= BUS_CAPTURE_CAPACITY) ? g_bus_capture_head : 0u;
    uint16_t idx = (uint16_t)((oldest + offset) % BUS_CAPTURE_CAPACITY);
    *out = g_bus_capture[idx];
    return 1;
}

void bus_inject_log(uint8_t flags)
{
    event_log_append(EVT_BUS_INJECT, flags);
}

int bus_inject_allowed(uint8_t *flags_out)
{
    uint8_t flags = 0;
    if (g_bus_inject_override)
        flags |= BUS_INJECT_EVENT_OVERRIDE;

    if (g_config_active.mode != MODE_PRIVATE)
        flags |= BUS_INJECT_EVENT_BLOCKED_MODE;
    if (!g_bus_inject_armed)
        flags |= BUS_INJECT_EVENT_BLOCKED_ARMED;
    if (!g_bus_capture_enabled)
        flags |= BUS_INJECT_EVENT_BLOCKED_CAPTURE;
    if (!g_bus_inject_override)
    {
        if (g_inputs.speed_dmph > (int16_t)BUS_INJECT_SPEED_MAX_DMPH)
            flags |= BUS_INJECT_EVENT_BLOCKED_MOVING;
        if (!g_inputs.brake)
            flags |= BUS_INJECT_EVENT_BLOCKED_BRAKE;
    }

    if (flags_out)
        *flags_out = flags;

    return (flags == 0 || flags == BUS_INJECT_EVENT_OVERRIDE);
}

void bus_inject_emit(uint8_t bus_id, const uint8_t *data, uint8_t len, uint16_t dt_ms, uint8_t flags)
{
    (void)flags;
    if (!data)
        return;
    bus_capture_append_internal(bus_id, data, len, dt_ms, 1);
}

void bus_inject_set_armed(uint8_t armed, uint8_t override_flags)
{
    g_bus_inject_armed = armed ? 1u : 0u;
    g_bus_inject_override = override_flags ? (uint8_t)override_flags : 0u;
    if (g_bus_inject_override)
        g_bus_inject_override = BUS_INJECT_OVERRIDE_SPEED | BUS_INJECT_OVERRIDE_BRAKE;
}

void bus_replay_start(uint8_t offset, uint16_t rate_ms)
{
    if (rate_ms < BUS_REPLAY_RATE_MIN_MS)
        rate_ms = BUS_REPLAY_RATE_MIN_MS;
    if (rate_ms > BUS_REPLAY_RATE_MAX_MS)
        rate_ms = BUS_REPLAY_RATE_MAX_MS;
    g_bus_replay_rate_ms = rate_ms;
    g_bus_replay_offset = offset;
    g_bus_replay_active = 1;
    g_bus_replay_next_ms = g_ms;
}

void bus_replay_cancel(uint8_t flags)
{
    if (!g_bus_replay_active)
        return;
    g_bus_replay_active = 0;
    g_bus_replay_offset = 0;
    bus_inject_log((uint8_t)(flags | BUS_INJECT_EVENT_REPLAY));
}

void bus_replay_tick(void)
{
    if (!g_bus_replay_active)
        return;
    if (g_brake_edge && !g_bus_inject_override)
    {
        bus_replay_cancel(BUS_INJECT_EVENT_BLOCKED_BRAKE);
        return;
    }
    if (!g_bus_inject_override && g_inputs.speed_dmph > (int16_t)BUS_INJECT_SPEED_MAX_DMPH)
    {
        bus_replay_cancel(BUS_INJECT_EVENT_BLOCKED_MOVING);
        return;
    }
    if ((int32_t)(g_ms - g_bus_replay_next_ms) >= 0)
    {
        if (g_bus_replay_offset >= g_bus_capture_count)
        {
            bus_replay_cancel(BUS_INJECT_EVENT_OK);
            return;
        }
        uint16_t oldest = (g_bus_capture_count >= BUS_CAPTURE_CAPACITY) ? g_bus_capture_head : 0u;
        uint16_t idx = (uint16_t)((oldest + g_bus_replay_offset) % BUS_CAPTURE_CAPACITY);
        const bus_capture_record_t *r = &g_bus_capture[idx];
        uint8_t flags = BUS_INJECT_EVENT_OK | BUS_INJECT_EVENT_REPLAY;
        if (g_bus_inject_override)
            flags |= BUS_INJECT_EVENT_OVERRIDE;
        bus_inject_emit(r->bus_id, r->data, r->len, g_bus_replay_rate_ms, flags);
        g_bus_replay_offset++;
        g_bus_replay_next_ms = g_ms + g_bus_replay_rate_ms;
    }
}

static void bus_capture_append_internal(uint8_t bus_id, const uint8_t *data, uint8_t len,
                                        uint16_t dt_ms, uint8_t use_override)
{
    if (!g_bus_capture_enabled)
        return;
    if (len > BUS_CAPTURE_MAX_DATA)
        len = BUS_CAPTURE_MAX_DATA;

    if (use_override)
    {
        if (g_bus_capture_last_ms == 0)
            g_bus_capture_last_ms = g_ms;
        g_bus_capture_last_ms += dt_ms;
    }
    else
    {
        if (g_bus_capture_last_ms == 0)
            dt_ms = 0;
        else
        {
            uint32_t delta = g_ms - g_bus_capture_last_ms;
            if (delta > 0xFFFFu)
                delta = 0xFFFFu;
            dt_ms = (uint16_t)delta;
        }
        g_bus_capture_last_ms = g_ms;
    }

    bus_capture_record_t *r = &g_bus_capture[g_bus_capture_head];
    r->dt_ms = dt_ms;
    r->bus_id = bus_id;
    r->len = len;
    for (uint8_t i = 0; i < len; ++i)
        r->data[i] = data[i];

    g_bus_capture_head = (uint16_t)((g_bus_capture_head + 1u) % BUS_CAPTURE_CAPACITY);
    if (g_bus_capture_count < BUS_CAPTURE_CAPACITY)
        g_bus_capture_count++;
    g_bus_capture_seq++;

    bus_ui_on_capture(bus_id, data, len, dt_ms);
}
