# OEM_UI_NOTES — BC280 display UI (IDA findings)

These notes capture quick UI-relevant observations from the OEM combined image
(`firmware/BC280_Combined_Firmware_3.3.6_4.2.5.bin`) to align our UI + LCD
driver assumptions. This is *not* a port of OEM assets; it is behavioral
reference only.

## LCD geometry + controller
- LCD IO uses the 8080-style parallel window at `0x6000_0000` (CMD) and
  `0x6002_0000` (DATA).
- `lcd_clear_screen` (0x801A938) fills **240x320** (w=0xF0, h=0x140).
- `lcd_set_window` (0x801ABAC) uses CASET/PASET + RAMWR (0x2C).
- The init routine (`lcd_init_st7789`, renamed from `st7789_init_240x240`,
  0x801A70C) programs CASET/PASET with **0..239** for both X and Y. This is a
  mismatch with the 240x320 clear path, so it may be a stale name or a panel
  mode quirk. Empirically, all UI drawing code assumes **240x320**.

## OEM glyphs / sprites
- `ui_draw_glyph_char` (0x801AC30) draws characters via a **glyph pointer
  table** (`g_ui_glyph_ptrs` in SRAM at 0x20000010).
- Each glyph image is **RGB565** in the form:
  ```
  struct GlyphImage {
      uint16_t w;
      uint16_t h;
      uint16_t pixels[w*h];
  };
  ```
  and is blitted by `lcd_blit_rgb565` (renamed from `lcd_draw_image_data`,
  0x801A5F4).
- The decimal point is a special case that uses `ui_draw_element_idx` with
  element id `0x105`, and width comes from `g_ui_elem_widths` in SRAM.

These tables appear to be **populated at runtime** (RAM addresses), which is
consistent with assets being unpacked from flash/storage rather than static
const data.

## Practical implication for open-firmware
- Keep UI and LCD assumptions at **240x320**.
- OEM uses **RGB565 sprites** and **glyph tables in RAM**; we mirror the *format
  class* (RGB565 / A4 alpha + row-compressed) with a tight budget and tinting
  to avoid OEM asset reuse.

## ST7789V init command meanings (OEM v2.2.8 sequence)
These notes map the OEM init bytes to datasheet terms so the driver reads like
an SDK rather than a raw table.

- `0x11` `SLPOUT`: exit sleep mode.
- `0x36` `MADCTL`: memory data access control (rotation, RGB/BGR, refresh order).
- `0x3A` `COLMOD`: color mode. `0x05` = 16-bit RGB565.
- `0x21` `INVON`: display inversion on.
- `0xE7` `SPI2EN`: 2-data-lane serial interface enable. (Not gate control.)
- `0x2A` `CASET`: column address set.
- `0x2B` `RASET` / `PASET`: row/page address set.
- `0x2C` `RAMWR`: memory write.
- `0xB2` `PORCTRL`: porch setting (back/front porch + idle/partial enable).
- `0xB7` `GCTRL`: gate voltage control (VGH/VGL).
- `0xBB` `VCOMS`: VCOMS setting.
- `0xC0` `LCMCTRL`: LCM control (affects scan/inversion behavior).
- `0xC2` `VDVVRHEN`: enable command-based VDV/VRH.
- `0xC3` `VRHS`: VRH set (VAP/GVDD).
- `0xC4` `VDVS`: VDV set. (VCMOFSET is `0xC5` on ST7789V.)
- `0xC6` `FRCTRL2`: frame rate control (normal mode).
- `0xD0` `PWCTRL1`: power control 1 (AVDD/AVCL/VDDS).
- `0xE9` `EQCTRL`: equalize time control (source/gate timing).
- `0xE0` `GMCTRP1`: positive gamma correction.
- `0xE1` `GMCTRN1`: negative gamma correction.
- `0x29` `DISPON`: display on.
