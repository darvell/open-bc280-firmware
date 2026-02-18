/*
 * System control helpers shared across app logic.
 */

#include "src/system_control.h"
#include "src/app_state.h"
#include "src/input/input.h"
#include "drivers/spi_flash.h"
#include "src/app.h"
#include "platform/board_init.h"

#define KEY_SEQ_DELAY_MS 10u

typedef enum key_seq_state_t {
    KEY_SEQ_BOOT_WAIT_HIGH = 0,
    KEY_SEQ_RUN = 1,
    KEY_SEQ_RECOVERY_WAIT_HIGH = 2,
} key_seq_state_t;

static struct {
    key_seq_state_t state;
    uint32_t edge_ms;
    uint8_t fault_latched;
} g_key_seq;

void request_bootloader_recovery(uint8_t long_press_mask)
{
    const uint8_t combo = (uint8_t)(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
    if ((long_press_mask & combo) != combo)
        return;
    if (g_request_soft_reboot == REBOOT_REQUEST_BOOTLOADER)
        return;
    spi_flash_set_bootloader_mode_flag();
    g_request_soft_reboot = REBOOT_REQUEST_BOOTLOADER;
}

void system_control_key_sequencer_init(uint32_t now_ms)
{
    /*
     * OEM-style power/key sequencing:
     * - early init drives PB1 low
     * - after scheduler starts, PB1 is raised after a short delay
     */
    g_key_seq.state = KEY_SEQ_BOOT_WAIT_HIGH;
    g_key_seq.edge_ms = now_ms;
    g_key_seq.fault_latched = 0u;
    platform_key_output_set(0u);
}

void system_control_key_sequencer_tick(uint32_t now_ms, uint8_t link_fault_active, uint8_t reboot_req)
{
    if (reboot_req != REBOOT_REQUEST_NONE)
        return;

    switch (g_key_seq.state)
    {
        case KEY_SEQ_BOOT_WAIT_HIGH:
            if ((uint32_t)(now_ms - g_key_seq.edge_ms) >= KEY_SEQ_DELAY_MS)
            {
                platform_key_output_set(1u);
                g_key_seq.state = KEY_SEQ_RUN;
            }
            break;

        case KEY_SEQ_RUN:
            /*
             * Match OEM case6->case1 behavior on comm-loss recovery:
             * pulse PB1 low, then re-assert high after a short delay.
             */
            if (link_fault_active)
            {
                if (!g_key_seq.fault_latched)
                {
                    g_key_seq.fault_latched = 1u;
                    g_key_seq.edge_ms = now_ms;
                    platform_key_output_set(0u);
                    g_key_seq.state = KEY_SEQ_RECOVERY_WAIT_HIGH;
                }
            }
            else
            {
                g_key_seq.fault_latched = 0u;
            }
            break;

        case KEY_SEQ_RECOVERY_WAIT_HIGH:
            if ((uint32_t)(now_ms - g_key_seq.edge_ms) >= KEY_SEQ_DELAY_MS)
            {
                platform_key_output_set(1u);
                g_key_seq.state = KEY_SEQ_RUN;
            }
            break;

        default:
            g_key_seq.state = KEY_SEQ_RUN;
            break;
    }
}
