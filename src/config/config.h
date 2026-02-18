#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* Legal mode + PIN configuration */
#define MODE_STREET  0u
#define MODE_PRIVATE 1u

/* Config flags (stored in config_t.flags) */
#define CONFIG_FLAG_WALK_ENABLED    0x01u  /* Walk mode capability */
#define CONFIG_FLAG_DEV_SCREENS     0x02u  /* Show developer screens (Bus, Capture, Engineer) */

/*
 * Reserved bits (stored in config_t.reserved).
 *
 * These must not change CONFIG_BLOB_SIZE. We use them for small, persistent
 * knobs that affect on-wire compatibility with OEM variants.
 *
 * OEM BC280 app v2.5.1 STX02/XOR (0x02-framed) transmit flags:
 * - bit6_src default 0
 * - bit3_src default 1
 * - speed_gate default 0
 *
 * To keep backwards compatibility with existing configs (reserved==0), we
 * encode the OEM defaults as "all bits clear".
 */
#define CFG_RSVD_STX02_BIT6_ENABLE        0x0001u /* if set, set STX02 flags bit6 */
#define CFG_RSVD_STX02_BIT3_DISABLE       0x0002u /* if set, clear STX02 flags bit3 (OEM default is set) */
#define CFG_RSVD_STX02_SPEED_GATE_ENABLE  0x0004u /* if set, enable OEM-like speed-limit gating (flags bit2) */
#define CFG_RSVD_STX02_MASK (CFG_RSVD_STX02_BIT6_ENABLE | CFG_RSVD_STX02_BIT3_DISABLE | CFG_RSVD_STX02_SPEED_GATE_ENABLE)
#define MODE_PIN_DEFAULT      1234u
#define MODE_PIN_MAX          9999u
#define MODE_PIN_RATE_LIMIT_MS 2000u
#define STREET_MAX_CURRENT_DA 200u
#define STREET_MAX_SPEED_DMPH 400u

/* Configuration curve point */
typedef struct {
    uint16_t x;
    uint16_t y;
} config_curve_pt_t;

#ifndef ASSIST_CURVE_MAX_POINTS
#define ASSIST_CURVE_MAX_POINTS 8
#endif

#ifndef PROFILE_COUNT
#define PROFILE_COUNT 5
#endif

/* Config blob serialization */
#define CONFIG_VERSION 6u
#define CONFIG_BLOB_CURVE_COUNT_OFFSET 48u
#define CONFIG_BLOB_CURVE_OFFSET 49u
#define CONFIG_BLOB_SIZE (CONFIG_BLOB_CURVE_OFFSET + (ASSIST_CURVE_MAX_POINTS * 4u))

/* Configuration slot */
typedef struct {
    uint8_t  version;
    uint8_t  size;
    uint16_t reserved;
    uint32_t seq;
    uint32_t crc32;
    uint16_t wheel_mm;
    uint8_t  units;       /* 0=imperial, 1=metric */
    uint8_t  profile_id;  /* active profile selector */
    uint8_t  theme;       /* UI theme id */
    uint8_t  flags;       /* spare feature flags */
    uint8_t  button_map;  /* button mapping preset */
    uint8_t  button_flags;/* reserved for future button options */
    uint8_t  mode;        /* 0=street legal, 1=private */
    uint16_t pin_code;    /* numeric PIN for private-mode unlock */
    uint16_t cap_current_dA;   /* overall current clamp (deci-amps) */
    uint16_t cap_speed_dmph;   /* overall speed cap (0 = none) */
    uint16_t log_period_ms;    /* telemetry/event logging period hint */
    uint16_t soft_start_ramp_wps;   /* launch ramp rate (W/s) */
    uint16_t soft_start_deadband_w; /* ignore commands below this */
    uint16_t soft_start_kick_w;     /* initial kick cap (W) */
    uint8_t  drive_mode;           /* 0=auto, 1=manual current, 2=manual power, 3=sport */
    uint16_t manual_current_dA;    /* manual current setpoint */
    uint16_t manual_power_w;       /* manual power setpoint */
    uint16_t boost_budget_ms;      /* max boost time at high current */
    uint16_t boost_cooldown_ms;    /* time to recover full budget */
    uint16_t boost_threshold_dA;   /* current threshold for budget burn */
    uint16_t boost_gain_q15;       /* scale for burn rate */
    uint8_t  curve_count;      /* assist curve override point count */
    config_curve_pt_t curve[ASSIST_CURVE_MAX_POINTS];
} config_t;

/* Global config blob (defined in config.c). */
extern config_t g_config_active;

/* Config validation rejections */
typedef enum {
    CFG_REJECT_NONE      = 0,
    CFG_REJECT_RANGE     = 1,
    CFG_REJECT_MONOTONIC = 2,
    CFG_REJECT_RATE      = 3,
    CFG_REJECT_CRC       = 4,
    CFG_REJECT_UNSUPPORTED = 5,
    CFG_REJECT_POLICY    = 6,
    CFG_REJECT_PIN       = 7,
} config_reject_reason_t;

/* Wizard state */
typedef enum {
    WIZ_STEP_WHEEL = 0,
    WIZ_STEP_UNITS = 1,
    WIZ_STEP_BUTTONS = 2,
    WIZ_STEP_PROFILE = 3,
    WIZ_STEP_DONE = 4,
} wizard_step_t;

typedef struct {
    uint8_t active;
    wizard_step_t step;
    uint8_t error;
    uint8_t last_buttons;
    config_t cfg;
} wizard_state_t;

/* API declarations */
void config_defaults(config_t *cfg);
void config_store_be(uint8_t *dst, const config_t *c);
void config_load_from_be(config_t *c, const uint8_t *src);
uint32_t config_crc_expected(const config_t *c);
int config_validate_reason(const config_t *c, int check_crc, config_reject_reason_t *reason_out);
int config_validate(const config_t *c, int check_crc);
int config_policy_validate(const config_t *c, config_reject_reason_t *reason_out);
void config_write_slot(int slot, const config_t *c);
int config_read_slot(int slot, config_t *out);
void config_load_active(void);
void config_persist_active(void);
int config_commit_active(const config_t *c);
void config_stage_reset(void);
uint8_t config_stage_blob(const uint8_t *p);
uint8_t config_commit_staged(const uint8_t *p, uint8_t len);

void wizard_reset(void);
void wizard_start(void);
void wizard_handle_buttons(uint8_t buttons);
void wizard_get_state(wizard_state_t *out);
uint8_t wizard_is_active(void);

#endif /* CONFIG_H */
