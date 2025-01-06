#ifndef OPEN_FIRMWARE_STORAGE_CRASH_DUMP_H
#define OPEN_FIRMWARE_STORAGE_CRASH_DUMP_H

#include <stdint.h>

#include "storage/logs.h"

/* Crash dump snapshot (fixed-size record in SPI flash) */
#define CRASH_DUMP_MAGIC 0x43525348u /* 'CRSH' */
#define CRASH_DUMP_VERSION 1u
#define CRASH_DUMP_EVENT_MAX 4u
#define CRASH_DUMP_HEADER_SIZE 72u
#define CRASH_DUMP_SIZE (CRASH_DUMP_HEADER_SIZE + (CRASH_DUMP_EVENT_MAX * EVENT_LOG_RECORD_SIZE))

#define CRASH_DUMP_OFF_MAGIC 0u
#define CRASH_DUMP_OFF_VERSION 4u
#define CRASH_DUMP_OFF_SIZE 6u
#define CRASH_DUMP_OFF_FLAGS 8u
#define CRASH_DUMP_OFF_SEQ 12u
#define CRASH_DUMP_OFF_CRC 16u
#define CRASH_DUMP_OFF_MS 20u
#define CRASH_DUMP_OFF_SP 24u
#define CRASH_DUMP_OFF_LR 28u
#define CRASH_DUMP_OFF_PC 32u
#define CRASH_DUMP_OFF_PSR 36u
#define CRASH_DUMP_OFF_CFSR 40u
#define CRASH_DUMP_OFF_HFSR 44u
#define CRASH_DUMP_OFF_DFSR 48u
#define CRASH_DUMP_OFF_MMFAR 52u
#define CRASH_DUMP_OFF_BFAR 56u
#define CRASH_DUMP_OFF_AFSR 60u
#define CRASH_DUMP_OFF_EVENT_COUNT 64u
#define CRASH_DUMP_OFF_EVENT_REC_SIZE 66u
#define CRASH_DUMP_OFF_EVENT_SEQ 68u
#define CRASH_DUMP_OFF_EVENT_RECORDS 72u

void crash_dump_clear_storage(void);
uint8_t crash_dump_load(uint8_t *out);
void crash_dump_capture(uint32_t sp, uint32_t lr, uint32_t pc, uint32_t psr);

#endif

