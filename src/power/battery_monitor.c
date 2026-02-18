#include "battery_monitor.h"

#include "battery_soc.h"
#include "drivers/spi_flash.h"
#include "platform/mmio.h"
#include "platform/hw.h"
#include "src/motor/motor_link.h"
#include "app_data.h"
#include "src/power/power.h"
#include "../util/bool_to_u8.h"

/* ADC1 base for AT32F403A (STM32F1-ish register layout). */
#define ADC1_BASE 0x40012400u
#define ADC_SR    (ADC1_BASE + 0x00u)
#define ADC_CR2   (ADC1_BASE + 0x08u)
#define ADC_DR    (ADC1_BASE + 0x4Cu)

#define ADC_CR2_START_BITS 0x500000u
#define ADC_EOC_READY_MASK (1u << 1)
#define ADC_DR_DATA_MASK 0x0FFFu

/* OEM cadence + filter geometry. */
#define BATTERY_SAMPLE_INTERVAL_MS 50u
#define BATTERY_FILTER_SIZE 10u
#define BATTERY_FILTER_MIN_SAMPLES 3u
#define BATTERY_FILTER_TRIMMED_DIVISOR ((uint8_t)(BATTERY_FILTER_SIZE - 2u))

/* OEM config block mirrors (see src/config/config.c). */
#define OEM_CFG_PRIMARY_ADDR 0x003FD000u
#define OEM_CFG_BACKUP_ADDR  0x003FB000u
#define OEM_CFG_SIZE         0xD0u
#define OEM_CFG_OFF_N69300   0x78u /* little-endian u32 */
#define OEM_CFG_OFF_N48      0x80u /* u8: 24/36/48 */

/* OEM accepted range (see sub_801AFxx). */
#define OEM_N69300_MIN 0xFE4Cu /* 65100 */
#define OEM_N69300_MAX 0x11F1Cu /* 73500 */
#define OEM_N69300_DEFAULT 69300u

typedef struct {
    uint16_t ring[BATTERY_FILTER_SIZE];
    uint8_t pos;
    uint8_t count;
    uint16_t last;
} batt_filter_t;

static struct {
    uint8_t inited;
    uint32_t n69300;        /* scale factor, in mV*4096/ADCcount-ish units */
    uint8_t nominal_v;      /* 24/36/48, 0=infer */
    uint32_t last_req_ms;
    uint8_t req_pending;
    uint32_t last_update_ms;
    batt_filter_t filt;
} g_batt;

static uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int oem_blob_valid(const uint8_t *buf)
{
    uint8_t all_zero = 1u;
    uint8_t all_ff = 1u;
    for (uint32_t i = 0; i < OEM_CFG_SIZE; ++i)
    {
        if (buf[i] != 0u)
            all_zero = 0u;
        if (buf[i] != 0xFFu)
            all_ff = 0u;
    }
    return (all_zero || all_ff) ? 0 : 1;
}

static void batt_filter_reset(batt_filter_t *f)
{
    if (!f)
        return;
    for (uint8_t i = 0u; i < BATTERY_FILTER_SIZE; ++i)
        f->ring[i] = 0u;
    f->pos = 0u;
    f->count = 0u;
    f->last = 0u;
}

static uint16_t batt_filter_push(batt_filter_t *f, uint16_t sample)
{
    if (!f)
        return 0u;

    f->ring[f->pos] = (uint16_t)(sample & ADC_DR_DATA_MASK);
    f->pos++;
    if (f->pos >= BATTERY_FILTER_SIZE)
        f->pos = 0u;
    if (f->count < BATTERY_FILTER_SIZE)
        f->count++;

    if (f->count < BATTERY_FILTER_MIN_SAMPLES)
    {
        /* Not enough for trimmed mean; return last sample. */
        f->last = sample;
        return sample;
    }

    uint32_t sum = 0u;
    uint16_t mn = 0xFFFFu;
    uint16_t mx = 0u;
    for (uint8_t i = 0; i < f->count; ++i)
    {
        uint16_t v = f->ring[i];
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }

    if (f->count >= BATTERY_FILTER_SIZE)
    {
        sum -= mn;
        sum -= mx;
        f->last = (uint16_t)(sum / (uint32_t)BATTERY_FILTER_TRIMMED_DIVISOR);
        return f->last;
    }

    /* Pre-fill: average all available samples. */
    f->last = (uint16_t)(sum / (uint32_t)f->count);
    return f->last;
}

static void battery_monitor_load_oem_params(void)
{
    uint8_t buf[OEM_CFG_SIZE];
    g_batt.n69300 = OEM_N69300_DEFAULT;
    g_batt.nominal_v = 0u; /* infer */

    spi_flash_read(OEM_CFG_PRIMARY_ADDR, buf, OEM_CFG_SIZE);
    if (!oem_blob_valid(buf))
    {
        spi_flash_read(OEM_CFG_BACKUP_ADDR, buf, OEM_CFG_SIZE);
        if (!oem_blob_valid(buf))
            return;
    }

    uint32_t n69300 = load_le32(&buf[OEM_CFG_OFF_N69300]);
    if (n69300 >= (uint32_t)OEM_N69300_MIN && n69300 <= (uint32_t)OEM_N69300_MAX)
        g_batt.n69300 = n69300;

    uint8_t n48 = buf[OEM_CFG_OFF_N48];
    if (n48 == 24u || n48 == 36u || n48 == 48u)
        g_batt.nominal_v = n48;
}

static inline void adc_start_conversion(void)
{
    /*
     * OEM helper (`sub_8010F3A`) sets 0x500000 in CR2 after calibration.
     * Setting bit22 (SWSTART-ish in this family) is also how our host sim
     * snapshots the next ADC reading.
     */
    mmio_write32(ADC_CR2, mmio_read32(ADC_CR2) | ADC_CR2_START_BITS);
}

static inline uint8_t adc_eoc(void)
{
    return bool_to_u8((mmio_read32(ADC_SR) & ADC_EOC_READY_MASK) != 0u);
}

static inline uint16_t adc_read_dr_12b(void)
{
    return (uint16_t)(mmio_read32(ADC_DR) & ADC_DR_DATA_MASK);
}

void battery_monitor_init(void)
{
    if (g_batt.inited)
        return;
    g_batt.inited = 1u;
    g_batt.last_req_ms = 0u;
    g_batt.req_pending = 0u;
    g_batt.last_update_ms = 0u;
    batt_filter_reset(&g_batt.filt);
    battery_monitor_load_oem_params();

    /* Start conversions so DR will have sane data when we begin sampling. */
    adc_start_conversion();
}

void battery_monitor_tick(uint32_t now_ms)
{
    if (!g_batt.inited)
        battery_monitor_init();

    /* OEM cadence: 0x32 (50ms) periodic that sets a "sample request" flag. */
    if (!g_batt.req_pending && (uint32_t)(now_ms - g_batt.last_req_ms) >= BATTERY_SAMPLE_INTERVAL_MS)
    {
        g_batt.last_req_ms = now_ms;
        g_batt.req_pending = 1u;
        adc_start_conversion();
    }

    if (!g_batt.req_pending)
        return;
    if (!adc_eoc())
        return;

    uint16_t raw = adc_read_dr_12b();
    g_batt.req_pending = 0u;

    uint16_t filt = batt_filter_push(&g_batt.filt, raw);
    uint32_t batt_mv = ((uint32_t)filt * (uint32_t)g_batt.n69300) >> 12;
    uint16_t batt_dV = (uint16_t)(batt_mv / 100u); /* truncate like OEM shifts/truncates */

    g_inputs.battery_dV = (int16_t)batt_dV;
    g_input_caps |= INPUT_CAP_BATT_V;
    g_inputs.last_ms = now_ms;
    g_batt.last_update_ms = now_ms;

    motor_proto_t proto = motor_link_get_active_proto();
    if (proto != MOTOR_PROTO_STX02_XOR && proto != MOTOR_PROTO_AUTH_XOR_CR)
    {
        g_motor.soc_pct = battery_soc_pct_from_mv(batt_mv, g_batt.nominal_v);
    }
}

bool battery_monitor_has_sample(void)
{
    return g_batt.last_update_ms != 0u;
}

uint32_t battery_monitor_last_update_ms(void)
{
    return g_batt.last_update_ms;
}
