#ifndef BATTERY_SOC_H
#define BATTERY_SOC_H

#include <stdint.h>

/*
 * OEM v2.5.1-style battery state-of-charge estimation from pack voltage.
 *
 * Inputs:
 * - batt_mv: battery pack voltage in millivolts
 * - nominal_v: 24/36/48 selects the curve. Any other value means "auto" (infer).
 *
 * Output:
 * - 0..100 percent
 */
uint8_t battery_soc_pct_from_mv(uint32_t batt_mv, uint8_t nominal_v);

#endif /* BATTERY_SOC_H */

