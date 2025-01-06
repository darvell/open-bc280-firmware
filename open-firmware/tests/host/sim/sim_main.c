#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

#include "ui.h"
#include "sim_shengyi.h"
#include "sim_shengyi_bus.h"
#include "sim_shengyi_motor.h"
#include "sim_ble.h"
#include "sim_mcu.h"
#include "sim_protocol.h"
#include "sim_uart.h"
#include "comm_proto.h"
#include "src/input/oem_buttons.h"
#include "util/byteorder.h"

static size_t build_frame(uint8_t cmd, const uint8_t *payload, uint8_t len,
                          uint8_t *out, size_t cap)
{
    return comm_frame_build(out, cap, cmd, payload, len);
}

static void emit_set_state(const sim_shengyi_t *s, uint8_t *frame, size_t cap, size_t *flen, uint32_t step)
{
    uint8_t p[21];
    uint16_t rpm = (uint16_t)((s->cadence_rpm * 3.0) + 0.5);
    uint16_t speed_dmph = sim_shengyi_speed_dmph(s);
    uint16_t cadence_rpm = sim_shengyi_cadence_rpm(s);
    uint16_t power_w = sim_shengyi_power_w(s);
    int16_t batt_dV = sim_shengyi_batt_dV(s);
    int16_t batt_dA = sim_shengyi_batt_dA(s);

    store_be16(&p[0], rpm);
    store_be16(&p[2], s->torque_raw);
    store_be16(&p[4], speed_dmph);
    p[6] = s->soc_pct;
    p[7] = s->err;
    store_be16(&p[8], cadence_rpm);
    p[10] = 37; /* throttle */
    p[11] = (step == 10u) ? 1u : 0u; /* brake pulse */
    p[12] = 0; /* buttons are injected in host sim for UI page testing */
    store_be16(&p[13], power_w);
    store_be16(&p[15], (uint16_t)batt_dV);
    store_be16(&p[17], (uint16_t)batt_dA);
    p[19] = 0;
    p[20] = 0;
    *flen = build_frame(0x0C, p, 21, frame, cap);
}

static void emit_ping(uint8_t *frame, size_t cap, size_t *flen)
{
    *flen = build_frame(0x01, NULL, 0, frame, cap);
}

static void emit_set_stream(uint16_t period_ms, uint8_t *frame, size_t cap, size_t *flen)
{
    uint8_t p[2];
    store_be16(&p[0], period_ms);
    *flen = build_frame(0x0D, p, 2, frame, cap);
}

typedef struct {
    uint32_t step;
    uint8_t mask;
} sim_btn_step_t;


static size_t parse_button_seq(const char *s, sim_btn_step_t *out, size_t cap)
{
    size_t n = 0;
    if (!s || !out || cap == 0)
        return 0;
    const char *p = s;
    while (*p && n < cap)
    {
        while (*p == ' ' || *p == ',')
            p++;
        if (!*p)
            break;
        char *end = NULL;
        uint32_t step = (uint32_t)strtoul(p, &end, 10);
        if (end == p || *end != ':')
            break;
        p = end + 1;
        uint32_t mask = (uint32_t)strtoul(p, &end, 0);
        if (end == p)
            break;
        out[n++] = (sim_btn_step_t){step, (uint8_t)mask};
        p = end;
    }
    return n;
}

static uint8_t sample_buttons_oem(sim_mcu_t *mcu, uint8_t logical_buttons)
{
    /* OEM: buttons_sample_GPIOC_IDR reads GPIOC IDR, uses bits[4:0], bit5 forced high. */
    for (uint8_t bit = 0; bit < 5; ++bit)
    {
        uint8_t pressed = (uint8_t)((logical_buttons >> bit) & 1u);
        /* Active-low inputs: pressed -> 0, released -> 1. */
        sim_mcu_gpio_set_input(mcu, 'C', bit, pressed ? 0u : 1u);
    }
    sim_mcu_gpio_set_input(mcu, 'C', 5, 1u);
    uint16_t idr = sim_mcu_gpio_get_idr(mcu, 'C');
    uint8_t raw = (uint8_t)((idr & OEM_BTN_MASK) | OEM_BTN_VIRTUAL);
    return raw;
}

static int validate_tx_frames(const uint8_t *buf, size_t len, uint8_t *saw_stream)
{
    size_t i = 0;
    uint8_t saw_any = 0;
    if (saw_stream)
        *saw_stream = 0;
    while (i < len)
    {
        if (buf[i] != COMM_SOF)
        {
            i++;
            continue;
        }
        if (i + 3 >= len)
            return 0;
        uint8_t cmd = buf[i + 1];
        uint8_t plen = buf[i + 2];
        size_t frame_len = (size_t)(4u + plen);
        if (i + frame_len > len)
            return 0;
        if (!comm_frame_is_valid(&buf[i], frame_len))
            return 0;
        saw_any = 1;
        if (cmd == 0x81u && saw_stream)
            *saw_stream = 1;
        i += frame_len;
    }
    return saw_any ? 1 : 0;
}

/* Full simulation mode using complete BLE and Shengyi motor simulators */
static int run_full_sim(uint32_t steps, uint32_t dt_ms, const char *outdir)
{
    FILE *trace = NULL;
    FILE *ble_trace = NULL;
    FILE *ts_trace = NULL;

    if (outdir && outdir[0])
    {
        char path[512];
        mkdir(outdir, 0755);
        snprintf(path, sizeof(path), "%s/sim_ui_trace.txt", outdir);
        trace = fopen(path, "w");
        snprintf(path, sizeof(path), "%s/ble_frames.log", outdir);
        ble_trace = fopen(path, "w");
        snprintf(path, sizeof(path), "%s/shengyi_motor.log", outdir);
        ts_trace = fopen(path, "w");
    }

    /* Initialize all simulators */
    sim_uart_init();
    sim_mcu_t *mcu = sim_mcu_create();

    sim_ble_t ble;
    sim_ble_init(&ble);

    sim_dwg_motor_t motor;
    sim_dwg_motor_init(&motor);

    ui_state_t ui;
    ui_init(&ui);

    /* Environment config */
    const char *btn_env = getenv("BC280_SIM_BUTTONS");
    uint8_t btn_mask = btn_env ? (uint8_t)strtoul(btn_env, NULL, 0) : 0u;
    sim_btn_step_t btn_seq[16];
    size_t btn_seq_len = parse_button_seq(getenv("BC280_SIM_BUTTONS_SEQ"), btn_seq, 16);
    const char *force_page_env = getenv("BC280_SIM_FORCE_PAGE");
    int force_page = force_page_env ? atoi(force_page_env) : -1;

    /* Rider power profile from env */
    double rider_power = 100.0;
    const char *power_env = getenv("BC280_SIM_RIDER_POWER");
    if (power_env)
        rider_power = atof(power_env);
    sim_dwg_motor_set_rider_power(&motor, rider_power);

    /* Initial BLE commands to display */
    uint8_t frame[256];
    size_t flen;

    /* Note: BLE commands will be sent after TTM connection (auto-connects at 500ms) */

    /* Send initial Shengyi status request (display -> motor via UART2 TX) */
    flen = sim_dwg_build_0xC2_request(frame, sizeof(frame));
    sim_uart_tx_write(SIM_UART2, frame, flen);

    uint8_t saw_ui = 0;
    uint8_t saw_hash = 0;
    uint16_t render_over_budget = 0;
    uint32_t t_ms = 0;

    for (uint32_t i = 0; i < steps; ++i)
    {
        t_ms += dt_ms;

        /* Step MCU and motor physics */
        sim_mcu_step(mcu, dt_ms);
        sim_dwg_motor_tick(&motor, dt_ms);

        /* sim_ble is the EXTERNAL TTM chip + BLE app - it GENERATES stimuli.
         * The display firmware (not compiled in host sim) would read UART1 RX.
         * For now, we check if display wrote anything to UART1 TX.
         * In future, when display BLE handler is implemented, it will process
         * the commands sim_ble pushes to UART1 RX. */
        {
            /* Check what display sent to UART1 TX (responses to TTM/BLE) */
            uint8_t tx_buf[256];
            size_t tx_len = sim_uart_tx_read(SIM_UART1, tx_buf, sizeof(tx_buf));
            if (tx_len > 0)
            {
                /* Display firmware would respond here - for now just count */
                /* When display BLE handler is implemented, sim_ble can verify responses */
                (void)tx_buf;
            }
        }

        /* Feed UART2 RX bytes to motor simulator (from display TX) */
        {
            uint8_t tx_buf[4096];
            size_t tx_len = sim_uart_tx_read(SIM_UART2, tx_buf, sizeof(tx_buf));
            for (size_t j = 0; j < tx_len; ++j)
                sim_dwg_motor_feed_byte(&motor, tx_buf[j]);
        }

        /* Process motor simulator and generate responses */
        sim_dwg_motor_process(&motor);

        /* Note: sim_ble_process() is NOT called here.
         * sim_ble is the EXTERNAL TTM chip - it doesn't process commands,
         * it GENERATES commands that the display firmware would process.
         * The display firmware's BLE handler is not yet compiled in host sim. */

        /* Update BLE telemetry from motor */
        sim_ble_update_telemetry(&ble,
                                  sim_dwg_motor_speed_dmph(&motor),
                                  sim_dwg_motor_cadence_rpm(&motor),
                                  sim_dwg_motor_power_w(&motor),
                                  sim_dwg_motor_batt_dV(&motor),
                                  sim_dwg_motor_batt_dA(&motor),
                                  sim_dwg_motor_temp_dC(&motor),
                                  sim_dwg_motor_soc_pct(&motor),
                                  sim_dwg_motor_error_code(&motor));
        sim_ble_tick(&ble, dt_ms);

        /* Periodically send BLE commands (every 500ms) - only when connected */
        if (sim_ttm_is_connected(&ble) && (i % (500 / dt_ms)) == 0 && i > 0)
        {
            flen = sim_ble_build_get_realtime(frame, sizeof(frame));
            sim_uart_rx_push(SIM_UART1, frame, flen);
        }

        /* Periodically send motor status request (every 100ms) - display -> motor via UART2 TX */
        if ((t_ms % 100) == 0 && t_ms > 0)
        {
            flen = sim_dwg_build_0x52_request(motor.bike.assist_level, 0, frame, sizeof(frame));
            sim_uart_tx_write(SIM_UART2, frame, flen);
        }

        /* Log traces */
        if (ble_trace && ble.frames_tx > 0)
        {
            fprintf(ble_trace, "t=%u ble_rx=%u ble_tx=%u errs=%u\n",
                    t_ms, ble.frames_rx, ble.frames_tx, ble.parse_errors);
        }
        if (ts_trace && motor.frames_tx > 0)
        {
            fprintf(ts_trace, "t=%u motor_rx=%u motor_tx=%u speed=%.1f cadence=%u power=%u soc=%u\n",
                    t_ms, motor.frames_rx, motor.frames_tx,
                    motor.bike.v_mps * 3.6,
                    (unsigned)motor.bike.cadence_rpm,
                    sim_dwg_motor_power_w(&motor),
                    motor.bike.soc_pct);
        }

        /* Buttons */
        uint8_t ui_buttons = btn_mask;
        if (btn_seq_len)
        {
            for (size_t bi = 0; bi < btn_seq_len; ++bi)
            {
                if (btn_seq[bi].step == i)
                    ui_buttons = btn_seq[bi].mask;
            }
        }
        else if (!btn_env)
        {
            if (i == 12u)
                ui_buttons = OEM_BTN_MENU;
            else if (i == 22u)
                ui_buttons = OEM_BTN_POWER;
        }

        uint8_t n63 = sample_buttons_oem(mcu, ui_buttons);
        ui_buttons = oem_buttons_map_raw(n63, NULL);

        /* Build UI model from motor state */
        ui_model_t model = {0};
        model.page = (force_page >= 0) ? (uint8_t)force_page : UI_PAGE_DASHBOARD;
        model.speed_dmph = sim_dwg_motor_speed_dmph(&motor);
        model.cadence_rpm = sim_dwg_motor_cadence_rpm(&motor);
        model.power_w = sim_dwg_motor_power_w(&motor);
        model.soc_pct = sim_dwg_motor_soc_pct(&motor);
        model.batt_dV = sim_dwg_motor_batt_dV(&motor);
        model.batt_dA = sim_dwg_motor_batt_dA(&motor);
        model.ctrl_temp_dC = sim_dwg_motor_temp_dC(&motor);
        model.err = sim_dwg_motor_error_code(&motor);
        model.assist_mode = motor.config.gear_setting;
        model.virtual_gear = 2;
        model.buttons = ui_buttons;
        model.throttle_pct = 37;
        model.brake = (i == 10u) ? 1 : 0;
        model.theme = UI_THEME_NIGHT;
        model.units = motor.config.units_mode;
        model.range_est_d10 = 120;
        model.range_confidence = 3;
        model.graph_channel = UI_GRAPH_CH_SPEED;
        model.graph_window_s = 30;
        model.graph_sample_hz = (uint8_t)(1000u / UI_TICK_MS);

        /* Tick UI */
        ui_trace_t tr;
        if (ui_tick(&ui, &model, t_ms, &tr))
        {
            saw_ui = 1;
            if (tr.hash != 0)
                saw_hash = 1;
            if (tr.render_ms > UI_TICK_MS)
            {
                render_over_budget = tr.render_ms;
                break;
            }
            if (trace)
            {
                fprintf(trace, "t=%u hash=%08x ops=%u dirty=%u full=%u\n",
                        t_ms, tr.hash, tr.draw_ops, tr.dirty_count, tr.full);
            }
        }
    }

    if (trace)
        fclose(trace);
    if (ble_trace)
        fclose(ble_trace);
    if (ts_trace)
        fclose(ts_trace);
    sim_mcu_destroy(mcu);

    const char *lcd_out = getenv("UI_LCD_OUTDIR");
    if (!lcd_out || !lcd_out[0])
        lcd_out = "tests/host/lcd_out";
    printf("LCD DUMP: %s/host_lcd_latest.ppm\n", lcd_out);

    printf("FULL SIM: TTM MAC=%s connects=%u disconnects=%u mac_queries=%u\n",
           sim_ttm_get_mac_str(&ble),
           ble.ttm.connections, ble.ttm.disconnections, ble.ttm.mac_queries);
    printf("FULL SIM: BLE frames rx=%u tx=%u errs=%u\n",
           ble.frames_rx, ble.frames_tx, ble.parse_errors);
    printf("FULL SIM: Motor frames rx=%u tx=%u errs=%u\n",
           motor.frames_rx, motor.frames_tx, motor.parse_errors);

    if (render_over_budget)
    {
        fprintf(stderr, "SIM FAIL: ui render dt %u > %u\n", render_over_budget, UI_TICK_MS);
        return 1;
    }
    if (!saw_ui || !saw_hash)
    {
        fprintf(stderr, "SIM FAIL: missing UI ticks or hash\n");
        return 1;
    }

    printf("FULL SIM PASS: steps=%u dt=%u ms\n", steps, dt_ms);
    return 0;
}

int main(void)
{
    const char *steps_env = getenv("BC280_SIM_STEPS");
    const char *dt_env = getenv("BC280_SIM_DT_MS");
    const char *outdir = getenv("BC280_SIM_OUTDIR");
    const char *full_sim_env = getenv("BC280_SIM_FULL");
    uint32_t steps = steps_env ? (uint32_t)atoi(steps_env) : 60u;
    uint32_t dt_ms = dt_env ? (uint32_t)atoi(dt_env) : UI_TICK_MS;
    if (steps == 0 || dt_ms == 0)
    {
        fprintf(stderr, "Invalid sim params\n");
        return 1;
    }

    /* Use full simulation mode if BC280_SIM_FULL=1 */
    if (full_sim_env && full_sim_env[0] == '1')
    {
        return run_full_sim(steps, dt_ms, outdir);
    }

    FILE *trace = NULL;
    FILE *ts_trace = NULL;
    if (outdir && outdir[0])
    {
        char path[512];
        mkdir(outdir, 0755);
        snprintf(path, sizeof(path), "%s/sim_ui_trace.txt", outdir);
        trace = fopen(path, "w");
        snprintf(path, sizeof(path), "%s/shengyi_frames.log", outdir);
        ts_trace = fopen(path, "w");
    }

    sim_uart_init();
    sim_mcu_t *mcu = sim_mcu_create();
    sim_proto_state_t proto;
    sim_proto_init(&proto);

    sim_shengyi_t ts;
    sim_shengyi_init(&ts);

    ui_state_t ui;
    ui_init(&ui);

    uint8_t frame[64];
    size_t flen = 0;
    emit_ping(frame, sizeof(frame), &flen);
    sim_uart_rx_push(SIM_UART2, frame, flen);
    emit_set_stream(200, frame, sizeof(frame), &flen);
    sim_uart_rx_push(SIM_UART2, frame, flen);

    const char *btn_env = getenv("BC280_SIM_BUTTONS");
    uint8_t btn_mask = btn_env ? (uint8_t)strtoul(btn_env, NULL, 0) : 0u;
    sim_btn_step_t btn_seq[16];
    size_t btn_seq_len = parse_button_seq(getenv("BC280_SIM_BUTTONS_SEQ"), btn_seq, 16);
    const char *force_page_env = getenv("BC280_SIM_FORCE_PAGE");
    int force_page = force_page_env ? atoi(force_page_env) : -1;
    const char *btn_map_env = getenv("BC280_SIM_BUTTON_MAP");
    if (btn_map_env && btn_map_env[0])
        proto.button_map = (uint8_t)strtoul(btn_map_env, NULL, 0);
    const char *qa_env = getenv("BC280_SIM_QA_FLAGS");
    if (qa_env && qa_env[0])
        proto.qa_flags = (uint8_t)strtoul(qa_env, NULL, 0);

    uint8_t saw_ui = 0;
    uint8_t saw_hash = 0;
    uint8_t saw_ts_ok = 0;
    uint8_t saw_c3_ok = 0;
    uint8_t saw_status14_ok = 0;
    uint16_t render_over_budget = 0;
    for (uint32_t i = 0; i < steps; ++i)
    {
        sim_mcu_step(mcu, dt_ms);
        sim_shengyi_step(&ts, dt_ms);
        emit_set_state(&ts, frame, sizeof(frame), &flen, i);
        sim_uart_rx_push(SIM_UART1, frame, flen);

        proto.ms = ts.t_ms;

        uint8_t ts_frame[96];
        size_t ts_len = sim_shengyi_build_frame_0x52(&ts, ts_frame, sizeof(ts_frame));
        if (ts_len)
        {
            double speed_kph_x10 = 0.0;
            int cur_mA = 0;
            uint8_t batt_v = 0;
            uint8_t err = 0;
            int ok = sim_shengyi_decode_frame_0x52(ts_frame, ts_len, &ts, &speed_kph_x10, &cur_mA, &batt_v, &err);
            if (ok)
                saw_ts_ok = 1;
            if (ts_trace)
                fprintf(ts_trace, "t=%u cmd=0x52 ok=%d speed_kph_x10=%.1f current_mA=%d batt_q=%u err=%u raw=%02X%02X%02X%02X%02X\n",
                        proto.ms, ok, speed_kph_x10, cur_mA, batt_v, err,
                        ts_frame[4], ts_frame[5], ts_frame[6], ts_frame[7], ts_frame[8]);
        }

        size_t ts53_len = sim_shengyi_build_frame_0x53(&ts, ts_frame, sizeof(ts_frame));
        if (ts_trace && ts53_len)
            fprintf(ts_trace, "t=%u cmd=0x53 len=%zu raw=%02X%02X%02X%02X%02X%02X%02X\n",
                    proto.ms, ts53_len,
                    ts_frame[4], ts_frame[5], ts_frame[6],
                    ts_frame[7], ts_frame[8], ts_frame[9], ts_frame[10]);

        if (i == 0u)
        {
            size_t ts52_req_len = sim_shengyi_build_frame_0xC2(ts_frame, sizeof(ts_frame));
            if (ts_trace && ts52_req_len)
                fprintf(ts_trace, "t=%u cmd=0xC2 len=%zu\n", proto.ms, ts52_req_len);

            sim_shengyi_c3_t c3 = {0};
            c3.screen_brightness_level = 3;
            c3.auto_poweroff_minutes = 10;
            c3.batt_nominal_voltage_V = 48;
            c3.config_profile_id = 1;
            c3.lights_enabled = 1;
            c3.max_assist_level = 5;
            c3.gear_setting = ts.assist_level;
            c3.motor_enable_flag = 1;
            c3.brake_flag = 0;
            c3.speed_mode = 2;
            c3.display_setting = 1;
            c3.batt_voltage_threshold_mV = 42000;
            c3.batt_current_limit_mA = 15000;
            c3.speed_limit_kph_x10 = 250;
            c3.wheel_size_x10 = 240;
            c3.wheel_circumference_mm = 1914;
            c3.motor_current_mA_reported = (uint16_t)(sim_shengyi_batt_dA(&ts) * 100);
            c3.motor_power_W_reported = (uint16_t)sim_shengyi_power_w(&ts);
            c3.param_0235 = 0;
            c3.param_021C = 0;
            c3.param_0238 = 0;
            c3.param_0230 = 0;
            c3.param_023A = 0;
            c3.param_023B = 0;
            c3.param_023C = 0;

            size_t ts_c3_len = sim_shengyi_build_frame_0xC3(&c3, ts_frame, sizeof(ts_frame));
            if (ts_c3_len)
            {
                sim_shengyi_c3_t parsed = {0};
                if (sim_shengyi_decode_frame_0xC3(ts_frame, ts_c3_len, &parsed))
                    saw_c3_ok = 1;
                if (ts_trace)
                    fprintf(ts_trace, "t=%u cmd=0xC3 len=%zu ok=%u\n", proto.ms, ts_c3_len, saw_c3_ok);
            }

            sim_shengyi_status14_t st = {0};
            st.frame_type = 1;
            st.frame_counter = 1;
            st.profile_type = 3;
            st.power_level = (uint8_t)(ts.assist_level * 3);
            st.status_flags = 0x80u;
            st.display_setting = 1;
            st.wheel_size_x10 = 240;
            st.batt_current_raw = (uint8_t)(sim_shengyi_batt_dA(&ts) & 0xFF);
            st.batt_voltage_raw = (uint8_t)(sim_shengyi_batt_dV(&ts) & 0xFF);
            st.controller_temp_raw = (uint8_t)(ts.temp_c);
            st.speed_limit_kph = 25;
            st.batt_current_limit_a = 15;
            st.batt_voltage_threshold_div100 = 420;
            st.status2 = 0;

            size_t st_len = sim_shengyi_build_status14(&st, ts_frame, sizeof(ts_frame));
            if (st_len)
            {
                sim_shengyi_status14_t parsed = {0};
                if (sim_shengyi_decode_status14(ts_frame, st_len, &parsed))
                    saw_status14_ok = 1;
                if (ts_trace)
                    fprintf(ts_trace, "t=%u cmd=0x14 len=%zu\n", proto.ms, st_len);
            }
        }

        for (int port = 0; port < SIM_UART_MAX; ++port)
        {
            uint8_t b;
            while (sim_uart_rx_pop((sim_uart_port_t)port, &b))
                sim_proto_feed(&proto, (sim_uart_port_t)port, b);
        }

        sim_proto_update_inputs(&proto,
                                (uint16_t)((ts.cadence_rpm * 3.0) + 0.5),
                                ts.torque_raw,
                                sim_shengyi_speed_dmph(&ts),
                                ts.soc_pct,
                                ts.err,
                                sim_shengyi_cadence_rpm(&ts),
                                sim_shengyi_power_w(&ts),
                                sim_shengyi_batt_dV(&ts),
                                sim_shengyi_batt_dA(&ts),
                                (int16_t)(ts.temp_c * 10.0));

        sim_proto_tick(&proto);

        uint8_t ui_buttons = btn_mask;
        if (btn_seq_len)
        {
            for (size_t bi = 0; bi < btn_seq_len; ++bi)
            {
                if (btn_seq[bi].step == i)
                    ui_buttons = btn_seq[bi].mask;
            }
        }
        else if (!btn_env)
        {
            if (i == 12u)
                ui_buttons = OEM_BTN_MENU;
            else if (i == 22u)
                ui_buttons = OEM_BTN_POWER;
        }
        uint8_t n63 = sample_buttons_oem(mcu, ui_buttons);
        ui_buttons = oem_buttons_map_raw(n63, NULL);
        ui_model_t model;
        sim_proto_fill_model_with_buttons(&proto, &model, ui_buttons, 37u, (i == 10u) ? 1u : 0u);
        if (force_page >= 0)
            model.page = (uint8_t)force_page;
    ui_trace_t t;
    if (ui_tick(&ui, &model, proto.ms, &t))
    {
        saw_ui = 1;
        if (t.hash != 0)
            saw_hash = 1;
        if (t.render_ms > UI_TICK_MS)
        {
            render_over_budget = t.render_ms;
            break;
        }
        if (trace)
        {
            fprintf(trace, "t=%u hash=%08x ops=%u dirty=%u full=%u\n",
                    proto.ms, t.hash, t.draw_ops, t.dirty_count, t.full);
            if (model.page != UI_PAGE_DASHBOARD)
            {
                char line[256];
                size_t n = ui_format_engineer_trace(line, sizeof(line), &model);
                if (n)
                    fprintf(trace, "%s", line);
            }
        }
    }
    }

    if (trace)
        fclose(trace);
    if (ts_trace)
        fclose(ts_trace);
    sim_mcu_destroy(mcu);

    const char *lcd_out = getenv("UI_LCD_OUTDIR");
    if (!lcd_out || !lcd_out[0])
        lcd_out = "tests/host/lcd_out";
    printf("LCD DUMP: %s/host_lcd_latest.ppm\n", lcd_out);

    if (render_over_budget)
    {
        fprintf(stderr, "SIM FAIL: ui render dt %u > %u\n", render_over_budget, UI_TICK_MS);
        return 1;
    }
    if (!saw_ui || !saw_hash)
    {
        fprintf(stderr, "SIM FAIL: missing UI ticks or hash\n");
        return 1;
    }
    if (!saw_ts_ok)
    {
        fprintf(stderr, "SIM FAIL: no valid Shengyi frame decode\n");
        return 1;
    }
    if (!saw_c3_ok)
    {
        fprintf(stderr, "SIM FAIL: no valid Shengyi C3 decode\n");
        return 1;
    }
    if (!saw_status14_ok)
    {
        fprintf(stderr, "SIM FAIL: no valid Shengyi status14 decode\n");
        return 1;
    }
    size_t tx_len = sim_uart_tx_size(SIM_UART2);
    if (tx_len == 0)
    {
        fprintf(stderr, "SIM FAIL: no BLE UART TX\n");
        return 1;
    }
    uint8_t tx_buf[4096];
    size_t got = sim_uart_tx_read(SIM_UART2, tx_buf, sizeof(tx_buf));
    uint8_t saw_stream = 0;
    if (!validate_tx_frames(tx_buf, got, &saw_stream))
    {
        fprintf(stderr, "SIM FAIL: invalid BLE UART framing\n");
        return 1;
    }
    if (!saw_stream)
    {
        fprintf(stderr, "SIM FAIL: missing telemetry stream frames\n");
        return 1;
    }

    printf("SIM PASS: steps=%u dt=%u ms\n", steps, dt_ms);
    return 0;
}
