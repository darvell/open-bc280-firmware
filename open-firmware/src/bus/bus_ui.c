#include "bus.h"


static bus_ui_entry_t g_bus_ui_view[BUS_UI_VIEW_MAX];
static uint8_t g_bus_ui_count;
static uint8_t g_bus_ui_head;
static uint8_t g_bus_ui_enabled;
static uint8_t g_bus_ui_filter_id;
static uint8_t g_bus_ui_filter_opcode;
static uint8_t g_bus_ui_diff_enabled;
static uint8_t g_bus_ui_changed_only;
static uint8_t g_bus_ui_filter_bus_id;
static uint8_t g_bus_ui_filter_opcode_val;
static uint8_t g_bus_ui_prev_valid;
static uint8_t g_bus_ui_prev_len;
static uint8_t g_bus_ui_prev_data[BUS_CAPTURE_MAX_DATA];


void bus_ui_reset(void)
{
    g_bus_ui_count = 0;
    g_bus_ui_head = 0;
    g_bus_ui_prev_valid = 0;
}

static uint32_t bus_ui_mask_for_len(uint8_t len)
{
    if (len == 0)
        return 0;
    if (len >= 32u)
        return 0xFFFFFFFFu;
    return (uint32_t)((1u << len) - 1u);
}

static uint32_t bus_ui_diff_mask(const uint8_t *data, uint8_t len)
{
    if (!data || len == 0)
        return 0;
    if (!g_bus_ui_prev_valid)
        return bus_ui_mask_for_len(len);

    uint8_t max_len = (len > g_bus_ui_prev_len) ? len : g_bus_ui_prev_len;
    if (max_len > BUS_CAPTURE_MAX_DATA)
        max_len = BUS_CAPTURE_MAX_DATA;

    uint32_t mask = 0;
    for (uint8_t i = 0; i < max_len && i < 32u; ++i)
    {
        uint8_t a = (i < len) ? data[i] : 0;
        uint8_t b = (i < g_bus_ui_prev_len) ? g_bus_ui_prev_data[i] : 0;
        if (i >= len || i >= g_bus_ui_prev_len || a != b)
            mask |= (1u << i);
    }
    return mask;
}

static int bus_ui_match(uint8_t bus_id, const uint8_t *data, uint8_t len)
{
    if (g_bus_ui_filter_id && bus_id != g_bus_ui_filter_bus_id)
        return 0;
    if (g_bus_ui_filter_opcode)
    {
        if (len == 0)
            return 0;
        if (data[0] != g_bus_ui_filter_opcode_val)
            return 0;
    }
    return 1;
}

void bus_ui_set_control(uint8_t flags, uint8_t bus_id, uint8_t opcode)
{
    g_bus_ui_enabled = (flags & BUS_UI_FLAG_ENABLE) ? 1u : 0u;
    g_bus_ui_filter_id = (flags & BUS_UI_FLAG_FILTER_ID) ? 1u : 0u;
    g_bus_ui_filter_opcode = (flags & BUS_UI_FLAG_FILTER_OPCODE) ? 1u : 0u;
    g_bus_ui_diff_enabled = (flags & BUS_UI_FLAG_DIFF) ? 1u : 0u;
    g_bus_ui_changed_only = (flags & BUS_UI_FLAG_CHANGED_ONLY) ? 1u : 0u;
    g_bus_ui_filter_bus_id = bus_id;
    g_bus_ui_filter_opcode_val = opcode;
    if (flags & BUS_UI_FLAG_RESET)
        bus_ui_reset();
}

void bus_ui_on_capture(uint8_t bus_id, const uint8_t *data, uint8_t len, uint16_t dt_ms)
{
    if (!g_bus_ui_enabled)
        return;
    if (!bus_ui_match(bus_id, data, len))
        return;

    bus_ui_entry_t *entry = &g_bus_ui_view[g_bus_ui_head];
    entry->dt_ms = dt_ms;
    entry->bus_id = bus_id;
    entry->len = len;
    for (uint8_t i = 0; i < len; ++i)
        entry->data[i] = data[i];

    uint8_t diff_active = (g_bus_ui_diff_enabled || g_bus_ui_changed_only) ? 1u : 0u;
    entry->diff_mask = diff_active ? bus_ui_diff_mask(data, len) : 0u;

    g_bus_ui_head = (uint8_t)((g_bus_ui_head + 1u) % BUS_UI_VIEW_MAX);
    if (g_bus_ui_count < BUS_UI_VIEW_MAX)
        g_bus_ui_count++;

    g_bus_ui_prev_valid = 1u;
    g_bus_ui_prev_len = len;
    for (uint8_t i = 0; i < len; ++i)
        g_bus_ui_prev_data[i] = data[i];

}

void bus_ui_get_state(bus_ui_state_t *out)
{
    if (!out)
        return;
    out->count = g_bus_ui_count;
    out->diff_enabled = g_bus_ui_diff_enabled;
    out->changed_only = g_bus_ui_changed_only;
    out->filter_id = g_bus_ui_filter_id;
    out->filter_opcode = g_bus_ui_filter_opcode;
    out->filter_bus_id = g_bus_ui_filter_bus_id;
    out->filter_opcode_val = g_bus_ui_filter_opcode_val;
}

int bus_ui_get_last(bus_ui_entry_t *out)
{
    if (!out)
        return 0;
    if (!g_bus_ui_count)
        return 0;
    uint8_t idx = g_bus_ui_head ? (uint8_t)(g_bus_ui_head - 1u) : (uint8_t)(BUS_UI_VIEW_MAX - 1u);
    *out = g_bus_ui_view[idx];
    return 1;
}
