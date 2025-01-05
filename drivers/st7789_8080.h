#ifndef OPEN_FIRMWARE_DRIVERS_ST7789_8080_H
#define OPEN_FIRMWARE_DRIVERS_ST7789_8080_H

#include <stdint.h>

typedef struct {
    void (*write_cmd)(uint8_t cmd);
    void (*write_data)(uint8_t data);
    void (*write_data16)(uint16_t data);
    void (*delay_ms)(uint32_t ms);
} st7789_8080_bus_t;

void st7789_8080_init_oem(const st7789_8080_bus_t *bus);
void st7789_8080_set_address_window(const st7789_8080_bus_t *bus,
                                    uint16_t x0, uint16_t y0,
                                    uint16_t x1, uint16_t y1);
void st7789_8080_write_pixels(const st7789_8080_bus_t *bus,
                              const uint16_t *pixels,
                              uint32_t count);
void st7789_8080_fill_rect(const st7789_8080_bus_t *bus,
                           uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h,
                           uint16_t color);

#endif
