# OEM LCD + asset notes (IDA RE, reference-only)

This document captures *structural* facts learned from the OEM combined image
(`BC280_Combined_Firmware_3.3.6_4.2.5.bin`) in IDA.

**Important:** we do **not** copy OEM pixel assets/blobs into this repo’s source tree.
This repo uses OEM information only for compatibility and for designing
equivalent open tooling/implementations.

## LCD geometry

- Panel is **240×320** (portrait).
- OEM code sets full window to `(x=0,y=0,w=240,h=320)` and loops
  `row < 320`, `col < 240` in initialization/test fills.

## Bootloader font

The bootloader has a tiny text renderer:

- `lcd_draw_character(x, y, ch, color)` draws **6×16** monochrome glyphs.
- It calls `get_character_bitmap(ch)` which accepts ASCII **32..126** and
  returns a pointer to the glyph bitmap.
- The per-glyph pointer table lives in flash, but points into **SRAM**
  (e.g. base around `0x20000028`), implying the bootloader populates/unpacks
  the glyph bitmaps into RAM at runtime.

Practical implication for open firmware:
- Don’t assume the OEM’s font is directly stored in flash as a flat table.
- If we want a high-quality font, shipping our own compact font atlas is
  simpler (we use TX‑02 as a 4‑bit alpha atlas).

## App sprites / images

The application contains routines to draw full-color RGB565 images from
external storage:

- `lcd_blit_rgb565_from_spi_flash(x, y, w, h, flash_addr)` sets a window and
  reads pixel data from external SPI flash to LCD GRAM via DMA in chunks.
- `flash_addr` is a byte address within the external SPI-flash asset store.

Practical implication for open firmware:
- Sprites are feasible if we keep an **asset budget** and stream/decompress
  line-by-line (no full-screen buffers in SRAM).

## OEM “LZ/RLE” fixed-length decompressor (format class)

The app includes a small custom decompressor (also present in bootloader):

- Produces a fixed number of output bytes (`dst_len`).
- Token byte encodes:
  - literal run length (`token & 0x7`, extended via next byte when 0),
  - run/backref length (`token >> 4`, extended via next byte when 0),
  - mode bit (`token & 0x8`): backref vs zero-run.

We can adopt a similar *format class* for **our own** assets (sprites/fonts),
without copying any OEM payload data.
