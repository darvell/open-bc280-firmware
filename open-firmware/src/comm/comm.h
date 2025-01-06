#ifndef COMM_H
#define COMM_H

#include <stdint.h>
#include <stddef.h>

#include "comm_proto.h"
#define LOG_FRAME_CMD 0x7Du
#define LOG_FRAME_MAX 64u
#define BLE_HACKER_MAX_PAYLOAD (COMM_MAX_PAYLOAD - 3u)

/* Port indices */
#define PORT_BLE    0
#define PORT_DEBUG  1
#define PORT_MOTOR  2

/* Command IDs */
#define CMD_PING            0x01
#define CMD_STATE_DUMP      0x02
#define CMD_CONFIG_GET      0x10
#define CMD_CONFIG_STAGE    0x11
#define CMD_CONFIG_COMMIT   0x12
#define CMD_TRIP_GET        0x20
#define CMD_TRIP_RESET      0x21
#define CMD_STREAM_START    0x30
#define CMD_STREAM_STOP     0x31

/* Frame structure */
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[COMM_MAX_PAYLOAD];
uint8_t checksum;
} comm_frame_t;

/* API declarations */
void comm_init(void);
void comm_tick(void);

void uart_write_port(int port_idx, const uint8_t *data, size_t len);
void send_frame_port(int port_idx, uint8_t cmd, const uint8_t *payload, uint8_t len);
void send_status(uint8_t cmd, uint8_t status);


int comm_handle_command(uint8_t cmd, const uint8_t *payload, uint8_t len);

extern int g_last_rx_port;

/* Stream API */
void stream_start(uint16_t period_ms);
void stream_stop(void);
void stream_tick(void);

/* Main loop helpers. */
void poll_uart_rx_ports(void);
void send_state_frame_bin(void);
void print_status(void);

#endif /* COMM_H */
