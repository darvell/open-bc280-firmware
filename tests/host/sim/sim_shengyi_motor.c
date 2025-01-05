#include "sim_shengyi_motor.h"
#include "sim_shengyi_frame.h"
#include "util/byteorder.h"
#include <string.h>
#include <math.h>

/* Internal helper to send a frame TO the display (motor -> display via UART2 RX)
 * The motor is an external device, so its responses go into the display's RX buffer */
static void send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[SIM_DWG_MAX_FRAME];
    size_t flen = shengyi_frame_build(cmd, payload, len, frame, sizeof(frame));
    if (!flen)
        return;

    sim_uart_rx_push(SIM_UART2, frame, flen);
}

/* Send ACK (0xC1) */
static void send_ack(uint8_t status)
{
    uint8_t payload[1] = {status};
    send_frame(SIM_DWG_CMD_ACK, payload, 1);
}

/* Initialize motor controller simulator */
void sim_dwg_motor_init(sim_dwg_motor_t *m)
{
    if (!m)
        return;

    memset(m, 0, sizeof(*m));

    /* Initialize physics simulator */
    sim_shengyi_init(&m->bike);

    /* Default configuration */
    m->config.screen_brightness = 3;
    m->config.auto_poweroff_min = 10;
    m->config.batt_nominal_V = 48;
    m->config.config_profile_id = 1;
    m->config.lights_enabled = 0;
    m->config.max_assist_level = 5;
    m->config.gear_setting = 2;
    m->config.motor_enable = 1;
    m->config.brake_flag = 0;
    m->config.speed_mode = 2;
    m->config.display_setting = 1;
    m->config.batt_threshold_mV = 42000;
    m->config.batt_current_limit_mA = 15000;
    m->config.speed_limit_kph_x10 = 250;
    m->config.wheel_size_code = 5;  /* 26" */
    m->config.wheel_circumference_mm = 2074;
    m->config.units_mode = 0;
    m->config.motor_timeout_s = 5;

    m->motor_enabled = 1;
    m->status_period_ms = 100;

    /* Initialize flash slots with test data */
    for (int i = 0; i < 17; ++i)
    {
        m->flash_slot_lens[i] = 4;
        memset(m->flash_slots[i], (uint8_t)i, 4);
    }
}

/* Feed a byte from UART2 RX into the parser */
void sim_dwg_motor_feed_byte(sim_dwg_motor_t *m, uint8_t byte)
{
    if (!m)
        return;

    switch (m->parse_state)
    {
        case SIM_DWG_STATE_WAIT_SOF:
            if (byte == SIM_DWG_SOF)
            {
                m->parse_checksum = 0;
                m->parse_state = SIM_DWG_STATE_FRAME_ID;
            }
            break;

        case SIM_DWG_STATE_FRAME_ID:
            if (byte == SIM_DWG_FRAME_ID)
            {
                m->parse_checksum += byte;
                m->parse_state = SIM_DWG_STATE_CMD;
            }
            else
            {
                m->parse_errors++;
                m->parse_state = SIM_DWG_STATE_WAIT_SOF;
            }
            break;

        case SIM_DWG_STATE_CMD:
            m->parse_cmd = byte;
            m->parse_checksum += byte;
            m->parse_state = SIM_DWG_STATE_LEN;
            break;

        case SIM_DWG_STATE_LEN:
            m->parse_len = byte;
            m->parse_checksum += byte;
            m->parse_pos = 0;
            if (byte == 0)
                m->parse_state = SIM_DWG_STATE_CHECKSUM_LO;
            else if (byte > SIM_DWG_MAX_PAYLOAD)
            {
                m->parse_errors++;
                m->parse_state = SIM_DWG_STATE_WAIT_SOF;
            }
            else
                m->parse_state = SIM_DWG_STATE_PAYLOAD;
            break;

        case SIM_DWG_STATE_PAYLOAD:
            m->parse_buf[m->parse_pos++] = byte;
            m->parse_checksum += byte;
            if (m->parse_pos >= m->parse_len)
                m->parse_state = SIM_DWG_STATE_CHECKSUM_LO;
            break;

        case SIM_DWG_STATE_CHECKSUM_LO:
            m->parse_checksum_rx = byte;
            m->parse_state = SIM_DWG_STATE_CHECKSUM_HI;
            break;

        case SIM_DWG_STATE_CHECKSUM_HI:
            m->parse_checksum_rx |= (uint16_t)byte << 8;
            m->parse_state = SIM_DWG_STATE_CR;
            break;

        case SIM_DWG_STATE_CR:
            if (byte == SHENGYI_FRAME_CR)
                m->parse_state = SIM_DWG_STATE_LF;
            else
            {
                m->parse_errors++;
                m->parse_state = SIM_DWG_STATE_WAIT_SOF;
            }
            break;

        case SIM_DWG_STATE_LF:
            if (byte == SHENGYI_FRAME_LF && m->parse_checksum == m->parse_checksum_rx)
            {
                /* Valid frame - queue it */
                uint8_t next_wr = (m->rx_wr_idx + 1u) % SIM_DWG_RX_SLOTS;
                if (next_wr != m->rx_rd_idx)
                {
                    sim_dwg_frame_t *slot = &m->rx_frames[m->rx_wr_idx];
                    slot->cmd = m->parse_cmd;
                    slot->len = m->parse_len;
                    memcpy(slot->payload, m->parse_buf, m->parse_len);
                    slot->valid = 1;
                    m->rx_wr_idx = next_wr;
                    m->frames_rx++;
                }
            }
            else
            {
                m->parse_errors++;
            }
            m->parse_state = SIM_DWG_STATE_WAIT_SOF;
            break;

        default:
            m->parse_state = SIM_DWG_STATE_WAIT_SOF;
            break;
    }
}

/* Build 0x52 motor status response (5 bytes)
 *
 * Field mapping derived from IDA analysis of APP_process_motor_response_packet
 * and sub_8024768:
 *
 *   [0]: bits 0-5 = battery_voltage (0-63V raw)
 *        bit 6    = motor_enabled
 *        bit 7    = status_flag (error indicator)
 *   [1]: battery_current_raw
 *        actual_amps = raw * 3.0 / 10.0
 *        Formula: raw = (actual_amps * 10.0) / 3.0
 *   [2]: speed_raw_hi (16-bit BE)
 *   [3]: speed_raw_lo
 *        Speed calculation (in display firmware):
 *          if (speed_raw < 3500 && speed_raw > 0):
 *            speed_kph = (3.6 * wheel_circumference_mm) / speed_raw
 *        Inverse (motor builds raw from speed):
 *          speed_raw = (3.6 * wheel_circumference_mm) / speed_kph
 *   [4]: error_code (0=none, 33-41=error types)
 */
static void send_motor_status(sim_dwg_motor_t *m)
{
    uint8_t payload[5];

    /* Byte 0: Battery voltage (bits 0-5) + status flags (bits 6-7) */
    int batt_v = (int)(m->bike.batt_v);
    if (batt_v > 63)
        batt_v = 63;
    if (batt_v < 0)
        batt_v = 0;
    payload[0] = (uint8_t)(batt_v & 0x3F);
    if (m->motor_enabled)
        payload[0] |= 0x40;  /* bit 6 = motor_enabled */
    if (m->error_code != 0)
        payload[0] |= 0x80;  /* bit 7 = error indicator */

    /* Byte 1: Battery current raw
     * Formula: raw = (actual_amps * 10.0) / 3.0 */
    uint32_t batt_mA = (uint32_t)fmax(0.0, m->bike.batt_a * 1000.0);
    payload[1] = sim_shengyi_current_raw_from_mA(batt_mA);

    /* Bytes 2-3: Speed raw (16-bit BE)
     * Formula: speed_raw = (3.6 * wheel_circumference_mm) / speed_kph
     * Display reverses: speed_kph = (3.6 * wheel_circumference_mm) / speed_raw */
    double wheel_mm = m->config.wheel_circumference_mm;
    if (wheel_mm < 1.0)
    {
        uint16_t fallback = sim_shengyi_wheel_circumference_mm_from_code(5);
        wheel_mm = (fallback > 0) ? (double)fallback : 2074.0;
    }

    double speed_kph_x10 = fmax(0.0, (m->bike.v_mps * 3.6 * 10.0));
    uint16_t speed_raw = sim_shengyi_speed_raw_from_kph_x10((uint16_t)(speed_kph_x10 + 0.5),
                                                            (uint16_t)(wheel_mm + 0.5));
    store_be16(&payload[2], speed_raw);

    /* Byte 4: Error code */
    payload[4] = m->error_code;

    send_frame(SIM_DWG_CMD_MOTOR_STATUS, payload, 5);
    m->frames_tx++;
}

/* Build 0xC3 full status response (47 bytes)
 *
 * Field mapping derived from IDA analysis of sub_80249A4:
 *   [0]:  assist_level (1-5)
 *   [1]:  auto_poweroff_minutes
 *   [2]:  batt_nominal_V (24/36/48)
 *   [3]:  config_profile_id (3/5/9)
 *   [4]:  lights_config
 *   [5]:  max_assist_level
 *   [6]:  gear_setting
 *   [7]:  motor_enable_config
 *   [8]:  brake_config
 *   [9]:  speed_mode
 *   [10]: display_setting
 *   [11-12]: batt_threshold (BE)
 *   [13]: current_limit_A
 *   [14]: speed_limit_kph
 *   [15]: wheel_size_code (0-7)
 *   [16]: timeout_param
 *   [17]: motor_timeout_s
 *   [18]: assist_sensitivity
 *   [19]: units_mode
 *   [20]: display_flag
 *   [21-22]: wheel_circumference_mm (BE)
 *   [23]: pas_config_0
 *   [24]: pas_config_1
 *   [25]: torque_config
 *   [26]: cadence_config
 *   [27]: power_config
 *   [28-30]: assist_curves[3]
 *   [31]: motor_config
 *   [32-33]: motor_current_mA (BE)
 *   [34-35]: motor_power_W (BE)
 *   [36]: constant_1 (always 1)
 *   [37]: power_display_mode
 *   [38-39]: trip_distance (BE)
 *   [40-41]: total_distance (BE)
 *   [42-43]: speed_config (BE)
 *   [44]: brightness
 *   [45]: contrast
 *   [46]: theme
 */
static void send_status_c3(sim_dwg_motor_t *m)
{
    uint8_t payload[47];
    memset(payload, 0, sizeof(payload));

    /* Basic config */
    payload[0] = m->bike.assist_level;              /* assist_level (1-5) */
    payload[1] = m->config.auto_poweroff_min;       /* auto_poweroff_minutes */
    payload[2] = m->config.batt_nominal_V;          /* batt_nominal_V (24/36/48) */
    payload[3] = m->config.config_profile_id;       /* config_profile_id (3/5/9) */
    payload[4] = m->config.lights_enabled;          /* lights_config */
    payload[5] = m->config.max_assist_level;        /* max_assist_level */
    payload[6] = m->config.gear_setting;            /* gear_setting */
    payload[7] = m->config.motor_enable;            /* motor_enable_config */
    payload[8] = m->config.brake_flag;              /* brake_config */
    payload[9] = m->config.speed_mode;              /* speed_mode */
    payload[10] = m->config.display_setting;        /* display_setting */

    /* Battery threshold (BE) */
    uint16_t batt_thresh = m->config.batt_threshold_mV / 100;
    store_be16(&payload[11], batt_thresh);

    /* Current limit (A) */
    payload[13] = (uint8_t)(m->config.batt_current_limit_mA / 1000);

    /* Speed limit (km/h) */
    payload[14] = (uint8_t)(m->config.speed_limit_kph_x10 / 10);

    payload[15] = m->config.wheel_size_code;        /* wheel_size_code (0-7) */
    payload[16] = 30;                               /* timeout_param */
    payload[17] = m->config.motor_timeout_s;        /* motor_timeout_s */
    payload[18] = 5;                                /* assist_sensitivity */
    payload[19] = m->config.units_mode;             /* units_mode */
    payload[20] = 0;                                /* display_flag */

    /* Wheel circumference (BE) */
    store_be16(&payload[21], m->config.wheel_circumference_mm);

    /* PAS and assist configuration */
    payload[23] = 0;   /* pas_config_0 */
    payload[24] = 50;  /* pas_config_1 */
    payload[25] = 50;  /* torque_config */
    payload[26] = 10;  /* cadence_config */
    payload[27] = 20;  /* power_config */
    payload[28] = 30;  /* assist_curves[0] */
    payload[29] = 40;  /* assist_curves[1] */
    payload[30] = 50;  /* assist_curves[2] */
    payload[31] = 0;   /* motor_config */

    /* Motor current mA (BE) - live telemetry */
    uint16_t motor_mA = (uint16_t)(m->bike.batt_a * 1000.0);
    store_be16(&payload[32], motor_mA);

    /* Motor power W (BE) - live telemetry */
    uint16_t motor_W = (uint16_t)(m->bike.motor_power_w);
    store_be16(&payload[34], motor_W);

    payload[36] = 1;   /* constant_1 (always 1) */
    payload[37] = 0;   /* power_display_mode */

    /* Trip distance (BE) */
    payload[38] = 0;
    payload[39] = 0;

    /* Total distance (BE) */
    payload[40] = 0;
    payload[41] = 0;

    /* Speed config (BE) */
    payload[42] = 0;
    payload[43] = 0;

    payload[44] = m->config.screen_brightness;  /* brightness */
    payload[45] = 50;                           /* contrast */
    payload[46] = 0;                            /* theme */

    send_frame(SIM_DWG_CMD_STATUS_RESP, payload, 47);
    m->frames_tx++;
}

/* Handle 0x52 request from display (2 bytes)
 *
 * Field mapping derived from IDA analysis of APP_BLE_command_dispatcher
 * and APP_process_motor_control_flags:
 *
 *   [0]: assist_level (mapped value 0-15)
 *   [1]: control_flags
 *        bit 7: headlight_on
 *        bit 5: battery_low
 *        bit 4: walk_assist_active
 *        bit 0: speed_over_limit
 */
static void handle_0x52_request(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    if (len >= 2)
    {
        m->assist_level_mapped = payload[0];
        m->control_flags = payload[1];

        /* Parse control flags */
        m->headlight_on = (payload[1] >> 7) & 1;
        m->battery_low = (payload[1] >> 5) & 1;
        m->walk_assist_active = (payload[1] >> 4) & 1;
        m->speed_over_limit = payload[1] & 1;

        /* Update bike assist level */
        m->bike.assist_level = m->assist_level_mapped;
    }

    /* Send motor status response */
    send_motor_status(m);
}

/* Handle 0x53 control params from display */
static void handle_0x53_control(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    if (len < 7)
        return;

    m->config.max_assist_level = payload[0] & 0x3F;
    m->config.lights_enabled = (payload[0] & 0x40) ? 0 : 1;  /* Inverted logic */
    m->config.gear_setting = payload[1];
    m->config.display_setting = payload[2] & 0x0F;
    m->config.speed_mode = (payload[2] >> 4) & 0x03;
    m->config.brake_flag = (payload[2] >> 6) & 1;
    m->config.motor_enable = (payload[2] >> 7) & 1;

    /* Bytes 3-4: Battery threshold (encoded) */
    /* Byte 5: Reserved */
    /* Byte 6: Wheel size code + speed limit */
    m->config.wheel_size_code = payload[6] & 0x07;
    if (m->config.wheel_size_code < 8)
        m->config.wheel_circumference_mm = sim_shengyi_wheel_circumference_mm_from_code(m->config.wheel_size_code);
    m->config.speed_limit_kph_x10 = (uint16_t)(((payload[6] >> 3) & 0x1F) + 10) * 10;

    m->motor_enabled = m->config.motor_enable;

    /* No response needed - motor just processes */
}

/* Handle 0xC0 full config write (52 bytes)
 *
 * Field mapping derived from IDA analysis of APP_BLE_command_dispatcher
 * at case n169 == 192 (0xC0):
 *
 *   [0]:  assist_level (1-5)
 *   [1]:  auto_poweroff_minutes (0-10)
 *   [2]:  year_offset (0-99, actual_year = 2000 + offset)
 *   [3]:  month (1-12)
 *   [4]:  day (1-31)
 *   [5]:  hour (0-23)
 *   [6]:  minute (0-59)
 *   [7]:  batt_nominal_V (24, 36, or 48)
 *   [8]:  config_profile_id (3, 5, or 9)
 *   [9]:  lights_config
 *   [10]: max_assist_level (2-64)
 *   [11]: gear_setting
 *   [12]: motor_enable_config
 *   [13]: brake_config
 *   [14]: speed_mode (0-3)
 *   [15]: display_setting (0-15)
 *   [16-17]: batt_threshold (BE, mV/100)
 *   [18]: current_limit_A (actual_mA = value * 1000)
 *   [19]: speed_limit_kph (10-51, actual_x10 = value * 10)
 *   [20]: wheel_size_code (0-7, see wheel size table)
 *   [21]: timeout_param (1-60)
 *   [22]: motor_timeout_s (>=5, actual_ms = value * 1000)
 *   [23]: assist_sensitivity (0-10)
 *   [24]: units_mode (0=metric, 1=imperial)
 *   [25]: display_flag
 *   [26-27]: wheel_circumference_mm (BE)
 *   [28-51]: additional config parameters
 */
static void handle_0xC0_config(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    if (len < 52)
    {
        send_ack(0);
        return;
    }

    /* Parse all config fields from payload */
    m->bike.assist_level = payload[0];              /* assist_level (1-5) */
    m->config.auto_poweroff_min = payload[1];       /* auto_poweroff_minutes */
    /* payload[2-6]: datetime - ignored in simulator */
    m->config.batt_nominal_V = payload[7];          /* batt_nominal_V (24/36/48) */
    m->config.config_profile_id = payload[8];       /* config_profile_id (3/5/9) */
    m->config.lights_enabled = payload[9];          /* lights_config */
    m->config.max_assist_level = payload[10];       /* max_assist_level (2-64) */
    m->config.gear_setting = payload[11];           /* gear_setting */
    m->config.motor_enable = payload[12];           /* motor_enable_config */
    m->config.brake_flag = payload[13];             /* brake_config */
    m->config.speed_mode = payload[14];             /* speed_mode (0-3) */
    m->config.display_setting = payload[15];        /* display_setting (0-15) */

    /* Battery threshold (BE, in mV/100) */
    m->config.batt_threshold_mV = load_be16(&payload[16]);

    /* Current and speed limits */
    m->config.batt_current_limit_mA = (uint16_t)payload[18] * 1000;
    m->config.speed_limit_kph_x10 = (uint16_t)payload[19] * 10;

    /* Wheel size */
    m->config.wheel_size_code = payload[20];
    if (m->config.wheel_size_code < 8)
        m->config.wheel_circumference_mm = sim_shengyi_wheel_circumference_mm_from_code(m->config.wheel_size_code);

    /* Timeout and mode settings */
    /* payload[21]: timeout_param */
    m->config.motor_timeout_s = payload[22];        /* motor_timeout_s */
    /* payload[23]: assist_sensitivity */
    m->config.units_mode = payload[24];             /* units_mode */
    /* payload[25]: display_flag */

    /* Wheel circumference override (BE) */
    if (len >= 28)
    {
        uint16_t circ = load_be16(&payload[26]);
        if (circ > 0)
            m->config.wheel_circumference_mm = circ;
    }

    /* Screen brightness from end of packet */
    if (len >= 50)
        m->config.screen_brightness = payload[49];

    m->motor_enabled = m->config.motor_enable;

    send_ack(1);
}

/* Handle 0xC2 status request */
static void handle_0xC2_request(sim_dwg_motor_t *m)
{
    send_status_c3(m);
}

/* Handle 0xA6 flash read request */
static void handle_0xA6_request(sim_dwg_motor_t *m)
{
    uint8_t payload[65];
    payload[0] = 64;  /* data length */
    /* Return all zeros for flash data */
    memset(&payload[1], 0, 64);
    send_frame(SIM_DWG_CMD_FLASH_READ, payload, 65);
    m->frames_tx++;
}

/* Handle 0xA7 flash write (4 bytes) */
static void handle_0xA7_write(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    if (len >= 6)
    {
        uint8_t slot = payload[0];
        if (slot < 17)
        {
            memcpy(m->flash_slots[slot], &payload[1], 4);
            m->flash_slot_lens[slot] = 4;
        }
        /* payload[5] = reinit_ble flag - ignored in sim */
    }
    send_ack(1);
}

/* Handle 0xA8 flash write (variable) */
static void handle_0xA8_write(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    if (len >= 2)
    {
        uint8_t slot = payload[0];
        uint8_t data_len = payload[1];
        if (slot < 17 && data_len <= 64 && len >= (size_t)(2 + data_len))
        {
            memcpy(m->flash_slots[slot], &payload[2], data_len);
            m->flash_slot_lens[slot] = data_len;
        }
    }

    /* Response: slot + success */
    uint8_t resp[2] = {payload[0], 1};
    send_frame(SIM_DWG_CMD_FLASH_WRITEN, resp, 2);
    m->frames_tx++;
}

/* Handle 0xA9 config slot read */
static void handle_0xA9_read(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    if (len < 1)
        return;

    uint8_t slot = payload[0];
    uint8_t resp[66];
    resp[0] = slot;

    if (slot < 17)
    {
        resp[1] = m->flash_slot_lens[slot];
        memcpy(&resp[2], m->flash_slots[slot], m->flash_slot_lens[slot]);
        send_frame(SIM_DWG_CMD_CFG_READ, resp, (uint8_t)(2 + m->flash_slot_lens[slot]));
    }
    else
    {
        resp[1] = 0;
        send_frame(SIM_DWG_CMD_CFG_READ, resp, 2);
    }
    m->frames_tx++;
}

/* Handle 0xAA display mode */
static void handle_0xAA_mode(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    if (len >= 1)
    {
        m->bike.assist_level = payload[0] & 0x0F;
    }
    /* No response */
}

/* Handle 0xAB protocol mode */
static void handle_0xAB_proto(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    (void)m;
    (void)payload;
    (void)len;
    /* Protocol mode selection - just acknowledge */
    send_ack(1);
}

/* Handle 0xAC calibration request */
static void handle_0xAC_calibrate(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    (void)m;
    (void)payload;
    (void)len;
    /* Calibration - just acknowledge */
    send_ack(1);
}

/* Handle 0xB0 telemetry */
static void handle_0xB0_telemetry(sim_dwg_motor_t *m, const uint8_t *payload, uint8_t len)
{
    (void)m;
    (void)payload;
    (void)len;
    /* Telemetry frame - no response needed */
}

/* Process a single frame */
static void process_frame(sim_dwg_motor_t *m, sim_dwg_frame_t *frame)
{
    switch (frame->cmd)
    {
        case SIM_DWG_CMD_MOTOR_STATUS:
            /* 0x52 with 2-byte payload = request from display */
            if (frame->len == 2)
                handle_0x52_request(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_CONTROL:
            handle_0x53_control(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_FLASH_READ:
            handle_0xA6_request(m);
            break;

        case SIM_DWG_CMD_FLASH_WRITE4:
            handle_0xA7_write(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_FLASH_WRITEN:
            handle_0xA8_write(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_CFG_READ:
            handle_0xA9_read(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_DISPLAY_MODE:
            handle_0xAA_mode(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_PROTO_MODE:
            handle_0xAB_proto(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_CALIBRATE:
            handle_0xAC_calibrate(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_TELEMETRY:
            handle_0xB0_telemetry(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_CONFIG_WRITE:
            handle_0xC0_config(m, frame->payload, frame->len);
            break;

        case SIM_DWG_CMD_STATUS_REQ:
            handle_0xC2_request(m);
            break;

        default:
            /* Unknown command - ignore */
            break;
    }
}

/* Process all pending frames and generate responses */
void sim_dwg_motor_process(sim_dwg_motor_t *m)
{
    if (!m)
        return;

    while (m->rx_rd_idx != m->rx_wr_idx)
    {
        sim_dwg_frame_t *frame = &m->rx_frames[m->rx_rd_idx];
        if (frame->valid)
        {
            process_frame(m, frame);
            frame->valid = 0;
        }
        m->rx_rd_idx = (m->rx_rd_idx + 1u) % SIM_DWG_RX_SLOTS;
    }

    /* Send periodic status if needed */
    if (m->send_status_0x52)
    {
        send_motor_status(m);
        m->send_status_0x52 = 0;
    }
    if (m->send_status_0xC3)
    {
        send_status_c3(m);
        m->send_status_0xC3 = 0;
    }
}

/* Advance simulation time and physics */
void sim_dwg_motor_tick(sim_dwg_motor_t *m, uint32_t dt_ms)
{
    if (!m)
        return;

    m->t_ms += dt_ms;

    /* Step physics simulation */
    sim_shengyi_step(&m->bike, dt_ms);

    /* Send periodic motor status every status_period_ms */
    if (m->status_period_ms > 0 &&
        (m->t_ms - m->last_status_ms) >= m->status_period_ms)
    {
        m->last_status_ms = m->t_ms;
        /* Queue a status response for next process */
        m->send_status_0x52 = 1;
    }
}

/* Set rider input (pedaling) */
void sim_dwg_motor_set_rider_power(sim_dwg_motor_t *m, double power_w)
{
    if (m)
        m->bike.rider_power_w = power_w;
}

/* Set environmental conditions */
void sim_dwg_motor_set_grade(sim_dwg_motor_t *m, double grade)
{
    if (m)
        m->bike.grade = grade;
}

void sim_dwg_motor_set_wind(sim_dwg_motor_t *m, double wind_mps)
{
    if (m)
        m->bike.wind_mps = wind_mps;
}

/* Set error condition */
void sim_dwg_motor_set_error(sim_dwg_motor_t *m, uint8_t error_code)
{
    if (m)
    {
        m->error_code = error_code;
        m->bike.err = error_code;
    }
}

/* Get current telemetry for UI */
uint16_t sim_dwg_motor_speed_dmph(const sim_dwg_motor_t *m)
{
    return m ? sim_shengyi_speed_dmph(&m->bike) : 0;
}

uint16_t sim_dwg_motor_cadence_rpm(const sim_dwg_motor_t *m)
{
    return m ? sim_shengyi_cadence_rpm(&m->bike) : 0;
}

uint16_t sim_dwg_motor_power_w(const sim_dwg_motor_t *m)
{
    return m ? sim_shengyi_power_w(&m->bike) : 0;
}

int16_t sim_dwg_motor_batt_dV(const sim_dwg_motor_t *m)
{
    return m ? sim_shengyi_batt_dV(&m->bike) : 0;
}

int16_t sim_dwg_motor_batt_dA(const sim_dwg_motor_t *m)
{
    return m ? sim_shengyi_batt_dA(&m->bike) : 0;
}

int16_t sim_dwg_motor_temp_dC(const sim_dwg_motor_t *m)
{
    return m ? (int16_t)(m->bike.temp_c * 10.0) : 0;
}

uint8_t sim_dwg_motor_soc_pct(const sim_dwg_motor_t *m)
{
    return m ? m->bike.soc_pct : 0;
}

uint8_t sim_dwg_motor_error_code(const sim_dwg_motor_t *m)
{
    return m ? m->error_code : 0;
}

/* Build frames for testing (display -> motor) */
size_t sim_dwg_build_0x52_request(uint8_t assist_level, uint8_t flags,
                                  uint8_t *out, size_t cap)
{
    uint8_t payload[2];
    payload[0] = assist_level;
    payload[1] = flags;
    return shengyi_frame_build(SIM_DWG_CMD_MOTOR_STATUS, payload, 2u, out, cap);
}

size_t sim_dwg_build_0xC2_request(uint8_t *out, size_t cap)
{
    return shengyi_frame_build(SIM_DWG_CMD_STATUS_REQ, NULL, 0u, out, cap);
}
