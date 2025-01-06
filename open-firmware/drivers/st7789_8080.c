#include "drivers/st7789_8080.h"

#define ST7789_CMD_SWRESET  0x01u
#define ST7789_CMD_SLPOUT   0x11u
#define ST7789_CMD_MADCTL   0x36u
#define ST7789_CMD_COLMOD   0x3Au
#define ST7789_CMD_INVON    0x21u
#define ST7789_CMD_CASET    0x2Au
#define ST7789_CMD_RASET    0x2Bu
#define ST7789_CMD_RAMWR    0x2Cu
#define ST7789_CMD_PORCTRL  0xB2u  /* Porch setting (back/front porch). */
#define ST7789_CMD_GCTRL    0xB7u  /* Gate control (VGH/VGL). */
#define ST7789_CMD_VCOMS    0xBBu  /* VCOMS setting. */
#define ST7789_CMD_LCMCTRL  0xC0u  /* LCM control. */
#define ST7789_CMD_VDVVRHEN 0xC2u  /* VDV/VRH command enable. */
#define ST7789_CMD_VRHS     0xC3u  /* VRH set. */
#define ST7789_CMD_VDVS     0xC4u  /* VDV set. */
#define ST7789_CMD_FRCTRL2  0xC6u  /* Frame rate control (normal mode). */
#define ST7789_CMD_PWCTRL1  0xD0u  /* Power control 1. */
#define ST7789_CMD_SPI2EN   0xE7u  /* 2-lane SPI enable. */
#define ST7789_CMD_EQCTRL   0xE9u  /* Equalize time control. */
#define ST7789_CMD_GMCTRP1  0xE0u  /* Gamma +. */
#define ST7789_CMD_GMCTRN1  0xE1u  /* Gamma -. */
#define ST7789_CMD_DISPON   0x29u

static void st7789_delay_ms(const st7789_8080_bus_t *bus, uint32_t ms)
{
    if (bus && bus->delay_ms)
        bus->delay_ms(ms);
}

static void st7789_write_cmd(const st7789_8080_bus_t *bus, uint8_t cmd)
{
    bus->write_cmd(cmd);
}

static void st7789_write_data(const st7789_8080_bus_t *bus, uint8_t data)
{
    bus->write_data(data);
}

static void st7789_write_cmd_data(const st7789_8080_bus_t *bus,
                                  uint8_t cmd,
                                  const uint8_t *data,
                                  uint8_t len)
{
    st7789_write_cmd(bus, cmd);
    for (uint8_t i = 0; i < len; ++i)
        st7789_write_data(bus, data[i]);
}

static void st7789_write_u16be(const st7789_8080_bus_t *bus, uint16_t value)
{
    st7789_write_data(bus, (uint8_t)(value >> 8));
    st7789_write_data(bus, (uint8_t)(value & 0xFFu));
}

void st7789_8080_set_address_window(const st7789_8080_bus_t *bus,
                                    uint16_t x0, uint16_t y0,
                                    uint16_t x1, uint16_t y1)
{
    st7789_write_cmd(bus, ST7789_CMD_CASET);
    st7789_write_u16be(bus, x0);
    st7789_write_u16be(bus, x1);

    st7789_write_cmd(bus, ST7789_CMD_RASET);
    st7789_write_u16be(bus, y0);
    st7789_write_u16be(bus, y1);

    st7789_write_cmd(bus, ST7789_CMD_RAMWR);
}

void st7789_8080_init_oem(const st7789_8080_bus_t *bus)
{
    const uint8_t madctl = 0x00u; /* RGB order, no row/column swap. */
    const uint8_t colmod = 0x05u; /* 16-bit (RGB565). */
    const uint8_t porctrl[] = {0x0Cu, 0x0Cu, 0x00u, 0x33u, 0x33u}; /* BPA/FPA + idle porch. */
    const uint8_t gctrl = 0x35u; /* Gate voltage (VGH/VGL). */
    const uint8_t vcoms = 0x2Au; /* VCOMS level. */
    const uint8_t lcmctrl = 0x2Cu; /* LCM control overrides. */
    const uint8_t vdvvrhen = 0x01u; /* Enable VDV/VRH command values. */
    const uint8_t vrhs = 0x05u; /* VRH (VAP/GVDD). */
    const uint8_t vdvs = 0x20u; /* VDV. */
    const uint8_t frctrl2 = 0x0Fu; /* Frame rate control (normal mode). */
    const uint8_t pwctrl1[] = {0xA4u, 0xA1u}; /* AVDD/AVCL/VDDS. */
    const uint8_t eqctrl[] = {0x11u, 0x11u, 0x03u}; /* Source/gate equalize timing. */
    const uint8_t gamma_p[] = {0xF0u, 0x09u, 0x13u, 0x0Au, 0x0Bu, 0x06u,
                               0x38u, 0x33u, 0x4Fu, 0x04u, 0x0Du, 0x19u,
                               0x2Eu, 0x2Fu};
    const uint8_t gamma_n[] = {0xF0u, 0x09u, 0x13u, 0x0Au, 0x0Bu, 0x06u,
                               0x38u, 0x33u, 0x4Fu, 0x04u, 0x0Du, 0x19u,
                               0x2Eu, 0x2Fu};

    st7789_write_cmd(bus, ST7789_CMD_SLPOUT);
    st7789_delay_ms(bus, 2u);

    st7789_write_cmd_data(bus, ST7789_CMD_MADCTL, &madctl, 1u);
    st7789_write_cmd_data(bus, ST7789_CMD_COLMOD, &colmod, 1u);
    const uint8_t spi2en = 0x00u;

    st7789_write_cmd(bus, ST7789_CMD_INVON);
    st7789_write_cmd_data(bus, ST7789_CMD_SPI2EN, &spi2en, 1u);

    st7789_write_cmd(bus, ST7789_CMD_CASET);
    st7789_write_u16be(bus, 0u);
    st7789_write_u16be(bus, 0x00EFu);

    st7789_write_cmd(bus, ST7789_CMD_RASET);
    st7789_write_u16be(bus, 0u);
    st7789_write_u16be(bus, 0x00EFu);

    st7789_write_cmd_data(bus, ST7789_CMD_PORCTRL, porctrl, (uint8_t)sizeof(porctrl));
    st7789_write_cmd_data(bus, ST7789_CMD_GCTRL, &gctrl, 1u);
    st7789_write_cmd_data(bus, ST7789_CMD_VCOMS, &vcoms, 1u);
    st7789_write_cmd_data(bus, ST7789_CMD_LCMCTRL, &lcmctrl, 1u);
    st7789_write_cmd_data(bus, ST7789_CMD_VDVVRHEN, &vdvvrhen, 1u);
    st7789_write_cmd_data(bus, ST7789_CMD_VRHS, &vrhs, 1u);
    st7789_write_cmd_data(bus, ST7789_CMD_VDVS, &vdvs, 1u);
    st7789_write_cmd_data(bus, ST7789_CMD_FRCTRL2, &frctrl2, 1u);
    st7789_write_cmd_data(bus, ST7789_CMD_PWCTRL1, pwctrl1, (uint8_t)sizeof(pwctrl1));
    st7789_write_cmd_data(bus, ST7789_CMD_EQCTRL, eqctrl, (uint8_t)sizeof(eqctrl));
    st7789_write_cmd_data(bus, ST7789_CMD_GMCTRP1, gamma_p, (uint8_t)sizeof(gamma_p));
    st7789_write_cmd_data(bus, ST7789_CMD_GMCTRN1, gamma_n, (uint8_t)sizeof(gamma_n));

    st7789_write_cmd(bus, ST7789_CMD_DISPON);
}
