/*
 * System control helpers shared across app logic.
 */

#include "src/system_control.h"
#include "src/app_state.h"
#include "src/input/input.h"
#include "drivers/spi_flash.h"

void request_bootloader_recovery(uint8_t long_press_mask)
{
    const uint8_t combo = (uint8_t)(UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
    if ((long_press_mask & combo) != combo)
        return;
    if (g_request_soft_reboot == 1u)
        return;
    spi_flash_set_bootloader_mode_flag();
    g_request_soft_reboot = 1u;
}
