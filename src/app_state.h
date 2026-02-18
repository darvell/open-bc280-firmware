#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>

typedef enum {
    REBOOT_REQUEST_NONE = 0u,
    REBOOT_REQUEST_BOOTLOADER = 1u,
    REBOOT_REQUEST_APP = 2u,
} reboot_request_t;

/* Main loop / scheduler state shared across modules (defined in main.c). */
extern uint32_t g_last_print;
extern uint32_t g_stream_period_ms;
extern uint32_t g_last_stream_ms;
extern uint8_t g_brake_edge;
extern reboot_request_t g_request_soft_reboot;
extern uint16_t g_curve_power_w;
extern uint16_t g_curve_cadence_q15;

#define DEBUG_UART_TRACE_UI 0x01u
#define DEBUG_UART_STATUS   0x02u

extern uint8_t g_debug_uart_mask;

#endif /* APP_STATE_H */
