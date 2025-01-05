# UI_FEATURES - full UI plan for BC280-class display

This is the feature plan for a modern, fast, asset-light UI. It extends UI_SPEC
with a complete screen map, component library, and motion language.
See `UI_DESIGN_SYSTEM.md` for tokens, icons, and widget patterns.

## Visual language
- 8px grid, generous padding, rounded chips, clear hierarchy.
- Primary value is always dominant; secondary data sits in cards.
- No bitmap assets in default build; icons are procedural (rects/lines/triangles).
- Palettes: day, night, high-contrast, colorblind-safe.
- Ambient background uses subtle 2x2/4x4 ordered dither in panels.

## Motion language (bounded, low-cost)
- Accent sweep on card value change (1-2px line, 2-4 frames).
- Warning pulse (2-step brightness change, no continuous blinking).
- Power halo arc behind speed (updates at 2-5 Hz).
- Regen glow: accent color swap + decay pulse on activation.
- Chip pop: single-frame thicker outline when a chip changes.

Rule: animations run only on state change and are capped to a few frames.

## Interaction model
- Short press: next page / increment value.
- Long press: quick action (configurable).
- Back: up one level / cancel.
- Home: long-press back or dedicated button (if present).

## Core components
- Big digits (segmented stroke font) for speed/power.
- Small stroke font for labels (uppercase subset).
- Chips: mode, assist/gear, limiter, cruise, regen.
- Cards: watts, amps, volts, cadence, efficiency, temps.
- Battery icon: outline + fill + sag bar.
- Range confidence: 3-5 tick bar.
- Power halo: thin arc showing utilization.
- Alerts list: icon + short label + timestamp/distance.

## Screen map (by phase)

MVP (Phase 1)
- Dashboard (home): speed, assist/gear, battery, range, cards.
- Trip summary: distance, time, avg/max, Wh, Wh/mi.
- Graphs MVP: single-channel strip chart (30s window).
- Profiles & gears: profile chips + quick gear range edits.
- Settings entry + setup wizard link.
- Focus mode (optional): single large value + battery chip.

Phase 2
- Cruise screen: setpoint, safety status, resume gating.
- Battery screen: SOC, sag, voltage, health estimate.
- Thermal screen: temps, derate state, why text.
- Diagnostics/engineer: dense table of raw inputs (brake/throttle/cadence), state flags (assist/cruise/drive/regen), limit reason, link timeouts/errors, buttons/errors in hex; muted labels + accent for active flags.
- Bus monitor: rolling frame list + filters.
- Capture/replay: start/stop capture, list replays.
- Alerts timeline: last N warnings with acknowledge.
- Tune screen: quick knobs (assist strength, ramp, eco bias).

Phase 3
- Ambient/charging: SOC, time-to-full, charge power + slow ring.
- About: version, build hash, hardware IDs, reboot to bootloader (MENU+POWER long-press).
- Lock screen + PIN entry and quick actions (if enabled).

## Rendering + testability (summary)
- Dirty-rect redraws only; bounded list of rects.
- No full framebuffer; line buffer for fills and text.
- Deterministic render-hash for regression.

## Acceptance criteria (testable)
1) Procedural-only build works with no bitmap assets or external font files.
2) Deterministic render hash matches across host runs.
3) Dashboard updates for speed-only changes stay within MAX_DIRTY and avoid full redraw.
