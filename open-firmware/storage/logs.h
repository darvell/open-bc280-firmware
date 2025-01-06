#ifndef OPEN_FIRMWARE_STORAGE_LOGS_H
#define OPEN_FIRMWARE_STORAGE_LOGS_H

#include <stdint.h>

#include "storage/event_types.h"

/* Event log (fixed-size flash-backed log; erase-on-wrap) */
#define EVENT_LOG_MAGIC        0x45564C47u /* 'EVLG' */
#define EVENT_LOG_VERSION      1u
#define EVENT_LOG_RECORD_SIZE  20u
#define EVENT_LOG_CAPACITY     256u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint16_t capacity;
    uint16_t reserved;
    uint32_t head;
    uint32_t count;
    uint32_t seq;
    uint32_t crc32;
} event_log_meta_t;

extern event_log_meta_t g_event_meta;

void event_log_load(void);
void event_log_reset(void);
void event_log_append(uint8_t type, uint8_t flags);
uint8_t event_log_copy(uint16_t offset, uint8_t max_records, uint8_t *out);

/* Stream log (sampled telemetry; flash-backed; erase-on-wrap) */
#define STREAM_LOG_MAGIC        0x53544C47u /* 'STLG' */
#define STREAM_LOG_VERSION      1u
#define STREAM_LOG_RECORD_SIZE  20u
#define STREAM_LOG_CAPACITY     512u
#define STREAM_LOG_PERIOD_MIN_MS 100u
#define STREAM_LOG_PERIOD_MAX_MS 60000u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint16_t capacity;
    uint16_t reserved;
    uint32_t head;
    uint32_t count;
    uint32_t seq;
    uint32_t crc32;
} stream_log_meta_t;

extern stream_log_meta_t g_stream_meta;
extern uint8_t g_stream_log_enabled;
extern uint16_t g_stream_log_period_ms;
extern uint32_t g_stream_log_last_ms;
extern uint32_t g_stream_log_last_sample_ms;

uint16_t stream_log_period_sanitize(uint16_t period);
void stream_log_load(void);
void stream_log_reset(void);
void stream_log_append(uint8_t flags);
uint8_t stream_log_copy(uint16_t offset, uint8_t max_records, uint8_t *out);
void stream_log_tick(void);

#endif
