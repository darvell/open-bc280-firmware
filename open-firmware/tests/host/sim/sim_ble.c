#include "sim_ble.h"
#include "sim_uart.h"
#include "util/byteorder.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * BLE Simulator Architecture
 *
 * sim_ble represents the EXTERNAL TTM BLE chip + mobile app - it is NOT
 * the display firmware. Its role is to GENERATE stimuli that the display
 * firmware would react to.
 *
 * Data Flow:
 *   sim_ble (TTM chip) ---> UART1 RX ---> Display firmware (processes)
 *   Display firmware ---> UART1 TX ---> sim_ble (can verify responses)
 *
 * TTM Text Messages (sent BY sim_ble TO display):
 *   "TTM:CONNECTED\n"  - BLE client connected
 *   "TTM:DISCONNECT\n" - BLE client disconnected
 *   "TTM:MAC-XX:XX:XX:XX:XX:XX\n" - Response to MAC query
 *
 * BLE Binary Protocol (0x55 framed, sent BY sim_ble TO display):
 *   Commands like get_realtime (0x60), get_params (0x30), etc.
 *
 * Note: The sim_ble_process() and handle_* functions exist for testing
 * purposes where sim_ble can act as a complete BLE mock that both sends
 * commands AND processes responses. In the host simulation, the display
 * firmware's BLE handler is not yet compiled in, so these are unused.
 * ============================================================================
 */

/* ============================================================================
 * TTM BLE Module Implementation
 * ============================================================================
 */

/* TTM text message strings */
static const char TTM_CONNECTED[] = "TTM:CONNECTED";
static const char TTM_DISCONNECT[] = "TTM:DISCONNECT";

/* Initialize TTM module */
void sim_ttm_init(sim_ttm_t *ttm, const uint8_t mac[6])
{
    if (!ttm)
        return;

    memset(ttm, 0, sizeof(*ttm));

    /* Set MAC address */
    if (mac)
    {
        memcpy(ttm->mac, mac, SIM_TTM_MAC_LEN);
    }
    else
    {
        /* Default MAC: 00:11:22:33:44:55 */
        ttm->mac[0] = 0x00;
        ttm->mac[1] = 0x11;
        ttm->mac[2] = 0x22;
        ttm->mac[3] = 0x33;
        ttm->mac[4] = 0x44;
        ttm->mac[5] = 0x55;
    }

    /* Format MAC string */
    snprintf(ttm->mac_str, SIM_TTM_MAC_STR_LEN, "%02X:%02X:%02X:%02X:%02X:%02X",
             ttm->mac[0], ttm->mac[1], ttm->mac[2],
             ttm->mac[3], ttm->mac[4], ttm->mac[5]);

    ttm->mac_valid = 1;
    ttm->state = SIM_TTM_STATE_ADVERTISING;
}

/* Send TTM text message TO the display (via UART1 RX)
 * TTM chip sends notifications like "TTM:CONNECTED" to display's RX pin */
static void ttm_send_to_display(const char *text)
{
    size_t len = strlen(text);
    sim_uart_rx_push(SIM_UART1, (const uint8_t *)text, len);
    sim_uart_rx_push(SIM_UART1, (const uint8_t *)"\n", 1);
}

/* Send TTM MAC response TO display (TTM chip responding to MAC query) */
/* Send TTM connection status TO display (TTM chip notifying display) */
static void ttm_send_connected(void)
{
    ttm_send_to_display(TTM_CONNECTED);
}

static void ttm_send_disconnect(void)
{
    ttm_send_to_display(TTM_DISCONNECT);
}

/* Feed a byte from UART1 RX into the BLE simulator
 * This is data arriving AT the display (from TTM chip / BLE app)
 * The TTM chip has already processed it and is passing it through */
void sim_ttm_feed_byte(sim_ble_t *ble, uint8_t byte)
{
    if (!ble)
        return;

    sim_ttm_t *ttm = &ble->ttm;

    /* Skip TTM text messages - these go TO the display firmware, not to our sim_ble
     * The display firmware would parse "TTM:CONNECTED" etc, but sim_ble is just
     * handling the 0x55 protocol layer */
    if (byte == 'T')
    {
        /* Start of potential TTM message - skip it for now
         * In a full simulation, we'd parse this and update connection state */
        ttm->parse_state = SIM_TTM_PARSE_TEXT;
        ttm->text_pos = 0;
        return;
    }

    if (ttm->parse_state == SIM_TTM_PARSE_TEXT)
    {
        /* Accumulating TTM text - skip until newline */
        if (byte == '\n' || byte == '\r')
        {
            ttm->parse_state = SIM_TTM_PARSE_IDLE;
            ttm->text_pos = 0;
        }
        return;
    }

    /* Check for 0x55 SOF - binary protocol frame */
    if (byte == COMM_SOF)
    {
        /* Only process binary frames when connected */
        if (ttm->state == SIM_TTM_STATE_CONNECTED)
        {
            sim_ble_feed_byte(ble, byte);
        }
        return;
    }

    /* If connected, pass other bytes to binary parser */
    if (ttm->state == SIM_TTM_STATE_CONNECTED)
    {
        sim_ble_feed_byte(ble, byte);
    }
}

/* Check display's TX for TTM queries (display sending to TTM chip) */
void sim_ttm_check_display_tx(sim_ble_t *ble)
{
    if (!ble)
        return;

    /* Read what the display wrote to TX and check for TTM queries */
    uint8_t tx_buf[256];
    size_t tx_len = sim_uart_tx_size(SIM_UART1);
    if (tx_len > 0 && tx_len <= sizeof(tx_buf))
    {
        /* Peek at TX buffer without consuming - we'd need a peek function
         * For now, skip this - the display firmware would send TTM:MAC-?
         * but our sim_ble doesn't */
    }
}

/* Process TTM state machine */
void sim_ttm_tick(sim_ble_t *ble, uint32_t dt_ms)
{
    if (!ble)
        return;

    sim_ttm_t *ttm = &ble->ttm;
    ttm->state_timer_ms += dt_ms;

    switch (ttm->state)
    {
        case SIM_TTM_STATE_IDLE:
            /* Nothing to do */
            break;

        case SIM_TTM_STATE_ADVERTISING:
            /* Check for auto-connect */
            if (ttm->connect_delay_ms > 0 &&
                ttm->state_timer_ms >= ttm->connect_delay_ms)
            {
                sim_ttm_connect(ble);
            }
            break;

        case SIM_TTM_STATE_CONNECTED:
            /* Check for auto-disconnect */
            if (ttm->disconnect_after_ms > 0 &&
                ttm->state_timer_ms >= ttm->disconnect_after_ms)
            {
                sim_ttm_disconnect(ble);
            }
            break;

        case SIM_TTM_STATE_DISCONNECTING:
            /* Transition back to advertising after short delay */
            if (ttm->state_timer_ms >= 100)
            {
                ttm->state = SIM_TTM_STATE_ADVERTISING;
                ttm->state_timer_ms = 0;
            }
            break;
    }
}

/* Trigger BLE connection */
void sim_ttm_connect(sim_ble_t *ble)
{
    if (!ble)
        return;

    sim_ttm_t *ttm = &ble->ttm;

    if (ttm->state != SIM_TTM_STATE_CONNECTED)
    {
        ttm->state = SIM_TTM_STATE_CONNECTED;
        ttm->state_timer_ms = 0;
        ttm->connections++;

        /* Send connection notification to firmware */
        ttm_send_connected();
    }
}

/* Trigger BLE disconnection */
void sim_ttm_disconnect(sim_ble_t *ble)
{
    if (!ble)
        return;

    sim_ttm_t *ttm = &ble->ttm;

    if (ttm->state == SIM_TTM_STATE_CONNECTED)
    {
        ttm->state = SIM_TTM_STATE_DISCONNECTING;
        ttm->state_timer_ms = 0;
        ttm->disconnections++;

        /* Send disconnection notification to firmware */
        ttm_send_disconnect();
    }
}

/* Set auto-connect delay */
void sim_ttm_set_auto_connect(sim_ble_t *ble, uint32_t delay_ms)
{
    if (!ble)
        return;
    ble->ttm.connect_delay_ms = delay_ms;
}

/* Check if connected */
int sim_ttm_is_connected(const sim_ble_t *ble)
{
    if (!ble)
        return 0;
    return (ble->ttm.state == SIM_TTM_STATE_CONNECTED) ? 1 : 0;
}

/* Get MAC address string */
const char *sim_ttm_get_mac_str(const sim_ble_t *ble)
{
    if (!ble)
        return "00:00:00:00:00:00";
    return ble->ttm.mac_str;
}

/* ============================================================================
 * BLE 0x55 Protocol Implementation
 * ============================================================================
 */

/* Internal helper to send a response frame to UART1 TX */
static void send_response(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[SIM_BLE_MAX_FRAME];
    if (len > SIM_BLE_MAX_PAYLOAD)
        return;
    size_t flen = comm_frame_build(frame, sizeof(frame), cmd, payload, len);
    if (!flen)
        return;
    sim_uart_tx_write(SIM_UART1, frame, flen);
}

/* Send simple status response: [cmd+1] [01] [status] */
static void send_status(uint8_t cmd, uint8_t status)
{
    uint8_t payload[1] = {status};
    send_response((uint8_t)(cmd | 0x01u), payload, 1);
}

/* Initialize BLE simulator */
void sim_ble_init(sim_ble_t *ble)
{
    if (!ble)
        return;

    memset(ble, 0, sizeof(*ble));

    /* Initialize TTM module with default MAC */
    sim_ttm_init(&ble->ttm, NULL);

    /* Default display settings */
    ble->headlight_enabled = 0;
    ble->screen_brightness = 3;
    ble->auto_poweroff_min = 10;
    ble->speed_limit_kph = 25;
    ble->units_mode = 0;  /* Metric */
    ble->assist_level = 2;

    /* Default firmware version: 3.3.6 */
    ble->fw_version[0] = 0x03;
    ble->fw_version[1] = 0x03;
    ble->fw_version[2] = 0x06;
    ble->fw_version[3] = 0x00;
    ble->fw_version[4] = 0x00;
    ble->fw_version[5] = 0x00;
    ble->fw_version[6] = 0x00;

    /* Initialize auth table with predictable pattern for testing */
    for (int i = 0; i < 768; ++i)
        ble->auth_table[i] = (uint8_t)(i & 0xFF);

    ble->telemetry_period_ms = 200;

    /* Set auto-connect after 500ms for simulation */
    ble->ttm.connect_delay_ms = 500;
}

/* Feed a byte from UART1 RX into the parser */
void sim_ble_feed_byte(sim_ble_t *ble, uint8_t byte)
{
    if (!ble)
        return;
    uint8_t frame_len = 0;
    comm_parse_result_t res = comm_parser_feed(ble->parse_frame, sizeof(ble->parse_frame),
                                               SIM_BLE_MAX_PAYLOAD, &ble->parse_len,
                                               byte, &frame_len);
    if (res == COMM_PARSE_ERROR)
    {
        ble->parse_errors++;
        return;
    }
    if (res != COMM_PARSE_FRAME)
        return;

    if (!comm_frame_validate(ble->parse_frame, frame_len, NULL))
    {
        ble->parse_errors++;
        return;
    }

    /* Valid frame - queue it */
    uint8_t next_wr = (ble->rx_wr_idx + 1u) % SIM_BLE_RX_SLOTS;
    if (next_wr != ble->rx_rd_idx)
    {
        sim_ble_frame_t *slot = &ble->rx_frames[ble->rx_wr_idx];
        slot->cmd = ble->parse_frame[1];
        slot->len = ble->parse_frame[2];
        memcpy(slot->payload, &ble->parse_frame[3], slot->len);
        slot->valid = 1;
        ble->rx_wr_idx = next_wr;
        ble->frames_rx++;
    }
}

/* Handle command 0x02 - Authentication */
static void handle_auth(sim_ble_t *ble, const uint8_t *payload, uint8_t len)
{
    uint8_t success = 1;

    if (len >= 9)
    {
        /* Validate 3 key-value pairs against auth table */
        for (int i = 0; i < 3; ++i)
        {
            uint8_t key = payload[i * 3];
            uint8_t idx = payload[i * 3 + 1];
            uint8_t val = payload[i * 3 + 2];

            if (key < 1 || key > 3)
            {
                success = 0;
                break;
            }

            uint8_t expected = ble->auth_table[(key - 1) * 256 + idx];
            if (expected != val)
            {
                success = 0;
                break;
            }
        }
    }
    else
    {
        success = 0;
    }

    ble->authenticated = success;
    send_status(SIM_BLE_CMD_AUTH, success ? 0 : 1);
}

/* Handle command 0x04 - Get firmware version */
static void handle_get_version(sim_ble_t *ble)
{
    send_response(0x05, ble->fw_version, 7);
}

/* Handle command 0x06 - Set date/time */
static void handle_set_time(sim_ble_t *ble, const uint8_t *payload, uint8_t len)
{
    (void)ble;
    (void)payload;
    if (len >= 7)
    {
        /* In real firmware this sets RTC - we just ACK */
        send_status(SIM_BLE_CMD_SET_TIME, 0);
    }
    else
    {
        send_status(SIM_BLE_CMD_SET_TIME, 1);
    }
}

/* Handle command 0x30 - Get instrument parameters */
static void handle_get_params(sim_ble_t *ble)
{
    uint8_t resp[22];
    memset(resp, 0, sizeof(resp));

    /* Odometer distance (4 bytes BE) */
    store_be32(&resp[0], ble->odometer.distance_m);

    /* Moving time (4 bytes BE) */
    store_be32(&resp[4], ble->odometer.moving_time_s);

    /* Distance subunits (4 bytes BE) */
    resp[8] = 0;
    resp[9] = 0;
    resp[10] = 0;
    resp[11] = 0;

    /* Battery voltage mV (2 bytes BE) */
    uint16_t batt_mV = (uint16_t)(ble->batt_dV * 100);
    store_be16(&resp[12], batt_mV);

    /* Current mA (2 bytes BE) */
    uint16_t curr_mA = (uint16_t)(ble->batt_dA * 100);
    store_be16(&resp[14], curr_mA);

    resp[16] = 0;  /* status flag */
    resp[17] = ble->assist_level;
    resp[18] = ble->headlight_enabled;
    resp[19] = ble->auto_poweroff_min;
    resp[20] = ble->speed_limit_kph;
    resp[21] = ble->units_mode;

    send_response(0x31, resp, 22);
}

/* Handle command 0x32 - Set configuration */
static void handle_set_config(sim_ble_t *ble, const uint8_t *payload, uint8_t len)
{
    if (len < 3)
    {
        send_status(SIM_BLE_CMD_SET_CONFIG, 1);
        return;
    }

    uint8_t cfg_type = payload[0];
    uint8_t value = payload[2];
    uint8_t new_value = 0;
    uint8_t status = 0;

    switch (cfg_type)
    {
        case SIM_BLE_CFG_HEADLIGHT:
            ble->headlight_enabled = value ? 1 : 0;
            new_value = ble->headlight_enabled;
            break;

        case SIM_BLE_CFG_DISPLAY_MODE:
            ble->auto_poweroff_min = value;
            new_value = value;
            break;

        case SIM_BLE_CFG_SPEED_LIMIT:
            ble->speed_limit_kph = value;
            new_value = value;
            break;

        case SIM_BLE_CFG_UNITS:
            ble->units_mode = value;
            new_value = value;
            break;

        case SIM_BLE_CFG_ASSIST:
            if (value == 1 && ble->assist_level < 4)
                ble->assist_level++;
            else if (value == 0 && ble->assist_level > 0)
                ble->assist_level--;
            new_value = ble->assist_level;
            break;

        case SIM_BLE_CFG_BRIGHTNESS:
            ble->screen_brightness = value;
            new_value = value;
            break;

        default:
            status = 1;
            break;
    }

    uint8_t resp[3] = {cfg_type, status, new_value};
    send_response(0x33, resp, 3);
}

/* Handle command 0x37 - Get instrument group data */
static void handle_get_group(sim_ble_t *ble, const uint8_t *payload, uint8_t len)
{
    if (len < 1)
    {
        send_status(SIM_BLE_CMD_GET_GROUP, 1);
        return;
    }

    uint8_t group = payload[0];
    uint8_t resp[48];
    size_t resp_len = 0;

    switch (group)
    {
        case 1:
            resp[resp_len++] = 1;  /* group_id */
            /* Odometer distance (4 bytes BE) */
            store_be32(&resp[resp_len], ble->odometer.distance_m);
            resp_len += 4;
            /* Moving time (4 bytes BE) */
            store_be32(&resp[resp_len], ble->odometer.moving_time_s);
            resp_len += 4;
            /* Distance subunits (4 bytes BE) */
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            /* Max speed (2 bytes BE) */
            store_be16(&resp[resp_len], ble->odometer.max_speed_dmph);
            resp_len += 2;
            /* Avg speed (2 bytes BE) */
            store_be16(&resp[resp_len], ble->odometer.avg_speed_dmph);
            resp_len += 2;
            resp[resp_len++] = 0;  /* pad */
            resp[resp_len++] = ble->assist_level;
            resp[resp_len++] = ble->headlight_enabled;
            resp[resp_len++] = ble->units_mode;
            resp[resp_len++] = 0;  /* reserved */
            break;

        case 2:
            resp[resp_len++] = 2;  /* group_id */
            resp[resp_len++] = ble->screen_brightness;
            resp[resp_len++] = ble->auto_poweroff_min;
            resp[resp_len++] = ble->speed_limit_kph;
            resp[resp_len++] = 1;  /* reserved */
            resp[resp_len++] = 1;  /* reserved */
            break;

        case 3:
            resp[resp_len++] = 3;  /* group_id */
            /* 42 bytes of zeros (reserved) */
            for (int i = 0; i < 42; ++i)
                resp[resp_len++] = 0;
            break;

        case 4:
            resp[resp_len++] = 4;  /* group_id */
            /* Odometer (12 bytes) */
            store_be32(&resp[resp_len], ble->odometer.distance_m);
            resp_len += 4;
            store_be32(&resp[resp_len], ble->odometer.moving_time_s);
            resp_len += 4;
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            /* Max/avg speed (4 bytes) */
            store_be16(&resp[resp_len], ble->odometer.max_speed_dmph);
            resp_len += 2;
            store_be16(&resp[resp_len], ble->odometer.avg_speed_dmph);
            resp_len += 2;
            /* CO2 saved (4 bytes) */
            uint32_t co2 = ble->odometer.distance_m / 10;  /* approx */
            store_be32(&resp[resp_len], co2);
            resp_len += 4;
            /* Calories (2 bytes) */
            uint16_t cal = (uint16_t)(ble->odometer.distance_m / 100);
            store_be16(&resp[resp_len], cal);
            resp_len += 2;
            /* Trip A (12 bytes) */
            store_be32(&resp[resp_len], ble->trip_a.distance_m);
            resp_len += 4;
            store_be32(&resp[resp_len], ble->trip_a.moving_time_s);
            resp_len += 4;
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            resp[resp_len++] = 0;
            /* Trip A avg speed (2 bytes) */
            store_be16(&resp[resp_len], ble->trip_a.avg_speed_dmph);
            resp_len += 2;
            /* Trip B (4 bytes, truncated) */
            store_be32(&resp[resp_len], ble->trip_b.distance_m);
            resp_len += 4;
            break;

        default:
            send_status(SIM_BLE_CMD_GET_GROUP, 1);
            return;
    }

    send_response(0x38, resp, (uint8_t)resp_len);
}

/* Handle command 0x60 - Get realtime motion data */
static void handle_get_realtime(sim_ble_t *ble)
{
    uint8_t resp[14];
    memset(resp, 0, sizeof(resp));

    /* Battery power W (2 bytes BE) */
    store_be16(&resp[0], ble->power_w);

    /* Motor temperature (2 bytes BE) */
    uint16_t temp = (uint16_t)(ble->motor_temp_dC);
    store_be16(&resp[2], temp);

    /* Assist level */
    resp[4] = ble->assist_level;

    /* Speed x10 (2 bytes BE) */
    store_be16(&resp[5], ble->speed_dmph);

    /* Odometer distance (4 bytes BE) */
    store_be32(&resp[7], ble->odometer.distance_m);

    resp[11] = 0;  /* pad */
    resp[12] = ble->error_code;

    /* Motor tick period (2 bytes BE) - just use time */
    resp[13] = 0;

    send_response(0x61, resp, 11);
}

/* Handle command 0xF0 - Get battery stats */
static void handle_get_batt_stats(sim_ble_t *ble)
{
    uint8_t resp[8];
    memset(resp, 0, sizeof(resp));

    /* Reserved (2 bytes) */
    resp[0] = 0;
    resp[1] = 0;

    /* Battery voltage mV (2 bytes BE) */
    uint16_t batt_mV = (uint16_t)(ble->batt_dV * 100);
    store_be16(&resp[2], batt_mV);

    /* Reserved (2 bytes) */
    resp[4] = 0;
    resp[5] = 0;

    /* Battery current mA (2 bytes BE) */
    uint16_t curr_mA = (uint16_t)(ble->batt_dA * 100);
    store_be16(&resp[6], curr_mA);

    send_response(0xF1, resp, 8);
}

/* Process a single frame */
static void process_frame(sim_ble_t *ble, sim_ble_frame_t *frame)
{
    switch (frame->cmd)
    {
        case SIM_BLE_CMD_AUTH:
            handle_auth(ble, frame->payload, frame->len);
            break;

        case SIM_BLE_CMD_VERSION:
            handle_get_version(ble);
            break;

        case SIM_BLE_CMD_SET_TIME:
            handle_set_time(ble, frame->payload, frame->len);
            break;

        case SIM_BLE_CMD_UPDATE_CACHE:
            /* Just ACK - data cache is internal */
            send_status(SIM_BLE_CMD_UPDATE_CACHE, 0);
            break;

        case SIM_BLE_CMD_GET_BATT_DIST:
        {
            /* Return version digits */
            uint8_t resp[2] = {3, 6};  /* e.g., 3.6 */
            send_response(0x0B, resp, 2);
            break;
        }

        case SIM_BLE_CMD_ENTER_BOOTLOADER:
            /* Just ACK - we don't actually enter bootloader in sim */
            send_response(0x21, NULL, 0);
            break;

        case SIM_BLE_CMD_GET_PARAMS:
            handle_get_params(ble);
            break;

        case SIM_BLE_CMD_SET_CONFIG:
            handle_set_config(ble, frame->payload, frame->len);
            break;

        case SIM_BLE_CMD_GET_GROUP:
            handle_get_group(ble, frame->payload, frame->len);
            break;

        case SIM_BLE_CMD_GET_REALTIME:
            handle_get_realtime(ble);
            break;

        case SIM_BLE_CMD_GET_HISTORY:
        case SIM_BLE_CMD_GET_HISTORY_NEXT:
        {
            /* Fake history data */
            uint8_t resp[10];
            memset(resp, 0, sizeof(resp));
            uint32_t ts = ble->t_ms / 1000;
            store_be32(&resp[0], ts);
            send_response(0x64, resp, 10);
            break;
        }

        case SIM_BLE_CMD_GET_MOTOR:
        case SIM_BLE_CMD_GET_MOTOR_NEXT:
        {
            /* Fake motor/trip data */
            uint8_t resp[22];
            memset(resp, 0, sizeof(resp));
            uint32_t ts = ble->t_ms / 1000;
            store_be32(&resp[0], ts);
            store_be32(&resp[4], ts);
            send_response(0x68, resp, 22);
            break;
        }

        case SIM_BLE_CMD_GET_BATT_STATS:
            handle_get_batt_stats(ble);
            break;

        default:
            /* Unknown command - send error */
            send_status(frame->cmd, 0xFF);
            break;
    }

    ble->frames_tx++;
}

/* Process all pending frames and generate responses */
void sim_ble_process(sim_ble_t *ble)
{
    if (!ble)
        return;

    while (ble->rx_rd_idx != ble->rx_wr_idx)
    {
        sim_ble_frame_t *frame = &ble->rx_frames[ble->rx_rd_idx];
        if (frame->valid)
        {
            process_frame(ble, frame);
            frame->valid = 0;
        }
        ble->rx_rd_idx = (ble->rx_rd_idx + 1u) % SIM_BLE_RX_SLOTS;
    }
}

/* Update telemetry data */
void sim_ble_update_telemetry(sim_ble_t *ble,
                               uint16_t speed_dmph,
                               uint16_t cadence_rpm,
                               uint16_t power_w,
                               int16_t batt_dV,
                               int16_t batt_dA,
                               int16_t motor_temp_dC,
                               uint8_t soc_pct,
                               uint8_t error_code)
{
    if (!ble)
        return;

    ble->speed_dmph = speed_dmph;
    ble->cadence_rpm = cadence_rpm;
    ble->power_w = power_w;
    ble->batt_dV = batt_dV;
    ble->batt_dA = batt_dA;
    ble->motor_temp_dC = motor_temp_dC;
    ble->soc_pct = soc_pct;
    ble->error_code = error_code;
}

/* Update trip data based on current speed */
void sim_ble_update_trips(sim_ble_t *ble, uint32_t dt_ms)
{
    if (!ble || dt_ms == 0)
        return;

    /* Convert speed from deci-mph to m/s: dmph * 0.044704 */
    double v_mps = (double)ble->speed_dmph * 0.044704 / 10.0;
    uint32_t dist_m = (uint32_t)(v_mps * (double)dt_ms / 1000.0);

    if (ble->speed_dmph > 5)  /* ~0.5 mph threshold */
    {
        uint32_t dt_s = dt_ms / 1000;
        if (dt_s == 0)
            dt_s = 1;

        ble->odometer.distance_m += dist_m;
        ble->odometer.moving_time_s += dt_s;
        if (ble->speed_dmph > ble->odometer.max_speed_dmph)
            ble->odometer.max_speed_dmph = ble->speed_dmph;
        if (ble->odometer.moving_time_s > 0)
            ble->odometer.avg_speed_dmph = (uint16_t)(
                (uint32_t)ble->odometer.distance_m * 36 /
                ble->odometer.moving_time_s);

        ble->trip_a.distance_m += dist_m;
        ble->trip_a.moving_time_s += dt_s;
        if (ble->speed_dmph > ble->trip_a.max_speed_dmph)
            ble->trip_a.max_speed_dmph = ble->speed_dmph;
        if (ble->trip_a.moving_time_s > 0)
            ble->trip_a.avg_speed_dmph = (uint16_t)(
                (uint32_t)ble->trip_a.distance_m * 36 /
                ble->trip_a.moving_time_s);

        ble->trip_b.distance_m += dist_m;
        ble->trip_b.moving_time_s += dt_s;
    }
}

/* Advance time */
void sim_ble_tick(sim_ble_t *ble, uint32_t dt_ms)
{
    if (!ble)
        return;

    ble->t_ms += dt_ms;

    /* Process TTM state machine (connection events, auto-connect) */
    sim_ttm_tick(ble, dt_ms);

    /* Update trip data based on speed */
    sim_ble_update_trips(ble, dt_ms);
}

/* Build a command frame */
size_t sim_ble_build_command(uint8_t cmd, const uint8_t *payload, uint8_t len,
                             uint8_t *out, size_t cap)
{
    if (len > SIM_BLE_MAX_PAYLOAD)
        return 0;
    return comm_frame_build(out, cap, cmd, payload, len);
}

size_t sim_ble_build_ping(uint8_t *out, size_t cap)
{
    return sim_ble_build_command(0x01, NULL, 0, out, cap);
}

size_t sim_ble_build_get_version(uint8_t *out, size_t cap)
{
    return sim_ble_build_command(SIM_BLE_CMD_VERSION, NULL, 0, out, cap);
}

size_t sim_ble_build_set_time(uint16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second,
                              uint8_t *out, size_t cap)
{
    uint8_t payload[7];
    payload[0] = 0;  /* reserved */
    payload[1] = (uint8_t)(year - 2000);
    payload[2] = month;
    payload[3] = day;
    payload[4] = hour;
    payload[5] = minute;
    payload[6] = second;
    return sim_ble_build_command(SIM_BLE_CMD_SET_TIME, payload, 7, out, cap);
}

size_t sim_ble_build_get_realtime(uint8_t *out, size_t cap)
{
    return sim_ble_build_command(SIM_BLE_CMD_GET_REALTIME, NULL, 0, out, cap);
}

size_t sim_ble_build_get_params(uint8_t *out, size_t cap)
{
    return sim_ble_build_command(SIM_BLE_CMD_GET_PARAMS, NULL, 0, out, cap);
}

size_t sim_ble_build_get_group(uint8_t group, uint8_t *out, size_t cap)
{
    uint8_t payload[1] = {group};
    return sim_ble_build_command(SIM_BLE_CMD_GET_GROUP, payload, 1, out, cap);
}

size_t sim_ble_build_set_config(uint8_t cfg_type, uint8_t value,
                                uint8_t *out, size_t cap)
{
    uint8_t payload[3] = {cfg_type, 0, value};
    return sim_ble_build_command(SIM_BLE_CMD_SET_CONFIG, payload, 3, out, cap);
}

size_t sim_ble_build_get_batt_stats(uint8_t *out, size_t cap)
{
    return sim_ble_build_command(SIM_BLE_CMD_GET_BATT_STATS, NULL, 0, out, cap);
}
