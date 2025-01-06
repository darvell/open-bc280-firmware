#ifndef OPEN_FIRMWARE_STORAGE_EVENT_TYPES_H
#define OPEN_FIRMWARE_STORAGE_EVENT_TYPES_H

#include <stdint.h>

typedef enum {
    EVT_NONE            = 0,
    EVT_BRAKE           = 1,
    EVT_COMM_FLAKY      = 2,
    EVT_SENSOR_DROPOUT  = 3,
    EVT_OVERTEMP_WARN   = 4,
    EVT_DERATE_ACTIVE   = 5, /* limiter active; flags=reason (LUG/THERM/SAG), bit7=injected */
    EVT_CRUISE_EVENT    = 6,
    EVT_CONFIG_REJECT   = 7,
    EVT_PIN_ATTEMPT     = 8,
    EVT_RESET_REASON    = 9, /* reset reason flags snapshot */
    EVT_BUS_INJECT      = 10,
    EVT_TEST_MARK       = 250, /* reserved for tests */
} event_type_t;

#endif /* OPEN_FIRMWARE_STORAGE_EVENT_TYPES_H */
