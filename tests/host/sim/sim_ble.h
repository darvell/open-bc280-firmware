#ifndef SIM_BLE_H
#define SIM_BLE_H

#include <stddef.h>
#include <stdint.h>

#include "comm_proto.h"
#include "sim_uart.h"

/* ============================================================================
 * BLE Simulator
 *
 * sim_ble represents the EXTERNAL TTM BLE chip + BLE mobile app. It is NOT
 * the display firmware - it GENERATES stimuli that the display reacts to.
 *
 * Architecture:
 *   sim_ble (TTM chip) ---> UART1 RX ---> Display firmware (processes)
 *   Display firmware ---> UART1 TX ---> sim_ble (can verify responses)
 *
 * ============================================================================
 * TTM BLE Module Layer
 *
 * The TTM module is a hardware BLE chip connected via UART1. It uses a
 * text-based AT-style protocol for module control and status, while the
 * actual BLE data uses the 0x55-framed binary protocol.
 *
 * TTM Protocol Messages (sent BY TTM chip TO display via UART1 RX):
 *   "TTM:MAC-XX:XX:XX:XX:XX:XX\n" - MAC address response
 *   "TTM:CONNECTED\n"  - BLE client connected
 *   "TTM:DISCONNECT\n" - BLE client disconnected
 *
 * TTM Queries (sent BY display TO TTM chip via UART1 TX):
 *   "TTM:MAC-?\r\n" - Query module MAC address
 *
 * When connected, the TTM module passes through 0x55-framed binary data
 * bidirectionally between the BLE client app and the display firmware.
 * ============================================================================
 */

/* TTM connection states */
typedef enum {
    SIM_TTM_STATE_IDLE = 0,       /* Module powered up, not connected */
    SIM_TTM_STATE_ADVERTISING,    /* Advertising, waiting for connection */
    SIM_TTM_STATE_CONNECTED,      /* BLE client connected */
    SIM_TTM_STATE_DISCONNECTING   /* Disconnection in progress */
} sim_ttm_state_t;

/* TTM text parser states */
typedef enum {
    SIM_TTM_PARSE_IDLE = 0,
    SIM_TTM_PARSE_TEXT           /* Accumulating TTM text message */
} sim_ttm_parse_state_t;

#define SIM_TTM_MAC_LEN         6u
#define SIM_TTM_MAC_STR_LEN     18u  /* "XX:XX:XX:XX:XX:XX\0" */
#define SIM_TTM_TEXT_BUF_LEN    64u

/* TTM BLE Module state */
typedef struct {
    /* Connection state */
    sim_ttm_state_t state;
    uint32_t state_timer_ms;

    /* MAC address */
    uint8_t mac[SIM_TTM_MAC_LEN];
    char mac_str[SIM_TTM_MAC_STR_LEN];
    uint8_t mac_valid;

    /* Text message parser */
    sim_ttm_parse_state_t parse_state;
    char text_buf[SIM_TTM_TEXT_BUF_LEN];
    uint8_t text_pos;

    /* Timing for simulated events */
    uint32_t connect_delay_ms;     /* Delay before auto-connect (0=disabled) */
    uint32_t disconnect_after_ms;  /* Auto-disconnect after N ms (0=never) */

    /* Statistics */
    uint32_t connections;
    uint32_t disconnections;
    uint32_t mac_queries;
} sim_ttm_t;

/* ============================================================================
 * BLE Protocol Layer (0x55-framed binary)
 * ============================================================================
 */

#define SIM_BLE_MAX_PAYLOAD     146u
#define SIM_BLE_MAX_FRAME       150u
#define SIM_BLE_RX_SLOTS        7u

/* BLE Command codes - Application mode */
#define SIM_BLE_CMD_ERROR           0x00u
#define SIM_BLE_CMD_AUTH            0x02u
#define SIM_BLE_CMD_VERSION         0x04u
#define SIM_BLE_CMD_SET_TIME        0x06u
#define SIM_BLE_CMD_UPDATE_CACHE    0x09u
#define SIM_BLE_CMD_GET_BATT_DIST   0x0Au
#define SIM_BLE_CMD_ENTER_BOOTLOADER 0x20u
#define SIM_BLE_CMD_GET_PARAMS      0x30u
#define SIM_BLE_CMD_SET_CONFIG      0x32u
#define SIM_BLE_CMD_GET_GROUP       0x37u
#define SIM_BLE_CMD_GET_REALTIME    0x60u
#define SIM_BLE_CMD_GET_HISTORY     0x63u
#define SIM_BLE_CMD_GET_HISTORY_NEXT 0x65u
#define SIM_BLE_CMD_GET_MOTOR       0x67u
#define SIM_BLE_CMD_GET_MOTOR_NEXT  0x69u
#define SIM_BLE_CMD_GET_BATT_STATS  0xF0u

/* Config types for CMD_SET_CONFIG (0x32) */
#define SIM_BLE_CFG_HEADLIGHT       1u
#define SIM_BLE_CFG_DISPLAY_MODE    2u
#define SIM_BLE_CFG_SPEED_LIMIT     3u
#define SIM_BLE_CFG_UNITS           4u
#define SIM_BLE_CFG_ASSIST          5u
#define SIM_BLE_CFG_BRIGHTNESS      7u

/* Button masks for internal representation */
#define SIM_BLE_BTN_GEAR_UP         0x10u
#define SIM_BLE_BTN_GEAR_DOWN       0x20u
#define SIM_BLE_BTN_WALK            0x40u
#define SIM_BLE_BTN_CRUISE          0x80u

/* Frame slot for parsed frames */
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[SIM_BLE_MAX_PAYLOAD];
    uint8_t valid;
} sim_ble_frame_t;

/* Odometer and trip data */
typedef struct {
    uint32_t distance_m;
    uint32_t moving_time_s;
    uint16_t max_speed_dmph;
    uint16_t avg_speed_dmph;
    uint32_t energy_wh;
} sim_ble_trip_t;

/* BLE Simulator state - emulates display's BLE processor AND TTM module */
typedef struct {
    /* ========== TTM BLE Module Layer ========== */
    sim_ttm_t ttm;

    /* ========== 0x55 Protocol Parser ========== */
    uint8_t parse_frame[SIM_BLE_MAX_FRAME];
    uint8_t parse_len;
    /* RX frame queue */
    sim_ble_frame_t rx_frames[SIM_BLE_RX_SLOTS];
    uint8_t rx_rd_idx;
    uint8_t rx_wr_idx;

    /* Authentication state */
    uint8_t authenticated;
    uint8_t auth_table[768];  /* 3 keys x 256 entries */

    /* Display state */
    uint8_t headlight_enabled;
    uint8_t screen_brightness;
    uint8_t auto_poweroff_min;
    uint8_t speed_limit_kph;
    uint8_t units_mode;  /* 0=metric, 1=imperial */
    uint8_t assist_level;  /* 0-4 */

    /* Current telemetry (from Shengyi) */
    uint16_t speed_dmph;
    uint16_t cadence_rpm;
    uint16_t power_w;
    int16_t batt_dV;  /* decivolts */
    int16_t batt_dA;  /* deciamps */
    int16_t motor_temp_dC;  /* decidegrees C */
    uint8_t soc_pct;
    uint8_t error_code;

    /* Odometer and trips */
    sim_ble_trip_t odometer;
    sim_ble_trip_t trip_a;
    sim_ble_trip_t trip_b;

    /* Firmware version */
    uint8_t fw_version[7];

    /* Timing */
    uint32_t t_ms;
    uint32_t last_telemetry_ms;
    uint16_t telemetry_period_ms;

    /* Statistics */
    uint32_t frames_rx;
    uint32_t frames_tx;
    uint32_t parse_errors;
} sim_ble_t;

/* Initialize BLE simulator */
void sim_ble_init(sim_ble_t *ble);

/* Feed a byte from UART1 RX into the parser */
void sim_ble_feed_byte(sim_ble_t *ble, uint8_t byte);

/* Process all pending frames and generate responses */
void sim_ble_process(sim_ble_t *ble);

/* Update telemetry data (call with motor controller data) */
void sim_ble_update_telemetry(sim_ble_t *ble,
                               uint16_t speed_dmph,
                               uint16_t cadence_rpm,
                               uint16_t power_w,
                               int16_t batt_dV,
                               int16_t batt_dA,
                               int16_t motor_temp_dC,
                               uint8_t soc_pct,
                               uint8_t error_code);

/* Update trip data */
void sim_ble_update_trips(sim_ble_t *ble, uint32_t dt_ms);

/* Advance time */
void sim_ble_tick(sim_ble_t *ble, uint32_t dt_ms);

/* Send a command frame to the display (simulates BLE app sending command) */
size_t sim_ble_build_command(uint8_t cmd, const uint8_t *payload, uint8_t len,
                             uint8_t *out, size_t cap);

/* Build specific command frames */
size_t sim_ble_build_ping(uint8_t *out, size_t cap);
size_t sim_ble_build_get_version(uint8_t *out, size_t cap);
size_t sim_ble_build_set_time(uint16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second,
                              uint8_t *out, size_t cap);
size_t sim_ble_build_get_realtime(uint8_t *out, size_t cap);
size_t sim_ble_build_get_params(uint8_t *out, size_t cap);
size_t sim_ble_build_get_group(uint8_t group, uint8_t *out, size_t cap);
size_t sim_ble_build_set_config(uint8_t cfg_type, uint8_t value,
                                uint8_t *out, size_t cap);
size_t sim_ble_build_get_batt_stats(uint8_t *out, size_t cap);

/* ============================================================================
 * TTM Module API
 * ============================================================================
 */

/* Initialize TTM module with MAC address */
void sim_ttm_init(sim_ttm_t *ttm, const uint8_t mac[6]);

/* Feed a byte from UART1 into TTM parser (handles both TTM text and 0x55) */
void sim_ttm_feed_byte(sim_ble_t *ble, uint8_t byte);

/* Process TTM state machine (connection events, timers) */
void sim_ttm_tick(sim_ble_t *ble, uint32_t dt_ms);

/* Trigger connection from BLE app */
void sim_ttm_connect(sim_ble_t *ble);

/* Trigger disconnection */
void sim_ttm_disconnect(sim_ble_t *ble);

/* Set auto-connect delay (0 = disabled, >0 = connect after N ms) */
void sim_ttm_set_auto_connect(sim_ble_t *ble, uint32_t delay_ms);

/* Check if TTM module is connected */
int sim_ttm_is_connected(const sim_ble_t *ble);

/* Get TTM MAC address string */
const char *sim_ttm_get_mac_str(const sim_ble_t *ble);

/* Check display's TX for TTM queries (call periodically) */
void sim_ttm_check_display_tx(sim_ble_t *ble);

#endif /* SIM_BLE_H */
