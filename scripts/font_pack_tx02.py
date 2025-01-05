#!/usr/bin/env python3
import argparse
import math
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ASCII_32_126 = "".join(chr(c) for c in range(32, 127))
BASIC_SET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,:;!?/\\+-_*%()[]{}<>#@=\"'"


def next_pow2(v):
    p = 1
    while p < v:
        p <<= 1
    return p


def pack_glyphs(glyphs, max_w):
    x = 0
    y = 0
    row_h = 0
    placements = {}
    for ch, g in glyphs:
        gw = g['w']
        gh = g['h']
        if gw == 0 or gh == 0:
            placements[ch] = (0, 0)
            continue
        if x + gw > max_w:
            x = 0
            y += row_h + 1
            row_h = 0
        placements[ch] = (x, y)
        x += gw + 1
        row_h = max(row_h, gh)
    atlas_h = y + row_h
    return placements, atlas_h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--font', required=True, help='Path to TX-02 font file (.otf/.ttf)')
    ap.add_argument('--size', type=int, default=14, help='Font size in px')
    ap.add_argument('--charset', choices=['ascii', 'basic', 'custom'], default='ascii')
    ap.add_argument('--text', help='Custom charset when --charset=custom')
    ap.add_argument('--out', required=True, help='Output base path (without extension)')
    ap.add_argument('--atlas-max-width', type=int, default=256)
    ap.add_argument('--preview', help='PNG preview output path')
    args = ap.parse_args()

    if args.charset == 'ascii':
        charset = ASCII_32_126
    elif args.charset == 'basic':
        charset = BASIC_SET
    else:
        if not args.text:
            raise SystemExit('custom charset requires --text')
        charset = args.text

    font = ImageFont.truetype(args.font, args.size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    glyphs = []
    for ch in charset:
        # Important: use a baseline anchor so y offsets are negative above the
        # baseline (the common convention in bitmap font rendering).
        bbox = font.getbbox(ch, anchor='ls')
        if bbox is None:
            bbox = (0, 0, 0, 0)
        x0, y0, x1, y1 = bbox
        w = max(0, x1 - x0)
        h = max(0, y1 - y0)
        try:
            advance = font.getlength(ch)
        except Exception:
            advance = font.getsize(ch)[0]
        g = {
            'w': w,
            'h': h,
            'xoff': x0,
            'yoff': y0,
            'xadv': int(round(advance)),
            'img': None,
        }
        if w > 0 and h > 0:
            img = Image.new('L', (w, h), 0)
            draw = ImageDraw.Draw(img)
            draw.text((-x0, -y0), ch, fill=255, font=font, anchor='ls')
            g['img'] = img
        glyphs.append((ch, g))

    max_w = min(next_pow2(args.atlas_max_width), 1024)
    placements, atlas_h = pack_glyphs(glyphs, max_w)
    atlas_w = max_w
    atlas_h = max(1, atlas_h)

    atlas = Image.new('L', (atlas_w, atlas_h), 0)
    for ch, g in glyphs:
        if g['img'] is None:
            continue
        x, y = placements[ch]
        atlas.paste(g['img'], (x, y))

    if args.preview:
        Path(args.preview).parent.mkdir(parents=True, exist_ok=True)
        atlas.save(args.preview)

    data = list(atlas.getdata())
    packed = bytearray()
    for i in range(0, len(data), 2):
        a0 = data[i] >> 4
        a1 = (data[i + 1] >> 4) if i + 1 < len(data) else 0
        packed.append((a0 << 4) | (a1 & 0x0F))

    out_base = Path(args.out)
    c_path = out_base.with_suffix('.c')
    h_path = out_base.with_suffix('.h')

    first = 32
    last = 126

    with open(h_path, 'w', encoding='ascii') as f:
        f.write('#ifndef UI_FONT_TX02_H\n')
        f.write('#define UI_FONT_TX02_H\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write('#define UI_FONT_TX02_FIRST %d\n' % first)
        f.write('#define UI_FONT_TX02_LAST %d\n' % last)
        f.write('#define UI_FONT_TX02_COUNT %d\n' % (last - first + 1))
        f.write('#define UI_FONT_TX02_ATLAS_W %d\n' % atlas_w)
        f.write('#define UI_FONT_TX02_ATLAS_H %d\n' % atlas_h)
        f.write('#define UI_FONT_TX02_ASCENT %d\n' % ascent)
        f.write('#define UI_FONT_TX02_DESCENT %d\n' % descent)
        f.write('#define UI_FONT_TX02_LINE_HEIGHT %d\n' % line_height)
        f.write('#define UI_FONT_TX02_ADV_X %d\n\n' % max(1, args.size // 2))
        f.write('typedef struct {\n')
        f.write('    uint16_t x;\n')
        f.write('    uint16_t y;\n')
        f.write('    uint8_t w;\n')
        f.write('    uint8_t h;\n')
        f.write('    int8_t xoff;\n')
        f.write('    int8_t yoff;\n')
        f.write('    uint8_t xadv;\n')
        f.write('} ui_font_tx02_glyph_t;\n\n')
        f.write('extern const ui_font_tx02_glyph_t g_ui_font_tx02_glyphs[UI_FONT_TX02_COUNT];\n')
        f.write('extern const uint8_t g_ui_font_tx02_alpha[];\n\n')
        f.write('static inline const ui_font_tx02_glyph_t *ui_font_tx02_glyph(char c) {\n')
        f.write('    if ((uint8_t)c < UI_FONT_TX02_FIRST || (uint8_t)c > UI_FONT_TX02_LAST)\n')
        f.write('        return &g_ui_font_tx02_glyphs[0];\n')
        f.write('    return &g_ui_font_tx02_glyphs[(uint8_t)c - UI_FONT_TX02_FIRST];\n')
        f.write('}\n\n')
        f.write('static inline uint8_t ui_font_tx02_alpha_at(uint16_t x, uint16_t y) {\n')
        f.write('    uint32_t idx = (uint32_t)y * (uint32_t)UI_FONT_TX02_ATLAS_W + x;\n')
        f.write('    uint8_t b = g_ui_font_tx02_alpha[idx >> 1];\n')
        f.write('    return (idx & 1u) ? (b & 0x0Fu) : (uint8_t)(b >> 4);\n')
        f.write('}\n\n')
        f.write('#endif\n')

    with open(c_path, 'w', encoding='ascii') as f:
        f.write('#include "ui_font_tx02.h"\n\n')
        f.write('const ui_font_tx02_glyph_t g_ui_font_tx02_glyphs[UI_FONT_TX02_COUNT] = {\n')
        for ch, g in glyphs:
            if ord(ch) < first or ord(ch) > last:
                continue
            x, y = placements[ch]
            f.write('    { %d, %d, %d, %d, %d, %d, %d }, /* %r */\n' % (
                x, y, g['w'], g['h'], g['xoff'], g['yoff'], g['xadv'], ch
            ))
        f.write('};\n\n')
        f.write('const uint8_t g_ui_font_tx02_alpha[] = {\n')
        for i, b in enumerate(packed):
            if i % 16 == 0:
                f.write('    ')
            f.write('0x%02X,' % b)
            if i % 16 == 15:
                f.write('\n')
        if len(packed) % 16 != 0:
            f.write('\n')
        f.write('};\n')

    print('Wrote:', c_path, h_path)
    if args.preview:
        print('Preview:', args.preview)


if __name__ == '__main__':
    main()
