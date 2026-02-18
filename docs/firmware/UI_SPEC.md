# UI_SPEC — “beautiful, tiny, mostly-vector” UI for BC280-class displays

This document defines a UI that:
- **Looks modern** (consistent spacing, typography, subtle hierarchy),
- **Fits in a tiny binary** (no bitmap assets, no full-frame buffers),
- **Runs fast on a small MCU** (dirty-rect + region streaming),
- **Is testable** via deterministic UI state + host tests.

The intent is to build the UI almost entirely from **procedural shapes** (“vector-ish”):
filled rectangles (incl. rounded), lines, simple arcs, and a stroked digit font.

---

## Constraints & design goals

### Hard constraints
- **LCD**: 240×320 portrait (ST7789-class panel).
- **No full framebuffer in SRAM.** (Typical RGB565 240×320 is ~150 KiB.)
- **96 KiB default SRAM target** (extendable to 224 KiB; leave headroom for stacks, protocol buffers, logs).
- **Binary size matters**: avoid large fonts, icons, and string tables.
- **No malloc required**: everything bounded / fixed-size.
- **Procedural-only default**: UI must render without bitmap assets or external font files.

### Practical goals
- **5–10 Hz UI tick** (human perception) + higher internal sampling for telemetry.
- **Dirty rect rendering**: redraw only what changed.
- **Immediate-mode UI**: compute layout and issue draw calls; keep minimal retained UI state.
- **Deterministic rendering** for regression tests.

---

## Visual language (what makes it “beautiful” without assets)
See `docs/UI_DESIGN_SYSTEM.md` for tokens, icon set, and widget patterns.

### Grid & spacing
Use an 8px grid with consistent padding:
- `G = 8` (grid unit)
- `PAD = 2*G` (outer margin)
- `GAP = G` (between widgets)
- `RADIUS = G` (rounded corners for “chips” and cards)
- `STROKE = 2` (icon strokes)

Everything aligns to this grid to look intentional even with simple shapes.

### Hierarchy
One screen should have **one dominant datum** (usually speed).
Use:
- Big stroked digits for primary value.
- “Chips” (rounded pills) for status (assist/gear/mode).
- Low-contrast secondary text for units and labels.

### Motion & dynamics (tiny, but premium)
We want the UI to feel **alive** without heavy animation costs:
- **Accent sweep**: a 1–2px accent line that slides across a card on value change.
- **Pulse**: a subtle 2-step brightness pulse for warnings/limiters (no continuous blinking).
- **Power halo**: a thin arc behind speed that grows/shrinks with watts (update at 2–5 Hz).
- **Regen glow**: swap accent color + add a short decay pulse when regen activates.
- **Chip pop**: when a chip value changes, draw a single-frame thicker outline.

Implementation rule: only animate **on state change**, not continuously. Each animation is a
short, bounded sequence (2–6 frames) driven by the UI tick. No per-frame allocations.

### State layers (visual priority)
1) **Safety/warnings** (red/orange) override all
2) **Limiters** (yellow) override accent color on affected widgets
3) **Primary focus** (speed / cruise / charge)
4) **Secondary info** (cards)
5) **Ambient** (background patterns)

### Color & themes (tiny)
Themes are just small RGB565 palettes (no gradients required).
Each theme is ~8 colors:
- `bg`, `panel`, `text`, `muted`, `accent`, `warn`, `danger`, `ok`

Recommended built-in themes:
- **Night**: near-black bg, cyan accent, warm warnings.
- **Day**: light bg, dark text, blue accent.
- **High-contrast**: black/white + one accent, thick strokes.

Implementation: `const uint16_t theme_palettes[][8] = { … }`.

### Icons (procedural)
No bitmaps. Icons are composed from:
- rectangles,
- lines,
- optional small arcs,
- filled triangles (for warnings).

Icons to standardize early:
- Battery (outline + fill level),
- BLE (stylized “runes” with 2 triangles + spine),
- Lock,
- Warning triangle,
- Thermometer,
- Info/About,
- Settings,
- Profiles,
- Graphs,
- Trip,
- Cruise,
- Diagnostics,
- Bus,
- Capture,
- Alerts.

All icons fit in a 16×16 or 24×24 box and draw with `STROKE` width.

---

## Typography with almost no flash

We use **two procedural “fonts”**:

### 1) “Big digits” — stroked vector digits (primary speed)
For the speed readout we use a **7/9-seg style stroked font** that is:
- tiny in flash (segment definitions),
- scalable (just scale segment coordinates),
- looks modern when anti-aliasing is absent (thick strokes).

Digits are built from segments:
- Each segment is a rounded rectangle (or a thick line with endcaps).
- Optional diagonal segments to make it less “calculator-y”.

Allowed glyphs:
- `0-9`, `.` (decimal), `:` (time), maybe `-`.

This keeps us from shipping a large bitmap font but still looks “premium”.

### 2) “Small text” — stroked vector glyphs (ASCII subset)
For labels (`MPH`, `WH/MI`, `TEMP`, etc.), we use a **tiny stroke font**:
- Each glyph is a handful of line segments (Hershey-style).
- Only needed characters: digits, uppercase A–Z, a few punctuation (`/`, `.`, `-`, `%`, `:`).
- Stored as segment lists + per-glyph width table (no bitmap atlas).

If binary is extremely tight, we can:
- drop lowercase entirely,
- abbreviate labels (“WH/MI”, “CAD”, “V”, “A”),
- use only a **single** small font size.

## Rendering architecture (no framebuffer)

### Core idea
Every draw operation ultimately becomes:
1) **set LCD address window** (x0,y0,x1,y1),
2) **stream pixels** (RGB565) for that region.

We never hold the whole screen in RAM.

### Region streaming
For fills and simple shapes, render directly:
- For `fill_rect`, emit scanlines of a single color (fast).
- For stroked primitives, emit only the bounding rect region.

Use a small line buffer:
- `uint16_t line[DISPLAY_W]` (worst-case ~640 bytes for 320px width).
This is cheap and enables:
- patterned fills (dither),
- rounded corners,
- text blitting.

### Dirty rectangles
Maintain a small dirty-rect list:
- `MAX_DIRTY = 8` or `12` (bounded)
- Merge overlapping rects to avoid explosion.

Each widget decides whether it needs redraw by comparing new value vs last value.

### Immediate-mode widgets (retained minimal state)
Widget API shape:
```c
bool ui_widget_speed(ui_ctx_t *ui, rect_t r, speed_t current);
```
Return `true` if the widget invalidated `r` (i.e., drew).

Widget stores the last rendered value in a tiny retained struct (`ui_prev`).

### “Vector-ish” primitives we actually need
Keep the primitive set small:
- `fill_rect`
- `fill_round_rect` (radius fixed small)
- `stroke_round_rect` (optional)
- `draw_line_thick` (Bresenham or DDA with thickness)
- `fill_triangle` (for warning)
- `draw_ring_arc` / `draw_ring_gauge` (AA ring segments; used for halos/dials)
- `blit_glyph` (small bitmap font)
- `draw_big_digit` (segment/stroke font)

This is enough to create a modern UI without any assets.

### Optional: micro-dithering for “premium” look
To emulate subtle gradients (without cost):
- Use ordered dithering patterns (2×2 or 4×4) in `panel` fills.
- Only for large panel backgrounds, not for every pixel.

---

## Screen system (full list + layout intent)

Navigation model:
- Short press: next/prev item or change value
- Long press: “quick action” (configurable)
- Back: goes up one level
- Home: long-press back (or dedicated button if available)

### Screen 0: Dashboard (Home)
Goal: one glance.

Layout:
- Top row: **mode chip** (LEGAL/PRIVATE), **LIMIT chip** (LUG/THERM/SAG when constraining), small icons (BLE, warning)
- Center: **big speed** (vector digits) + unit (`mph`/`km/h`)
- Side/under speed: assist/gear chip (e.g., `TRAIL • G3`)
- Bottom “card row” (3–5 cards):
  - `W` (motor watts)
  - `A` (battery amps)
  - `V` (battery volts)
  - `CAD` (cadence)
  - `WH/MI` (efficiency)

Range:
- Right side card: `RANGE 23.4 mi` + confidence bar (3–5 ticks).

Power protection visibility:
- If a governor is limiting output, show the reason (LIMIT chip) rather than silently reducing power.
- Optional tiny gauges: duty (utilization), thermal headroom, sag margin (see `POWER_CONTROL_SPEC.md`).

Rendering:
- Speed digits update at 5–10 Hz.
- Cards update at 2–5 Hz (unless value changes > threshold).
- Icons update only on state change.

### Screen 0.1: Focus mode (optional)
Minimal single-value view for speed or power. Intended for bright sunlight or
night riding. One giant value + unit + tiny battery chip.

### Screen 1: Graphs (MVP = 1 channel)
MVP:
- One strip chart, selectable channel (`SPD`, `W`, `V`, `CAD`).
- Window fixed at 30s initially.

UI:
- Top: channel chip + window chip.
- Graph region: thick polyline / sparkline + min/max labels.
- Bottom: sample rate + “logging” indicator if enabled.

Testing:
- Output graph stats (count/min/max) and a render-hash.

### Screen 2: Trip summary
Layout:
- Two-column list with big numbers:
  - Distance
  - Moving time
  - Avg / Max speed
  - Wh / Wh/mi
  - Time in assist / gear (compact)

### Screen 3: Profiles & Gears
Layout:
- Profile selector as stacked chips.
- Gear indicator + quick edit (min/max + shape).

No huge editors on-device:
- Use “adjust a few core knobs” UX.
- Detailed curve editing stays in host tool / BLE config.

### Screen 4: Cruise (Phase 2)
Layout:
- Large cruise state and setpoint.
- Safety status row (brake ok, pedaling ok, etc.)
- Resume gating reason text.

### Screen 5: Battery
Layout:
- Large battery icon (procedural) + SOC.
- Voltage + sag indicator (bar that shrinks under load).
- Optional health estimate (simple, labeled “estimate”).

### Screen 6: Thermal
Layout:
- Temps (motor/controller) as cards.
- Derate state badge + “why” text.

### Screen 7: Diagnostics / Engineer
Layout:
- Dense, consistent table layout:
  - Raw inputs (speed, cadence, torque, throttle %, brake, buttons)
  - Link stats (timeouts/errors)
  - State machine states (assist/mode, walk, cruise, drive, limit, regen)
  - Buttons/error values rendered as hex for quick bit checks

### Screen 8: Bus monitor (Phase 2)
Layout:
- Rolling frame list (N lines).
- Filter chip row.
- Diff mode indicator.

### Screen 9: Capture/Replay (Phase 2)
Layout:
- Start/stop capture big button (chip).
- Export status.
- Replay list (if any).

### Screen 10: Settings
Layout:
- Wizard entry
- Units
- Button mappings
- Themes
- Legal/private mode and PIN

### Screen 11: About
Layout:
- Firmware version, build hash, hardware IDs.
- Bootloader entry test (“Reboot to BL”).

### Screen 12: Alerts (rolling timeline)
Shows last N alerts/warnings with timestamps or ride distance.
Icons + short label. Acknowledge clears the active warning chip.

### Screen 13: Tune (quick knobs)
Fast in-ride tweaks: assist strength, ramp rate, eco bias.
Large slider cards with +- buttons; no deep curve editing.

### Screen 14: Ambient / Charging
While charging: big SOC, time-to-full estimate, charge power.
Use a slow, low-cost animated ring (4–6 steps) to show active charge.

---


### Golden rule
Every screen must have a **deterministic trace** mode that proves it rendered the
right values (not necessarily the exact pixels).

Recommended approach:
- Wrap all draw primitives in a thin “draw recorder” for regression tests.
- Recorder outputs:
  - a compact line per UI tick, or
  - a running **render hash** (CRC32) over draw calls + parameters.

Example “render hash” update:
```
hash = crc32(hash, "fill_round_rect", x,y,w,h,color,radius)
hash = crc32(hash, "big_digit", digit, x,y,scale,color)
…
```

Tests can then:
1) inject inputs via UART debug protocol,
2) wait for one UI tick,
3) assert the render hash equals the expected value for that state.

This avoids huge pixel dumps and keeps tests stable.

---

## Memory budgeting (rough guidance)

Keep UI RAM fixed and boring:
- UI retained state (`ui_prev`): < 1 KiB
- Dirty rect list: < 128 bytes
- Line buffer: `2 * DISPLAY_W` bytes (e.g., 640 bytes @ 320w)
- Graph buffers: bounded (e.g., 256 samples per channel in MVP)
- String buffers: a couple of 64–128B scratch buffers (no sprintf)

Avoid:
- per-widget allocations,
- general-purpose printf,
- large lookup tables for fonts.

---

## Suggested code organization (when implementing)

When we implement this, keep it modular and size-friendly:
- `ui/gfx.[ch]` — low-level primitives + LCD window streaming
- `ui/font_stroke.[ch]` — stroke glyphs for small text
- `ui/font_digits.[ch]` — segment definitions for big digits
- `ui/icons.[ch]` — procedural icon set
- `ui/widgets_*.c` — widgets (speed chip, battery, cards)
- `ui/screens_*.c` — per-screen layout + event handling
- `ui/ui.[ch]` — state machine + dirty rect scheduler

Testing hooks can log UI tick traces and expose render hash + current screen in the debug state dump.
