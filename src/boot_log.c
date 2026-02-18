#include "boot_log.h"

#include <stddef.h>

#include "core/trace_format.h"
#include "platform/time.h"

#include "drivers/uart.h"
#include "platform/hw.h"

#if !defined(HOST_TEST)
#include "gfx/ui_lcd.h"
#include "ui_display.h"
#endif

#define BOOT_LOG_MAX_ENTRIES 32u
#define BOOT_LOG_LCD_LINE_H  12u
#define BOOT_LOG_LCD_X       4u
#define BOOT_LOG_LCD_Y       4u
#define BOOT_LOG_LCD_FG      0xFFFFu
#define BOOT_LOG_LCD_BG      0x0000u

typedef struct {
    uint32_t code;
    uint32_t ms;
} boot_log_entry_t;

static boot_log_entry_t g_boot_log[BOOT_LOG_MAX_ENTRIES];
static uint32_t g_boot_log_count;
static uint32_t g_uart_flushed;
static uint32_t g_lcd_flushed;
static uint8_t g_uart_ready;
static uint8_t g_lcd_ready;
static uint8_t g_lcd_line;

static uint32_t boot_log_start_index(void)
{
    if (g_boot_log_count > BOOT_LOG_MAX_ENTRIES)
        return g_boot_log_count - BOOT_LOG_MAX_ENTRIES;
    return 0u;
}

static boot_log_entry_t boot_log_get(uint32_t idx)
{
    return g_boot_log[idx % BOOT_LOG_MAX_ENTRIES];
}

static size_t boot_log_format(char *line, size_t size, const boot_log_entry_t *entry)
{
    char *ptr = line;
    size_t rem = size;
    append_str(&ptr, &rem, "[boot] 0x");
    append_hex_u32(&ptr, &rem, entry->code);
    append_str(&ptr, &rem, " t=");
    append_u32(&ptr, &rem, entry->ms);
    append_str(&ptr, &rem, "ms");
    append_str(&ptr, &rem, "\n");
    if (rem)
        *ptr = '\0';
    return (size_t)(ptr - line);
}

static void boot_log_flush_uart(void)
{
    if (!g_uart_ready)
        return;
    uint32_t start = boot_log_start_index();
    if (g_uart_flushed < start)
        g_uart_flushed = start;
    for (uint32_t i = g_uart_flushed; i < g_boot_log_count; ++i) {
        char line[64];
        boot_log_entry_t entry = boot_log_get(i);
        size_t len = boot_log_format(line, sizeof(line), &entry);
        if (len)
            uart_write(UART1_BASE, (const uint8_t *)line, len);
    }
    g_uart_flushed = g_boot_log_count;
}

static void boot_log_flush_lcd(void)
{
#if !defined(HOST_TEST)
    if (!g_lcd_ready)
        return;
    uint32_t start = boot_log_start_index();
    if (g_lcd_flushed < start) {
        g_lcd_flushed = start;
        g_lcd_line = 0u;
        ui_lcd_fill_rect(0u, 0u, DISP_W, DISP_H, BOOT_LOG_LCD_BG);
    }
    for (uint32_t i = g_lcd_flushed; i < g_boot_log_count; ++i) {
        char line[48];
        boot_log_entry_t entry = boot_log_get(i);
        size_t len = boot_log_format(line, sizeof(line), &entry);
        if (len && line[len - 1u] == '\n')
            line[len - 1u] = '\0';
        if (g_lcd_line >= (DISP_H / BOOT_LOG_LCD_LINE_H)) {
            g_lcd_line = 0u;
            ui_lcd_fill_rect(0u, 0u, DISP_W, DISP_H, BOOT_LOG_LCD_BG);
        }
        ui_lcd_draw_text_stroke(BOOT_LOG_LCD_X,
                                (uint16_t)(BOOT_LOG_LCD_Y + g_lcd_line * BOOT_LOG_LCD_LINE_H),
                                line,
                                BOOT_LOG_LCD_FG,
                                BOOT_LOG_LCD_BG);
        g_lcd_line++;
    }
    g_lcd_flushed = g_boot_log_count;
#endif
}

void boot_log_stage(uint32_t code)
{
    uint32_t idx = g_boot_log_count;
    g_boot_log[idx % BOOT_LOG_MAX_ENTRIES] = (boot_log_entry_t){ code, g_ms };
    g_boot_log_count++;
    boot_log_flush_uart();
    boot_log_flush_lcd();
}

void boot_log_uart_ready(void)
{
    g_uart_ready = 1u;
    boot_log_flush_uart();
}

void boot_log_lcd_ready(void)
{
    g_lcd_ready = 1u;
#if !defined(HOST_TEST)
    g_lcd_line = 0u;
    ui_lcd_fill_rect(0u, 0u, DISP_W, DISP_H, BOOT_LOG_LCD_BG);
#endif
    boot_log_flush_lcd();
}
