/*
 * Trip Telemetry Module - Implementation
 *
 * Tracks and persists ride statistics.
 */

#include "trip.h"

#include <string.h>

#include "../../util/byteorder.h"
#include "../../util/crc32.h"

#ifndef HOST_TEST
#include "../../platform/time.h"
#include "../../storage/layout.h"
#include "../motor/app_data.h"

/* SPI flash API (from storage module) */
extern void spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
extern void spi_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len);
extern void spi_flash_erase_4k(uint32_t addr);
#else
/* Host test stubs */
extern volatile uint32_t g_ms;
#define TRIP_STORAGE_BASE 0x10000u
static void spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len) {
    (void)addr; memset(buf, 0, len);
}
static void spi_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len) {
    (void)addr; (void)buf; (void)len;
}
static void spi_flash_erase_4k(uint32_t addr) { (void)addr; }

/* Stub for g_outputs */
typedef struct { uint16_t cmd_power_w; } stub_outputs_t;
static stub_outputs_t g_outputs = {0};
#endif

#include "../core/math_util.h"

/*
 * Constants
 */
#define MM_PER_MILE 1609340u
#define MM_PER_KM   1000000u
#define TRIP_MOVING_THRESHOLD_DMPH 5u  /* >= 0.5 mph counts as moving */

/*
 * Module state
 */
static trip_acc_t     g_trip;
static trip_hist_t    g_trip_hist;
static trip_summary_t g_trip_last;
static uint8_t        g_trip_last_valid;

/*
 * Saturating add for uint32_t
 */
static uint32_t sat_add_u32(uint32_t a, uint32_t b)
{
    uint32_t out = a + b;
    if (out < a)
        return 0xFFFFFFFFu;
    return out;
}

/*
 * Compute expected CRC for trip summary
 */
static uint32_t trip_crc_expected(const trip_summary_t *ts)
{
    trip_summary_t tmp = *ts;
    tmp.crc32 = 0;
    return crc32_compute((const uint8_t *)&tmp, TRIP_STORAGE_SIZE);
}

/*
 * Validate trip summary from flash
 */
static int trip_summary_validate(const trip_summary_t *ts)
{
    if (!ts)
        return 0;
    if (ts->magic != TRIP_MAGIC || ts->version != TRIP_VERSION || ts->size != TRIP_STORAGE_SIZE)
        return 0;
    if (ts->crc32 != trip_crc_expected(ts))
        return 0;
    return 1;
}

/*
 * Store trip summary to flash
 */
static void trip_store_last(const trip_summary_t *ts)
{
    if (!ts)
        return;
    spi_flash_erase_4k(TRIP_STORAGE_BASE);
    spi_flash_write(TRIP_STORAGE_BASE, (const uint8_t *)ts, TRIP_STORAGE_SIZE);
}

/*
 * Load trip summary from flash
 */
static int trip_load_last(trip_summary_t *out)
{
    if (!out)
        return 0;
    spi_flash_read(TRIP_STORAGE_BASE, (uint8_t *)out, TRIP_STORAGE_SIZE);
    return trip_summary_validate(out);
}

/*
 * Create snapshot from accumulator
 */
static void trip_snapshot_from_acc(trip_snapshot_t *out, const trip_acc_t *acc)
{
    if (!out || !acc)
        return;

    out->distance_mm = acc->distance_mm;
    out->elapsed_ms  = acc->elapsed_ms;
    out->moving_ms   = acc->moving_ms;
    out->energy_mwh  = acc->energy_mwh;
    out->max_speed_dmph = acc->max_speed_dmph;

    /* Calculate average speed */
    if (acc->elapsed_ms > 0 && acc->distance_mm > 0) {
        uint64_t num = (uint64_t)acc->distance_mm * 3600000ull * 10ull;
        uint32_t den = (uint32_t)((uint64_t)MM_PER_MILE * (uint64_t)acc->elapsed_ms);
        if (den == 0)
            out->avg_speed_dmph = 0;
        else
            out->avg_speed_dmph = (uint16_t)divu64_32(num + (den / 2u), den);
    } else {
        out->avg_speed_dmph = 0;
    }

    /* Calculate efficiency metrics */
    if (acc->distance_mm > 0 && acc->energy_mwh > 0) {
        uint64_t num_wh_mile = (uint64_t)acc->energy_mwh * 10ull * (uint64_t)MM_PER_MILE;
        uint32_t den_mile = acc->distance_mm * 1000u;
        if (den_mile)
            out->wh_per_mile_d10 = (uint16_t)divu64_32(num_wh_mile + ((uint64_t)den_mile / 2ull), den_mile);
        else
            out->wh_per_mile_d10 = 0;

        uint64_t num_wh_km = (uint64_t)acc->energy_mwh * 10ull * (uint64_t)MM_PER_KM;
        if (den_mile)
            out->wh_per_km_d10 = (uint16_t)divu64_32(num_wh_km + ((uint64_t)den_mile / 2ull), den_mile);
        else
            out->wh_per_km_d10 = 0;
    } else {
        out->wh_per_mile_d10 = 0;
        out->wh_per_km_d10   = 0;
    }
}

/*
 * Public API
 */

void trip_init(void)
{
    memset(&g_trip, 0, sizeof(g_trip));
    memset(&g_trip_hist, 0, sizeof(g_trip_hist));
    memset(&g_trip_last, 0, sizeof(g_trip_last));
    g_trip_last_valid = 0;

    /* Try to load last trip from flash */
    if (trip_load_last(&g_trip_last)) {
        g_trip_last_valid = 1;
    }
}

void trip_reset_acc(void)
{
    memset(&g_trip, 0, sizeof(g_trip));
    memset(&g_trip_hist, 0, sizeof(g_trip_hist));
}

void trip_update(uint16_t speed_dmph, uint16_t power_w, uint8_t assist_mode,
                 uint8_t virtual_gear, uint8_t profile_id)
{
    if (g_trip.start_ms == 0)
        g_trip.start_ms = g_ms;

    if (g_trip.last_ms == 0)
        g_trip.last_ms = g_ms;

    uint32_t dt = g_ms - g_trip.last_ms;
    g_trip.last_ms = g_ms;

    if (dt == 0)
        return;

    /* Update elapsed time */
    g_trip.elapsed_ms = sat_add_u32(g_trip.elapsed_ms, dt);
    if (speed_dmph >= TRIP_MOVING_THRESHOLD_DMPH)
        g_trip.moving_ms = sat_add_u32(g_trip.moving_ms, dt);

    /* Update distance: speed_dmph * dt_ms * conversion */
    /* speed_dmph * 0.1 mph * dt_ms / 1000s * 1609340 mm/mile / 3600 s/hr */
    /* = speed_dmph * dt * 44.704 / 1000 mm */
    uint64_t dist_num = (uint64_t)speed_dmph * (uint64_t)dt * 44704ull + 500000ull;
    g_trip.distance_mm += (uint32_t)divu64_32(dist_num, 1000000u);

    /* Update energy */
    uint16_t pwr = power_w;
#ifndef HOST_TEST
    if (pwr == 0)
        pwr = g_outputs.cmd_power_w;
#endif
    if (pwr) {
        /* mWh = W * ms / 3600000, but we accumulate mWh */
        uint64_t mwh_num = ((uint64_t)pwr * (uint64_t)dt + 1799ull);
        g_trip.energy_mwh += (uint32_t)divu64_32(mwh_num, 3600u);
    }

    /* Update max speed */
    if (speed_dmph > g_trip.max_speed_dmph)
        g_trip.max_speed_dmph = speed_dmph;

    /* Update assist time histogram */
    uint8_t am = (assist_mode > 2u) ? 0u : assist_mode;
    g_trip.assist_time_ms[am] = sat_add_u32(g_trip.assist_time_ms[am], dt);

    /* Update gear time */
    if (virtual_gear >= 1 && virtual_gear <= 12u)
        g_trip.gear_time_ms[virtual_gear - 1u] = sat_add_u32(g_trip.gear_time_ms[virtual_gear - 1u], dt);

    /* Update histograms */
    if (assist_mode == 1u && profile_id < HIST_ASSIST_BINS)
        g_trip_hist.assist_ms[profile_id] = sat_add_u32(g_trip_hist.assist_ms[profile_id], dt);
    if (virtual_gear >= 1 && virtual_gear <= HIST_GEAR_BINS)
        g_trip_hist.gear_ms[virtual_gear - 1u] = sat_add_u32(g_trip_hist.gear_ms[virtual_gear - 1u], dt);
    {
        uint16_t bin = (uint16_t)(pwr / HIST_POWER_BIN_W);
        if (bin >= HIST_POWER_BINS)
            bin = HIST_POWER_BINS - 1u;
        g_trip_hist.power_ms[bin] = sat_add_u32(g_trip_hist.power_ms[bin], dt);
    }

    g_trip.samples++;
}

void trip_finalize_and_persist(void)
{
    trip_snapshot_t snap;
    trip_snapshot_from_acc(&snap, &g_trip);

    g_trip_last.magic = TRIP_MAGIC;
    g_trip_last.version = TRIP_VERSION;
    g_trip_last.size = TRIP_STORAGE_SIZE;
    g_trip_last.reserved = 0;
    g_trip_last.snap = snap;
    g_trip_last.crc32 = 0;
    g_trip_last.crc32 = trip_crc_expected(&g_trip_last);

    trip_store_last(&g_trip_last);
    g_trip_last_valid = 1;
    trip_reset_acc();
}

void trip_get_current(trip_snapshot_t *out)
{
    if (out)
        trip_snapshot_from_acc(out, &g_trip);
}

int trip_get_last(trip_snapshot_t *out)
{
    if (!out)
        return 0;
    if (!g_trip_last_valid)
        return 0;
    *out = g_trip_last.snap;
    return 1;
}

int trip_last_valid(void)
{
    return g_trip_last_valid ? 1 : 0;
}

const trip_hist_t *trip_get_histogram(void)
{
    return &g_trip_hist;
}

const trip_acc_t *trip_get_acc(void)
{
    return &g_trip;
}

void trip_snapshot_to_be(uint8_t *dst, const trip_snapshot_t *s)
{
    if (!dst || !s)
        return;
    store_be32(&dst[0], s->distance_mm);
    store_be32(&dst[4], s->elapsed_ms);
    store_be32(&dst[8], s->moving_ms);
    store_be32(&dst[12], s->energy_mwh);
    store_be16(&dst[16], s->max_speed_dmph);
    store_be16(&dst[18], s->avg_speed_dmph);
    store_be16(&dst[20], s->wh_per_mile_d10);
    store_be16(&dst[22], s->wh_per_km_d10);
}
