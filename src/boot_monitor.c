#include "src/boot_monitor.h"

#include "drivers/spi_flash.h"
#include "platform/hw.h"
#include "platform/mmio.h"
#include "platform/time.h"
#include "platform/cpu.h"
#include "src/boot_phase.h"
#include "src/comm/comm.h"
#include "storage/crash_dump.h"
#include "storage/layout.h"
#include "util/byteorder.h"

extern uint16_t g_reset_flags;
extern uint32_t g_reset_csr;

enum {
    PANIC_MONITOR_TIMEOUT_MS = 15000u,
};

static volatile uint8_t g_continue;

void boot_monitor_request_continue(void)
{
    g_continue = 1u;
}

uint8_t boot_monitor_should_continue(void)
{
    return g_continue ? 1u : 0u;
}

void boot_monitor_clear_continue(void)
{
    g_continue = 0u;
}

static int boot_stage_read_last(uint32_t *code_out, uint32_t *ms_out)
{
    if (code_out)
        *code_out = 0u;
    if (ms_out)
        *ms_out = 0u;

    uint8_t sec[SPI_FLASH_SECTOR_SIZE];
    spi_flash_read(BOOT_STAGE_STORAGE_BASE, sec, SPI_FLASH_SECTOR_SIZE);

    /* Entries are 8 bytes: code(be32), ms(be32). Scan for first erased code. */
    uint32_t last_code = 0u;
    uint32_t last_ms = 0u;
    uint8_t have = 0u;
    for (uint16_t i = 0; i < (SPI_FLASH_SECTOR_SIZE / 8u); ++i)
    {
        const uint8_t *e = &sec[i * 8u];
        uint32_t code = load_be32(&e[0]);
        uint32_t ms = load_be32(&e[4]);
        if (code == 0xFFFFFFFFu)
            break;
        last_code = code;
        last_ms = ms;
        have = 1u;
    }

    if (!have)
        return 0;
    if (code_out)
        *code_out = last_code;
    if (ms_out)
        *ms_out = last_ms;
    return 1;
}

uint8_t boot_monitor_build_info(uint8_t *out, uint8_t cap)
{
    /* Payload v1, 16 bytes:
     * [0]=ver(1) [1]=size(16) [2..3]=reset_flags [4..7]=reset_csr
     * [8]=crash_valid [9]=boot_stage_valid [10..13]=last_code [14..15]=last_ms_lo16
     */
    if (!out || cap < 16u)
        return 0;

    out[0] = 1u;
    out[1] = 16u;
    store_be16(&out[2], g_reset_flags);
    store_be32(&out[4], g_reset_csr);

    uint8_t crash_buf[CRASH_DUMP_SIZE];
    uint8_t crash_valid = crash_dump_load(crash_buf) ? 1u : 0u;
    out[8] = crash_valid;

    uint32_t last_code = 0u;
    uint32_t last_ms = 0u;
    uint8_t stage_valid = boot_stage_read_last(&last_code, &last_ms) ? 1u : 0u;
    out[9] = stage_valid;
    store_be32(&out[10], stage_valid ? last_code : 0u);
    store_be16(&out[14], stage_valid ? (uint16_t)(last_ms & 0xFFFFu) : 0u);

    return 16u;
}

void boot_monitor_run(void)
{
    uint32_t start_ms = g_ms;
    boot_monitor_clear_continue();
    while (!boot_monitor_should_continue())
    {
        platform_time_poll_1ms();
        mmio_write32(IWDG_KR, IWDG_KR_FEED);
        poll_uart_rx_ports();

        /* Panic monitor is best-effort: auto-exit to reset after a bounded window. */
        if (g_boot_phase == BOOT_PHASE_PANIC &&
            (uint32_t)(g_ms - start_ms) >= PANIC_MONITOR_TIMEOUT_MS)
        {
            break;
        }

        /* TIM2 interrupts (5ms) keep WFI waking even if no BLE traffic. */
        wfi();
    }
    /* Clear for next use (panic monitor reuses the same flag). */
    boot_monitor_clear_continue();
}
