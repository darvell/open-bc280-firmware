#!/usr/bin/env python3
"""Generate compact 1-bit bitmap font atlas for embedded use.

Produces C header and source with packed glyph data.
Target: ~2-4KB for ASCII printable characters.

Usage:
    python font_bitmap_gen.py --size 12 --out gfx/ui_font_bitmap
    python font_bitmap_gen.py --font /path/to/font.ttf --size 14 --out gfx/ui_font_bitmap
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


def main():
    ap = argparse.ArgumentParser(description='Generate 1-bit bitmap font atlas')
    ap.add_argument('--font', help='Path to TTF/OTF font (default: system monospace)')
    ap.add_argument('--size', type=int, default=12, help='Font size in pixels (default: 12)')
    ap.add_argument('--threshold', type=int, default=128, help='Binarization threshold (default: 128)')
    ap.add_argument('--out', required=True, help='Output base path (without extension)')
    ap.add_argument('--preview', help='Optional PNG preview output')
    args = ap.parse_args()

    # Load font
    if args.font:
        font = ImageFont.truetype(args.font, args.size)
    else:
        # Try system monospace fonts
        for name in ['DejaVuSansMono.ttf', 'LiberationMono-Regular.ttf',
                     'Menlo.ttc', 'Monaco.dfont', 'Courier New.ttf']:
            try:
                font = ImageFont.truetype(name, args.size)
                print(f"Using system font: {name}")
                break
            except OSError:
                continue
        else:
            # Fallback to default
            try:
                font = ImageFont.load_default()
                print("Using PIL default font")
            except Exception:
                print("ERROR: No font available. Specify --font path.")
                return 1

    # Get font metrics
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    # Render all glyphs
    glyphs = []
    max_w = 0
    max_h = 0
    for char in ASCII_PRINTABLE:
        g = render_glyph(font, char, args.threshold)
        glyphs.append((char, g))
        max_w = max(max_w, g['w'])
        max_h = max(max_h, g['h'])

    # Pack glyph data
    packed, offsets = pack_glyphs(glyphs)

    # Generate preview if requested
    if args.preview:
        # Create preview image showing all glyphs
        cols = 16
        rows = (len(glyphs) + cols - 1) // cols
        cell_w = max_w + 2
        cell_h = max_h + 2
        preview = Image.new('L', (cols * cell_w, rows * cell_h), 32)
        draw = ImageDraw.Draw(preview)

        for i, (char, g) in enumerate(glyphs):
            cx = (i % cols) * cell_w + 1
            cy = (i // cols) * cell_h + 1
            if g['w'] > 0 and g['h'] > 0:
                for row in range(g['h']):
                    for col in range(g['w']):
                        if g['bits'][row] & (1 << (g['w'] - 1 - col)):
                            preview.putpixel((cx + col, cy + row), 255)

        Path(args.preview).parent.mkdir(parents=True, exist_ok=True)
        preview.save(args.preview)
        print(f"Preview: {args.preview}")

    # Generate C files
    out_base = Path(args.out)
    out_base.parent.mkdir(parents=True, exist_ok=True)
    c_path = out_base.with_suffix('.c')
    h_path = out_base.with_suffix('.h')

    guard = out_base.name.upper().replace('-', '_') + '_H'
    prefix = out_base.name.upper().replace('-', '_')

    # Header file
    with open(h_path, 'w') as f:
        f.write(f'#ifndef {guard}\n')
        f.write(f'#define {guard}\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'#define {prefix}_FIRST 32\n')
        f.write(f'#define {prefix}_LAST 126\n')
        f.write(f'#define {prefix}_COUNT 95\n')
        f.write(f'#define {prefix}_ASCENT {ascent}\n')
        f.write(f'#define {prefix}_DESCENT {descent}\n')
        f.write(f'#define {prefix}_LINE_HEIGHT {line_height}\n\n')

        f.write('typedef struct {\n')
        f.write('    uint16_t offset;  /* Byte offset into bitmap data */\n')
        f.write('    uint8_t w;        /* Width in pixels */\n')
        f.write('    uint8_t h;        /* Height in pixels */\n')
        f.write('    int8_t xoff;      /* X offset from cursor */\n')
        f.write('    int8_t yoff;      /* Y offset from baseline */\n')
        f.write('    uint8_t xadv;     /* X advance to next glyph */\n')
        f.write(f'}} {prefix.lower()}_glyph_t;\n\n')

        f.write(f'extern const {prefix.lower()}_glyph_t g_{prefix.lower()}_glyphs[{prefix}_COUNT];\n')
        f.write(f'extern const uint8_t g_{prefix.lower()}_bits[];\n\n')

        # Inline lookup function
        f.write(f'static inline const {prefix.lower()}_glyph_t *{prefix.lower()}_glyph(char c) {{\n')
        f.write(f'    if ((uint8_t)c < {prefix}_FIRST || (uint8_t)c > {prefix}_LAST)\n')
        f.write(f'        return &g_{prefix.lower()}_glyphs[0]; /* space */\n')
        f.write(f'    return &g_{prefix.lower()}_glyphs[(uint8_t)c - {prefix}_FIRST];\n')
        f.write('}\n\n')

        # Text width calculation
        f.write(f'static inline uint16_t {prefix.lower()}_text_width(const char *text) {{\n')
        f.write('    uint16_t w = 0;\n')
        f.write('    while (text && *text) {\n')
        f.write(f'        const {prefix.lower()}_glyph_t *g = {prefix.lower()}_glyph(*text++);\n')
        f.write('        w += g->xadv;\n')
        f.write('    }\n')
        f.write('    return w;\n')
        f.write('}\n\n')

        f.write(f'#endif /* {guard} */\n')

    # Source file
    with open(c_path, 'w') as f:
        f.write(f'#include "{h_path.name}"\n\n')

        # Glyph metrics table
        f.write(f'const {prefix.lower()}_glyph_t g_{prefix.lower()}_glyphs[{prefix}_COUNT] = {{\n')
        for i, (char, g) in enumerate(glyphs):
            offset = offsets[i] if i < len(offsets) else 0
            # Escape special chars for comment
            char_repr = repr(char) if char not in ('"', '\\') else f"'{char}'"
            f.write(f'    {{ {offset:5d}, {g["w"]:2d}, {g["h"]:2d}, {g["xoff"]:3d}, {g["yoff"]:3d}, {g["xadv"]:2d} }}, /* {char_repr} */\n')
        f.write('};\n\n')

        # Packed bitmap data
        f.write(f'const uint8_t g_{prefix.lower()}_bits[] = {{\n')
        for i, b in enumerate(packed):
            if i % 16 == 0:
                f.write('    ')
            f.write(f'0x{b:02X},')
            if i % 16 == 15:
                f.write('\n')
        if len(packed) % 16 != 0:
            f.write('\n')
        f.write('};\n')

    print(f'Generated: {h_path} ({h_path.stat().st_size} bytes)')
    print(f'Generated: {c_path} ({c_path.stat().st_size} bytes)')
    print(f'Bitmap data: {len(packed)} bytes')
    print(f'Glyph count: {len(glyphs)}')
    print(f'Font metrics: ascent={ascent}, descent={descent}, line_height={line_height}')

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
