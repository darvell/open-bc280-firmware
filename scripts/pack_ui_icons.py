#!/usr/bin/env python3
"""
Generate a few small UI icon sprites (A4 alpha) and emit C source.

We intentionally keep these tiny and budgeted:
- A4 = 4-bit alpha, 2 pixels/byte (good for runtime tint + AA edges).
- Row-compressed with a simple RLE token stream (literal / repeat / zero-run).

This avoids shipping big bitmap assets while still enabling smooth icons.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw


def pack_a4(img_l: Image.Image) -> list[list[int]]:
    """Return rows of packed A4 bytes (len = ceil(w/2))."""
    if img_l.mode != "L":
        img_l = img_l.convert("L")
    w, h = img_l.size
    rows: list[list[int]] = []
    px = img_l.load()
    for y in range(h):
        row = []
        for x in range(0, w, 2):
            a0 = (px[x, y] + 8) >> 4
            a1 = (px[x + 1, y] + 8) >> 4 if x + 1 < w else 0
            row.append(((a0 & 0xF) << 4) | (a1 & 0xF))
        rows.append(row)
    return rows


def rle_token(kind: int, length: int) -> int:
    assert 0 <= kind <= 3
    assert 1 <= length <= 64
    return (kind << 6) | ((length - 1) & 0x3F)


def rle_compress_row(row: list[int]) -> bytes:
    """
    Very small, deterministic compressor for a row of bytes.
    Supported tokens:
      00xxxxxx: literal run (len 1..64), followed by len bytes
      01xxxxxx: repeat run  (len 1..64), followed by 1 byte
      10xxxxxx: zero run    (len 1..64), no extra bytes
    The decoder also supports backrefs, but we keep this encoder simple.
    """
    out = bytearray()
    i = 0
    n = len(row)
    while i < n:
        # Zero run (best compression)
        if row[i] == 0:
            j = i
            while j < n and row[j] == 0 and (j - i) < 64:
                j += 1
            zr = j - i
            if zr >= 2:
                out.append(rle_token(2, zr))
                i = j
                continue

        # Repeat run
        j = i + 1
        while j < n and row[j] == row[i] and (j - i) < 64:
            j += 1
        rr = j - i
        if rr >= 3:
            out.append(rle_token(1, rr))
            out.append(row[i] & 0xFF)
            i = j
            continue

        # Literal run: accumulate until we see a compressible run or we hit 64.
        lit = []
        while i < n and len(lit) < 64:
            # Stop if a zero-run would start here.
            if row[i] == 0:
                j = i
                while j < n and row[j] == 0 and (j - i) < 64:
                    j += 1
                if (j - i) >= 2:
                    break
            # Stop if a repeat-run would start here.
            j = i + 1
            while j < n and row[j] == row[i] and (j - i) < 64:
                j += 1
            if (j - i) >= 3:
                break
            lit.append(row[i] & 0xFF)
            i += 1
        if not lit:
            # Fallback: emit 1 literal byte to guarantee progress.
            lit = [row[i] & 0xFF]
            i += 1
        out.append(rle_token(0, len(lit)))
        out.extend(lit)
    return bytes(out)


def encode_rows(rows: list[list[int]]) -> bytes:
    blob = bytearray()
    for row in rows:
        c = rle_compress_row(row)
        blob.append(len(c) & 0xFF)
        blob.append((len(c) >> 8) & 0xFF)
        blob.extend(c)
    return bytes(blob)


def draw_icon_ble(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    cx = W // 2
    top = int(1.5 * scale)
    bot = H - int(1.5 * scale)
    mid = H // 2
    diag = int(4.0 * scale)
    stroke = max(1, int(1.8 * scale))
    # Spine
    d.line([(cx, top), (cx, bot)], fill=255, width=stroke)
    # Upper "K"
    d.line([(cx, mid), (cx + diag, mid - diag)], fill=255, width=stroke)
    d.line([(cx + diag, mid - diag), (cx, top)], fill=255, width=stroke)
    # Lower "K"
    d.line([(cx, mid), (cx + diag, mid + diag)], fill=255, width=stroke)
    d.line([(cx + diag, mid + diag), (cx, bot)], fill=255, width=stroke)
    # Small inner chevrons (gives the familiar rune shape)
    d.line([(cx, mid), (cx - diag, mid - diag)], fill=255, width=max(1, stroke - 1))
    d.line([(cx, mid), (cx - diag, mid + diag)], fill=255, width=max(1, stroke - 1))
    # Downsample with AA
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_lock(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.6 * scale))
    # Body
    body_w = int(10 * scale)
    body_h = int(8 * scale)
    bx0 = (W - body_w) // 2
    by0 = int(6.5 * scale)
    bx1 = bx0 + body_w
    by1 = by0 + body_h
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=int(2 * scale), fill=255)
    # Shackle (outline)
    sh_w = int(8 * scale)
    sh_h = int(7 * scale)
    sx0 = (W - sh_w) // 2
    sy0 = int(1.5 * scale)
    sx1 = sx0 + sh_w
    sy1 = sy0 + sh_h
    d.rounded_rectangle([sx0, sy0, sx1, sy1], radius=int(3 * scale), outline=255, width=stroke)
    # Cut out bottom of shackle to attach to body
    cut = Image.new("L", (W, H), 0)
    cd = ImageDraw.Draw(cut)
    cd.rectangle([sx0 + stroke, (sy0 + sh_h // 2), sx1 - stroke, sy1], fill=255)
    img = Image.composite(Image.new("L", (W, H), 0), img, cut.point(lambda p: 255 - p))
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_thermo(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    cx = W // 2
    top = int(2.0 * scale)
    bottom = H - int(2.5 * scale)

    # Thicker silhouette (so it doesn't collapse at tiny sizes).
    bulb_r = int(5.0 * scale)
    bulb_cy = bottom - bulb_r

    stem_w = int(5.0 * scale)
    sx0 = cx - stem_w // 2
    sy0 = top
    sy1 = bulb_cy

    d.rounded_rectangle([sx0, sy0, sx0 + stem_w, sy1], radius=stem_w // 2, fill=255)
    d.ellipse([cx - bulb_r, bulb_cy - bulb_r, cx + bulb_r, bulb_cy + bulb_r], fill=255)

    # Inner "glass" cut-out to make it read as a thermometer, not a lollipop.
    inner = Image.new("L", (W, H), 0)
    di = ImageDraw.Draw(inner)
    cut = int(1.6 * scale)
    inner_w = max(1, stem_w - 2 * cut)
    inner_r = max(1, bulb_r - cut)
    di.rounded_rectangle([cx - inner_w // 2, sy0 + cut, cx + inner_w // 2, sy1], radius=inner_w // 2, fill=255)
    di.ellipse([cx - inner_r, bulb_cy - inner_r, cx + inner_r, bulb_cy + inner_r], fill=255)
    img = Image.composite(Image.new("L", (W, H), 0), img, inner.point(lambda p: 255 - p))

    # Tick marks (right side) for instant recognition.
    stroke = max(1, int(1.4 * scale))
    for k in range(3):
        yy = sy0 + int((sy1 - sy0) * (k + 1) / 4)
        d.line([(cx + stem_w // 2 + int(1.0 * scale), yy),
                (cx + stem_w // 2 + int(3.2 * scale), yy)], fill=255, width=stroke)
    return img.resize((w, h), resample=Image.LANCZOS)

def draw_icon_graph(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.6 * scale))
    pad = int(2.0 * scale)
    x0 = pad
    y0 = pad
    x1 = W - pad
    y1 = H - pad
    # Axes
    d.line([(x0, y1), (x0, y0)], fill=255, width=stroke)
    d.line([(x0, y1), (x1, y1)], fill=255, width=stroke)
    # Plot line
    pts = [
        (x0 + int(1.0 * scale), y1 - int(2.0 * scale)),
        (x0 + int(4.0 * scale), y1 - int(5.0 * scale)),
        (x0 + int(7.0 * scale), y1 - int(4.0 * scale)),
        (x0 + int(10.0 * scale), y1 - int(8.0 * scale)),
        (x0 + int(12.5 * scale), y1 - int(6.0 * scale)),
    ]
    d.line(pts, fill=255, width=stroke)
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_trip(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    cx = W // 2
    # Teardrop / pin silhouette
    top_r = int(4.8 * scale)
    top_y = int(5.5 * scale)
    d.ellipse([cx - top_r, top_y - top_r, cx + top_r, top_y + top_r], fill=255)
    tip_y = H - int(2.0 * scale)
    left = cx - int(5.3 * scale)
    right = cx + int(5.3 * scale)
    mid_y = int(9.0 * scale)
    d.polygon([(cx, tip_y), (left, mid_y), (right, mid_y)], fill=255)
    # Hollow center (nice UX at small sizes)
    hole_r = int(2.2 * scale)
    hole_y = top_y
    hole = Image.new("L", (W, H), 0)
    hd = ImageDraw.Draw(hole)
    hd.ellipse([cx - hole_r, hole_y - hole_r, cx + hole_r, hole_y + hole_r], fill=255)
    img = Image.composite(Image.new("L", (W, H), 0), img, hole.point(lambda p: 255 - p))
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_settings(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    cx = W // 2
    cy = H // 2
    outer_r = int(5.5 * scale)
    inner_r = int(2.6 * scale)
    tooth_r = int(1.4 * scale)
    # Outer disc
    d.ellipse([cx - outer_r, cy - outer_r, cx + outer_r, cy + outer_r], fill=255)
    # Teeth (8 small blobs around)
    import math
    for k in range(8):
        ang = (math.pi * 2.0) * (k / 8.0)
        tx = int(cx + (outer_r + int(1.6 * scale)) * math.cos(ang))
        ty = int(cy + (outer_r + int(1.6 * scale)) * math.sin(ang))
        d.ellipse([tx - tooth_r, ty - tooth_r, tx + tooth_r, ty + tooth_r], fill=255)
    # Inner hole cut-out
    hole = Image.new("L", (W, H), 0)
    hd = ImageDraw.Draw(hole)
    hd.ellipse([cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r], fill=255)
    img = Image.composite(Image.new("L", (W, H), 0), img, hole.point(lambda p: 255 - p))
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_cruise(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.6 * scale))
    cx = W // 2
    cy = H // 2 + int(1.0 * scale)
    r = int(6.0 * scale)
    # Gauge arc (speedometer vibe)
    box = [cx - r, cy - r, cx + r, cy + r]
    d.arc(box, start=200, end=340, fill=255, width=stroke)
    # Needle
    d.line([(cx, cy), (cx + int(4.5 * scale), cy - int(3.5 * scale))], fill=255, width=stroke)
    # Center dot
    dot_r = max(1, int(1.4 * scale))
    d.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=255)
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_battery(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.4 * scale))
    # Body outline
    bx0 = int(2.0 * scale)
    by0 = int(3.0 * scale)
    bx1 = W - int(3.0 * scale)
    by1 = H - int(3.0 * scale)
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=int(2.2 * scale), outline=255, width=stroke)
    # Terminal
    tx0 = bx1
    ty0 = by0 + int(3.0 * scale)
    ty1 = by1 - int(3.0 * scale)
    d.rectangle([tx0, ty0, tx0 + int(1.8 * scale), ty1], fill=255)
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_alert(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    # Triangle
    top = int(2.0 * scale)
    left = int(2.0 * scale)
    right = W - int(2.0 * scale)
    bottom = H - int(2.0 * scale)
    cx = W // 2
    d.polygon([(cx, top), (right, bottom), (left, bottom)], fill=255)
    # Hollow center for outline feel
    inner = Image.new("L", (W, H), 0)
    di = ImageDraw.Draw(inner)
    inset = int(2.0 * scale)
    di.polygon([(cx, top + inset), (right - inset, bottom - inset), (left + inset, bottom - inset)], fill=255)
    img = Image.composite(Image.new("L", (W, H), 0), img, inner.point(lambda p: 255 - p))
    # Exclamation mark
    ex = ImageDraw.Draw(img)
    bar_w = int(1.6 * scale)
    bar_h = int(5.5 * scale)
    ex.rectangle([cx - bar_w // 2, (H // 2) - bar_h, cx + bar_w // 2, (H // 2) + int(1.0 * scale)], fill=255)
    dot_r = int(1.0 * scale)
    ex.ellipse([cx - dot_r, bottom - int(4.0 * scale), cx + dot_r, bottom - int(2.0 * scale)], fill=255)
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_bus(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.6 * scale))
    pad = int(2.0 * scale)
    y = H // 2
    # Waveform-ish polyline
    pts = [
        (pad, y),
        (pad + int(3.0 * scale), y),
        (pad + int(4.0 * scale), y - int(3.0 * scale)),
        (pad + int(6.5 * scale), y + int(3.0 * scale)),
        (pad + int(8.0 * scale), y),
        (W - pad, y),
    ]
    d.line(pts, fill=255, width=stroke, joint="curve")
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_capture(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.6 * scale))
    cx = W // 2
    cy = H // 2
    r = int(5.8 * scale)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=255, width=stroke)
    dot_r = int(2.2 * scale)
    d.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=255)
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_tune(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.4 * scale))
    pad = int(2.5 * scale)
    ys = [int(4.0 * scale), int(8.0 * scale), int(12.0 * scale)]
    knobs = [int(6.0 * scale), int(10.0 * scale), int(7.5 * scale)]
    for y, kx in zip(ys, knobs):
        d.line([(pad, y), (W - pad, y)], fill=255, width=stroke)
        kr = int(1.8 * scale)
        d.ellipse([kx - kr, y - kr, kx + kr, y + kr], fill=255)
    return img.resize((w, h), resample=Image.LANCZOS)


def draw_icon_info(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.6 * scale))
    cx = W // 2
    cy = H // 2
    r = int(6.0 * scale)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=255, width=stroke)
    # "i"
    bar_w = int(1.6 * scale)
    bar_h = int(4.5 * scale)
    d.rectangle([cx - bar_w // 2, cy - int(1.0 * scale), cx + bar_w // 2, cy - int(1.0 * scale) + bar_h], fill=255)
    dot_r = int(1.1 * scale)
    d.ellipse([cx - dot_r, cy - int(4.5 * scale) - dot_r, cx + dot_r, cy - int(4.5 * scale) + dot_r], fill=255)
    return img.resize((w, h), resample=Image.LANCZOS)

def draw_icon_profile(w: int, h: int, scale: int) -> Image.Image:
    W, H = w * scale, h * scale
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    stroke = max(1, int(1.4 * scale))
    pad = int(2.2 * scale)
    r = int(2.0 * scale)
    # Stacked "cards" icon (profiles/layers vibe)
    d.rounded_rectangle([pad + int(2.0 * scale), pad, W - pad, pad + int(6.5 * scale)], radius=r, outline=255, width=stroke)
    d.rounded_rectangle([pad + int(1.0 * scale), pad + int(4.0 * scale), W - pad - int(1.0 * scale), pad + int(10.5 * scale)], radius=r, outline=255, width=stroke)
    d.rounded_rectangle([pad, pad + int(8.0 * scale), W - pad - int(2.0 * scale), H - pad], radius=r, outline=255, width=stroke)
    return img.resize((w, h), resample=Image.LANCZOS)


@dataclass(frozen=True)
class IconDef:
    name: str
    cid: int
    w: int
    h: int
    img: Image.Image


def emit_c_array(f, name: str, data: bytes) -> None:
    f.write(f"static const uint8_t {name}[] = {{\n")
    for i, b in enumerate(data):
        if i % 16 == 0:
            f.write("    ")
        f.write(f"0x{b:02X},")
        if i % 16 == 15:
            f.write("\n")
    if len(data) % 16 != 0:
        f.write("\n")
    f.write("};\n\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="open-firmware/ui_sprites", help="Output base path (no extension)")
    ap.add_argument("--preview-dir", default="open-firmware/docs/icons", help="Write PNG previews here")
    ap.add_argument("--size", type=int, default=20, help="Icon size (px)")
    ap.add_argument("--scale", type=int, default=8, help="Oversample factor for AA")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parents[1]
    out_base = (repo / args.out).resolve()
    preview_dir = (repo / args.preview_dir).resolve()
    preview_dir.mkdir(parents=True, exist_ok=True)

    w = h = int(args.size)
    s = int(args.scale)

    icons = [
        IconDef("ble", 0x424C4501, w, h, draw_icon_ble(w, h, s)),
        IconDef("lock", 0x4C4F4301, w, h, draw_icon_lock(w, h, s)),
        IconDef("thermo", 0x54485201, w, h, draw_icon_thermo(w, h, s)),
        IconDef("graph", 0x47524101, w, h, draw_icon_graph(w, h, s)),
        IconDef("trip", 0x54524901, w, h, draw_icon_trip(w, h, s)),
        IconDef("settings", 0x53455401, w, h, draw_icon_settings(w, h, s)),
        IconDef("cruise", 0x43525501, w, h, draw_icon_cruise(w, h, s)),
        IconDef("battery", 0x42415401, w, h, draw_icon_battery(w, h, s)),
        IconDef("alert", 0x414C5201, w, h, draw_icon_alert(w, h, s)),
        IconDef("bus", 0x42555301, w, h, draw_icon_bus(w, h, s)),
        IconDef("capture", 0x43415001, w, h, draw_icon_capture(w, h, s)),
        IconDef("tune", 0x54554E01, w, h, draw_icon_tune(w, h, s)),
        IconDef("info", 0x494E4601, w, h, draw_icon_info(w, h, s)),
        IconDef("profile", 0x50524F01, w, h, draw_icon_profile(w, h, s)),
    ]

    # Write previews (for UX review).
    for ic in icons:
        ic.img.save(preview_dir / f"icon_{ic.name}.png")

    h_path = out_base.with_suffix(".h")
    c_path = out_base.with_suffix(".c")

    with open(h_path, "w", encoding="ascii") as f:
        f.write("#ifndef UI_SPRITES_H\n")
        f.write("#define UI_SPRITES_H\n\n")
        f.write('#include "ui_sprite.h"\n\n')
        for ic in icons:
            f.write(f"extern const ui_sprite_t g_ui_sprite_{ic.name};\n")
        f.write("\n#endif\n")

    with open(c_path, "w", encoding="ascii") as f:
        f.write('#include "ui_sprites.h"\n\n')
        total = 0
        for ic in icons:
            rows = pack_a4(ic.img)
            blob = encode_rows(rows)
            emit_c_array(f, f"g_ui_sprite_{ic.name}_data", blob)
            total += len(blob)
            f.write(f"const ui_sprite_t g_ui_sprite_{ic.name} = {{\n")
            f.write(f"    .id = 0x{ic.cid:08X}u,\n")
            f.write(f"    .w = {ic.w}u,\n")
            f.write(f"    .h = {ic.h}u,\n")
            f.write("    .fmt = UI_SPRITE_FMT_A4,\n")
            f.write("    .flags = 0u,\n")
            f.write(f"    .data = g_ui_sprite_{ic.name}_data,\n")
            f.write(f"    .data_len = (uint32_t)sizeof(g_ui_sprite_{ic.name}_data),\n")
            f.write("};\n\n")

        # Budget guardrails (edit as needed).
        f.write("#define UI_SPRITES_TOTAL_COMPRESSED_BYTES %du\n" % total)
        f.write("_Static_assert(UI_SPRITES_TOTAL_COMPRESSED_BYTES <= 4096u, \"UI sprites exceed 4KB compressed budget\");\n")

    print("Wrote:", h_path)
    print("Wrote:", c_path)
    print("Previews:", preview_dir)


if __name__ == "__main__":
    main()
