#ifndef SIM_MCU_H
#define SIM_MCU_H

#include <stddef.h>
#include <stdint.h>

typedef struct sim_mcu sim_mcu_t;

sim_mcu_t *sim_mcu_create(void);
void sim_mcu_destroy(sim_mcu_t *s);

void sim_mcu_reset(sim_mcu_t *s);
void sim_mcu_step(sim_mcu_t *s, uint32_t dt_ms);

uint32_t sim_mcu_read32(sim_mcu_t *s, uint32_t addr);
void sim_mcu_write32(sim_mcu_t *s, uint32_t addr, uint32_t value);

void sim_mcu_gpio_set_input(sim_mcu_t *s, char port, uint8_t pin, uint8_t level);
uint16_t sim_mcu_gpio_get_idr(sim_mcu_t *s, char port);
void sim_mcu_adc_set_channel(sim_mcu_t *s, uint8_t ch, uint16_t value);

size_t sim_mcu_uart_pop_tx(sim_mcu_t *s, int uart, uint8_t *out, size_t cap);
size_t sim_mcu_uart_push_rx(sim_mcu_t *s, int uart, const uint8_t *data, size_t len);

void sim_mcu_spi_flash_write(sim_mcu_t *s, uint32_t addr, const uint8_t *data, size_t len);
void sim_mcu_spi_flash_read(sim_mcu_t *s, uint32_t addr, uint8_t *out, size_t len);

#endif
