#include "sim_protocol.h"
#include "util/byteorder.h"
#include "comm_proto.h"
#include "src/config/config.h"

typedef struct {
    uint8_t frame[COMM_MAX_PAYLOAD + 4];
    uint8_t len;
} sim_parser_t;

static sim_parser_t g_parser[SIM_UART_MAX];

static void send_frame(sim_uart_port_t port, uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[COMM_MAX_PAYLOAD + 4];
    size_t flen = comm_frame_build(frame, sizeof(frame), cmd, payload, len);
    if (!flen)
        return;
    sim_uart_tx_write(port, frame, flen);
}

static void send_status(sim_uart_port_t port, uint8_t cmd, uint8_t code)
{
    uint8_t payload[1] = {code};
    send_frame(port, (uint8_t)(cmd | 0x80u), payload, 1);
}

static void send_stream_frame(sim_proto_state_t *s)
{
    uint8_t payload[COMM_STATE_FRAME_V1_LEN];
    comm_state_frame_t state = {
        .ms = s->ms,
        .speed_dmph = s->speed_dmph,
        .cadence_rpm = s->cadence_rpm,
        .power_w = s->power_w,
        .batt_dV = s->batt_dV,
        .batt_dA = s->batt_dA,
        .ctrl_temp_dC = s->ctrl_temp_dC,
        .assist_mode = 0,
        .profile_id = 0,
        .virtual_gear = 0,
        .flags = 0,
    };
    uint8_t len = comm_state_frame_build_v1(payload, (uint8_t)sizeof(payload), &state);
    if (!len)
        return;
    send_frame(s->last_rx_port, 0x81u, payload, len);
}

static void sim_quick_action_handle(sim_proto_state_t *s, uint8_t long_press_mask)
{
    if (!s)
        return;
    if ((long_press_mask & CRUISE_BUTTON_MASK) && (s->qa_flags & CFG_FLAG_QA_CRUISE))
    {
        s->cruise_mode = (s->cruise_mode == CRUISE_OFF) ? CRUISE_SPEED : CRUISE_OFF;
    }
    if ((long_press_mask & BUTTON_GEAR_UP_MASK) && (s->qa_flags & CFG_FLAG_QA_PROFILE))
    {
        uint8_t next = (uint8_t)(s->profile_id + 1u);
        if (next >= PROFILE_COUNT)
            next = 0u;
        s->profile_id = next;
    }
    if ((long_press_mask & BUTTON_GEAR_DOWN_MASK) && (s->qa_flags & CFG_FLAG_QA_CAPTURE))
    {
        s->capture_enabled = s->capture_enabled ? 0u : 1u;
    }
}

static void sim_ui_nav_update(sim_proto_state_t *s, uint8_t short_press, uint8_t long_press)
{
    if (!s)
        return;
    s->ui_page = ui_page_from_buttons(short_press, long_press, s->ui_page);
}

void sim_proto_init(sim_proto_state_t *s)
{
    if (!s)
        return;
    *s = (sim_proto_state_t){0};
    s->last_rx_port = SIM_UART1;
    s->ui_page = UI_PAGE_DASHBOARD;
    s->button_map = 0;
    s->qa_flags = 0;
    s->cruise_mode = CRUISE_OFF;
    s->profile_id = 0;
    s->capture_enabled = 0;
    button_track_reset_state(&s->button_track);
    s->button_short_press = 0;
    s->button_long_press = 0;
    for (int i = 0; i < SIM_UART_MAX; ++i)
        g_parser[i] = (sim_parser_t){0};
}

void sim_proto_update_inputs(sim_proto_state_t *s, uint16_t rpm, uint16_t torque_raw,
                             uint16_t speed_dmph, uint8_t soc, uint8_t err,
                             uint16_t cadence_rpm, uint16_t power_w,
                             int16_t batt_dV, int16_t batt_dA, int16_t ctrl_temp_dC)
{
    if (!s)
        return;
    s->rpm = rpm;
    s->torque_raw = torque_raw;
    s->speed_dmph = speed_dmph;
    s->soc = soc;
    s->err = err;
    s->cadence_rpm = cadence_rpm;
    s->power_w = power_w;
    s->batt_dV = batt_dV;
    s->batt_dA = batt_dA;
    s->ctrl_temp_dC = ctrl_temp_dC;
}

static void handle_frame(sim_proto_state_t *s, sim_uart_port_t port, uint8_t cmd, const uint8_t *p, uint8_t len)
{
    s->last_rx_port = port;
    switch (cmd)
    {
        case 0x01: /* ping */
            send_status(port, cmd, 0);
            break;
        case 0x0A: /* state dump */
        {
            uint8_t out[16] = {0};
            store_be32(&out[0], s->ms);
            store_be16(&out[4], s->rpm);
            store_be16(&out[6], s->torque_raw);
            store_be16(&out[8], s->speed_dmph);
            out[10] = s->soc;
            out[11] = s->err;
            send_frame(port, (uint8_t)(cmd | 0x80u), out, 16);
            break;
        }
        case 0x0C: /* set_state */
            if (len < 8)
                break;
            s->rpm = load_be16(&p[0]);
            s->torque_raw = load_be16(&p[2]);
            s->speed_dmph = load_be16(&p[4]);
            s->soc = p[6];
            s->err = p[7];
            if (len >= 10)
                s->cadence_rpm = load_be16(&p[8]);
            if (len >= 15)
                s->power_w = load_be16(&p[13]);
            if (len >= 17)
                s->batt_dV = (int16_t)load_be16(&p[15]);
            if (len >= 19)
                s->batt_dA = (int16_t)load_be16(&p[17]);
            send_status(port, cmd, 0);
            break;
        case 0x0D: /* set_stream */
            if (len < 2)
                break;
            s->stream_period_ms = load_be16(&p[0]);
            send_status(port, cmd, 0);
            break;
        default:
            send_status(port, cmd, 0xFF);
            break;
    }
}

void sim_proto_feed(sim_proto_state_t *s, sim_uart_port_t port, uint8_t byte)
{
    sim_parser_t *p = &g_parser[port];
    uint8_t frame_len = 0;
    comm_parse_result_t res = comm_parser_feed(p->frame, sizeof(p->frame), COMM_MAX_PAYLOAD,
                                               &p->len, byte, &frame_len);
    if (res == COMM_PARSE_FRAME)
    {
        if (comm_frame_validate(p->frame, frame_len, NULL))
            handle_frame(s, port, p->frame[1], &p->frame[3], p->frame[2]);
    }
}

void sim_proto_tick(sim_proto_state_t *s)
{
    if (!s)
        return;
    if (!s->stream_period_ms)
        return;
    if ((uint32_t)(s->ms - s->last_stream_ms) >= s->stream_period_ms)
    {
        s->last_stream_ms = s->ms;
        send_stream_frame(s);
    }
}

void sim_proto_fill_model(const sim_proto_state_t *s, ui_model_t *m)
{
    if (!s || !m)
        return;
    *m = (ui_model_t){
        .page = s->ui_page,
        .speed_dmph = s->speed_dmph,
        .rpm = s->rpm,
        .torque_raw = s->torque_raw,
        .assist_mode = 1,
        .virtual_gear = 2,
        .soc_pct = s->soc,
        .err = s->err,
        .batt_dV = s->batt_dV,
        .batt_dA = s->batt_dA,
        .phase_dA = 0,
        .sag_margin_dV = 0,
        .thermal_state = 0,
        .ctrl_temp_dC = s->ctrl_temp_dC,
        .cadence_rpm = s->cadence_rpm,
        .power_w = s->power_w,
        .limit_power_w = s->power_w,
        .trip_distance_mm = (uint32_t)s->ms * (uint32_t)s->speed_dmph / 36u,
        .trip_energy_mwh = (uint32_t)s->ms * (uint32_t)s->power_w / 3600u,
        .trip_max_speed_dmph = s->speed_dmph,
        .trip_avg_speed_dmph = s->speed_dmph,
        .trip_moving_ms = (s->speed_dmph >= 5u) ? s->ms : 0u,
        .trip_assist_ms = s->ms,
        .trip_gear_ms = s->ms,
        .units = 0,
        .theme = UI_THEME_NIGHT,
        .mode = 0,
        .limit_reason = 0,
        .drive_mode = 0,
        .boost_seconds = 0,
        .range_est_d10 = 120,
        .range_confidence = 3,
        .graph_channel = UI_GRAPH_CH_SPEED,
        .graph_window_s = 30,
        .graph_sample_hz = (uint8_t)(1000u / UI_TICK_MS),
        .profile_id = s->profile_id,
        .capture_enabled = s->capture_enabled,
        .cruise_mode = s->cruise_mode,
        .button_map = s->button_map,
    };
}

void sim_proto_fill_model_with_buttons(sim_proto_state_t *s, ui_model_t *m, uint8_t buttons,
                                       uint8_t throttle_pct, uint8_t brake)
{
    if (!s || !m)
        return;
    uint8_t mapped = button_map_apply(buttons, s->button_map);
    button_track_update_state(&s->button_track, mapped, 0xFFu, s->ms, 0u,
                              &s->button_short_press, &s->button_long_press);
    sim_ui_nav_update(s, s->button_short_press, s->button_long_press);
    sim_quick_action_handle(s, s->button_long_press);
    sim_proto_fill_model(s, m);
    m->buttons = mapped;
    m->throttle_pct = throttle_pct;
    m->brake = brake;
}
