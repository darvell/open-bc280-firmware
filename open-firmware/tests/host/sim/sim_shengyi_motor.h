#ifndef SIM_SHENGYI_MOTOR_H
#define SIM_SHENGYI_MOTOR_H

#include <stddef.h>
#include <stdint.h>

#include "shengyi.h"
#include "sim_uart.h"
#include "sim_shengyi.h"
#include "sim_shengyi_bus.h"

/* ============================================================================
 * Shengyi DWG22 Hub Motor Controller Protocol
 *
 * Frame format: [SOF][ID][CMD][LEN][PAYLOAD...][CKS_LO][CKS_HI][CR][LF]
 *   SOF: 0x3A (start of frame)
 *   ID:  0x1A (frame identifier)
 *   CMD: Command code
 *   LEN: Payload length (0-142)
 *   CKS: 16-bit LE checksum = sum of bytes[1..len-4]
 *   CR:  0x0D
 *   LF:  0x0A
 *
 * Communication:
 *   Display --> Motor: UART2 TX (commands/config)
 *   Motor --> Display: UART2 RX (responses/telemetry)
 * ============================================================================
 */

/* Protocol constants */
#define SIM_DWG_SOF              SHENGYI_FRAME_START
#define SIM_DWG_FRAME_ID         SHENGYI_FRAME_SECOND
#define SIM_DWG_MAX_PAYLOAD      142u
#define SIM_DWG_MAX_FRAME        150u
#define SIM_DWG_RX_SLOTS         5u

/* ============================================================================
 * Command Codes
 * ============================================================================
 */

/* 0x52 - Motor Status (bidirectional, most frequent message) */
#define SIM_DWG_CMD_MOTOR_STATUS 0x52u

/* ============================================================================
 * 0x52 Motor Status Request (Display -> Motor, 2 bytes)
 * Sent every ~100ms to request motor telemetry and send control inputs.
 * ============================================================================
 */
typedef struct __attribute__((packed)) {
    uint8_t  assist_level;      /* [0] Mapped assist level (0=off, 1-15=strength) */
    uint8_t  control_flags;     /* [1] Control flags bitfield:
                                 *     bit 7: headlight_on
                                 *     bit 6: lights_enabled
                                 *     bit 5: walk_assist_active
                                 *     bit 4: cruise_control
                                 *     bit 3: brake_active
                                 *     bit 2: speed_limit_exceeded
                                 *     bit 1: walk_mode
                                 *     bit 0: motor_running */
} sim_dwg_request_52_t;

/* Control flag bit masks */
#define SIM_DWG_CTRL_HEADLIGHT       0x80u
#define SIM_DWG_CTRL_LIGHTS_ENABLED  0x40u
#define SIM_DWG_CTRL_WALK_ASSIST     0x20u
#define SIM_DWG_CTRL_CRUISE          0x10u
#define SIM_DWG_CTRL_BRAKE           0x08u
#define SIM_DWG_CTRL_SPEED_LIMIT     0x04u
#define SIM_DWG_CTRL_WALK_MODE       0x02u
#define SIM_DWG_CTRL_MOTOR_RUNNING   0x01u

_Static_assert(sizeof(sim_dwg_request_52_t) == 2, "0x52 request must be 2 bytes");

/* ============================================================================
 * 0x52 Motor Status Response (Motor -> Display, 5 bytes)
 * Real-time telemetry from motor controller.
 *
 * Speed encoding:
 *   Motor sends:    speed_raw = (3.6 * wheel_circumference_mm) / speed_kph
 *   Display decodes: speed_kph = (3.6 * wheel_circumference_mm) / speed_raw
 *   Valid range: 1-3500 (0 = stopped)
 *
 * Current encoding:
 *   Motor sends:    current_raw = (actual_amps * 10.0) / 3.0
 *   Display decodes: actual_amps = current_raw * 3.0 / 10.0
 * ============================================================================
 */
typedef struct __attribute__((packed)) {
    uint8_t  status_voltage;    /* [0] bits 0-5: battery_voltage (0-63V)
                                 *     bit 6: motor_enabled
                                 *     bit 7: error_present */
    uint8_t  current_raw;       /* [1] Battery current (see encoding above) */
    uint8_t  speed_raw_hi;      /* [2] Speed raw value (BE high byte) */
    uint8_t  speed_raw_lo;      /* [3] Speed raw value (BE low byte) */
    uint8_t  error_code;        /* [4] Error code (0=none, see E-codes) */
} sim_dwg_response_52_t;

/* Status/voltage byte bit masks */
#define SIM_DWG_STAT_VOLTAGE_MASK    0x3Fu  /* bits 0-5 */
#define SIM_DWG_STAT_MOTOR_ENABLED   0x40u  /* bit 6 */
#define SIM_DWG_STAT_ERROR_PRESENT   0x80u  /* bit 7 */

_Static_assert(sizeof(sim_dwg_response_52_t) == 5, "0x52 response must be 5 bytes");

/* 0x53 - Control Trigger (Display -> Motor)
 *
 * Payload: typically empty or minimal
 * Sets flags that trigger the motor control packet building.
 * The actual control data is sent via the 0x01/0x14 BLE-style packet.
 */
#define SIM_DWG_CMD_CONTROL      0x53u

/* 0xA6 - Flash Read (Display -> Motor)
 *
 * REQUEST: empty payload
 * RESPONSE: 65 bytes
 *   [0]: data_length (64)
 *   [1-64]: flash data
 */
#define SIM_DWG_CMD_FLASH_READ   0xA6u

/* 0xA7 - Flash Write 4 bytes (Display -> Motor)
 *
 * REQUEST: 6 bytes
 *   [0]: slot_id (0-16)
 *   [1-4]: 4 bytes of data
 *   [5]: reinit_ble_flag
 * RESPONSE: ACK (0xC1)
 */
#define SIM_DWG_CMD_FLASH_WRITE4 0xA7u

/* 0xA8 - Flash Write N bytes (Display -> Motor)
 *
 * REQUEST: 2 + data_len bytes
 *   [0]: slot_id (0-16)
 *   [1]: data_len (1-64)
 *   [2..]: data bytes
 * RESPONSE: 2 bytes
 *   [0]: slot_id
 *   [1]: success (1=ok, 0=fail)
 */
#define SIM_DWG_CMD_FLASH_WRITEN 0xA8u

/* 0xA9 - Config Slot Read (Display -> Motor)
 *
 * REQUEST: 1 byte
 *   [0]: slot_id (0-16)
 *        Slots 0-4, 8: variable length (1-64 bytes)
 *        Slots 5-6: 4 bytes each
 *        Slot 7: 32 bytes
 * RESPONSE: 2 + data_len bytes
 *   [0]: slot_id
 *   [1]: data_len
 *   [2..]: data bytes
 */
#define SIM_DWG_CMD_CFG_READ     0xA9u

/* 0xAA - Display Mode (Display -> Motor)
 *
 * REQUEST: 1 byte
 *   [0]: display_mode (triggers assist level change)
 * RESPONSE: none (builds empty response packet)
 */
#define SIM_DWG_CMD_DISPLAY_MODE 0xAAu

/* 0xAB - Protocol Mode (Display -> Motor)
 *
 * REQUEST: 2 bytes
 *   [0]: enable_flag
 *   [1]: mode (0-3 selects protocol variant)
 * RESPONSE: ACK (0xC1) with status=1
 */
#define SIM_DWG_CMD_PROTO_MODE   0xABu

/* 0xAC - Calibration (Display -> Motor)
 *
 * REQUEST: 1 byte
 *   [0]: calibration_trigger
 * RESPONSE: calibration_value (4 bytes BE) or ACK
 */
#define SIM_DWG_CMD_CALIBRATE    0xACu

/* 0xB0 - Telemetry (Motor -> Display)
 *
 * Extended telemetry packet, no response needed
 */
#define SIM_DWG_CMD_TELEMETRY    0xB0u

/* 0xC0 - Full Config Write (Display -> Motor, 52 bytes)
 * RESPONSE: ACK (0xC1) with status=1
 */
#define SIM_DWG_CMD_CONFIG_WRITE 0xC0u

/* ============================================================================
 * 0xC0 Config Write Struct (52 bytes payload)
 *
 * Full configuration packet sent from display to motor.
 * Same as 0xC3 but includes 5 datetime bytes at offset [2-6].
 * All multi-byte values are BIG ENDIAN.
 * ============================================================================
 */
typedef struct __attribute__((packed)) {
    /* Basic settings */
    uint8_t  assist_level;          /* [0]  Current assist level (1-5, 0=off) */
    uint8_t  auto_poweroff_min;     /* [1]  Auto power-off timeout in minutes (0-10) */

    /* Date/Time (not present in 0xC3 response) */
    uint8_t  year_offset;           /* [2]  Year offset from 2000 (0-99) */
    uint8_t  month;                 /* [3]  Month (1-12) */
    uint8_t  day;                   /* [4]  Day (1-31) */
    uint8_t  hour;                  /* [5]  Hour (0-23) */
    uint8_t  minute;                /* [6]  Minute (0-59) */

    /* Configuration (same layout as 0xC3 starting at offset [2]) */
    uint8_t  batt_nominal_V;        /* [7]  Battery nominal voltage (24, 36, or 48) */
    uint8_t  pas_mode;              /* [8]  PAS mode / config profile (3, 5, or 9) */
    uint8_t  lights_config;         /* [9]  Lights configuration flags */
    uint8_t  max_assist_level;      /* [10] Maximum assist level */
    uint8_t  gear_ratio;            /* [11] Gear ratio setting */
    uint8_t  motor_characteristics; /* [12] Motor characteristics */
    uint8_t  brake_config;          /* [13] Brake sensor configuration */
    uint8_t  speed_mode;            /* [14] Speed mode (0-3) */
    uint8_t  display_mode;          /* [15] Display mode (0-15) */
    uint8_t  batt_cutoff_hi;        /* [16] Battery cutoff (BE high, mV/100) */
    uint8_t  batt_cutoff_lo;        /* [17] Battery cutoff (BE low) */
    uint8_t  current_limit_A;       /* [18] Current limit in Amps */
    uint8_t  speed_limit_kph;       /* [19] Speed limit in km/h */
    uint8_t  wheel_size_code;       /* [20] Wheel size code (0-7) */
    uint8_t  cadence_timeout;       /* [21] Cadence sensor timeout */
    uint8_t  motor_timeout_s;       /* [22] Motor timeout in seconds */
    uint8_t  assist_sensitivity;    /* [23] Assist sensitivity (0-10) */
    uint8_t  units_mode;            /* [24] Units (0=km/h, 1=mph) */
    uint8_t  display_flags;         /* [25] Display flags */
    uint8_t  wheel_circ_hi;         /* [26] Wheel circumference (BE high, mm) */
    uint8_t  wheel_circ_lo;         /* [27] Wheel circumference (BE low) */
    uint8_t  pas_start_current;     /* [28] PAS start current % */
    uint8_t  pas_slow_start;        /* [29] PAS slow start mode */
    uint8_t  torque_sensor_type;    /* [30] Torque sensor type */
    uint8_t  cadence_sensor_type;   /* [31] Cadence sensor type */
    uint8_t  power_assist_factor;   /* [32] Power assist factor */
    uint8_t  assist_curve_1;        /* [33] Assist level 1 power % */
    uint8_t  assist_curve_2;        /* [34] Assist level 2 power % */
    uint8_t  assist_curve_3;        /* [35] Assist level 3 power % */
    uint8_t  motor_config;          /* [36] Motor configuration */
    uint8_t  motor_current_hi;      /* [37] Motor current (BE high, mA) */
    uint8_t  motor_current_lo;      /* [38] Motor current (BE low) */
    uint8_t  motor_power_hi;        /* [39] Motor power (BE high, W) */
    uint8_t  motor_power_lo;        /* [40] Motor power (BE low) */
    uint8_t  reserved_1;            /* [41] Reserved */
    uint8_t  power_display_mode;    /* [42] Power display mode */
    uint8_t  trip_distance_hi;      /* [43] Trip distance (BE high) */
    uint8_t  trip_distance_lo;      /* [44] Trip distance (BE low) */
    uint8_t  total_distance_hi;     /* [45] Total distance (BE high) */
    uint8_t  total_distance_lo;     /* [46] Total distance (BE low) */
    uint8_t  speed_config_hi;       /* [47] Speed config (BE high) */
    uint8_t  speed_config_lo;       /* [48] Speed config (BE low) */
    uint8_t  brightness;            /* [49] Screen brightness */
    uint8_t  contrast;              /* [50] Screen contrast */
    uint8_t  theme;                 /* [51] Display theme */
} sim_dwg_config_c0_t;

_Static_assert(sizeof(sim_dwg_config_c0_t) == 52, "0xC0 config struct must be 52 bytes");

/* 0xC1 - Acknowledgment (Motor -> Display)
 *
 * Payload: 1 byte
 *   [0]: status (1=success, 0=failure)
 */
#define SIM_DWG_CMD_ACK          0xC1u

/* 0xC2 - Status Request (Display -> Motor)
 *
 * REQUEST: empty payload
 * RESPONSE: 0xC3 full status (47 bytes)
 */
#define SIM_DWG_CMD_STATUS_REQ   0xC2u

/* 0xC3 - Full Status Response (Motor -> Display, 47 bytes)
 * Same as 0xC0 but without the 5 datetime bytes
 */
#define SIM_DWG_CMD_STATUS_RESP  0xC3u

/* ============================================================================
 * 0xC3 Status Response Struct (47 bytes payload)
 *
 * This is the complete configuration/status packet sent from motor to display.
 * Mirrors 0xC0 config write but excludes datetime fields.
 * All multi-byte values are BIG ENDIAN.
 * ============================================================================
 */
typedef struct __attribute__((packed)) {
    /* Basic settings */
    uint8_t  assist_level;          /* [0]  Current assist level (1-5, 0=off) */
    uint8_t  auto_poweroff_min;     /* [1]  Auto power-off timeout in minutes (0-10, 0=disabled) */
    uint8_t  batt_nominal_V;        /* [2]  Battery nominal voltage (24, 36, or 48) */
    uint8_t  pas_mode;              /* [3]  PAS mode / config profile (3, 5, or 9 magnets) */
    uint8_t  lights_config;         /* [4]  Lights configuration flags */
    uint8_t  max_assist_level;      /* [5]  Maximum assist level (2-64, typically 3/5/9) */
    uint8_t  gear_ratio;            /* [6]  Gear ratio setting */
    uint8_t  motor_characteristics; /* [7]  Motor characteristics / enable config */
    uint8_t  brake_config;          /* [8]  Brake sensor configuration */
    uint8_t  speed_mode;            /* [9]  Speed mode (0-3) */
    uint8_t  display_mode;          /* [10] Display mode / setting (0-15) */

    /* Battery and limits */
    uint8_t  batt_cutoff_hi;        /* [11] Battery cutoff voltage (BE high byte, mV/100) */
    uint8_t  batt_cutoff_lo;        /* [12] Battery cutoff voltage (BE low byte) */
    uint8_t  current_limit_A;       /* [13] Current limit in Amps (actual_mA = value * 1000) */
    uint8_t  speed_limit_kph;       /* [14] Speed limit in km/h (actual_x10 = value * 10) */

    /* Wheel configuration */
    uint8_t  wheel_size_code;       /* [15] Wheel size code (0-7, see wheel table) */
    uint8_t  cadence_timeout;       /* [16] Cadence sensor timeout */
    uint8_t  motor_timeout_s;       /* [17] Motor timeout in seconds */
    uint8_t  assist_sensitivity;    /* [18] Assist sensitivity (0-10) */
    uint8_t  units_mode;            /* [19] Units mode (0=metric km/h, 1=imperial mph) */
    uint8_t  display_flags;         /* [20] Display flags */
    uint8_t  wheel_circ_hi;         /* [21] Wheel circumference (BE high byte, mm) */
    uint8_t  wheel_circ_lo;         /* [22] Wheel circumference (BE low byte) */

    /* PAS and torque configuration */
    uint8_t  pas_start_current;     /* [23] PAS start current % */
    uint8_t  pas_slow_start;        /* [24] PAS slow start mode */
    uint8_t  torque_sensor_type;    /* [25] Torque sensor type/calibration */
    uint8_t  cadence_sensor_type;   /* [26] Cadence sensor type */
    uint8_t  power_assist_factor;   /* [27] Power assist factor */

    /* Assist level curves (power % at each level) */
    uint8_t  assist_curve_1;        /* [28] Assist level 1 power % */
    uint8_t  assist_curve_2;        /* [29] Assist level 2 power % */
    uint8_t  assist_curve_3;        /* [30] Assist level 3 power % */
    uint8_t  motor_config;          /* [31] Motor configuration flags */

    /* Live telemetry (updated in real-time) */
    uint8_t  motor_current_hi;      /* [32] Motor current (BE high byte, mA) */
    uint8_t  motor_current_lo;      /* [33] Motor current (BE low byte) */
    uint8_t  motor_power_hi;        /* [34] Motor power (BE high byte, Watts) */
    uint8_t  motor_power_lo;        /* [35] Motor power (BE low byte) */

    /* Trip and distance */
    uint8_t  reserved_1;            /* [36] Reserved (always 1) */
    uint8_t  power_display_mode;    /* [37] Power display mode */
    uint8_t  trip_distance_hi;      /* [38] Trip distance (BE high byte, units vary) */
    uint8_t  trip_distance_lo;      /* [39] Trip distance (BE low byte) */
    uint8_t  total_distance_hi;     /* [40] Total/ODO distance (BE high byte) */
    uint8_t  total_distance_lo;     /* [41] Total/ODO distance (BE low byte) */

    /* Speed and display config */
    uint8_t  speed_config_hi;       /* [42] Speed config (BE high byte) */
    uint8_t  speed_config_lo;       /* [43] Speed config (BE low byte) */
    uint8_t  brightness;            /* [44] Screen brightness (0-5) */
    uint8_t  contrast;              /* [45] Screen contrast */
    uint8_t  theme;                 /* [46] Display theme/color scheme */
} sim_dwg_status_c3_t;

/* Verify struct is exactly 47 bytes */
_Static_assert(sizeof(sim_dwg_status_c3_t) == 47, "0xC3 status struct must be 47 bytes");

/* ============================================================================
 * Motor Error Codes (E-codes displayed on screen)
 * ============================================================================
 */
#define SIM_DWG_ERR_NONE             0u

/* E2x - Hardware/Sensor Errors */
#define SIM_DWG_ERR_CURRENT          21u  /* E21: Current Error */
#define SIM_DWG_ERR_THROTTLE         22u  /* E22: Throttle Error */
#define SIM_DWG_ERR_MOTOR_PHASE      23u  /* E23: Motor Phase Error */
#define SIM_DWG_ERR_MOTOR_HALL       24u  /* E24: Motor Hall Sensor Error */
#define SIM_DWG_ERR_BRAKE_SENSOR     25u  /* E25: Brake Sensor Error */
#define SIM_DWG_ERR_OVERHEAT         26u  /* E26: Overheat Protection */
#define SIM_DWG_ERR_MOTOR_LOCK       27u  /* E27: Motor Lock Protection */
#define SIM_DWG_ERR_LOW_VOLTAGE      28u  /* E28: Low Voltage Protection */
#define SIM_DWG_ERR_OVER_VOLTAGE     29u  /* E29: Over Voltage Protection */
#define SIM_DWG_ERR_COMMUNICATION    30u  /* E30: Communication Error */
#define SIM_DWG_ERR_BATT_COMM        31u  /* E31: Battery CAN Communication Failure */

/* E4x - System/Component Faults */
#define SIM_DWG_ERR_CONTROLLER       41u  /* E41: Controller Error */
#define SIM_DWG_ERR_MOTOR_FAULT      42u  /* E42: Motor Fault Error */
#define SIM_DWG_ERR_BATTERY_FAULT    43u  /* E43: Battery Fault Error */
#define SIM_DWG_ERR_TORQUE_SENSOR    44u  /* E44: Torque Sensor Error */
#define SIM_DWG_ERR_HEADLIGHT        46u  /* E46: Headlight Fault */
#define SIM_DWG_ERR_REAR_LIGHT       47u  /* E47: Rear Light Fault */
#define SIM_DWG_ERR_TURN_LIGHT       48u  /* E48: Turn Light Fault */
#define SIM_DWG_ERR_CHARGER_MISMATCH 49u  /* E49: Charger Does Not Match */

/* Parser states */
typedef enum {
    SIM_DWG_STATE_WAIT_SOF = 0,
    SIM_DWG_STATE_FRAME_ID,
    SIM_DWG_STATE_CMD,
    SIM_DWG_STATE_LEN,
    SIM_DWG_STATE_PAYLOAD,
    SIM_DWG_STATE_CHECKSUM_LO,
    SIM_DWG_STATE_CHECKSUM_HI,
    SIM_DWG_STATE_CR,
    SIM_DWG_STATE_LF
} sim_dwg_parse_state_t;

/* Frame slot for parsed frames */
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[SIM_DWG_MAX_PAYLOAD];
    uint8_t valid;
} sim_dwg_frame_t;

/* Motor controller configuration (received from display) */
typedef struct {
    uint8_t screen_brightness;
    uint8_t auto_poweroff_min;
    uint8_t batt_nominal_V;
    uint8_t config_profile_id;
    uint8_t lights_enabled;
    uint8_t max_assist_level;
    uint8_t gear_setting;
    uint8_t motor_enable;
    uint8_t brake_flag;
    uint8_t speed_mode;
    uint8_t display_setting;
    uint16_t batt_threshold_mV;
    uint16_t batt_current_limit_mA;
    uint16_t speed_limit_kph_x10;
    uint8_t wheel_size_code;
    uint16_t wheel_circumference_mm;
    uint8_t units_mode;
    uint8_t motor_timeout_s;
} sim_dwg_config_t;

/* Motor controller state */
typedef struct {
    /* Parser state */
    sim_dwg_parse_state_t parse_state;
    uint8_t parse_cmd;
    uint8_t parse_len;
    uint8_t parse_pos;
    uint8_t parse_buf[SIM_DWG_MAX_PAYLOAD];
    uint16_t parse_checksum;
    uint16_t parse_checksum_rx;

    /* RX frame queue */
    sim_dwg_frame_t rx_frames[SIM_DWG_RX_SLOTS];
    uint8_t rx_rd_idx;
    uint8_t rx_wr_idx;

    /* Physical e-bike simulator */
    sim_shengyi_t bike;

    /* Motor controller state */
    uint8_t motor_enabled;
    uint8_t walk_assist_active;
    uint8_t headlight_on;
    uint8_t speed_over_limit;
    uint8_t error_code;

    /* Current control inputs from display */
    uint8_t assist_level_mapped;
    uint8_t control_flags;

    /* Configuration (from display) */
    sim_dwg_config_t config;

    /* Flash storage slots (simulated) */
    uint8_t flash_slots[17][64];
    uint8_t flash_slot_lens[17];

    /* Status frame counter */
    uint8_t frame_counter;

    /* Timing */
    uint32_t t_ms;
    uint32_t last_status_ms;
    uint16_t status_period_ms;

    /* Pending responses */
    uint8_t send_status_0x52;
    uint8_t send_status_0xC3;

    /* Statistics */
    uint32_t frames_rx;
    uint32_t frames_tx;
    uint32_t parse_errors;
} sim_dwg_motor_t;

/* Initialize motor controller simulator */
void sim_dwg_motor_init(sim_dwg_motor_t *m);

/* Feed a byte from UART2 RX into the parser */
void sim_dwg_motor_feed_byte(sim_dwg_motor_t *m, uint8_t byte);

/* Process all pending frames and generate responses */
void sim_dwg_motor_process(sim_dwg_motor_t *m);

/* Advance simulation time and physics */
void sim_dwg_motor_tick(sim_dwg_motor_t *m, uint32_t dt_ms);

/* Set rider input (pedaling) */
void sim_dwg_motor_set_rider_power(sim_dwg_motor_t *m, double power_w);

/* Set environmental conditions */
void sim_dwg_motor_set_grade(sim_dwg_motor_t *m, double grade);
void sim_dwg_motor_set_wind(sim_dwg_motor_t *m, double wind_mps);

/* Set error condition */
void sim_dwg_motor_set_error(sim_dwg_motor_t *m, uint8_t error_code);

/* Get current telemetry for UI */
uint16_t sim_dwg_motor_speed_dmph(const sim_dwg_motor_t *m);
uint16_t sim_dwg_motor_cadence_rpm(const sim_dwg_motor_t *m);
uint16_t sim_dwg_motor_power_w(const sim_dwg_motor_t *m);
int16_t sim_dwg_motor_batt_dV(const sim_dwg_motor_t *m);
int16_t sim_dwg_motor_batt_dA(const sim_dwg_motor_t *m);
int16_t sim_dwg_motor_temp_dC(const sim_dwg_motor_t *m);
uint8_t sim_dwg_motor_soc_pct(const sim_dwg_motor_t *m);
uint8_t sim_dwg_motor_error_code(const sim_dwg_motor_t *m);

/* Build frames for testing (display -> motor) */
size_t sim_dwg_build_0x52_request(uint8_t assist_level, uint8_t flags,
                                  uint8_t *out, size_t cap);
size_t sim_dwg_build_0xC2_request(uint8_t *out, size_t cap);

#endif /* SIM_SHENGYI_MOTOR_H */
