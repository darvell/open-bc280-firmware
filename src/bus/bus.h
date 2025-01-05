#ifndef BUS_H
#define BUS_H

#include <stdint.h>

/* Bus IDs */
#define BUS_MOTOR   0
#define BUS_BLE     1

/* Capture parameters */
#define BUS_CAPTURE_VERSION   1u
#define BUS_CAPTURE_MAX_DATA  32u
#define BUS_CAPTURE_CAPACITY  64u

/* Inject safety limits */
#define BUS_INJECT_SPEED_MAX_DMPH 10u
#define BUS_REPLAY_RATE_MIN_MS 20u
#define BUS_REPLAY_RATE_MAX_MS 1000u

/* Inject override flags */
#define BUS_INJECT_OVERRIDE_SPEED 0x01u
#define BUS_INJECT_OVERRIDE_BRAKE 0x02u

/* Inject event flags */
#define BUS_INJECT_EVENT_OK              0x01u
#define BUS_INJECT_EVENT_BLOCKED_MODE    0x02u
#define BUS_INJECT_EVENT_BLOCKED_ARMED   0x04u
#define BUS_INJECT_EVENT_BLOCKED_MOVING  0x08u
#define BUS_INJECT_EVENT_BLOCKED_BRAKE   0x10u
#define BUS_INJECT_EVENT_BLOCKED_CAPTURE 0x20u
#define BUS_INJECT_EVENT_OVERRIDE        0x40u
#define BUS_INJECT_EVENT_REPLAY          0x80u

/* Inject status codes */
#define BUS_INJECT_STATUS_NOT_ARMED        0xF1u
#define BUS_INJECT_STATUS_MODE             0xF2u
#define BUS_INJECT_STATUS_MOVING           0xF3u
#define BUS_INJECT_STATUS_BRAKE            0xF4u
#define BUS_INJECT_STATUS_CAPTURE_DISABLED 0xF5u
#define BUS_INJECT_STATUS_BAD_RANGE        0xF6u
#define BUS_INJECT_STATUS_BAD_PAYLOAD      0xFEu

/* Bus UI parameters */
#define BUS_UI_VIEW_MAX 6u
#define BUS_UI_FLAG_ENABLE        0x01u
#define BUS_UI_FLAG_FILTER_ID     0x02u
#define BUS_UI_FLAG_FILTER_OPCODE 0x04u
#define BUS_UI_FLAG_DIFF          0x08u
#define BUS_UI_FLAG_CHANGED_ONLY  0x10u
#define BUS_UI_FLAG_RESET         0x20u

/* Capture record */
typedef struct {
    uint8_t bus_id;
    uint8_t len;
    uint16_t dt_ms;
    uint8_t data[BUS_CAPTURE_MAX_DATA];
} bus_capture_record_t;

/* UI view entry for bus monitor */
typedef struct {
    uint8_t bus_id;
    uint8_t len;
    uint16_t dt_ms;
    uint8_t data[BUS_CAPTURE_MAX_DATA];
    uint32_t diff_mask;
} bus_ui_entry_t;

/* UI state snapshot */
typedef struct {
    uint8_t count;
    uint8_t diff_enabled;
    uint8_t changed_only;
    uint8_t filter_id;
    uint8_t filter_opcode;
    uint8_t filter_bus_id;
    uint8_t filter_opcode_val;
} bus_ui_state_t;

/* Capture state */
typedef struct {
    uint8_t enabled;
    uint8_t paused;
    uint16_t head;
    uint16_t count;
    uint16_t capacity;
    uint32_t seq;
    uint32_t last_ms;
} bus_capture_state_t;

/* Replay state */
typedef struct {
    uint8_t active;
    uint16_t offset;
    uint16_t rate_ms;
    uint32_t next_ms;
} bus_replay_state_t;

/* Inject state */
typedef struct {
    uint8_t armed;
    uint8_t override;
} bus_inject_state_t;

/* API declarations */
void bus_capture_reset(void);
void bus_capture_set_enabled(uint8_t enable, uint8_t reset);
void bus_capture_append(uint8_t bus_id, const uint8_t *data, uint8_t len, uint16_t dt_ms);
uint8_t bus_capture_get_enabled(void);
uint16_t bus_capture_get_count(void);
void bus_capture_get_state(bus_capture_state_t *out);
int bus_capture_get_record(uint16_t offset, bus_capture_record_t *out);

void bus_inject_emit(uint8_t bus_id, const uint8_t *data, uint8_t len, uint16_t dt_ms, uint8_t flags);
int bus_inject_allowed(uint8_t *flags_out);
void bus_inject_log(uint8_t flags);
void bus_inject_set_armed(uint8_t armed, uint8_t override_flags);

void bus_replay_start(uint8_t offset, uint16_t rate_ms);
void bus_replay_cancel(uint8_t flags);
void bus_replay_tick(void);

void bus_ui_reset(void);
void bus_ui_on_capture(uint8_t bus_id, const uint8_t *data, uint8_t len, uint16_t dt_ms);
void bus_ui_set_control(uint8_t flags, uint8_t bus_id, uint8_t opcode);
void bus_ui_get_state(bus_ui_state_t *out);
int bus_ui_get_last(bus_ui_entry_t *out);

#endif /* BUS_H */
