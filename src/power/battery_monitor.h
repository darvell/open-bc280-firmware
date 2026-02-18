#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/*
 * OEM v2.5.1-style battery voltage monitoring via ADC1 channel 0 (PA0).
 *
 * - Samples every ~50ms.
 * - Filters with 10-sample window, drop min/max, average remaining 8.
 * - Converts using OEM scale factor `n69300` (default 69300) read from the OEM
 *   SPI flash config block at 0x003FD000/0x003FB000 (offset 0x78).
 */
void battery_monitor_init(void);
void battery_monitor_tick(uint32_t now_ms);

/* Returns true once at least one filtered ADC sample has been applied. */
bool battery_monitor_has_sample(void);

/* Timestamp (g_ms) of the last applied sample; 0 if none. */
uint32_t battery_monitor_last_update_ms(void);

#endif /* BATTERY_MONITOR_H */
