#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

#include "core.h"

/* Trip statistics */
typedef struct {
    uint32_t distance_dm;       /* Total distance in decimeters */
    uint32_t duration_s;        /* Total duration in seconds */
    uint32_t energy_wh_d10;     /* Energy in Wh * 10 */
    uint16_t max_speed_dmph;    /* Max speed in dmph */
    uint16_t avg_speed_dmph;    /* Avg speed in dmph */
    uint16_t max_power_w;       /* Max power in watts */
    uint16_t avg_power_w;       /* Avg power in watts */
} trip_stats_t;

/* Range estimation */
typedef struct {
    uint16_t estimate_dm;       /* Estimated range in decimeters */
    uint16_t confidence_pct;    /* Confidence 0-100 */
    uint16_t wh_per_km_d10;     /* Wh/km * 10 */
} range_estimate_t;

/* Graph summary */
typedef struct {
    uint8_t channel;
    uint8_t window;
    ringbuf_i16_summary_t summary;
    uint16_t period_ms;
    uint16_t window_ms;
} graph_summary_t;

/* API declarations */
void trip_reset(void);
void trip_update(uint32_t dt_ms);
void trip_snapshot(trip_stats_t *out);

void range_reset(void);
void range_update(uint16_t speed_dmph, uint16_t power_w, uint8_t soc_pct);
void range_get(range_estimate_t *out);

/* Range estimate globals (defined in main.c for now). */
extern uint16_t g_range_wh_per_mile_d10;
extern uint16_t g_range_est_d10;
extern uint8_t g_range_confidence;
extern uint16_t g_range_count;

void speed_rb_init(void);
void speed_rb_push(uint16_t speed_dmph);
void speed_rb_summary(ringbuf_i16_summary_t *out);

void graph_init(void);
void graph_on_input_all(void);
void graph_tick(void);
int graph_set_active(uint8_t channel, uint8_t window, uint8_t reset);
void graph_get_active(uint8_t *channel, uint8_t *window);
void graph_get_active_summary(graph_summary_t *out);

/* Graph window labels (seconds) - defined in telemetry.c. */
extern const uint16_t g_graph_window_s[];

#endif /* TELEMETRY_H */
