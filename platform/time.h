#ifndef OPEN_FIRMWARE_PLATFORM_TIME_H
#define OPEN_FIRMWARE_PLATFORM_TIME_H

#include <stdint.h>

/* OEM timebase tick (5ms) driven by TIM2 update events. */
extern volatile uint32_t g_ms;

void platform_time_poll_1ms(void);
void platform_timebase_init_oem(void);
void platform_motor_isr_enable(void);

#endif
