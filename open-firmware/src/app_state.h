#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>

/* Main loop / scheduler state shared across modules (defined in main.c). */
extern uint32_t g_last_print;
extern uint32_t g_stream_period_ms;
extern uint32_t g_last_stream_ms;
extern uint8_t g_brake_edge;
extern uint8_t g_request_soft_reboot;
extern uint16_t g_curve_power_w;
extern uint16_t g_curve_cadence_q15;

#endif /* APP_STATE_H */
