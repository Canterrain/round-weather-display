#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
from pathlib import Path

import cairosvg


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Rasterize an SVG into a PNG with preserved transparency.")
    parser.add_argument("input", help="Input SVG path")
    parser.add_argument("output", help="Output PNG path")
    parser.add_argument("size", type=int, nargs="?", help="Optional square output size in pixels")
    parser.add_argument("--width", type=int, help="Explicit output width in pixels")
    parser.add_argument("--height", type=int, help="Explicit output height in pixels")
    parser.add_argument(
        "--blur",
        type=float,
        default=0.0,
        help=(
            "Gaussian blur radius in pixels, applied to the rasterized PNG after rendering. "
            "cairosvg does not implement SVG <filter>/feGaussianBlur (it silently drops them), "
            "so any blur must happen here as a post-process step instead of in the SVG itself."
        ),
    )
    return parser.parse_args()


def apply_gaussian_blur_rgba(image, radius: float):
    """Blur an RGBA image with a straight (non-premultiplied) alpha-safe Gaussian blur.

    A naive per-channel blur mixes the RGB of fully-transparent pixels (which is
    undefined/black in most PNGs) into the visible edge, producing a dark fringe.
    Premultiplying by alpha before blurring, then un-premultiplying after, avoids that.
    """
    import numpy as np
    from PIL import Image, ImageFilter

    def blur_channel(channel: "np.ndarray") -> "np.ndarray":
        band = Image.fromarray(np.clip(channel, 0, 255).astype("uint8"), mode="L")
        blurred = band.filter(ImageFilter.GaussianBlur(radius))
        return np.asarray(blurred).astype("float32")

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


def parse_viewbox_dimensions(svg_text: str) -> tuple[int, int] | None:
    match = re.search(r'viewBox="[^"]*?(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)"', svg_text)
    if not match:
      return None

    width = int(round(float(match.group(3))))
    height = int(round(float(match.group(4))))
    if width <= 0 or height <= 0:
      return None
    return width, height


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    output_width = args.width
    output_height = args.height

    if output_width is None and output_height is None and args.size is not None:
        output_width = args.size
        output_height = args.size

    if output_width is None or output_height is None:
        dimensions = parse_viewbox_dimensions(input_path.read_text(encoding="utf-8"))
        if dimensions is None:
            raise ValueError(f"Could not determine output size for {input_path}")
        if output_width is None:
            output_width = dimensions[0]
        if output_height is None:
            output_height = dimensions[1]

    if args.blur > 0:
        from io import BytesIO

        from PIL import Image

        png_bytes = cairosvg.svg2png(
            url=str(input_path),
            output_width=output_width,
            output_height=output_height,
        )
        image = Image.open(BytesIO(png_bytes))
        blurred = apply_gaussian_blur_rgba(image, args.blur)
        blurred.save(output_path)
    else:
        cairosvg.svg2png(
            url=str(input_path),
            write_to=str(output_path),
            output_width=output_width,
            output_height=output_height,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
