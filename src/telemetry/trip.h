/*
 * Trip Telemetry Module
 *
 * Tracks ride statistics: distance, time, energy, speed.
 * Persists last trip to SPI flash for resume after power-off.
 *
 * Usage:
 *   1. trip_init() on startup (loads last trip from flash)
 *   2. trip_update() every main loop iteration with current data
 *   3. trip_finalize() when ride ends (persists to flash)
 *   4. trip_get_current()/trip_get_last() for UI display
 */

#ifndef TELEMETRY_TRIP_H
#define TELEMETRY_TRIP_H

#include <stdint.h>
#include <stddef.h>

/*
 * Trip snapshot - point-in-time statistics
 */
typedef struct {
    uint32_t distance_mm;     /* Total distance in millimeters */
    uint32_t elapsed_ms;      /* Total elapsed time in milliseconds */
    uint32_t moving_ms;       /* Time spent moving (speed > threshold) */
    uint32_t energy_mwh;      /* Energy consumed in milliwatt-hours */
    uint16_t max_speed_dmph;  /* Maximum speed in deci-mph (0.1 mph) */
    uint16_t avg_speed_dmph;  /* Average speed in deci-mph */
    uint16_t wh_per_mile_d10; /* Efficiency: Wh/mile * 10 */
    uint16_t wh_per_km_d10;   /* Efficiency: Wh/km * 10 */
} trip_snapshot_t;

/*
 * Trip accumulator - running totals updated each tick
 */
typedef struct {
    uint32_t start_ms;          /* Trip start timestamp */
    uint32_t last_ms;           /* Last update timestamp */
    uint32_t elapsed_ms;        /* Total elapsed time */
    uint32_t moving_ms;         /* Time spent moving */
    uint32_t distance_mm;       /* Total distance */
    uint32_t energy_mwh;        /* Total energy consumed */
    uint16_t max_speed_dmph;    /* Maximum speed seen */
    uint32_t samples;           /* Number of updates */
    uint32_t assist_time_ms[3]; /* Time per assist mode: 0=off, 1=assist, 2=walk */
    uint32_t gear_time_ms[12];  /* Time per virtual gear (1-12) */
} trip_acc_t;

/*
 * Trip summary - stored in flash
 */
#define TRIP_MAGIC   0x54524950u  /* 'TRIP' */
#define TRIP_VERSION 1u
#define TRIP_STORAGE_SIZE 36u

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  size;
    uint16_t reserved;
    trip_snapshot_t snap;
    uint32_t crc32;
} trip_summary_t;

/*
 * Histogram bins for detailed statistics
 */
#define HIST_ASSIST_BINS 5u   /* Per-profile assist time */
#define HIST_GEAR_BINS   12u  /* Per-gear time */
#define HIST_POWER_BINS  16u  /* Power distribution (0-1500W in 100W bins) */
#define HIST_POWER_BIN_W 100u

typedef struct {
    uint32_t assist_ms[HIST_ASSIST_BINS];
    uint32_t gear_ms[HIST_GEAR_BINS];
    uint32_t power_ms[HIST_POWER_BINS];
} trip_hist_t;

/*
 * Initialize trip module
 *
 * Loads last trip from flash if valid.
 * Call once at startup.
 */
void trip_init(void);

/*
 * Reset current trip accumulator
 *
 * Clears all running totals. Does NOT finalize or persist.
 */
void trip_reset_acc(void);

/*
 * Update trip with current data
 *
 * Call every main loop iteration (or at regular intervals).
 *
 * Args:
 *   speed_dmph   - Current speed in deci-mph
 *   power_w      - Current power in watts (0 to use g_outputs.cmd_power_w)
 *   assist_mode  - Current assist mode (0=off, 1=assist, 2=walk)
 *   virtual_gear - Current virtual gear (1-12)
 *   profile_id   - Current profile ID (0-4)
 */
void trip_update(uint16_t speed_dmph, uint16_t power_w, uint8_t assist_mode,
                 uint8_t virtual_gear, uint8_t profile_id);

/*
 * Finalize current trip and persist to flash
 *
 * Creates snapshot, stores to flash, resets accumulator.
 * Call when ride ends (power off, explicit reset, etc.).
 */
void trip_finalize_and_persist(void);

/*
 * Get snapshot of current trip
 *
 * Args:
 *   out - Destination for snapshot
 */
void trip_get_current(trip_snapshot_t *out);

/*
 * Get last persisted trip
 *
 * Args:
 *   out - Destination for snapshot
 *
 * Returns:
 *   1 if valid last trip exists, 0 otherwise
 */
int trip_get_last(trip_snapshot_t *out);

/*
 * Check if last trip is valid
 */
int trip_last_valid(void);

/*
 * Get pointer to histogram data (for detailed stats)
 */
const trip_hist_t *trip_get_histogram(void);

/*
 * Get raw accumulator (for debugging/testing)
 */
const trip_acc_t *trip_get_acc(void);

/*
 * Serialize snapshot to big-endian byte array (24 bytes)
 *
 * For protocol transmission.
 */
void trip_snapshot_to_be(uint8_t *dst, const trip_snapshot_t *s);

#endif /* TELEMETRY_TRIP_H */
