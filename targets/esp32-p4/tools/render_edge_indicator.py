#!/usr/bin/env python3
"""Procedurally renders the house-messages edge-glow indicator.

This can't be authored as a normal hand-drawn SVG shape like the other
analog layers: the Pi reference (targets/pi/public/style.css,
`.message-edge-indicator::before`) is a CSS `conic-gradient` (color varies
by *angle* around the face) combined with a `mask: radial-gradient(...)`
(visibility varies by *radius*, confining the glow to a band near the rim
instead of reaching all the way to the center) and a `filter: blur(6px)`.
None of that -- conic gradients, mask radial-gradients, or blur filters --
has any SVG equivalent that cairosvg (this pipeline's rasterizer) actually
implements, so this renders the identical math directly, pixel by pixel.

Source of truth: targets/pi/public/style.css, `.message-edge-indicator::before`.
Keep this in sync if that CSS ever changes.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

STAGE_SIZE = 800
# Matches the live face/hand coordinate system in app_ui.c (ANALOG_CENTER_X/Y,
# ANALOG_FACE_SIZE=760/2=380) -- NOT the baked stage image's Y-trim, since this
# is a separate runtime lv_image object aligned directly on the 800x800 canvas.
FACE_CENTER = STAGE_SIZE / 2  # 400
FACE_BOX_RADIUS = 380  # ANALOG_FACE_SIZE / 2, matches Pi's 760px .clock-face box
MASK_BOX_RADIUS = FACE_BOX_RADIUS - 10  # Pi's `inset: 10px` -> 740px box -> r=370
BLUR_RADIUS_PX = 6  # Pi's `filter: blur(6px)` on the base (non-important) state

GRADIENT_FROM_DEG = 218.0
# (angle_deg, alpha) stops from the CSS conic-gradient, angle measured from
# GRADIENT_FROM_DEG going clockwise (matches CSS `from` semantics).
ANGLE_STOPS = [
    (0.0, 0.0),
    (2.0, 0.0),
    (10.0, 0.08),
    (30.0, 0.28),
    (54.0, 0.52),
    (78.0, 0.34),
    (94.0, 0.12),
    (106.0, 0.0),
    (360.0, 0.0),
]
COLOR_RGB = (255, 74, 74)

# (radius_px_from_edge, alpha) stops from the CSS mask radial-gradient, where
# radius is expressed as "farthest-side minus N px" per the original
# `calc(100% - Npx)` values, converted here to absolute px from center.
_MASK_STOPS_FROM_EDGE = [
    (132, 0.0),
    (108, 0.12),
    (80, 0.42),
    (46, 0.82),
    (14, 1.0),
    (2, 1.0),
    (0, 0.0),
]
RADIUS_STOPS = sorted(
    ((MASK_BOX_RADIUS - offset, alpha) for offset, alpha in _MASK_STOPS_FROM_EDGE),
    key=lambda pair: pair[0],
)


def _piecewise_interp(x: np.ndarray, stops: list[tuple[float, float]]) -> np.ndarray:
    out = np.zeros_like(x)
    out[x <= stops[0][0]] = stops[0][1]
    out[x >= stops[-1][0]] = stops[-1][1]
    for (x0, y0), (x1, y1) in zip(stops[:-1], stops[1:]):
        mask = (x >= x0) & (x < x1)
        span = (x1 - x0) or 1.0
        t = (x[mask] - x0) / span
        out[mask] = y0 + t * (y1 - y0)
    return out


def render_alpha(size: int) -> np.ndarray:
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    dx = xs - FACE_CENTER
    dy = ys - FACE_CENTER
    radius = np.sqrt(dx * dx + dy * dy)

    # CSS conic-gradient angle: 0deg = 12 o'clock (up, -Y), increasing clockwise.
    angle_deg = np.degrees(np.arctan2(dx, -dy)) % 360.0
    rel_deg = (angle_deg - GRADIENT_FROM_DEG) % 360.0

    angle_alpha = _piecewise_interp(rel_deg, ANGLE_STOPS)
    radius_alpha = _piecewise_interp(radius, RADIUS_STOPS)
    # Beyond the mask's own farthest-side radius, CSS masks clip to nothing.
    radius_alpha = np.where(radius <= MASK_BOX_RADIUS, radius_alpha, 0.0)

    return angle_alpha * radius_alpha


def gaussian_blur_rgba(image: Image.Image, radius: float) -> Image.Image:
    """Alpha-safe (premultiplied) Gaussian blur -- avoids dark fringing at edges."""

    def blur_channel(channel: np.ndarray) -> np.ndarray:
        band = Image.fromarray(np.clip(channel, 0, 255).astype("uint8"), mode="L")
        return np.asarray(band.filter(ImageFilter.GaussianBlur(radius))).astype("float32")

    rgba = np.asarray(image.convert("RGBA")).astype("float32")
    r, g, b, a = rgba[..., 0], rgba[..., 1], rgba[..., 2], rgba[..., 3]
    alpha_norm = a / 255.0

    pr_blurred = blur_channel(r * alpha_norm)
    pg_blurred = blur_channel(g * alpha_norm)
    pb_blurred = blur_channel(b * alpha_norm)
    a_blurred = blur_channel(a)

    safe_alpha = np.where(a_blurred > 1.0, a_blurred / 255.0, 1.0)
    out = np.stack(
        [
            np.clip(pr_blurred / safe_alpha, 0, 255),
            np.clip(pg_blurred / safe_alpha, 0, 255),
            np.clip(pb_blurred / safe_alpha, 0, 255),
            np.clip(a_blurred, 0, 255),
        ],
        axis=-1,
    ).astype("uint8")
    return Image.fromarray(out, mode="RGBA")


def render(output_path: Path, size: int = STAGE_SIZE) -> None:
    alpha = render_alpha(size)

    rgba = np.zeros((size, size, 4), dtype=np.uint8)
    rgba[..., 0] = COLOR_RGB[0]
    rgba[..., 1] = COLOR_RGB[1]
    rgba[..., 2] = COLOR_RGB[2]
    rgba[..., 3] = np.clip(alpha * 255.0, 0, 255).astype(np.uint8)

    image = Image.fromarray(rgba, mode="RGBA")
    if BLUR_RADIUS_PX > 0:
        # Blur over the full canvas first (so the kernel has real context at
        # the edges of the visible glow, not an artificial crop boundary),
        # THEN crop down to the bounding box below. Cropping before blurring
        # would risk edge artifacts for no size benefit, since the crop
        # happens right after anyway.
        image = gaussian_blur_rgba(image, BLUR_RADIUS_PX)

    # Only ~12% of the full 800x800 canvas is ever non-transparent (this is
    # a glow confined to one side of the face). Storing the full canvas as
    # ARGB8888 costs 2.56MB of flash for a background that's ~88% blank --
    # on top of the other analog assets, that was enough to push this
    # firmware's total flash-mapped (DROM/IROM) content past the ~16MB
    # window the ESP32-P4's bootloader can map, which silently truncated the
    # image on flash (confirmed on-device: bootloader read real segment data
    # up to ~16.55MB in, then found zeros where the next segment header
    # should have been -- reproduced identically across a full chip erase +
    # reflash, ruling out a bad/worn flash sector). Cropping to the actual
    # content bounding box (plus a small margin) avoids storing dead
    # transparent space. app_ui.c's ANALOG_EDGE_INDICATOR_X/Y must match
    # this crop's offset -- this prints it so that's easy to keep in sync.
    alpha_channel = np.asarray(image)[..., 3]
    nonzero_rows, nonzero_cols = np.nonzero(alpha_channel > 0)
    margin = 4
    min_x = max(0, int(nonzero_cols.min()) - margin)
    max_x = min(size, int(nonzero_cols.max()) + 1 + margin)
    min_y = max(0, int(nonzero_rows.min()) - margin)
    max_y = min(size, int(nonzero_rows.max()) + 1 + margin)

    image = image.crop((min_x, min_y, max_x, max_y))
    image.save(output_path)
    print(f"edge indicator crop: x={min_x} y={min_y} size={max_x - min_x}x{max_y - min_y}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, help="Output PNG path")
    parser.add_argument("--size", type=int, default=STAGE_SIZE)
    args = parser.parse_args()

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    render(output_path, args.size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
