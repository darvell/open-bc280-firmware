# UI_DESIGN_SYSTEM - BC280 procedural UI tokens + patterns

This document defines the design system for the BC280-class display UI.
It is procedural-only by default: **no bitmap assets** and **no external fonts**.

## Tokens

### Layout units
- `u = 4` (micro alignment)
- `g = 8` (grid)
- `pad = 16` (outer padding)

### Spacing scale (px)
- `s0=0`, `s1=4`, `s2=8`, `s3=12`, `s4=16`, `s5=24`, `s6=32`

### Radii
- `r0=0` (sharp)
- `r1=6` (chips)
- `r2=10` (cards / list rows)
- `r3=16` (hero panel)

### Stroke widths
- `stroke=1` (dividers)
- `stroke_bold=2` (icons, accents)
- `ring=8-10` (halo/gauge thickness)

## Color slots (RGB565 palette)
Use semantic slots only:
- `bg`, `panel`, `text`, `muted`, `accent`, `warn`, `danger`, `ok`

Themes: Day, Night, High-contrast, Colorblind-safe.

## Typography

### Big digits (primary)
- Segmented stroke digits for speed/power.
- Glyphs: `0-9`, `.`, `:`, `-`.
- Scalable, thick strokes, no anti-aliasing required.

### Small text (secondary)
- Stroke-vector glyphs (Hershey-style).
- Uppercase subset + minimal punctuation (`/`, `.`, `-`, `%`, `:`).
- Use abbreviations for long labels (e.g., `WH/MI`, `CAD`, `TEMP`).

## Icon set (procedural)
All icons are built from rects/lines/triangles/arcs:
- Battery, BLE, Lock, Warning, Thermometer
- Info/About, Settings, Profiles, Graphs, Trip, Cruise
- Diagnostics, Bus, Capture, Alerts

Standard sizes: 20x20 and 24x24.

## Widget patterns
- **Panel card**: rounded rect + optional shadow + title/value.
- **Stat card**: label (muted) + value + unit.
- **Chip**: rounded rect with value, thick outline on change.
- **List row**: full-width panel, left accent bar when selected.
- **Diagnostics row**: muted label + value; accent value when active; buttons/errors as hex for bitfields. Rows cover raw inputs (brake/throttle/cadence), link stats (timeouts/errors), and state flags (assist/cruise/drive/regen).
- **Ring gauge**: active arc + inactive arc + center label.
- **Toast**: small overlay panel with 1-2 lines of text.

## Layout rules
- One dominant datum per screen.
- Align to the 8px grid; keep padding consistent.
- Secondary data sits in cards; avoid dense text blocks.

## Motion language (bounded)
- Accent sweep on value change (2-4 frames).
- Warning pulse (2-step brightness change).
- Power halo arc updates at 2-5 Hz.
- Regen glow: accent swap + short decay.
- Chip pop on change (single frame).

Rule: **animate only on state change**, not continuously.

## Navigation grammar
- Short press: next/prev or increment value.
- Long press: quick action (configurable).
- Back: up one level / cancel.
- Home: long-press back.

## Rendering constraints
- No full framebuffer; render via LCD window + line buffer.
- Dirty-rect list capped (MAX_DIRTY).
- Deterministic render hash.

## Acceptance criteria
1) Default build compiles and runs without bitmap assets or external fonts.
2) UI render hash is deterministic across host runs.
3) Speed-only updates on dashboard avoid full redraw and stay within MAX_DIRTY.
4) No persistent buffers exceed one scanline (2 * DISPLAY_W bytes).

## Testing hooks
- Host sim: deterministic render hash and optional screenshots.
