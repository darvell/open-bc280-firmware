#ifndef POWER_H
#define POWER_H

#include <stdint.h>

/* Q16 fixed-point constants */
#define Q16_ONE             65535u
#define DUTY_MIN_Q16        6553u   /* ~0.10 */

/* Lug governor parameters */
#define LUG_D_START_Q16     29491u  /* ~0.45 */
#define LUG_D_HARD_Q16      19661u  /* ~0.30 */
#define LUG_F_MIN_Q16       22937u  /* ~0.35 */
#define LUG_RAMP_DOWN_MS    1500u
#define LUG_RAMP_UP_MS      700u
#define LUG_VNL_MIN_DMPH    50u
#define LUG_KV_Q16          43000u  /* deci-mph per deci-volt, Q16 */

/* Thermal governor parameters */
#define THERM_F_MIN_Q16     26214u  /* ~0.40 */
#define THERM_TEMP_SOFT_DC  700     /* 70.0 C */
#define THERM_TEMP_HARD_DC  900     /* 90.0 C */
#define THERM_STATE_SHIFT   4
#define THERM_HEAT_COOL     ((200u * 200u) >> THERM_STATE_SHIFT)
#define THERM_HEAT_HARD     ((280u * 280u) >> THERM_STATE_SHIFT)
#define THERM_TAU_FAST_MS   3000u
#define THERM_TAU_SLOW_MS   30000u

/* Sag governor parameters */
#define SAG_START_DV        360     /* 36.0 V */
#define SAG_CUTOFF_DV       320     /* 32.0 V */

/* Input capability flags */
#define INPUT_CAP_BATT_V    (1u << 0)
#define INPUT_CAP_BATT_I    (1u << 1)
#define INPUT_CAP_TEMP      (1u << 2)

/* Power limit reasons */
typedef enum {
    LIMIT_REASON_USER  = 0,
    LIMIT_REASON_LUG   = 1,
    LIMIT_REASON_THERM = 2,
    LIMIT_REASON_SAG   = 3,
} limit_reason_t;

/* Power policy internal state */
typedef struct {
    uint16_t p_user_w;
    uint16_t p_lug_w;
    uint16_t p_thermal_w;
    uint16_t p_sag_w;
    uint16_t p_final_w;
    uint16_t duty_q16;
    int16_t  i_phase_est_dA;
    uint16_t thermal_state;
    uint16_t thermal_factor_q16;
    int16_t  sag_margin_dV;
    uint8_t  limit_reason;
    uint8_t  reserved;
    uint16_t lug_factor_q16;
    int32_t  thermal_fast;
    int32_t  thermal_slow;
    uint32_t last_ms;
    uint32_t last_log_ms;
    uint8_t  last_reason;
} power_policy_state_t;

/* Soft start configuration */
#define SOFT_START_RAMP_MIN_WPS     50u
#define SOFT_START_RAMP_MAX_WPS     2000u
#define SOFT_START_DEADBAND_MAX_W   200u
#define SOFT_START_KICK_MAX_W       500u
#define SOFT_START_RAMP_DEFAULT_WPS 0u
#define SOFT_START_DEADBAND_DEFAULT_W 0u
#define SOFT_START_KICK_DEFAULT_W   0u

/* Adaptive assist state */
typedef struct {
    uint16_t speed_ema_dmph;
    int16_t  speed_delta_dmph;
    int32_t  power_ema_w;
    uint16_t eco_output_w;
    uint16_t last_speed_dmph;
    uint32_t last_ms;
    uint8_t  trend_active;
    uint8_t  eco_clamp_active;
} adaptive_assist_state_t;

/* Global state (defined in main.c, accessed via extern) */
extern power_policy_state_t g_power_policy;
extern adaptive_assist_state_t g_adapt;
extern uint8_t g_input_caps;
extern volatile uint32_t g_ms;

/* API declarations */
void power_policy_reset(void);
void power_policy_apply(uint16_t p_user_w);
uint16_t power_policy_final_w(void);
limit_reason_t power_policy_limit_reason(void);

void adaptive_reset(void);
void adaptive_update(uint16_t speed_dmph, uint16_t power_w, uint32_t now_ms);
uint16_t adaptive_effort_boost(uint16_t base_power_w, uint16_t target_speed_dmph);
uint16_t adaptive_eco_limit(uint16_t target_power_w);

#endif /* POWER_H */
