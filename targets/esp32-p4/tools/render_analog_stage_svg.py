#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
from pathlib import Path

STAGE_SIZE = 800
CENTER = STAGE_SIZE // 2
# Baked-in equivalent of the old runtime STAGE_Y_TRIM=-18 lv_obj_align offset
# in app_ui.c. Shifting the *asset* instead of shifting the LVGL image object
# at runtime avoids an LVGL rendering artifact confirmed on-device: an
# offset-aligned image object consistently produced a bright horizontal line
# at the bottom of the visible content that disappeared entirely when the
# image was hidden (everything else about the scene unchanged), pointing at
# the runtime align-offset mechanism itself rather than anything drawn in
# the image or anything physical. Only this baked-image circle moves — the
# procedural ticks/numerals in app_ui.c were never shifted (confirmed fine
# as-is) and must stay exactly where they are, at the true canvas center.
STAGE_Y_TRIM = -18
FACE_CY = CENTER + STAGE_Y_TRIM
FACE_RADIUS = 380
TICK_OUTER_RADIUS = 348
NUMERAL_RADIUS = 286
MINOR_TICK_LENGTH = 16
MAJOR_TICK_LENGTH = 24
QUARTER_TICK_LENGTH = 26

MAJOR_NUMERALS = {12, 3, 6, 9}
OPTICAL_OFFSETS = {
    12: (0, -2),
    3: (1, 0),
    6: (0, 2),
    9: (-1, 0),
}

PALETTE_DAY = {
    "stage_start": "rgba(39, 53, 79, 0.55)",
    "stage_mid": "rgba(8, 12, 18, 0.96)",
    "stage_end": "#010203",
    "face_start": "rgba(60, 79, 111, 0.30)",
    "face_mid": "rgba(19, 26, 38, 0.78)",
    "face_inner": "rgba(10, 14, 22, 0.96)",
    "face_end": "rgba(2, 4, 8, 0.98)",
    "glow_start": "rgba(103, 139, 255, 0.08)",
    "glow_mid": "rgba(103, 139, 255, 0.02)",
    "edge": "rgba(255, 255, 255, 0.08)",
    "inner_edge": "rgba(255, 255, 255, 0.04)",
    "minor_tick": "rgba(255, 255, 255, 0.38)",
    "major_tick": "rgba(255, 255, 255, 0.68)",
    "quarter_tick": "rgba(255, 255, 255, 0.78)",
    "major_numeral": "rgba(244, 248, 255, 0.88)",
    "minor_numeral": "rgba(224, 233, 247, 0.60)",
}

PALETTE_NIGHT = {
    "stage_start": "rgba(36, 10, 10, 0.72)",
    "stage_mid": "rgba(10, 4, 4, 0.98)",
    "stage_end": "#010101",
    "face_start": "rgba(82, 30, 30, 0.18)",
    "face_mid": "rgba(27, 11, 11, 0.78)",
    "face_inner": "rgba(11, 6, 6, 0.96)",
    "face_end": "rgba(3, 1, 1, 0.99)",
    "glow_start": "rgba(110, 28, 28, 0.12)",
    "glow_mid": "rgba(110, 28, 28, 0.03)",
    "edge": "rgba(179, 94, 94, 0.18)",
    "inner_edge": "rgba(255, 255, 255, 0.04)",
    "minor_tick": "rgba(193, 126, 126, 0.32)",
    "major_tick": "rgba(214, 160, 160, 0.56)",
    "quarter_tick": "rgba(224, 172, 172, 0.64)",
    "major_numeral": "rgba(225, 182, 182, 0.82)",
    "minor_numeral": "rgba(198, 150, 150, 0.68)",
}


def polar_point(angle_deg: float, radius: float) -> tuple[float, float]:
    angle_rad = math.radians(angle_deg - 90.0)
    return (
        CENTER + math.cos(angle_rad) * radius,
        CENTER + math.sin(angle_rad) * radius,
    )


def render_ticks(palette: dict[str, str]) -> str:
    lines: list[str] = []
    for tick in range(60):
        angle = tick * 6
        tick_length = MINOR_TICK_LENGTH
        stroke = palette["minor_tick"]
        stroke_width = 2

        if tick % 5 == 0:
            tick_length = MAJOR_TICK_LENGTH
            stroke = palette["major_tick"]
            stroke_width = 4

        if tick % 15 == 0:
            tick_length = QUARTER_TICK_LENGTH
            stroke = palette["quarter_tick"]

        outer_x, outer_y = polar_point(angle, TICK_OUTER_RADIUS)
        inner_x, inner_y = polar_point(angle, TICK_OUTER_RADIUS - tick_length)
        lines.append(
            "    "
            f'<line x1="{outer_x:.2f}" y1="{outer_y:.2f}" x2="{inner_x:.2f}" y2="{inner_y:.2f}" '
            f'stroke="{stroke}" stroke-width="{stroke_width}" stroke-linecap="round"/>'
        )
    return "\n".join(lines)


def render_numerals(palette: dict[str, str]) -> str:
    numerals: list[str] = []
    for hour in range(12):
        display_hour = 12 if hour == 0 else hour
        x, y = polar_point(hour * 30, NUMERAL_RADIUS)
        offset_x, offset_y = OPTICAL_OFFSETS.get(display_hour, (0, 0))
        font_size = 48 if display_hour in MAJOR_NUMERALS else 34
        font_weight = 500 if display_hour in MAJOR_NUMERALS else 450
        fill = palette["major_numeral"] if display_hour in MAJOR_NUMERALS else palette["minor_numeral"]
        numerals.append(
            "    "
            f'<text x="{x + offset_x:.2f}" y="{y + offset_y:.2f}" font-size="{font_size}" '
            f'font-weight="{font_weight}" fill="{fill}">{display_hour}</text>'
        )
    return "\n".join(numerals)


def render_stage_layer(palette: dict[str, str]) -> str:
    return f"""  <rect width="{STAGE_SIZE}" height="{STAGE_SIZE}" fill="url(#stage-bg)"/>

  <g filter="url(#face-shadow)">
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS}" fill="url(#face-fill)"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS}" fill="url(#face-glow)"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 1}" fill="none" stroke="{palette["edge"]}" stroke-width="2"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 18}" fill="none" stroke="{palette["inner_edge"]}" stroke-width="1"/>
  </g>"""


def render_dial_overlay_layer(palette: dict[str, str]) -> str:
    return f"""  <g>
{render_ticks(palette)}
  </g>

  <g
    font-family="Avenir Next, Helvetica Neue, Arial, sans-serif"
    text-anchor="middle"
    dominant-baseline="middle"
    paint-order="stroke fill"
    stroke="rgba(0, 0, 0, 0.14)"
    stroke-width="1">
{render_numerals(palette)}
  </g>"""


def build_svg(mode: str, layer: str) -> str:
    palette = PALETTE_NIGHT if mode == "night" else PALETTE_DAY
    if layer == "overlay":
        body = render_dial_overlay_layer(palette)
    else:
        body = render_stage_layer(palette)

    return f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{STAGE_SIZE}" height="{STAGE_SIZE}" viewBox="0 0 {STAGE_SIZE} {STAGE_SIZE}">
  <defs>
    <radialGradient id="stage-bg" cx="50%" cy="50%" r="60%">
      <stop offset="0%" stop-color="{palette["stage_start"]}"/>
      <stop offset="58%" stop-color="{palette["stage_mid"]}"/>
      <stop offset="100%" stop-color="{palette["stage_end"]}"/>
    </radialGradient>
    <radialGradient id="face-fill" cx="50%" cy="46%" r="54%">
      <stop offset="0%" stop-color="{palette["face_start"]}"/>
      <stop offset="38%" stop-color="{palette["face_mid"]}"/>
      <stop offset="72%" stop-color="{palette["face_inner"]}"/>
      <stop offset="100%" stop-color="{palette["face_end"]}"/>
    </radialGradient>
    <radialGradient id="face-glow" cx="50%" cy="46%" r="54%">
      <stop offset="0%" stop-color="{palette["glow_start"]}"/>
      <stop offset="44%" stop-color="{palette["glow_mid"]}"/>
      <stop offset="100%" stop-color="rgba(0, 0, 0, 0)"/>
    </radialGradient>
    <filter id="face-shadow" x="-12%" y="-12%" width="124%" height="136%">
      <feOffset dy="20" result="offset"/>
      <feGaussianBlur in="offset" stdDeviation="24" result="blur"/>
      <feColorMatrix
        in="blur"
        type="matrix"
        values="0 0 0 0 0
                0 0 0 0 0
                0 0 0 0 0
                0 0 0 0.45 0"
        result="shadow"/>
      <feMerge>
        <feMergeNode in="shadow"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
  </defs>

{body}
</svg>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Render the Pi-style analog stage SVG source.")
    parser.add_argument("--output", required=True, help="Destination SVG path")
    parser.add_argument("--mode", choices=("day", "night"), default="day", help="Palette mode")
    parser.add_argument(
        "--layer",
        choices=("stage", "overlay"),
        default="stage",
        help="Which analog layer to render",
    )
    args = parser.parse_args()

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(build_svg(args.mode, args.layer), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
