#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

STAGE_SIZE = 800
CENTER = STAGE_SIZE // 2
# See render_analog_stage_svg.py for why this is baked into the asset rather
# than applied as a runtime lv_obj_align offset in app_ui.c.
STAGE_Y_TRIM = -18
FACE_CY = CENTER + STAGE_Y_TRIM
FACE_RADIUS = 390
FORECAST_FRAME_SIZE = 500
FORECAST_FRAME_CENTER = FORECAST_FRAME_SIZE // 2

PALETTE_DAY = {
    "stage_start": "rgba(39, 53, 79, 0.55)",
    "stage_mid": "rgba(8, 12, 18, 0.96)",
    "stage_end": "#010203",
    "round_face_start": "rgba(60, 79, 111, 0.28)",
    "round_face_mid": "rgba(19, 26, 38, 0.80)",
    "round_face_inner": "rgba(10, 14, 22, 0.96)",
    "round_face_end": "rgba(2, 4, 8, 0.98)",
    "digital_face_start": "rgba(37, 74, 134, 0.28)",
    "digital_face_mid": "rgba(20, 39, 72, 0.40)",
    "digital_face_inner": "rgba(10, 20, 35, 0.86)",
    "digital_face_end": "rgba(4, 10, 18, 0.98)",
    "edge": "rgba(255, 255, 255, 0.08)",
    "inner_edge": "rgba(255, 255, 255, 0.04)",
    "glow": "rgba(103, 139, 255, 0.08)",
    "digital_ring": "rgba(89, 190, 255, 0.36)",
    "digital_ring_glow": "rgba(49, 146, 230, 0.16)",
    "digital_ring_inner": "rgba(49, 146, 230, 0.08)",
    "forecast_disc_start": "rgba(98, 141, 214, 0.22)",
    "forecast_disc_mid": "rgba(53, 78, 126, 0.16)",
    "forecast_disc_end": "rgba(14, 22, 39, 0.12)",
    "forecast_ring_outer": "rgba(214, 226, 240, 0.28)",
    "forecast_ring_inner": "rgba(196, 210, 226, 0.22)",
}

PALETTE_NIGHT = {
    "stage_start": "rgba(36, 10, 10, 0.72)",
    "stage_mid": "rgba(10, 4, 4, 0.98)",
    "stage_end": "#010101",
    "round_face_start": "rgba(82, 30, 30, 0.18)",
    "round_face_mid": "rgba(27, 11, 11, 0.78)",
    "round_face_inner": "rgba(11, 6, 6, 0.96)",
    "round_face_end": "rgba(3, 1, 1, 0.99)",
    "digital_face_start": "rgba(86, 34, 34, 0.18)",
    "digital_face_mid": "rgba(38, 18, 18, 0.28)",
    "digital_face_inner": "rgba(15, 8, 8, 0.90)",
    "digital_face_end": "rgba(5, 3, 3, 0.99)",
    "edge": "rgba(179, 94, 94, 0.18)",
    "inner_edge": "rgba(255, 255, 255, 0.04)",
    "glow": "rgba(110, 28, 28, 0.08)",
    "digital_ring": "rgba(165, 86, 86, 0.22)",
    "digital_ring_glow": "rgba(140, 52, 52, 0.12)",
    "digital_ring_inner": "rgba(140, 52, 52, 0.08)",
    "forecast_disc_start": "rgba(98, 50, 50, 0.18)",
    "forecast_disc_mid": "rgba(52, 24, 24, 0.16)",
    "forecast_disc_end": "rgba(14, 8, 8, 0.12)",
    "forecast_ring_outer": "rgba(186, 122, 122, 0.22)",
    "forecast_ring_inner": "rgba(163, 100, 100, 0.20)",
}


def stage_background_svg(palette: dict[str, str], inner: str) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{STAGE_SIZE}" height="{STAGE_SIZE}" viewBox="0 0 {STAGE_SIZE} {STAGE_SIZE}">
  <defs>
    <radialGradient id="stage-bg" cx="50%" cy="50%" r="60%">
      <stop offset="0%" stop-color="{palette["stage_start"]}"/>
      <stop offset="58%" stop-color="{palette["stage_mid"]}"/>
      <stop offset="100%" stop-color="{palette["stage_end"]}"/>
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

  <rect width="{STAGE_SIZE}" height="{STAGE_SIZE}" fill="url(#stage-bg)"/>
{inner}
</svg>
"""


def build_round_stage(mode: str) -> str:
    palette = PALETTE_NIGHT if mode == "night" else PALETTE_DAY
    inner = f"""
  <defs>
    <radialGradient id="face-fill" cx="50%" cy="46%" r="54%">
      <stop offset="0%" stop-color="{palette["round_face_start"]}"/>
      <stop offset="38%" stop-color="{palette["round_face_mid"]}"/>
      <stop offset="72%" stop-color="{palette["round_face_inner"]}"/>
      <stop offset="100%" stop-color="{palette["round_face_end"]}"/>
    </radialGradient>
    <radialGradient id="face-glow" cx="50%" cy="46%" r="54%">
      <stop offset="0%" stop-color="{palette["glow"]}"/>
      <stop offset="44%" stop-color="rgba(0, 0, 0, 0)"/>
      <stop offset="100%" stop-color="rgba(0, 0, 0, 0)"/>
    </radialGradient>
  </defs>

  <g filter="url(#face-shadow)">
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS}" fill="url(#face-fill)"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS}" fill="url(#face-glow)"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 1}" fill="none" stroke="{palette["edge"]}" stroke-width="2"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 18}" fill="none" stroke="{palette["inner_edge"]}" stroke-width="1"/>
  </g>
"""
    return stage_background_svg(palette, inner)


def build_digital_stage(mode: str) -> str:
    palette = PALETTE_NIGHT if mode == "night" else PALETTE_DAY
    inner = f"""
  <defs>
    <radialGradient id="digital-face-fill" cx="50%" cy="34%" r="56%">
      <stop offset="0%" stop-color="{palette["digital_face_start"]}"/>
      <stop offset="24%" stop-color="{palette["digital_face_mid"]}"/>
      <stop offset="60%" stop-color="{palette["digital_face_inner"]}"/>
      <stop offset="100%" stop-color="{palette["digital_face_end"]}"/>
    </radialGradient>
  </defs>

  <g filter="url(#face-shadow)">
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS}" fill="url(#digital-face-fill)"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 1}" fill="none" stroke="{palette["edge"]}" stroke-width="2"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 18}" fill="none" stroke="{palette["inner_edge"]}" stroke-width="1"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 32}" fill="none" stroke="{palette["digital_ring"]}" stroke-width="2"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 32}" fill="none" stroke="{palette["digital_ring_glow"]}" stroke-width="18" opacity="0.32"/>
    <circle cx="{CENTER}" cy="{FACE_CY}" r="{FACE_RADIUS - 32}" fill="none" stroke="{palette["digital_ring_inner"]}" stroke-width="36" opacity="0.12"/>
  </g>
"""
    return stage_background_svg(palette, inner)


def build_forecast_frame(mode: str) -> str:
    palette = PALETTE_NIGHT if mode == "night" else PALETTE_DAY
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{FORECAST_FRAME_SIZE}" height="{FORECAST_FRAME_SIZE}" viewBox="0 0 {FORECAST_FRAME_SIZE} {FORECAST_FRAME_SIZE}">
  <defs>
    <radialGradient id="forecast-disc-gradient" cx="46%" cy="34%" r="72%">
      <stop offset="0%" stop-color="{palette["forecast_disc_start"]}"/>
      <stop offset="52%" stop-color="{palette["forecast_disc_mid"]}"/>
      <stop offset="100%" stop-color="{palette["forecast_disc_end"]}"/>
    </radialGradient>
  </defs>
  <circle cx="{FORECAST_FRAME_CENTER}" cy="{FORECAST_FRAME_CENTER}" r="232" fill="url(#forecast-disc-gradient)"/>
  <circle cx="{FORECAST_FRAME_CENTER}" cy="{FORECAST_FRAME_CENTER}" r="248" fill="none" stroke="{palette["forecast_ring_outer"]}" stroke-width="3"/>
  <circle cx="{FORECAST_FRAME_CENTER}" cy="{FORECAST_FRAME_CENTER}" r="234" fill="none" stroke="{palette["forecast_ring_inner"]}" stroke-width="2"/>
</svg>
"""


def build_svg(variant: str, mode: str) -> str:
    if variant == "round_stage":
        return build_round_stage(mode)
    if variant == "digital_stage":
        return build_digital_stage(mode)
    if variant == "forecast_frame":
        return build_forecast_frame(mode)
    raise ValueError(f"Unsupported variant: {variant}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Render Pi-style non-analog screen SVG assets.")
    parser.add_argument("--variant", choices=("round_stage", "digital_stage", "forecast_frame"), required=True)
    parser.add_argument("--mode", choices=("day", "night"), default="day")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(build_svg(args.variant, args.mode), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
