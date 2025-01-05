/*
 * Telemetry Module
 *
 * Trip data, range estimation, and graph data collection.
 */

#include "telemetry.h"

#include <string.h>

#include "app_data.h"
#include "app_state.h"
#include "platform/time.h"
#include "src/control/control.h"
#include "src/power/power.h"
#include "storage/logs.h"
#include "util/byteorder.h"

/* Range estimation constants */
#define RANGE_SAMPLE_MAX 32u
#define RANGE_SAMPLE_MIN 8u
#define RANGE_BATTERY_WH 500u
#define RANGE_SPEED_MIN_DMPH 10u

static uint16_t g_range_samples[RANGE_SAMPLE_MAX];
static uint8_t g_range_head;
static uint8_t g_range_sample_count;
static uint64_t g_range_sum;
static uint64_t g_range_sumsq;

uint16_t g_range_wh_per_mile_d10;
uint16_t g_range_est_d10;
uint8_t g_range_confidence;
uint16_t g_range_count;

static uint16_t range_sample_wh_per_mile_d10(uint16_t speed_dmph, uint16_t power_w)
{
    if (speed_dmph < RANGE_SPEED_MIN_DMPH || power_w == 0u)
        return 0u;
    return (uint16_t)(((uint32_t)power_w * 100u + (uint32_t)(speed_dmph / 2u)) / (uint32_t)speed_dmph);
}

void range_reset(void)
{
    memset(g_range_samples, 0, sizeof(g_range_samples));
    g_range_head = 0u;
    g_range_sample_count = 0u;
    g_range_sum = 0u;
    g_range_sumsq = 0u;
    g_range_wh_per_mile_d10 = 0u;
    g_range_est_d10 = 0u;
    g_range_confidence = 0u;
    g_range_count = 0u;
}

void range_update(uint16_t speed_dmph, uint16_t power_w, uint8_t soc_pct)
{
    uint16_t sample = range_sample_wh_per_mile_d10(speed_dmph, power_w);
    if (sample == 0u)
        return;

    if (g_range_sample_count < RANGE_SAMPLE_MAX)
    {
        g_range_samples[g_range_head] = sample;
        g_range_sum += sample;
        g_range_sumsq += (uint64_t)sample * (uint64_t)sample;
        g_range_sample_count++;
    }
    else
    {
        uint16_t old = g_range_samples[g_range_head];
        g_range_samples[g_range_head] = sample;
        g_range_sum -= old;
        g_range_sumsq -= (uint64_t)old * (uint64_t)old;
        g_range_sum += sample;
        g_range_sumsq += (uint64_t)sample * (uint64_t)sample;
    }

    g_range_head = (uint8_t)((g_range_head + 1u) % RANGE_SAMPLE_MAX);
    g_range_count = g_range_sample_count;

    if (g_range_sample_count == 0u)
        return;

    uint64_t mean = g_range_sum / g_range_sample_count;
    g_range_wh_per_mile_d10 = (uint16_t)mean;

    uint64_t mean_sq = mean * mean;
    uint64_t avg_sq = g_range_sumsq / g_range_sample_count;
    uint64_t var = (avg_sq > mean_sq) ? (avg_sq - mean_sq) : 0u;

    uint8_t conf = 0u;
    if (mean_sq > 0u)
    {
        uint64_t ratio = (var * 100u) / mean_sq;
        if (ratio > 100u)
            ratio = 100u;
        conf = (uint8_t)(100u - (uint8_t)ratio);
        if (g_range_sample_count < RANGE_SAMPLE_MIN)
            conf = (uint8_t)((uint16_t)conf * g_range_sample_count / RANGE_SAMPLE_MIN);
    }
    g_range_confidence = conf;

    uint32_t available_wh = ((uint32_t)RANGE_BATTERY_WH * (uint32_t)soc_pct + 50u) / 100u;
    if (g_range_wh_per_mile_d10 > 0u)
    {
        uint32_t est = (available_wh * 100u + (g_range_wh_per_mile_d10 / 2u)) / g_range_wh_per_mile_d10;
        if (est > 0xFFFFu)
            est = 0xFFFFu;
        g_range_est_d10 = (uint16_t)est;
    }
    else
    {
        g_range_est_d10 = 0u;
    }
}

void range_get(range_estimate_t *out)
{
    if (!out)
        return;
    out->estimate_dm = g_range_est_d10;
    out->confidence_pct = g_range_confidence;
    out->wh_per_km_d10 = (uint16_t)(((uint32_t)g_range_wh_per_mile_d10 * 1000u) / 1609u);
}

/* -------------------------------------------------------------
 * Speed ring buffer
 * ------------------------------------------------------------- */
static ringbuf_i16_t g_speed_rb;
static int16_t g_speed_storage[64];     /* power-of-two for O(1) wrap */
static uint16_t g_speed_min_idx[64];
static uint16_t g_speed_max_idx[64];

void speed_rb_init(void)
{
    ringbuf_i16_init(&g_speed_rb, g_speed_storage,
                     (uint16_t)(sizeof(g_speed_storage) / sizeof(g_speed_storage[0])),
                     g_speed_min_idx, g_speed_max_idx);
    ringbuf_i16_reset(&g_speed_rb);
}

void speed_rb_push(uint16_t speed_dmph)
{
    ringbuf_i16_push(&g_speed_rb, (int16_t)speed_dmph);
}

void speed_rb_summary(ringbuf_i16_summary_t *out)
{
    if (!out)
        return;
    ringbuf_i16_summary(&g_speed_rb, out);
}

/* -------------------------------------------------------------
 * Multi-channel strip charts (downsampled, fixed memory)
 * ------------------------------------------------------------- */
typedef enum {
    GRAPH_CH_SPEED = 0,
    GRAPH_CH_POWER = 1,
    GRAPH_CH_VOLT  = 2,
    GRAPH_CH_CAD   = 3,
    GRAPH_CH_TEMP  = 4,
    GRAPH_CH_COUNT = 5
} graph_channel_t;

typedef enum {
    GRAPH_WIN_30S = 0,
    GRAPH_WIN_2M  = 1,
    GRAPH_WIN_10M = 2,
    GRAPH_WIN_COUNT = 3
} graph_window_t;

#define GRAPH_CAPACITY 256u /* power-of-two */
#define GRAPH_WINDOW_30S_MS 30000u
#define GRAPH_WINDOW_2M_MS  120000u
#define GRAPH_WINDOW_10M_MS 600000u
#define GRAPH_PERIOD_MS(window_ms) ((uint16_t)(((window_ms) + (GRAPH_CAPACITY / 2u)) / GRAPH_CAPACITY))

static const uint16_t g_graph_period_ms[GRAPH_WIN_COUNT] = {
    GRAPH_PERIOD_MS(GRAPH_WINDOW_30S_MS),
    GRAPH_PERIOD_MS(GRAPH_WINDOW_2M_MS),
    GRAPH_PERIOD_MS(GRAPH_WINDOW_10M_MS),
};
const uint16_t g_graph_window_s[GRAPH_WIN_COUNT] = {
    30u, 120u, 600u
};

static ringbuf_i16_t g_graph_rb[GRAPH_CH_COUNT][GRAPH_WIN_COUNT];
static int16_t g_graph_storage[GRAPH_CH_COUNT][GRAPH_WIN_COUNT][GRAPH_CAPACITY];
static uint16_t g_graph_min_idx[GRAPH_CH_COUNT][GRAPH_WIN_COUNT][GRAPH_CAPACITY];
static uint16_t g_graph_max_idx[GRAPH_CH_COUNT][GRAPH_WIN_COUNT][GRAPH_CAPACITY];
static uint32_t g_graph_last_tick_ms[GRAPH_CH_COUNT][GRAPH_WIN_COUNT];
static int16_t g_graph_last_value[GRAPH_CH_COUNT];
static int16_t g_graph_pending_value[GRAPH_CH_COUNT];
static uint8_t g_graph_pending[GRAPH_CH_COUNT];
static uint8_t g_graph_enabled[GRAPH_CH_COUNT];
static uint8_t g_graph_active_channel = GRAPH_CH_SPEED;
static uint8_t g_graph_active_window = GRAPH_WIN_30S;

static uint16_t graph_window_ms(uint16_t capacity, uint16_t period_ms)
{
    uint32_t window = (uint32_t)capacity * (uint32_t)period_ms;
    if (window > 0xFFFFu)
        window = 0xFFFFu;
    return (uint16_t)window;
}

static int16_t graph_channel_sample(uint8_t channel)
{
    switch (channel)
    {
    case GRAPH_CH_SPEED: return (int16_t)g_inputs.speed_dmph;
    case GRAPH_CH_POWER: return (int16_t)g_inputs.power_w;
    case GRAPH_CH_VOLT:  return g_inputs.battery_dV;
    case GRAPH_CH_CAD:   return (int16_t)g_inputs.cadence_rpm;
    case GRAPH_CH_TEMP:  return g_inputs.ctrl_temp_dC;
    default:             return 0;
    }
}

static void graph_reset_channel(uint8_t channel, int16_t seed)
{
    if (channel >= GRAPH_CH_COUNT)
        return;
    for (uint8_t win = 0; win < GRAPH_WIN_COUNT; ++win)
    {
        ringbuf_i16_reset(&g_graph_rb[channel][win]);
        uint16_t period = g_graph_period_ms[win];
        if (period == 0)
            g_graph_last_tick_ms[channel][win] = g_ms;
        else
            g_graph_last_tick_ms[channel][win] = g_ms - (g_ms % period);
    }
    g_graph_last_value[channel] = seed;
    g_graph_pending_value[channel] = seed;
    g_graph_pending[channel] = 0;
    g_graph_enabled[channel] = 1;
}

static void graph_on_input_channel(uint8_t channel, int16_t sample)
{
    if (channel >= GRAPH_CH_COUNT)
        return;
    if (!g_graph_enabled[channel])
        graph_reset_channel(channel, sample);
    g_graph_pending_value[channel] = sample;
    g_graph_pending[channel] = 1;
}

void graph_init(void)
{
    for (uint8_t ch = 0; ch < GRAPH_CH_COUNT; ++ch)
    {
        for (uint8_t win = 0; win < GRAPH_WIN_COUNT; ++win)
        {
            ringbuf_i16_init(&g_graph_rb[ch][win], g_graph_storage[ch][win],
                             (uint16_t)(sizeof(g_graph_storage[ch][win]) / sizeof(g_graph_storage[ch][win][0])),
                             g_graph_min_idx[ch][win], g_graph_max_idx[ch][win]);
            ringbuf_i16_reset(&g_graph_rb[ch][win]);
            g_graph_last_tick_ms[ch][win] = 0;
        }
        g_graph_enabled[ch] = 0;
        g_graph_pending[ch] = 0;
        g_graph_last_value[ch] = 0;
        g_graph_pending_value[ch] = 0;
    }
    g_graph_active_channel = GRAPH_CH_SPEED;
    g_graph_active_window = GRAPH_WIN_30S;
}

void graph_on_input_all(void)
{
    for (uint8_t ch = 0; ch < GRAPH_CH_COUNT; ++ch)
        graph_on_input_channel(ch, graph_channel_sample(ch));
}

void graph_tick(void)
{
    uint32_t now = g_ms;
    for (uint8_t ch = 0; ch < GRAPH_CH_COUNT; ++ch)
    {
        if (!g_graph_enabled[ch])
            continue;
        for (uint8_t win = 0; win < GRAPH_WIN_COUNT; ++win)
        {
            uint16_t period = g_graph_period_ms[win];
            if (period == 0)
                continue;
            while ((uint32_t)(now - g_graph_last_tick_ms[ch][win]) >= period)
            {
                int16_t sample = g_graph_pending[ch] ? g_graph_pending_value[ch] : g_graph_last_value[ch];
                ringbuf_i16_push(&g_graph_rb[ch][win], sample);
                g_graph_last_value[ch] = sample;
                g_graph_pending[ch] = 0;
                g_graph_last_tick_ms[ch][win] += period;
            }
        }
    }
}

int graph_set_active(uint8_t channel, uint8_t window, uint8_t reset)
{
    if (channel >= GRAPH_CH_COUNT || window >= GRAPH_WIN_COUNT)
        return 0;
    g_graph_active_channel = channel;
    g_graph_active_window = window;
    if (reset)
        graph_reset_channel(channel, graph_channel_sample(channel));
    return 1;
}

void graph_get_active(uint8_t *channel, uint8_t *window)
{
    if (channel)
        *channel = g_graph_active_channel;
    if (window)
        *window = g_graph_active_window;
}

void graph_get_active_summary(graph_summary_t *out)
{
    if (!out)
        return;
    ringbuf_i16_summary_t s;
    ringbuf_i16_summary(&g_graph_rb[g_graph_active_channel][g_graph_active_window], &s);
    out->channel = g_graph_active_channel;
    out->window = g_graph_active_window;
    out->summary = s;
    out->period_ms = g_graph_period_ms[g_graph_active_window];
    out->window_ms = graph_window_ms(s.capacity, out->period_ms);
}
