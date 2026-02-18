#ifndef APP_H
#define APP_H

#include <stdint.h>

/* Shared acknowledgement timeout for UI alert popups. */
#define UI_ALERT_ACK_MS 5000u

void app_main_loop(void) __attribute__((noreturn));
void app_process_time(void);
void app_process_events(void);
void app_apply_inputs(void);
void app_process_periodic(void);
void app_update_ui(void);
void app_housekeeping(void);
void watchdog_feed_runtime(void);

#endif /* APP_H */
