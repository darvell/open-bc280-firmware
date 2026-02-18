#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>

/* Capability and config flags */
#define CAP_FLAG_WALK       (1u << 0)
#define CAP_FLAG_REGEN      (1u << 1)
#define CFG_FLAG_QA_CRUISE  (1u << 2)
#define CFG_FLAG_QA_PROFILE (1u << 3)
#define CFG_FLAG_QA_CAPTURE (1u << 4)
#define CFG_FLAG_ADAPT_EFFORT (1u << 5)
#define CFG_FLAG_ADAPT_ECO    (1u << 6)
#define CFG_FLAG_QA_FOCUS     (1u << 7)

/* Button masks */
#define WALK_BUTTON_MASK      0x40u
#define CRUISE_BUTTON_MASK    0x80u
#define CRUISE_SPEED_SELECT_MASK 0x08u
#define BUTTON_GEAR_UP_MASK   0x10u
#define BUTTON_GEAR_DOWN_MASK 0x20u

/* Walk assist parameters */
#define WALK_SPEED_CAP_DMPH 40u   /* 4.0 mph */
#define WALK_BASE_POWER_W   180u  /* conservative push */
#define WALK_TIMEOUT_MS     8000u /* optional auto-exit */

/* Regen parameters */
#define REGEN_LEVEL_MAX 10u
#define REGEN_STEP_W    40u  /* per-level regen power target */

/* Cruise parameters */
#define CRUISE_MIN_SPEED_DMPH     60u   /* 6.0 mph minimum to engage */
#define CRUISE_MIN_POWER_W        40u   /* avoid zero-power engage */
#define CRUISE_SPEED_KP_W_PER_DMPH 4    /* proportional gain */
#define CRUISE_SPEED_STEP_MAX_W   80u   /* clamp per-tick adjustment */
#define CRUISE_RESUME_SPEED_DELTA_DMPH 20u /* 2.0 mph window for resume */

/* Cruise event codes */
#define CRUISE_EVT_ENGAGE_SPEED 0x01u
#define CRUISE_EVT_ENGAGE_POWER 0x02u
#define CRUISE_EVT_CANCEL_USER  0x10u
#define CRUISE_EVT_CANCEL_BRAKE 0x11u
#define CRUISE_EVT_CANCEL_PEDAL 0x12u
#define CRUISE_EVT_CANCEL_WALK  0x13u
#define CRUISE_EVT_CANCEL_CAP   0x14u
#define CRUISE_EVT_CANCEL_FAULT 0x15u
#define CRUISE_EVT_RESUME_OK    0x20u
#define CRUISE_EVT_RESUME_BLOCK_BRAKE 0x21u
#define CRUISE_EVT_RESUME_BLOCK_SPEED 0x22u
#define CRUISE_EVT_RESUME_BLOCK_PEDAL 0x23u
#define CRUISE_EVT_RESUME_BLOCK_LIMIT 0x24u
#define CRUISE_EVT_RESUME_BLOCK_FAULT 0x25u

/* Manual drive parameters */
#define MANUAL_CURRENT_MAX_DA 400u
#define MANUAL_POWER_MAX_W    1200u
#define MANUAL_POWER_KP_Q15   8192u  /* 0.25 */
#define MANUAL_POWER_RATE_WPS 800u

/* Boost parameters */
#define BOOST_BUDGET_DEFAULT_MS 6000u
#define BOOST_COOLDOWN_DEFAULT_MS 12000u
#define BOOST_THRESHOLD_DEFAULT_DA 180u
#define BOOST_GAIN_DEFAULT_Q15 1024u /* 1/32 scale */
#define BOOST_BUDGET_MAX_MS 60000u
#define BOOST_COOLDOWN_MAX_MS 60000u

/* Cruise control modes */
typedef enum {
    CRUISE_OFF = 0,
    CRUISE_SPEED = 1,
    CRUISE_POWER = 2,
} cruise_mode_t;

/* Cruise resume block reasons */
typedef enum {
    CRUISE_RESUME_NONE = 0,
    CRUISE_RESUME_BLOCK_BRAKE = 1,
    CRUISE_RESUME_BLOCK_SPEED = 2,
    CRUISE_RESUME_BLOCK_PEDAL = 3,
    CRUISE_RESUME_BLOCK_LIMIT = 4,
    CRUISE_RESUME_BLOCK_FAULT = 5,
} cruise_resume_reason_t;

/* Cruise state */
typedef struct {
    cruise_mode_t mode;
    uint8_t last_button;
    uint8_t require_pedaling;
    uint8_t resume_available;
    uint8_t resume_require_pedaling;
    uint8_t resume_mode;
    uint8_t resume_block_reason;
    uint16_t set_speed_dmph;
    uint16_t set_power_w;
    uint16_t output_w;
} cruise_state_t;

/* Walk assist states */
typedef enum {
    WALK_STATE_OFF = 0,
    WALK_STATE_ACTIVE = 1,
    WALK_STATE_CANCELLED = 2,
    WALK_STATE_DISABLED = 3,
} walk_state_t;

/* Regen state */
typedef struct {
    uint8_t level;
    uint8_t brake_level;
    uint8_t active;
    uint16_t cmd_power_w;
    uint16_t cmd_current_dA;
} regen_state_t;

/* Drive modes */
typedef enum {
    DRIVE_MODE_AUTO = 0,
    DRIVE_MODE_MANUAL_CURRENT = 1,
    DRIVE_MODE_MANUAL_POWER = 2,
    DRIVE_MODE_SPORT = 3,
} drive_mode_t;

/* Drive state */
typedef struct {
    drive_mode_t mode;
    uint16_t setpoint;
    uint16_t cmd_power_w;
    uint16_t cmd_current_dA;
    uint32_t last_ms;
} drive_state_t;

/* Boost state */
typedef struct {
    uint16_t budget_ms;
    uint8_t active;
    uint32_t last_ms;
} boost_state_t;

/* Soft start state */
typedef struct {
    uint8_t active;
    uint16_t target_w;
    uint16_t output_w;
    uint32_t last_ms;
} soft_start_state_t;

/* Virtual gear shapes */
typedef enum {
    VGEAR_SHAPE_LINEAR = 0,
    VGEAR_SHAPE_EXP = 1,
} vgear_shape_t;

/* Virtual gear table */
#define VGEAR_MAX 12
#define VGEAR_SCALE_MIN_Q15 3277u   /* ~0.1 */
#define VGEAR_SCALE_MAX_Q15 65535u

typedef struct {
    uint8_t  count;              /* 1..12 */
    uint8_t  shape;              /* VGEAR_SHAPE_* */
    uint16_t min_scale_q15;      /* >= 0.1, <= 2.0 (Q15) */
    uint16_t max_scale_q15;      /* >= min */
    uint16_t scales[VGEAR_MAX];  /* per-gear Q15 multipliers */
} vgear_table_t;

/* Cadence bias config */
typedef struct {
    uint8_t  enabled;
    uint16_t target_rpm;      /* center of preferred cadence band */
    uint16_t band_rpm;        /* width above target for taper */
    uint16_t min_bias_q15;    /* floor multiplier when above band */
} cadence_bias_t;

/* Global state (defined in main.c, accessed via extern) */
extern cruise_state_t g_cruise;
extern regen_state_t g_regen;
extern drive_state_t g_drive;
extern boost_state_t g_boost;
extern soft_start_state_t g_soft_start;
extern walk_state_t g_walk_state;
extern uint8_t g_walk_inhibit;
extern uint16_t g_walk_cmd_power_w;
extern uint16_t g_walk_cmd_current_dA;
extern uint32_t g_walk_entry_ms;
extern uint8_t g_hw_caps;
extern uint8_t g_cruise_toggle_request;
extern uint16_t g_effective_cap_current_dA;
extern uint16_t g_effective_cap_speed_dmph;
extern volatile uint32_t g_ms;
extern vgear_table_t g_vgears;
extern cadence_bias_t g_cadence_bias;
extern uint8_t g_active_vgear;
extern uint8_t g_headlight_enabled;

/* Walk assist API */
void walk_reset(void);
int walk_capable(void);
void walk_update(void);

/* Regen API */
void regen_reset(void);
void regen_set_levels(uint8_t level, uint8_t brake_level);
void regen_update(void);
uint8_t regen_capable(void);

/* Cruise API */
void cruise_reset(void);
void cruise_cancel(uint8_t reason);
uint16_t cruise_apply(uint16_t base_power, uint16_t limit_power);

/* Drive mode API */
void drive_reset(void);
void drive_apply_config(void);
uint16_t battery_power_w(void);
uint16_t manual_power_apply(uint16_t target_w);

/* Boost API */
void boost_reset(void);
void boost_update(void);

/* Soft start API */
void soft_start_reset(void);
uint16_t soft_start_apply(uint16_t desired_w);

/* Virtual gear API */
#define VGEAR_UI_STEP_Q15      1638u
#define VGEAR_UI_STEP_FAST_Q15 3277u

void vgear_generate_scales(vgear_table_t *t);
int vgear_validate(const vgear_table_t *t);
void vgear_defaults(void);
uint16_t vgear_q15_to_pct(uint16_t q15);
void vgear_adjust_min(int dir, uint16_t step);
void vgear_adjust_max(int dir, uint16_t step);

/* Cadence bias API */
void cadence_bias_defaults(void);

#endif /* CONTROL_H */
