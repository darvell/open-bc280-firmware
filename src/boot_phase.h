#ifndef OPEN_FIRMWARE_BOOT_PHASE_H
#define OPEN_FIRMWARE_BOOT_PHASE_H

#include <stdint.h>

typedef enum {
    BOOT_PHASE_MONITOR = 0,
    BOOT_PHASE_APP = 1,
    BOOT_PHASE_PANIC = 2,
} boot_phase_t;

/* Global boot phase. The boot monitor runs with BOOT_PHASE_MONITOR, then the
 * full firmware sets BOOT_PHASE_APP. HardFault panic monitor uses PANIC. */
extern volatile boot_phase_t g_boot_phase;

#endif /* OPEN_FIRMWARE_BOOT_PHASE_H */
