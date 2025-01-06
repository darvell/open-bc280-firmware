#!/usr/bin/env python3
"""
Generate PNG screenshots from the host UI simulator.

Usage:
  ./scripts/ui_screenshot.py                       # dashboard, default theme/model
  ./scripts/ui_screenshot.py --page 0              # dashboard (explicit)
  ./scripts/ui_screenshot.py --page 4 --name graphs
  ./scripts/ui_screenshot.py --all                 # dump all pages in ui.h enum

The sim always writes a PPM (`host_lcd_latest.ppm`). This script converts that
to PNG and also writes an x4 nearest-neighbor scaled image for easy review.
"""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path

from PIL import Image


def run(cmd: list[str], env: dict[str, str]) -> None:
    subprocess.run(cmd, env=env, check=True)


def ppm_to_png(ppm: Path, png: Path, scale: int) -> None:
    img = Image.open(ppm)
    png.parent.mkdir(parents=True, exist_ok=True)
    img.save(png)
    if scale > 1:
        big = img.resize((img.size[0] * scale, img.size[1] * scale), resample=Image.NEAREST)
        big.save(png.with_name(png.stem + f"_x{scale}" + png.suffix))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--page", type=int, default=0, help="ui_page_t id (default: dashboard=0)")
    ap.add_argument("--name", default=None, help="output basename (default: page_<id>)")
    ap.add_argument("--all", action="store_true", help="render all pages in ui.h enum")
    ap.add_argument("--steps", type=int, default=1, help="BC280_SIM_STEPS (default: 1)")
    ap.add_argument("--scale", type=int, default=4, help="PNG scale factor (default: 4)")
    ap.add_argument("--outdir", default="open-firmware/tests/host/lcd_shots", help="output folder")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parents[1]
    outdir = (repo / args.outdir).resolve()

    # Build once.
    run(["make", "-C", "open-firmware", "sim-host"], env=os.environ.copy())

    pages = [args.page]
    if args.all:
        # ui.h enum currently defines pages 0..17.
        pages = list(range(0, 18))

    for page in pages:
        name = args.name if (args.name and not args.all) else f"page_{page:02d}"
        env = os.environ.copy()
        env["UI_LCD_OUTDIR"] = str(outdir)
        env["BC280_SIM_STEPS"] = str(args.steps)
        env["BC280_SIM_FORCE_PAGE"] = str(page)

        run(["open-firmware/tests/host/host_sim"], env=env)

        ppm = outdir / "host_lcd_latest.ppm"
        png = outdir / f"{name}.png"
        ppm_to_png(ppm, png, args.scale)
        print(png)


if __name__ == "__main__":
    main()

