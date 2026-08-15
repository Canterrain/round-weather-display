#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys


SUMMARY_PREFIX = "BENCH_STAGE_SUMMARY "


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Parse ESP32-P4 benchmark summary lines.")
    parser.add_argument("logfile", help="Path to a captured serial log")
    parser.add_argument("--format", choices=("json", "markdown"), default="json")
    return parser.parse_args()


def parse_summary_line(line: str) -> dict[str, str]:
    payload = line.split(SUMMARY_PREFIX, 1)[1].strip()
    fields: dict[str, str] = {}
    for part in payload.split():
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        fields[key] = value
    return fields


def coerce_value(value: str) -> object:
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    if re.fullmatch(r"-?\d+\.\d+", value):
        number = float(value)
        if number < 0:
            return None
        return number
    return value


def parse_log(path: pathlib.Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if SUMMARY_PREFIX not in line:
                continue
            row = {key: coerce_value(value) for key, value in parse_summary_line(line).items()}
            rows.append(row)
    return rows


def render_markdown(rows: list[dict[str, object]]) -> str:
    headers = [
        "pass",
        "stage",
        "redraw",
        "build_ms",
        "activation_frame_ms",
        "steady_frames",
        "steady_avg_frame_ms",
        "steady_p95_frame_ms",
        "steady_max_frame_ms",
        "internal_min",
        "psram_min",
        "lvgl_min",
        "lvgl_used_pct",
        "lvgl_frag_pct",
        "draw_buffer_failures",
        "lock_failures",
        "refresh_failures",
        "object_alloc_failures",
        "stall_warnings",
    ]
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        values = [str(row.get(header, "")) for header in headers]
        lines.append("| " + " | ".join(values) + " |")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    path = pathlib.Path(args.logfile)
    rows = parse_log(path)
    if args.format == "json":
        json.dump(rows, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        sys.stdout.write(render_markdown(rows))
        sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
