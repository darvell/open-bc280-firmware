#!/usr/bin/env python3
"""Generate compact 1-bit bitmap font atlas for embedded use.

Produces C header and source with packed glyph data for multiple sizes.
Target: ~4-8KB for ASCII printable characters across all sizes.

Usage:
    python font_bitmap_gen.py --out gfx/ui_font_bitmap
    python font_bitmap_gen.py --font /path/to/font.ttf --out gfx/ui_font_bitmap
"""

import argparse
import os
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("PIL not found. Install with: pip install Pillow")
    raise SystemExit(1)

# ASCII printable range (32-126)
ASCII_PRINTABLE = "".join(chr(c) for c in range(32, 127))

# Font size configurations
FONT_SIZES = {
    'LARGE':  {'px': 28, 'desc': 'Large digits (speed display)'},
    'HEADER': {'px': 18, 'desc': 'Section headers'},
    'BODY':   {'px': 12, 'desc': 'Stats, values, general text'},
    'SMALL':  {'px': 9,  'desc': 'Units, fine print'},
}


def render_glyph(font, char, threshold=128):
    """Render a single glyph and return 1-bit bitmap data."""
    # Get bounding box
    bbox = font.getbbox(char, anchor='la')
    if bbox is None:
        return {'w': 0, 'h': 0, 'xoff': 0, 'yoff': 0, 'xadv': 0, 'bits': []}

    x0, y0, x1, y1 = bbox
    w = max(0, x1 - x0)
    h = max(0, y1 - y0)

    try:
        advance = int(round(font.getlength(char)))
    except Exception:
        advance = font.getsize(char)[0]

    if w == 0 or h == 0:
        return {'w': 0, 'h': 0, 'xoff': 0, 'yoff': y0, 'xadv': advance, 'bits': []}

    # Render to grayscale image
    img = Image.new('L', (w, h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((-x0, -y0), char, fill=255, font=font, anchor='la')

    # Convert to 1-bit: collect row data
    bits = []
    for row in range(h):
        row_bits = 0
        for col in range(w):
            px = img.getpixel((col, row))
            if px >= threshold:
                row_bits |= (1 << (w - 1 - col))
        bits.append(row_bits)

    return {
        'w': w,
        'h': h,
        'xoff': x0,
        'yoff': y0,
        'xadv': advance,
        'bits': bits,
    }


def pack_glyphs(glyphs):
    """Pack all glyph bitmaps into a single byte array.

    Format: Each glyph's rows are stored sequentially.
    Row width determines bytes per row: ceil(w/8) bytes per row.
    """
    packed = bytearray()
    offsets = []

    for char, g in glyphs:
        offsets.append(len(packed))
        w = g['w']
        h = g['h']
        if w == 0 or h == 0:
            continue

        bytes_per_row = (w + 7) // 8
        for row_bits in g['bits']:
            # Pack row bits into bytes (MSB first)
            for b in range(bytes_per_row):
                shift = (bytes_per_row - 1 - b) * 8
                byte_val = (row_bits >> shift) & 0xFF
                packed.append(byte_val)

    return packed, offsets


def load_font(font_path, size):
    """Load a font at the specified size."""
    if font_path:
        return ImageFont.truetype(font_path, size)

    # Try system monospace fonts
    for name in ['DejaVuSansMono.ttf', 'LiberationMono-Regular.ttf',
                 'Menlo.ttc', 'Monaco.dfont', 'Courier New.ttf']:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue

    # Fallback to default
    try:
        return ImageFont.load_default()
    except Exception:
        return None


def generate_size(font_path, size_name, size_px, threshold):
    """Generate glyph data for a single font size."""
    font = load_font(font_path, size_px)
    if font is None:
        return None

    # Get font metrics
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    # Render all glyphs
    glyphs = []
    for char in ASCII_PRINTABLE:
        g = render_glyph(font, char, threshold)
        glyphs.append((char, g))

    # Pack glyph data
    packed, offsets = pack_glyphs(glyphs)

    return {
        'name': size_name,
        'px': size_px,
        'ascent': ascent,
        'descent': descent,
        'line_height': line_height,
        'glyphs': glyphs,
        'offsets': offsets,
        'packed': packed,
    }


def main():
    ap = argparse.ArgumentParser(description='Generate multi-size 1-bit bitmap font atlas')
    ap.add_argument('--font', help='Path to TTF/OTF font (default: system monospace)')
    ap.add_argument('--threshold', type=int, default=128, help='Binarization threshold (default: 128)')
    ap.add_argument('--out', required=True, help='Output base path (without extension)')
    ap.add_argument('--preview', help='Optional PNG preview output')
    args = ap.parse_args()

    # Generate all sizes
    sizes = {}
    total_bitmap_bytes = 0
    for name, cfg in FONT_SIZES.items():
        print(f"Generating {name} ({cfg['px']}px)...")
        data = generate_size(args.font, name, cfg['px'], args.threshold)
        if data is None:
            print(f"  ERROR: Could not load font for {name}")
            continue
        sizes[name] = data
        total_bitmap_bytes += len(data['packed'])
        print(f"  → {len(data['packed'])} bytes, ascent={data['ascent']}, line_height={data['line_height']}")

    if not sizes:
        print("ERROR: No fonts generated. Specify --font path.")
        return 1

    # Generate C files
    out_base = Path(args.out)
    out_base.parent.mkdir(parents=True, exist_ok=True)
    c_path = out_base.with_suffix('.c')
    h_path = out_base.with_suffix('.h')

    # Header file
    with open(h_path, 'w') as f:
        f.write('#ifndef UI_FONT_BITMAP_H\n')
        f.write('#define UI_FONT_BITMAP_H\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write('#define UI_FONT_FIRST 32\n')
        f.write('#define UI_FONT_LAST 126\n')
        f.write('#define UI_FONT_COUNT 95\n\n')

        # Size enum
        f.write('/* Font size identifiers */\n')
        f.write('typedef enum {\n')
        for i, name in enumerate(FONT_SIZES.keys()):
            desc = FONT_SIZES[name]['desc']
            f.write(f'    UI_FONT_{name} = {i},  /* {FONT_SIZES[name]["px"]}px - {desc} */\n')
        f.write(f'    UI_FONT_COUNT_SIZES = {len(FONT_SIZES)}\n')
        f.write('} ui_font_size_t;\n\n')

        # Per-size metrics
        f.write('/* Per-size font metrics */\n')
        for name, data in sizes.items():
            prefix = f'UI_FONT_{name}'
            f.write(f'#define {prefix}_ASCENT {data["ascent"]}\n')
            f.write(f'#define {prefix}_DESCENT {data["descent"]}\n')
            f.write(f'#define {prefix}_LINE_HEIGHT {data["line_height"]}\n')
        f.write('\n')

        # Glyph struct
        f.write('typedef struct {\n')
        f.write('    uint16_t offset;  /* Byte offset into bitmap data */\n')
        f.write('    uint8_t w;        /* Width in pixels */\n')
        f.write('    uint8_t h;        /* Height in pixels */\n')
        f.write('    int8_t xoff;      /* X offset from cursor */\n')
        f.write('    int8_t yoff;      /* Y offset from baseline */\n')
        f.write('    uint8_t xadv;     /* X advance to next glyph */\n')
        f.write('} ui_font_glyph_t;\n\n')

        # Font data struct
        f.write('typedef struct {\n')
        f.write('    const ui_font_glyph_t *glyphs;\n')
        f.write('    const uint8_t *bits;\n')
        f.write('    uint8_t ascent;\n')
        f.write('    uint8_t descent;\n')
        f.write('    uint8_t line_height;\n')
        f.write('} ui_font_data_t;\n\n')

        # Extern declarations
        f.write('extern const ui_font_data_t g_ui_fonts[UI_FONT_COUNT_SIZES];\n\n')

        # Inline glyph lookup
        f.write('static inline const ui_font_glyph_t *ui_font_glyph(ui_font_size_t size, char c) {\n')
        f.write('    if (size >= UI_FONT_COUNT_SIZES)\n')
        f.write('        size = UI_FONT_BODY;\n')
        f.write('    const ui_font_data_t *fd = &g_ui_fonts[size];\n')
        f.write('    if ((uint8_t)c < UI_FONT_FIRST || (uint8_t)c > UI_FONT_LAST)\n')
        f.write('        return &fd->glyphs[0]; /* space */\n')
        f.write('    return &fd->glyphs[(uint8_t)c - UI_FONT_FIRST];\n')
        f.write('}\n\n')

        # Get font data
        f.write('static inline const ui_font_data_t *ui_font_get(ui_font_size_t size) {\n')
        f.write('    if (size >= UI_FONT_COUNT_SIZES)\n')
        f.write('        size = UI_FONT_BODY;\n')
        f.write('    return &g_ui_fonts[size];\n')
        f.write('}\n\n')

        # Text width calculation
        f.write('static inline uint16_t ui_font_text_width(ui_font_size_t size, const char *text) {\n')
        f.write('    uint16_t w = 0;\n')
        f.write('    while (text && *text) {\n')
        f.write('        const ui_font_glyph_t *g = ui_font_glyph(size, *text++);\n')
        f.write('        w += g->xadv;\n')
        f.write('    }\n')
        f.write('    return w;\n')
        f.write('}\n\n')

        # Backward compat aliases (for existing code using ui_font_bitmap_*)
        f.write('/* Backward compatibility aliases */\n')
        f.write('#define ui_font_bitmap_glyph_t ui_font_glyph_t\n')
        f.write('#define ui_font_bitmap_glyph(c) ui_font_glyph(UI_FONT_BODY, c)\n')
        f.write('#define ui_font_bitmap_text_width(t) ui_font_text_width(UI_FONT_BODY, t)\n')
        f.write('#define UI_FONT_BITMAP_ASCENT UI_FONT_BODY_ASCENT\n')
        f.write('#define UI_FONT_BITMAP_DESCENT UI_FONT_BODY_DESCENT\n')
        f.write('#define UI_FONT_BITMAP_LINE_HEIGHT UI_FONT_BODY_LINE_HEIGHT\n\n')

        # Callback types for rendering
        f.write('/* Callback types for rendering */\n')
        f.write('typedef void (*ui_font_plot_fn)(int x, int y, uint16_t color, void *user);\n')
        f.write('typedef void (*ui_font_rect_fn)(int x, int y, int w, int h, uint16_t color, void *user);\n\n')

        # Draw text function (size-aware)
        f.write('/* Draw text at (x, y) with foreground and background colors.\n')
        f.write(' * y is the baseline position. */\n')
        f.write('void ui_font_draw_text(ui_font_plot_fn plot,\n')
        f.write('                       ui_font_rect_fn rect,\n')
        f.write('                       void *user,\n')
        f.write('                       int x, int y,\n')
        f.write('                       const char *text,\n')
        f.write('                       ui_font_size_t size,\n')
        f.write('                       uint16_t fg,\n')
        f.write('                       uint16_t bg);\n\n')

        # Backward compat draw function
        f.write('/* Backward compatibility wrapper (uses BODY size) */\n')
        f.write('void ui_font_bitmap_draw_text(ui_font_plot_fn plot,\n')
        f.write('                              ui_font_rect_fn rect,\n')
        f.write('                              void *user,\n')
        f.write('                              int x, int y,\n')
        f.write('                              const char *text,\n')
        f.write('                              uint16_t fg,\n')
        f.write('                              uint16_t bg);\n\n')

        f.write('#endif /* UI_FONT_BITMAP_H */\n')

    # Source file
    with open(c_path, 'w') as f:
        f.write('#include "ui_font_bitmap.h"\n\n')

        # Per-size glyph tables and bitmap data
        for name, data in sizes.items():
            prefix = name.lower()

            # Glyph metrics table
            f.write(f'static const ui_font_glyph_t g_font_{prefix}_glyphs[UI_FONT_COUNT] = {{\n')
            for i, (char, g) in enumerate(data['glyphs']):
                offset = data['offsets'][i] if i < len(data['offsets']) else 0
                char_repr = repr(char) if char not in ('"', '\\') else f"'{char}'"
                f.write(f'    {{ {offset:5d}, {g["w"]:2d}, {g["h"]:2d}, {g["xoff"]:3d}, {g["yoff"]:3d}, {g["xadv"]:2d} }}, /* {char_repr} */\n')
            f.write('};\n\n')

            # Packed bitmap data
            f.write(f'static const uint8_t g_font_{prefix}_bits[] = {{\n')
            for i, b in enumerate(data['packed']):
                if i % 16 == 0:
                    f.write('    ')
                f.write(f'0x{b:02X},')
                if i % 16 == 15:
                    f.write('\n')
            if len(data['packed']) % 16 != 0:
                f.write('\n')
            f.write('};\n\n')

        # Font data array
        f.write('const ui_font_data_t g_ui_fonts[UI_FONT_COUNT_SIZES] = {\n')
        for name, data in sizes.items():
            prefix = name.lower()
            f.write(f'    [UI_FONT_{name}] = {{\n')
            f.write(f'        .glyphs = g_font_{prefix}_glyphs,\n')
            f.write(f'        .bits = g_font_{prefix}_bits,\n')
            f.write(f'        .ascent = {data["ascent"]},\n')
            f.write(f'        .descent = {data["descent"]},\n')
            f.write(f'        .line_height = {data["line_height"]},\n')
            f.write('    },\n')
        f.write('};\n\n')

        # Draw glyph helper
        f.write('static void ui_font_draw_glyph(ui_font_plot_fn plot,\n')
        f.write('                               ui_font_rect_fn rect,\n')
        f.write('                               void *user,\n')
        f.write('                               int x, int y,\n')
        f.write('                               const ui_font_glyph_t *g,\n')
        f.write('                               const uint8_t *bits,\n')
        f.write('                               uint16_t fg,\n')
        f.write('                               uint16_t bg)\n')
        f.write('{\n')
        f.write('    if (!g || g->w == 0 || g->h == 0)\n')
        f.write('        return;\n')
        f.write('    const int bytes_per_row = (g->w + 7) / 8;\n')
        f.write('    const uint8_t *data = &bits[g->offset];\n')
        f.write('    int gx = x + g->xoff;\n')
        f.write('    int gy = y + g->yoff;\n')
        f.write('    /* Draw background rect if rect callback provided */\n')
        f.write('    if (rect && bg != 0)\n')
        f.write('        rect(gx, gy, g->w, g->h, bg, user);\n')
        f.write('    /* Draw foreground pixels */\n')
        f.write('    for (int row = 0; row < g->h; ++row) {\n')
        f.write('        for (int col = 0; col < g->w; ++col) {\n')
        f.write('            int byte_idx = row * bytes_per_row + (col / 8);\n')
        f.write('            int bit_idx = 7 - (col % 8);\n')
        f.write('            if (data[byte_idx] & (1 << bit_idx))\n')
        f.write('                plot(gx + col, gy + row, fg, user);\n')
        f.write('        }\n')
        f.write('    }\n')
        f.write('}\n\n')

        # Size-aware draw text
        f.write('void ui_font_draw_text(ui_font_plot_fn plot,\n')
        f.write('                       ui_font_rect_fn rect,\n')
        f.write('                       void *user,\n')
        f.write('                       int x, int y,\n')
        f.write('                       const char *text,\n')
        f.write('                       ui_font_size_t size,\n')
        f.write('                       uint16_t fg,\n')
        f.write('                       uint16_t bg)\n')
        f.write('{\n')
        f.write('    if (!text || !plot)\n')
        f.write('        return;\n')
        f.write('    const ui_font_data_t *fd = ui_font_get(size);\n')
        f.write('    int cursor_x = x;\n')
        f.write('    for (const char *p = text; *p; ++p) {\n')
        f.write('        const ui_font_glyph_t *g = ui_font_glyph(size, *p);\n')
        f.write('        ui_font_draw_glyph(plot, rect, user, cursor_x, y, g, fd->bits, fg, bg);\n')
        f.write('        cursor_x += g->xadv;\n')
        f.write('    }\n')
        f.write('}\n\n')

        # Backward compat wrapper
        f.write('void ui_font_bitmap_draw_text(ui_font_plot_fn plot,\n')
        f.write('                              ui_font_rect_fn rect,\n')
        f.write('                              void *user,\n')
        f.write('                              int x, int y,\n')
        f.write('                              const char *text,\n')
        f.write('                              uint16_t fg,\n')
        f.write('                              uint16_t bg)\n')
        f.write('{\n')
        f.write('    ui_font_draw_text(plot, rect, user, x, y, text, UI_FONT_BODY, fg, bg);\n')
        f.write('}\n')

    print(f'\nGenerated: {h_path} ({h_path.stat().st_size} bytes)')
    print(f'Generated: {c_path} ({c_path.stat().st_size} bytes)')
    print(f'Total bitmap data: {total_bitmap_bytes} bytes')

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
