/*
 * Motor Link Manager
 *
 * OEM app v2.5.1 supports multiple motor UART wire formats across variants.
 * This module selects a protocol at runtime (AUTO or forced) and drives the
 * minimal periodic TX needed to observe traffic / keep the link alive.
 */

#ifndef MOTOR_LINK_H
#define MOTOR_LINK_H

#include <stdint.h>
#include <stdbool.h>

#include "motor_isr.h"

typedef enum {
    MOTOR_LINK_MODE_AUTO = 0,
    MOTOR_LINK_MODE_FORCE_SHENGYI = 1,
    MOTOR_LINK_MODE_FORCE_STX02 = 2,
    MOTOR_LINK_MODE_FORCE_AUTH = 3,
    MOTOR_LINK_MODE_FORCE_V2 = 4,
} motor_link_mode_t;

void motor_link_init(void);

/* Called from the main loop (not ISR). */
void motor_link_periodic_send_tick(void);

motor_link_mode_t motor_link_get_mode(void);
void motor_link_set_mode(motor_link_mode_t mode);

/* Active protocol: forced > locked > AUTO default (Shengyi). */
motor_proto_t motor_link_get_active_proto(void);
bool motor_link_is_locked(void);

/* Handle OEM 0xAB protocol switch command from motor controller.
 * proto_idx: 0=Shengyi, 1=STX02, 2=V2short, 3=Tongsheng.
 * Reinitializes the protocol stack. */
void motor_link_switch_protocol(uint8_t proto_idx);

#endif /* MOTOR_LINK_H */
