#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import struct
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def align_up(value: int, multiple: int) -> int:
    if multiple <= 1:
        return value
    remainder = value % multiple
    return value if remainder == 0 else value + multiple - remainder


def paeth_predictor(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(path: Path) -> tuple[int, int, list[list[tuple[int, int, int, int]]]]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path} is not a PNG file")

    width = 0
    height = 0
    color_type = None
    idat_chunks: list[bytes] = []

    offset = len(PNG_SIGNATURE)
    while offset < len(data):
        chunk_length = struct.unpack(">I", data[offset:offset + 4])[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk_data = data[offset + 8:offset + 8 + chunk_length]
        offset += 12 + chunk_length

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, png_filter, interlace = struct.unpack(
                ">IIBBBBB", chunk_data
            )
            if bit_depth != 8:
                raise ValueError(f"{path} uses unsupported bit depth {bit_depth}")
            if color_type not in (2, 6):
                raise ValueError(f"{path} uses unsupported PNG color type {color_type}")
            if compression != 0 or png_filter != 0 or interlace != 0:
                raise ValueError(f"{path} uses unsupported PNG encoding settings")
        elif chunk_type == b"IDAT":
            idat_chunks.append(chunk_data)
        elif chunk_type == b"IEND":
            break

    if width <= 0 or height <= 0 or color_type is None:
        raise ValueError(f"{path} is missing PNG header information")

    bytes_per_pixel = 4 if color_type == 6 else 3
    decompressed = zlib.decompress(b"".join(idat_chunks))
    raw_row_bytes = width * bytes_per_pixel
    cursor = 0
    previous_row = bytearray(raw_row_bytes)
    pixels: list[list[tuple[int, int, int, int]]] = []

    for _ in range(height):
        filter_type = decompressed[cursor]
        cursor += 1
        row = bytearray(decompressed[cursor:cursor + raw_row_bytes])
        cursor += raw_row_bytes

        if filter_type == 1:
            for index in range(raw_row_bytes):
                left = row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                row[index] = (row[index] + left) & 0xFF
        elif filter_type == 2:
            for index in range(raw_row_bytes):
                row[index] = (row[index] + previous_row[index]) & 0xFF
        elif filter_type == 3:
            for index in range(raw_row_bytes):
                left = row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                up = previous_row[index]
                row[index] = (row[index] + ((left + up) // 2)) & 0xFF
        elif filter_type == 4:
            for index in range(raw_row_bytes):
                left = row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                up = previous_row[index]
                up_left = previous_row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                row[index] = (row[index] + paeth_predictor(left, up, up_left)) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"{path} uses unsupported PNG row filter {filter_type}")

        pixel_row: list[tuple[int, int, int, int]] = []
        for index in range(0, raw_row_bytes, bytes_per_pixel):
            red = row[index]
            green = row[index + 1]
            blue = row[index + 2]
            alpha = row[index + 3] if bytes_per_pixel == 4 else 255
            pixel_row.append((red, green, blue, alpha))

        pixels.append(pixel_row)
        previous_row = row

    return width, height, pixels


def pack_rgb565(red: int, green: int, blue: int) -> tuple[int, int]:
    packed = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
    return packed & 0xFF, (packed >> 8) & 0xFF


BAYER_4X4 = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)


def dither_rgb565(red: int, green: int, blue: int, x: int, y: int) -> tuple[int, int, int]:
    """Ordered-dither a pixel so RGB565 truncation does not band smooth gradients.

    pack_rgb565 floors each channel to its quantization step (8 for the 5-bit
    red/blue, 4 for 6-bit green). Adding a sub-step threshold from the Bayer
    matrix before that floor scatters the rounding error spatially, which turns
    the hard steps of a radial gradient into imperceptible noise.
    """
    threshold = BAYER_4X4[y & 3][x & 3] / 16.0
    return (
        min(255, int(red + 8 * threshold)),
        min(255, int(green + 4 * threshold)),
        min(255, int(blue + 8 * threshold)),
    )


def convert_rows(
    pixels: list[list[tuple[int, int, int, int]]],
    fmt: str,
    align: int,
    dither: bool = False,
) -> tuple[list[int], int]:
    width = len(pixels[0])
    if fmt == "rgb565":
        bytes_per_pixel = 2
    elif fmt == "argb8565":
        bytes_per_pixel = 3
    elif fmt == "argb8888":
        bytes_per_pixel = 4
    else:
        raise ValueError(f"Unsupported LVGL format {fmt}")

    stride = align_up(width * bytes_per_pixel, align)
    out: list[int] = []

    for y, row in enumerate(pixels):
        row_bytes = [0] * stride
        cursor = 0
        for x, (red, green, blue, alpha) in enumerate(row):
            if dither and fmt != "argb8888":
                red, green, blue = dither_rgb565(red, green, blue, x, y)
            low, high = pack_rgb565(red, green, blue)
            if fmt == "rgb565":
                row_bytes[cursor:cursor + 2] = [low, high]
            elif fmt == "argb8565":
                row_bytes[cursor:cursor + 3] = [low, high, alpha]
            else:
                # LVGL stores lv_color32_t in memory as blue, green, red, alpha.
                row_bytes[cursor:cursor + 4] = [blue, green, red, alpha]
            cursor += bytes_per_pixel
        out.extend(row_bytes)

    return out, stride


def summarize_pixels(
    pixels: list[list[tuple[int, int, int, int]]],
) -> tuple[int, int, tuple[int, int], tuple[int, int, int, int] | None]:
    nontransparent = 0
    alpha_min = 255
    alpha_max = 0
    min_x = len(pixels[0])
    min_y = len(pixels)
    max_x = -1
    max_y = -1

    for y, row in enumerate(pixels):
        for x, (_, _, _, alpha) in enumerate(row):
            if alpha == 0:
                continue
            nontransparent += 1
            if alpha < alpha_min:
                alpha_min = alpha
            if alpha > alpha_max:
                alpha_max = alpha
            if x < min_x:
                min_x = x
            if y < min_y:
                min_y = y
            if x > max_x:
                max_x = x
            if y > max_y:
                max_y = y

    if nontransparent == 0:
        return 0, len(pixels) * len(pixels[0]), (0, 0), None

    return nontransparent, len(pixels) * len(pixels[0]), (alpha_min, alpha_max), (min_x, min_y, max_x, max_y)


def symbol_macro(symbol: str) -> str:
    return re.sub(r"[^A-Z0-9]+", "_", symbol.upper()).strip("_")


def render_byte_array(values: list[int]) -> str:
    lines: list[str] = []
    for offset in range(0, len(values), 12):
        chunk = values[offset:offset + 12]
        lines.append("  " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def render_header(symbol: str) -> str:
    guard = symbol_macro(symbol)
    return f"""#pragma once

#include "lvgl.h"

extern const lv_image_dsc_t {symbol};
"""


def render_source(symbol: str, header_name: str, fmt: str, width: int, height: int, stride: int, values: list[int]) -> str:
    attr_macro = symbol_macro(symbol)
    lv_format = {
        "rgb565": "LV_COLOR_FORMAT_RGB565",
        "argb8565": "LV_COLOR_FORMAT_ARGB8565",
        "argb8888": "LV_COLOR_FORMAT_ARGB8888",
    }[fmt]
    return f"""#include "lvgl.h"
#include "{header_name}"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_{attr_macro}
#define LV_ATTRIBUTE_IMAGE_{attr_macro}
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_{attr_macro} uint8_t {symbol}_map[] = {{
{render_byte_array(values)}
}};

const lv_image_dsc_t {symbol} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = {lv_format},
    .flags = 0,
    .w = {width},
    .h = {height},
    .stride = {stride},
    .reserved_2 = 0,
  }},
  .data_size = sizeof({symbol}_map),
  .data = {symbol}_map,
  .reserved = NULL,
}};
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a PNG into a compiled LVGL image descriptor.")
    parser.add_argument("--input", required=True, help="Input PNG file")
    parser.add_argument("--symbol", required=True, help="C symbol name")
    parser.add_argument("--format", choices=("rgb565", "argb8565", "argb8888"), required=True)
    parser.add_argument("--output-c", required=True, help="Destination C source")
    parser.add_argument("--output-h", required=True, help="Destination header")
    parser.add_argument("--align", type=int, default=4, help="Stride alignment in bytes")
    parser.add_argument(
        "--dither",
        action="store_true",
        help="Ordered-dither before RGB565 quantization (for smooth gradients)",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    output_c_path = Path(args.output_c)
    output_h_path = Path(args.output_h)
    width, height, pixels = decode_png(input_path)
    values, stride = convert_rows(pixels, args.format, args.align, args.dither)
    nontransparent, total_pixels, alpha_range, bbox = summarize_pixels(pixels)

    output_c_path.parent.mkdir(parents=True, exist_ok=True)
    output_h_path.parent.mkdir(parents=True, exist_ok=True)

    output_h_path.write_text(render_header(args.symbol), encoding="utf-8")
    output_c_path.write_text(
        render_source(args.symbol, output_h_path.name, args.format, width, height, stride, values),
        encoding="utf-8",
    )

    bbox_text = "none"
    if bbox is not None:
        bbox_text = f"{bbox[0]},{bbox[1]}-{bbox[2]},{bbox[3]}"
    print(
        f"{args.symbol}: {width}x{height} format={args.format} stride={stride} bytes={len(values)} "
        f"nontransparent={nontransparent}/{total_pixels} alpha={alpha_range[0]}..{alpha_range[1]} bbox={bbox_text}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
