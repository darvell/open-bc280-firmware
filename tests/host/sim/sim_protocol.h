#ifndef SIM_PROTOCOL_H
#define SIM_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "sim_uart.h"
#include "ui.h"
#include "src/input/input.h"

typedef struct {
    uint32_t ms;
    uint16_t rpm;
    uint16_t torque_raw;
    uint16_t speed_dmph;
    uint8_t soc;
    uint8_t err;
    uint16_t cadence_rpm;
    uint16_t power_w;
    int16_t batt_dV;
    int16_t batt_dA;
    int16_t ctrl_temp_dC;
    uint16_t stream_period_ms;
    uint32_t last_stream_ms;
    sim_uart_port_t last_rx_port;
    uint8_t ui_page;
    uint8_t button_map;
    uint8_t qa_flags;
    uint8_t cruise_mode;
    uint8_t profile_id;
    uint8_t capture_enabled;
    button_track_t button_track;
    uint8_t button_short_press;
    uint8_t button_long_press;
} sim_proto_state_t;

void sim_proto_init(sim_proto_state_t *s);
void sim_proto_update_inputs(sim_proto_state_t *s, uint16_t rpm, uint16_t torque_raw,
                             uint16_t speed_dmph, uint8_t soc, uint8_t err,
                             uint16_t cadence_rpm, uint16_t power_w,
                             int16_t batt_dV, int16_t batt_dA, int16_t ctrl_temp_dC);
void sim_proto_feed(sim_proto_state_t *s, sim_uart_port_t port, uint8_t byte);
void sim_proto_tick(sim_proto_state_t *s);
void sim_proto_fill_model(const sim_proto_state_t *s, ui_model_t *m);
void sim_proto_fill_model_with_buttons(sim_proto_state_t *s, ui_model_t *m, uint8_t buttons,
                                       uint8_t throttle_pct, uint8_t brake);

#endif
