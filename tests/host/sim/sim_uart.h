#ifndef SIM_UART_H
#define SIM_UART_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SIM_UART1 = 0,
    SIM_UART2 = 1,
    SIM_UART4 = 2,
    SIM_UART_MAX = 3
} sim_uart_port_t;

void sim_uart_init(void);
void sim_uart_rx_push(sim_uart_port_t port, const uint8_t *data, size_t len);
int sim_uart_rx_pop(sim_uart_port_t port, uint8_t *out);
void sim_uart_tx_write(sim_uart_port_t port, const uint8_t *data, size_t len);
size_t sim_uart_tx_size(sim_uart_port_t port);
size_t sim_uart_tx_read(sim_uart_port_t port, uint8_t *out, size_t max_len);

#endif
