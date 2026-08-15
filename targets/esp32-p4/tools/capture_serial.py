#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import re
import sys
import time

import serial


def open_serial(port: str, baud: int) -> serial.Serial:
    return serial.Serial(port, baud, timeout=0.25)


def reconnect_serial(
    port: str,
    baud: int,
    deadline: float,
    handle,
    reconnect_window: float = 15.0,
) -> serial.Serial:
    reconnect_deadline = min(deadline, time.monotonic() + reconnect_window)

    while time.monotonic() < reconnect_deadline:
        time.sleep(0.25)
        try:
            ser = open_serial(port, baud)
        except (serial.SerialException, OSError):
            continue

        handle.write(f"# serial_reconnected port={port} at={time.time():.3f}\n")
        handle.flush()
        return ser

    raise serial.SerialException(f"Timed out waiting for serial port {port} to reconnect")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Capture ESP32 serial output to a log file.")
    parser.add_argument("--port", required=True, help="Serial device path, for example /dev/cu.usbmodem83201")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--output", required=True, help="Log file to write")
    parser.add_argument("--duration", type=float, default=150.0, help="Maximum capture duration in seconds")
    parser.add_argument("--until", default="", help="Stop after this regex is seen and the settle period elapses")
    parser.add_argument(
        "--require-until",
        action="store_true",
        help="Exit non-zero if --until is provided but never matched before timeout",
    )
    parser.add_argument("--settle-seconds", type=float, default=2.0, help="Extra time to wait after the stop regex")
    parser.add_argument(
        "--reset-before-capture",
        action="store_true",
        help="Pulse RTS before capture so the device restarts after the port is open",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_path = pathlib.Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    stop_pattern = re.compile(args.until) if args.until else None
    start = time.monotonic()
    deadline = start + args.duration
    stop_deadline: float | None = None
    stop_seen = False

    with output_path.open("w", encoding="utf-8") as handle:
        handle.write(f"# port={args.port} baud={args.baud} started_at={time.time():.3f}\n")
        handle.flush()

        ser = open_serial(args.port, args.baud)

        if args.reset_before_capture:
            ser.dtr = False
            ser.rts = True
            time.sleep(0.10)
            ser.rts = False
            time.sleep(0.75)

        try:
            while True:
                now = time.monotonic()
                if now >= deadline:
                    break
                if stop_deadline is not None and now >= stop_deadline:
                    break

                try:
                    raw = ser.readline()
                except (serial.SerialException, OSError) as exc:
                    handle.write(f"# serial_disconnect error={exc} at={time.time():.3f}\n")
                    handle.flush()
                    try:
                        ser.close()
                    except Exception:
                        pass
                    ser = reconnect_serial(args.port, args.baud, deadline, handle)
                    continue

                if not raw:
                    continue

                try:
                    line = raw.decode("utf-8", errors="replace")
                except Exception:
                    line = raw.decode("latin-1", errors="replace")

                sys.stdout.write(line)
                sys.stdout.flush()
                handle.write(line)
                handle.flush()

                if stop_pattern is not None and stop_pattern.search(line):
                    stop_seen = True
                    stop_deadline = time.monotonic() + args.settle_seconds
        finally:
            try:
                ser.close()
            except Exception:
                pass

        handle.write(f"# capture_complete ended_at={time.time():.3f}\n")
        handle.flush()

    if args.require_until and stop_pattern is not None and not stop_seen:
        print(f"ERROR: stop pattern was not observed before timeout: {args.until}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
