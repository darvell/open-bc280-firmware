#include <stdint.h>
#include <stddef.h>
#include "app.h"
#include "app_state.h"
#include "app_data.h"
#include "core.h"
#include "src/core/trace_format.h"
#include "ui.h"
#include "ui_state.h"
#include "src/control/control.h"
#include "src/power/power.h"
#include "src/power/battery_monitor.h"
#include "src/input/input.h"
#include "src/input/oem_buttons.h"
#include "src/motor/shengyi.h"
#include "src/motor/motor_cmd.h"
#include "src/motor/motor_isr.h"
#include "src/motor/motor_link.h"
#include "src/kernel/event_queue.h"
#include "src/bus/bus.h"
#include "src/comm/comm.h"
#include "src/profiles/profiles.h"
#include "src/telemetry/trip.h"
#include "src/telemetry/telemetry.h"
#include "src/config/config.h"
#include "platform/clock.h"
#include "platform/cpu.h"
#include "platform/hw.h"
#include "platform/mmio.h"
#include "platform/time.h"
#include "platform/board_init.h"
#include "platform/early_init.h"
#include "drivers/spi_flash.h"
#include "drivers/uart.h"
#include "boot_log.h"
#include "storage/layout.h"
#include "storage/boot_stage.h"
#include "storage/logs.h"
#include "storage/ab_update.h"
#include "storage/crash_dump.h"
#include "util/byteorder.h"
#include "util/crc32.h"
#include "src/core/math_util.h"
#include "src/system_control.h"
#include "src/boot_phase.h"
#include "src/boot_monitor.h"

static inline uint16_t hw_gpio_read_idr(char port)
{
    uint32_t base = 0;
    switch (port)
    {
        case 'C': base = GPIOC_BASE; break;
        default: return 0;
    }
    return (uint16_t)mmio_read32(GPIO_IDR(base));
}

static void platform_power_hold_early(void)
{
    /* OEM app v2.5.1: PB1 is driven low during early init (likely motor/controller KEY/enable). */
    const uint32_t RCC_APB2ENR_IOPA = (1u << 2);
    const uint32_t RCC_APB2ENR_IOPB = (1u << 3);
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPA | RCC_APB2ENR_IOPB);
    uint32_t crl = mmio_read32(GPIO_CRL(GPIOB_BASE));
    crl = (crl & ~(0xFu << 4)) | (0x2u << 4); /* PB1 output push-pull @ 2MHz */
    mmio_write32(GPIO_CRL(GPIOB_BASE), crl);
    mmio_write32(GPIO_BRR(GPIOB_BASE), (1u << 1));
    /* Keep PA8 (backlight PWM) low until TIM1 is initialized. */
    uint32_t crh = mmio_read32(GPIO_CRH(GPIOA_BASE));
    crh = (crh & ~(0xFu << 0)) | 0x2u; /* PA8 output push-pull @ 2MHz */
    mmio_write32(GPIO_CRH(GPIOA_BASE), crh);
    mmio_write32(GPIO_BRR(GPIOA_BASE), (1u << 8));
}

static uint32_t uart_brr_div(uint32_t pclk_hz, uint32_t baud)
{
    if (!pclk_hz || !baud)
        return 7500u;
    uint32_t div = (pclk_hz + (baud / 2u)) / baud;
    return div ? div : 7500u;
}

static void uart1_init_9600(void)
{
    const uint32_t baud = 9600u;
    uint32_t pclk2 = rcc_get_pclk_hz_fallback(1u);
    uart_init_basic(UART1_BASE, uart_brr_div(pclk2, baud));
}

static void monitor_enter(boot_phase_t phase, uint8_t reinit_timebase)
{
    disable_irqs();
    if (reinit_timebase)
    {
        platform_clock_init();
        platform_nvic_init();
        platform_timebase_init_oem();
    }

    platform_ble_control_pins_init_early();
    platform_uart1_pins_init_early();
    uart1_init_9600();

    /* Boot monitor only speaks over BLE UART1 by default. */
    g_comm_skip_uart2 = 1;
    g_boot_phase = phase;

    platform_uart_irq_init();
    enable_irqs();

    boot_monitor_run();

    disable_irqs();
    g_boot_phase = BOOT_PHASE_MONITOR;
}

/* -------------------------------------------------------------
 * Core + board constants (BC280 platform description)
 * ------------------------------------------------------------- */


#define RESET_FLAG_BOR  (1u << 0)
#define RESET_FLAG_PIN  (1u << 1)
#define RESET_FLAG_POR  (1u << 2)
#define RESET_FLAG_SOFT (1u << 3)
#define RESET_FLAG_IWDG (1u << 4)
#define RESET_FLAG_WWDG (1u << 5)
#define RESET_FLAG_LPWR (1u << 6)

uint32_t g_last_print;
uint16_t g_reset_flags;
uint32_t g_reset_csr;
uint32_t g_inputs_debug_last_ms;
static uint32_t g_buttons_last_sample_ms;
uint32_t g_stream_period_ms = 0;   /* 0 = off */
uint32_t g_last_stream_ms = 0;
uint16_t g_curve_power_w;
uint16_t g_curve_cadence_q15;
uint16_t g_effective_cap_current_dA;
uint16_t g_effective_cap_speed_dmph;

uint8_t g_input_caps;
uint8_t g_headlight_enabled;

/* Walk/regen capability flags, button masks, and types from control.h */

uint8_t g_hw_caps = CAP_FLAG_WALK;
/* g_walk_*, g_regen defined in control.c */

/* Control types from control.h: cruise_mode_t, cruise_resume_reason_t, cruise_state_t,
 * drive_mode_t, drive_state_t, boost_state_t - globals defined in control.c */

/* Virtual gears - types and defines from control.h */
vgear_table_t  g_vgears;
cadence_bias_t g_cadence_bias;
uint8_t        g_active_vgear;   /* 1-based index */
uint16_t g_gear_limit_power_w;
uint16_t g_gear_scale_q15;
uint16_t g_cadence_bias_q15;

/* button_track_t and button globals from input.h (defined in input.c) */
/* g_cruise_toggle_request defined in control.c */

uint8_t g_active_profile_id = 0;
uint32_t g_last_profile_switch_ms = 0;
uint8_t g_debug_uart_mask = 0u;

/* Current boot phase, used to gate monitor-only commands. */
volatile boot_phase_t g_boot_phase = BOOT_PHASE_APP;

/* -------------------------------------------------------------
 * Config blob (versioned, CRC'd, double-buffered in SPI flash)
 * ------------------------------------------------------------- */
#define BUTTON_DEBUG_OVERRIDE_TIMEOUT_MS 250u

void process_buttons(uint8_t raw_buttons);

static uint8_t buttons_sample_oem_gpio(void)
{
    /* OEM app reads GPIOC IDR (PC0-4), pulled up; pressed = 0. */
    uint8_t idr = (uint8_t)(hw_gpio_read_idr('C') & OEM_BTN_MASK);
    uint8_t pressed = (uint8_t)(~idr) & OEM_BTN_MASK;
    uint8_t raw = (uint8_t)(pressed | OEM_BTN_VIRTUAL);
    if ((pressed & (OEM_BTN_UP | OEM_BTN_DOWN | OEM_BTN_LIGHT)) == 0u)
        raw = (uint8_t)(raw & (uint8_t)~OEM_BTN_VIRTUAL);
    return oem_buttons_map_raw(raw, &g_button_virtual);
}

void buttons_tick(void)
{
    if (g_ms == g_buttons_last_sample_ms)
        return;
    g_buttons_last_sample_ms = g_ms;

    if (g_inputs_debug_last_ms != 0u)
    {
        uint32_t delta = g_ms - g_inputs_debug_last_ms;
        if (delta <= BUTTON_DEBUG_OVERRIDE_TIMEOUT_MS)
            return;
    }

    process_buttons(buttons_sample_oem_gpio());
}

uint8_t g_last_brake_state;
uint8_t g_brake_edge;

reboot_request_t g_request_soft_reboot;
event_queue_t g_motor_events;

/* -------------------------------------------------------------
 * SysTick 1ms
 * ------------------------------------------------------------- */


/* -------------------------------------------------------------
 * Reset reason helpers
 * ------------------------------------------------------------- */
static uint16_t reset_flags_from_csr(uint32_t csr)
{
    uint16_t flags = 0;
    if (csr & RCC_CSR_BORRSTF)
        flags |= RESET_FLAG_BOR;
    if (csr & RCC_CSR_PINRSTF)
        flags |= RESET_FLAG_PIN;
    if (csr & RCC_CSR_PORRSTF)
        flags |= RESET_FLAG_POR;
    if (csr & RCC_CSR_SFTRSTF)
        flags |= RESET_FLAG_SOFT;
    if (csr & RCC_CSR_IWDGRSTF)
        flags |= RESET_FLAG_IWDG;
    if (csr & RCC_CSR_WWDGRSTF)
        flags |= RESET_FLAG_WWDG;
    if (csr & RCC_CSR_LPWRRSTF)
        flags |= RESET_FLAG_LPWR;
    return flags;
}

static void reset_flags_capture(void)
{
    uint32_t csr = mmio_read32(RCC_CSR);
    g_reset_csr = csr;
    g_reset_flags = reset_flags_from_csr(csr);
    /* Clear reset flags (RMVF) for next boot. */
    mmio_write32(RCC_CSR, RCC_CSR_RMVF);
}

/* -------------------------------------------------------------
 * Utilities
 * ------------------------------------------------------------- */
static void headlight_toggle_oem(void)
{
    g_headlight_enabled = g_headlight_enabled ? 0u : 1u;
    shengyi_request_update(1u);
}

static void system_reset(void)
{
    platform_key_output_set(0u);
    mmio_write32(SCB_AIRCR, SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ);
    while (1)
        ;
}

static void watchdog_start_runtime(void)
{
    /*
     * Start IWDG with a long timeout (~26s at 40kHz LSI, /256 prescaler)
     * so normal runtime can survive long flash/IO operations while still
     * guaranteeing eventual recovery from hangs.
     */
    mmio_write32(IWDG_KR, IWDG_KR_UNLOCK);
    mmio_write32(IWDG_PR, 0x6u);      /* /256 */
    mmio_write32(IWDG_RLR, 0x0FFFu);  /* max reload */
    mmio_write32(IWDG_KR, IWDG_KR_START);
    mmio_write32(IWDG_KR, IWDG_KR_FEED);
}

void watchdog_feed_runtime(void)
{
    mmio_write32(IWDG_KR, IWDG_KR_FEED);
}

__attribute__((used)) static void hardfault_capture(uint32_t *stack)
{
    disable_irqs();
    uint32_t sp = (uint32_t)stack;
    uint32_t lr = stack[5];
    uint32_t pc = stack[6];
    uint32_t psr = stack[7];
    crash_dump_capture(sp, lr, pc, psr);
    /* Enter a minimal UART1 boot monitor so the host can fetch crash_dump/memory
     * before we reset. */
    monitor_enter(BOOT_PHASE_PANIC, 1u);
    system_reset();
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm__ volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b hardfault_capture\n");
}


/* Trip telemetry moved to src/telemetry/trip.c */


/* -------------------------------------------------------------
 * Virtual gear + cadence helpers
 * ------------------------------------------------------------- */

void process_buttons(uint8_t raw_buttons)
{
    uint8_t mapped_buttons = button_map_apply(raw_buttons, g_config_active.button_map);
    g_inputs.buttons = mapped_buttons;
    wizard_handle_buttons(mapped_buttons);
    if (wizard_is_active())
    {
        g_inputs.buttons = 0;
        g_lock_active = 0;
        g_lock_allowed_mask = 0;
        button_track_reset();
    }
    else
    {
        g_lock_allowed_mask = lock_allowed_mask(g_config_active.button_flags);
        g_lock_active = lock_should_apply(g_config_active.button_flags);
        if (g_lock_active)
            g_inputs.buttons = (uint8_t)(g_inputs.buttons & g_lock_allowed_mask);
        button_track_update(g_inputs.buttons, g_lock_active ? g_lock_allowed_mask : 0xFFu, 0);

        if (g_button_virtual && !g_button_virtual_prev)
        {
            uint8_t enable = bus_capture_get_enabled() ? 0u : 1u;
            bus_capture_set_enabled(enable, enable);
        }
        g_button_virtual_prev = g_button_virtual;

        if ((g_button_short_press | g_button_long_press) & HEADLIGHT_BUTTON_MASK)
        {
            headlight_toggle_oem();
            g_button_short_press = (uint8_t)(g_button_short_press & (uint8_t)~HEADLIGHT_BUTTON_MASK);
            g_button_long_press = (uint8_t)(g_button_long_press & (uint8_t)~HEADLIGHT_BUTTON_MASK);
        }

        quick_action_handle(g_button_long_press);
        request_bootloader_recovery(g_button_long_press);
    }

    {
        ui_page_t prev_page = g_ui_page;
        g_ui_page = (ui_page_t)ui_page_from_buttons(g_button_short_press,
                                                    g_button_long_press,
                                                    g_ui_page);
        if (g_ui_page != prev_page)
        {
            if (g_ui_page == UI_PAGE_PROFILES)
            {
                g_ui_profile_select = g_active_profile_id;
                g_ui_profile_focus = UI_PROFILE_FOCUS_LIST;
            }
        }
    }
}

static uint16_t cadence_bias_q15(uint16_t cadence_rpm)
{
    if (!g_cadence_bias.enabled || g_cadence_bias.band_rpm == 0)
        return 32768u;
    if (cadence_rpm <= g_cadence_bias.target_rpm)
        return 32768u;
    uint32_t delta = cadence_rpm - g_cadence_bias.target_rpm;
    if (delta >= g_cadence_bias.band_rpm)
        return clamp_q15(g_cadence_bias.min_bias_q15, 0, 65535u);
    uint32_t span = g_cadence_bias.band_rpm;
    uint32_t drop = (uint32_t)(32768u - g_cadence_bias.min_bias_q15);
    uint32_t scaled = (drop * delta) / span;
    uint32_t bias = 32768u - scaled;
    return (uint16_t)clamp_q15((uint16_t)bias, g_cadence_bias.min_bias_q15, 32768u);
}

/* -------------------------------------------------------------
 * Profile helpers
 * ------------------------------------------------------------- */
int set_active_profile(uint8_t id, int persist)
{
    if (id >= PROFILE_COUNT)
        return 0xFE;
    g_active_profile_id = id;
    g_outputs.profile_id = g_active_profile_id;
    g_config_active.profile_id = g_active_profile_id;
    g_last_profile_switch_ms = g_ms;

    /* Keep CRC accurate even if we skip persistence. */
    g_config_active.crc32 = 0;
    g_config_active.crc32 = config_crc_expected(&g_config_active);

    if (persist)
    {
        config_persist_active();
    }
    return 0;
}
void recompute_outputs(void)
{
    walk_update();

    uint16_t base_power = (uint16_t)((g_inputs.throttle_pct * 8u) + (g_inputs.torque_raw / 4u));
    const assist_profile_t *p = &g_profiles[g_active_profile_id];
    const assist_curve_profile_t *cp = &g_assist_curves[g_active_profile_id];
    uint16_t eff_cap_current = p->cap_current_dA;
    uint16_t eff_cap_speed = p->cap_speed_dmph;

    if (g_config_active.cap_current_dA && g_config_active.cap_current_dA < eff_cap_current)
        eff_cap_current = g_config_active.cap_current_dA;
    if (g_config_active.cap_speed_dmph)
    {
        if (eff_cap_speed == 0 || g_config_active.cap_speed_dmph < eff_cap_speed)
            eff_cap_speed = g_config_active.cap_speed_dmph;
    }
    if (g_config_active.mode == MODE_STREET)
    {
        if (eff_cap_current > STREET_MAX_CURRENT_DA)
            eff_cap_current = STREET_MAX_CURRENT_DA;
        if (eff_cap_speed == 0 || eff_cap_speed > STREET_MAX_SPEED_DMPH)
            eff_cap_speed = STREET_MAX_SPEED_DMPH;
    }
    g_effective_cap_current_dA = eff_cap_current;
    g_effective_cap_speed_dmph = eff_cap_speed;

    g_outputs.profile_id     = g_active_profile_id;
    g_outputs.virtual_gear   = g_active_vgear;

    /* Curve-derived limits (piecewise-linear, fixed-point, bounded). */
    int32_t curve_pw = 0;
    if (cp->speed_curve.count)
        curve_pw = fxp_interp_linear((int32_t)g_inputs.speed_dmph, cp->speed_curve.pts, cp->speed_curve.count);
    int32_t cadence_q15 = 32768;
    if (cp->cadence_curve.count)
        cadence_q15 = fxp_interp_linear((int32_t)g_inputs.cadence_rpm, cp->cadence_curve.pts, cp->cadence_curve.count);
    if (cadence_q15 < 0)
        cadence_q15 = 0;
    int32_t curve_pw_scaled = (int32_t)((curve_pw * (int64_t)cadence_q15 + (1 << 14)) >> 15);
    if (curve_pw_scaled < 0)
        curve_pw_scaled = 0;
    if (curve_pw_scaled > 0xFFFF)
        curve_pw_scaled = 0xFFFF;

    uint16_t limit_power = (uint16_t)curve_pw_scaled;
    g_curve_power_w = limit_power;
    g_curve_cadence_q15 = (uint16_t)cadence_q15;

    /* Virtual gear multiplier (Q15) */
    g_gear_scale_q15 = 32768u;
    if (g_active_vgear >= 1 && g_active_vgear <= g_vgears.count)
        g_gear_scale_q15 = g_vgears.scales[g_active_vgear - 1u];
    g_gear_limit_power_w = (uint16_t)((limit_power * (uint32_t)g_gear_scale_q15 + (1u << 14)) >> 15);
    if (g_gear_limit_power_w > 0xFFFF)
        g_gear_limit_power_w = 0xFFFF;

    /* Optional cadence-friendly taper above target band. */
    g_cadence_bias_q15 = cadence_bias_q15(g_inputs.cadence_rpm);
    uint32_t biased_limit = (uint32_t)((g_gear_limit_power_w * (uint32_t)g_cadence_bias_q15 + (1u << 14)) >> 15);
    if (biased_limit > 0xFFFF)
        biased_limit = 0xFFFF;
    limit_power = (uint16_t)biased_limit;

    drive_mode_t drive_mode = g_drive.mode;
    uint16_t desired_power = 0;
    if (drive_mode == DRIVE_MODE_AUTO || drive_mode == DRIVE_MODE_SPORT)
    {
        desired_power = cruise_apply(base_power, limit_power);
    }
    else if (drive_mode == DRIVE_MODE_MANUAL_CURRENT)
    {
        if (g_cruise.mode != CRUISE_OFF)
            cruise_cancel(CRUISE_EVT_CANCEL_USER);
        uint32_t pwr = (uint32_t)g_drive.setpoint * 2u;
        if (pwr > 0xFFFF)
            pwr = 0xFFFF;
        desired_power = (uint16_t)pwr;
        limit_power = desired_power;
    }
    else if (drive_mode == DRIVE_MODE_MANUAL_POWER)
    {
        if (g_cruise.mode != CRUISE_OFF)
            cruise_cancel(CRUISE_EVT_CANCEL_USER);
        desired_power = manual_power_apply(g_drive.setpoint);
        limit_power = 0xFFFF;
    }
    g_outputs.cruise_state = (uint8_t)g_cruise.mode;

    {
        uint8_t allow_adapt = (drive_mode == DRIVE_MODE_AUTO || drive_mode == DRIVE_MODE_SPORT) ? 1u : 0u;
        uint8_t effort_on = (g_config_active.flags & CFG_FLAG_ADAPT_EFFORT) ? 1u : 0u;
        uint8_t eco_on = (g_config_active.flags & CFG_FLAG_ADAPT_ECO) ? 1u : 0u;
        if (!allow_adapt)
            effort_on = eco_on = 0;

        if (!(effort_on || eco_on))
        {
            adaptive_reset();
        }
        else
        {
            adaptive_update(g_inputs.speed_dmph, g_inputs.power_w, g_ms);
            if (effort_on)
            {
                uint16_t boost = adaptive_effort_boost(base_power, g_inputs.speed_dmph);
                if (boost > 0)
                {
                    uint32_t next = (uint32_t)desired_power + (uint32_t)boost;
                    if (limit_power && next > limit_power)
                        next = limit_power;
                    if (next > 0xFFFFu)
                        next = 0xFFFFu;
                    desired_power = (uint16_t)next;
                }
            }
            else
            {
                g_adapt.speed_delta_dmph = 0;
                g_adapt.trend_active = 0;
            }

            if (eco_on)
            {
                desired_power = adaptive_eco_limit(desired_power);
            }
            else
            {
                g_adapt.eco_output_w = desired_power;
                g_adapt.eco_clamp_active = 0u;
                g_adapt.last_speed_dmph = g_inputs.speed_dmph;
            }
        }
    }

    g_outputs.cmd_power_w = desired_power;
    if (g_outputs.cmd_power_w > limit_power)
        g_outputs.cmd_power_w = limit_power;
    g_outputs.cmd_current_dA = (uint16_t)(g_outputs.cmd_power_w / 2u);
    g_outputs.assist_mode = (g_outputs.cmd_power_w > 0) ? 1u : 0u;

    /* Enforce profile caps (speed cap zeros assist). */
    if (eff_cap_speed && g_inputs.speed_dmph > eff_cap_speed)
    {
        cruise_cancel(CRUISE_EVT_CANCEL_CAP);
        g_outputs.assist_mode = 0;
        g_outputs.cmd_power_w = 0;
        g_outputs.cmd_current_dA = 0;
    }
    else
    {
        if (g_outputs.cmd_power_w > p->cap_power_w)
            g_outputs.cmd_power_w = p->cap_power_w;
        if (g_outputs.cmd_current_dA > eff_cap_current)
            g_outputs.cmd_current_dA = eff_cap_current;
    }

    if (g_walk_state == WALK_STATE_ACTIVE)
    {
        soft_start_reset();
    }
    else
    {
        g_outputs.cmd_power_w = soft_start_apply(g_outputs.cmd_power_w);
        g_outputs.cmd_current_dA = (uint16_t)(g_outputs.cmd_power_w / 2u);
    }

    /* Apply multi-governor power policy (lugging/thermal/sag). */
    power_policy_apply(g_outputs.cmd_power_w);
    g_outputs.cmd_power_w = g_power_policy.p_final_w;
    g_outputs.cmd_current_dA = (uint16_t)(g_outputs.cmd_power_w / 2u);
    boost_update();

    /* Reflect zeroed outputs in mode flag. */
    if (g_outputs.cmd_power_w == 0 || g_outputs.cmd_current_dA == 0)
        g_outputs.assist_mode = 0;

    /* Walk assist overrides normal outputs when active. */
    if (g_walk_state == WALK_STATE_ACTIVE)
    {
        g_outputs.assist_mode = 2; /* distinct walk flag */
        g_outputs.cmd_power_w = g_walk_cmd_power_w;
        g_outputs.cmd_current_dA = g_walk_cmd_current_dA;
        g_outputs.cruise_state = 0;
    }

    /* Brake always zeros propulsion (walk + assist). */
    if (g_inputs.brake)
    {
        cruise_cancel(CRUISE_EVT_CANCEL_BRAKE);
        g_outputs.assist_mode = 0;
        g_outputs.cmd_power_w = 0;
        g_outputs.cmd_current_dA = 0;
        g_outputs.cruise_state = 0;
    }

    if (motor_cmd_link_fault_active())
    {
        cruise_cancel(CRUISE_EVT_CANCEL_FAULT);
        g_outputs.assist_mode = 0;
        g_outputs.cmd_power_w = 0;
        g_outputs.cmd_current_dA = 0;
        g_outputs.cruise_state = 0;
    }

    regen_update();

    if (g_outputs.cmd_current_dA > eff_cap_current)
    {
        g_outputs.cmd_current_dA = eff_cap_current;
        {
            uint32_t p_from_i = (uint32_t)g_outputs.cmd_current_dA * 2u;
            if (g_outputs.cmd_power_w > p_from_i)
                g_outputs.cmd_power_w = (uint16_t)p_from_i;
        }
    }

    g_outputs.cruise_state = (uint8_t)g_cruise.mode;
    g_outputs.last_ms        = g_ms;

    g_drive.cmd_power_w = g_outputs.cmd_power_w;
    g_drive.cmd_current_dA = g_outputs.cmd_current_dA;
    shengyi_request_update(0u);
}

void reboot_to_bootloader(void)
{
    const uint32_t bl_base = FLASH_BOOTLOADER_BASE;
    const uint32_t bl_sp   = *(volatile uint32_t *)(bl_base + FLASH_VECTOR_SP_OFFSET);
    const uint32_t bl_rst  = *(volatile uint32_t *)(bl_base + FLASH_VECTOR_RESET_OFFSET);
    disable_irqs();
    /* Match OEM shutdown semantics: deassert controller key/enable before reboot. */
    platform_key_output_set(0u);
    mmio_write32(SCB_VTOR, bl_base);
    set_msp(bl_sp);
    ((void (*)(void))(uintptr_t)bl_rst)();
    while (1)
        ;
}

void reboot_to_app(void)
{
    const uint32_t app_base = FLASH_APP_BASE;
    const uint32_t app_sp   = *(volatile uint32_t *)(app_base + FLASH_VECTOR_SP_OFFSET);
    const uint32_t app_rst  = *(volatile uint32_t *)(app_base + FLASH_VECTOR_RESET_OFFSET);
    disable_irqs();
    platform_key_output_set(0u);
    mmio_write32(SCB_VTOR, app_base);
    set_msp(app_sp);
    ((void (*)(void))(uintptr_t)app_rst)();
    while (1)
        ;
}

static inline void boot_stage_mark(uint32_t value)
{
    boot_stage_log(value);
    boot_log_stage(value);
}

void print_status(void)
{
    if ((g_debug_uart_mask & DEBUG_UART_STATUS) == 0u)
        return;
    char line[160];
    char *ptr = line;
    size_t rem = sizeof(line);

    append_str(&ptr, &rem, "[open-fw] t=");
    append_u32(&ptr, &rem, g_ms);
    append_str(&ptr, &rem, " ms rpm=");
    append_u16(&ptr, &rem, g_motor.rpm);
    append_str(&ptr, &rem, " tq=");
    append_u16(&ptr, &rem, g_motor.torque_raw);
    append_str(&ptr, &rem, " speed=");
    append_u16(&ptr, &rem, g_motor.speed_dmph / 10u);
    append_char(&ptr, &rem, '.');
    append_u16(&ptr, &rem, (uint16_t)(g_motor.speed_dmph % 10u));
    append_str(&ptr, &rem, " soc=");
    append_u16(&ptr, &rem, g_motor.soc_pct);
    append_str(&ptr, &rem, " err=");
    append_u16(&ptr, &rem, g_motor.err);
    append_char(&ptr, &rem, '\n');

    uart_write(UART1_BASE, (const uint8_t *)line, (size_t)(ptr - line));
}

/* -------------------------------------------------------------
 * Basic main
 * ------------------------------------------------------------- */
int main(void)
{
    disable_irqs();
    /*
     * Ensure vectors point at the app image.
     *
     * On hardware, 0x08010000 is the real flash address for the app vector table.
     * In simulator setups, the same image is also loaded at the alias 0x00010000; some setups
     * route vectors through the alias, so fall back if VTOR write appears ignored.
     */
    mmio_write32(SCB_VTOR, FLASH_APP_BASE);
    if (mmio_read32(SCB_VTOR) != FLASH_APP_BASE)
        mmio_write32(SCB_VTOR, FLASH_APP_ALIAS);
    reset_flags_capture();
    /* Encode reset flags for post-mortem: 0xE0000000 | flags. */
    boot_stage_mark(0xE0000000u | (uint32_t)g_reset_flags);
    platform_power_hold_early();

    platform_clock_init();
    platform_nvic_init();
    boot_stage_mark(0xB001);

    /* Bring up OEM timebase (TIM2 5ms) early so SPI flash timeouts can advance g_ms. */
    platform_timebase_init_oem();
    boot_stage_mark(0xB002);
    boot_stage_mark(0xB003);

    /* Always arm the OEM bootloader update flag for the next power cycle. */
    spi_flash_set_bootloader_mode_flag();

    /* OEM doesn't have safe-mode. Just init buttons and continue. */
    platform_buttons_init();

    platform_board_init();
    platform_backlight_set_level(5u);

    boot_log_lcd_ready();
    boot_stage_mark(0xB004);
    /* Keep controller/display power latched once basic display init succeeds. */
    platform_key_output_set(1u);

    /* OEM v2.5.1: battery ADC monitoring is active during normal runtime. */
    battery_monitor_init();
    boot_stage_mark(0xBAA1);

    /* UART1 (BLE) is on APB2; OEM app uses 9600. */
    const uint32_t uart_baud = 9600u;
    uint32_t pclk2 = rcc_get_pclk_hz_fallback(1u);
    uint32_t brr1 = uart_brr_div(pclk2, uart_baud);
    uart_init_basic(UART1_BASE, brr1);
    boot_stage_mark(0xBAA2);
    /* OEM v2.3.0: ble_uart1_full_init sends "TTM:MAC-?" to query BLE module MAC. */
    ble_ttm_send_mac_query();
    boot_stage_mark(0xBAAF);
    boot_log_uart_ready();

    /* OEM v2.5.1 configures PC5/PC6 pull-ups after BLE/UART init, not during LCD init. */
    platform_gpioc_aux_init();
    boot_stage_mark(0xBAA3);

    /* UART2 (motor) is on APB1; match OEM 9600 for Shengyi DWG22. */
    platform_motor_uart_pins_init();
    uint32_t pclk1 = rcc_get_pclk_hz_fallback(0u);
    uint32_t brr2 = uart_brr_div(pclk1, uart_baud);
    uart_init_basic(UART2_BASE, brr2);
    /* OEM v2.3.0: UART2 is always 8N1 (word_length=0 in usart2_hw_init).
     * The motor controller expects 8-bit framing on all protocols. */

    platform_uart_irq_init();
    boot_stage_mark(0xBAA4);

    g_motor.rpm = 0;
    g_motor.torque_raw = 0;
    g_motor.speed_dmph = 0;
    g_motor.soc_pct = 0;
    g_motor.err = 0;
    g_motor.last_ms = 0;

    g_inputs.speed_dmph = 0;
    g_inputs.cadence_rpm = 0;
    g_inputs.torque_raw = 0;
    g_inputs.power_w = 0;
    g_inputs.battery_dV = 0;
    g_inputs.battery_dA = 0;
    g_inputs.ctrl_temp_dC = 0;
    g_inputs.throttle_pct = 0;
    g_inputs.brake = 0;
    g_inputs.buttons = 0;
    g_inputs.last_ms = 0;
    g_input_caps = 0;

    g_outputs.assist_mode = 0;
    g_outputs.profile_id = 0;
    g_outputs.virtual_gear = 0;
    g_outputs.cruise_state = 0;
    g_outputs.cmd_power_w = 0;
    g_outputs.cmd_current_dA = 0;
    g_outputs.last_ms = 0;
    g_last_brake_state = 0;
    g_brake_edge = 0;
    drive_reset();
    g_boost = (boost_state_t){0};
    power_policy_reset();
    adaptive_reset();

    trip_init();
    range_reset();

    speed_rb_init();
    graph_init();
    bus_capture_set_enabled(0, 1);

    event_queue_init(&g_motor_events);
    motor_isr_init(&g_motor_events);
    /* Enable motor ISR tick AFTER motor_isr_init, not before. */
    platform_motor_isr_enable();
    boot_stage_mark(0xBAA5);
    motor_cmd_init();
    shengyi_init();
    motor_link_init();
    boot_stage_mark(0xBAA6);

    event_log_load();
    stream_log_load();
    boot_stage_mark(0xBAA7);
    if (g_reset_flags)
        event_log_append(EVT_RESET_REASON, (uint8_t)(g_reset_flags & 0xFFu));

    vgear_defaults();
    cadence_bias_defaults();
    button_track_reset();
    g_lock_active = 0;
    g_lock_allowed_mask = 0;
    g_quick_action_last = QUICK_ACTION_NONE;
    g_cruise_toggle_request = 0;
    g_ui_settings_index = 0;
    g_ui_tune_index = 0;
    g_ui_graph_channel = UI_GRAPH_CH_SPEED;
    g_ui_graph_window_idx = 1u;
    g_ui_bus_offset = 0u;
    g_ui_profile_select = 0u;
    g_ui_profile_focus = UI_PROFILE_FOCUS_LIST;
    g_ui_alert_index = 0u;
    g_ui_alert_ack_mask = 0u;
    g_ui_alert_last_seq = 0u;
    g_alert_ack_active = 0;
    g_alert_ack_until_ms = 0;

    g_request_soft_reboot = REBOOT_REQUEST_NONE;
    config_stage_reset();
    config_load_active();
    boot_stage_mark(0xBAA8);
    ab_update_init();
    g_outputs.profile_id = g_config_active.profile_id;
    g_ui_profile_select = g_active_profile_id;
    g_ui_profile_focus = UI_PROFILE_FOCUS_LIST;
    walk_reset();
    regen_reset();
    cruise_reset();
    soft_start_reset();
    wizard_reset();
    ui_init(&g_ui);
    boot_stage_mark(0xBAA9);

    g_ms = 0;
    enable_irqs();

    watchdog_start_runtime();
    boot_stage_mark(0xBAAA);
    /* Emit one status line early for bring-up visibility. */
    print_status();
    shengyi_request_update(1u);
    boot_stage_mark(0xBAAB);

    /*
     * Main loop - delegated to app.c for clean separation.
     * See app_main_loop() in app.c for implementation.
     */
    app_main_loop();

    /* Never reached - app_main_loop() is marked noreturn */
}
