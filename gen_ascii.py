#!/usr/bin/env python3
"""
Generate a random ASCII text file with random line breaks.

Writes in buffered chunks to stay memory-efficient even for very large files.
"""

import argparse
import os
import random
import sys
import time


def human_size(n: int) -> str:
    """Format byte count in human-readable form."""
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024:
            return f"{n:.1f} {unit}"
        n //= 1024
    return f"{n:.1f} PB"


def generate(
    path: str,
    total_bytes: int,
    min_line: int = 40,
    max_line: int = 200,
    buffer_size: int = 8 * 1024 * 1024,  # 8 MB write buffer
    seed: int | None = None,
) -> None:
    """
    Generate a random ASCII text file.

    Args:
        path: Output file path.
        total_bytes: Target file size in bytes.
        min_line: Minimum line length (before newline).
        max_line: Maximum line length (before newline).
        buffer_size: Write buffer size in bytes.
        seed: Random seed for reproducibility.
    """
    rng = random.Random(seed)

    # Printable ASCII range: space (32) to tilde (126) = 95 chars.
    # We exclude \r (13) and \n (10) to keep lines clean.
    PRINTABLE = bytes(range(32, 127))  # 95 bytes

    start_time = time.monotonic()
    written = 0
    last_report = start_time
    last_written = 0

    with open(path, "wb", buffering=buffer_size) as f:
        while written < total_bytes:
            remaining = total_bytes - written

            # Pick a random line length, capped by remaining space
            line_len = rng.randint(min_line, max_line)
            if line_len + 1 > remaining:  # +1 for newline
                line_len = max(0, remaining - 1)

            # Generate random printable characters for this line
            line_bytes = bytes(rng.choices(PRINTABLE, k=line_len))

            # Write line + newline
            f.write(line_bytes + b"\n")
            written += line_len + 1

            # Progress report every 5 seconds
            now = time.monotonic()
            if now - last_report >= 5:
                elapsed = now - start_time
                rate = written / elapsed if elapsed > 0 else 0
                pct = written / total_bytes * 100
                eta = (total_bytes - written) / rate if rate > 0 else 0
                print(
                    f"\r  {human_size(written)} / {human_size(total_bytes)} "
                    f"({pct:.1f}%)  {human_size(int(rate))}/s  "
                    f"ETA: {eta:.0f}s  ",
                    end="",
                    file=sys.stderr,
                )
                last_report = now
                last_written = written

    elapsed = time.monotonic() - start_time
    rate = written / elapsed if elapsed > 0 else 0
    print(
        f"\nDone: {human_size(written)} in {elapsed:.1f}s "
        f"({human_size(int(rate))}/s)",
        file=sys.stderr,
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a random ASCII text file with random line breaks.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s -o data.txt -s 50G
  %(prog)s -o data.txt -s 1G --min-line 20 --max-line 500
  %(prog)s -o data.txt -s 100M --seed 42
        """,
    )
    parser.add_argument(
        "-o", "--output",
        required=True,
        help="Output file path",
    )
    parser.add_argument(
        "-s", "--size",
        required=True,
        help="Target file size (e.g., 50G, 500M, 10K)",
    )
    parser.add_argument(
        "--min-line",
        type=int,
        default=40,
        help="Minimum line length in characters (default: 40)",
    )
    parser.add_argument(
        "--max-line",
        type=int,
        default=200,
        help="Maximum line length in characters (default: 200)",
    )
    parser.add_argument(
        "--buffer-size",
        type=str,
        default="8M",
        help="Write buffer size (default: 8M)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="Random seed for reproducible output",
    )
    args = parser.parse_args()

    # Parse sizes
    total_bytes = parse_size(args.size)
    buffer_size = parse_size(args.buffer_size)

    if args.min_line < 1:
        parser.error("--min-line must be >= 1")
    if args.max_line < args.min_line:
        parser.error("--max-line must be >= --min-line")

    print(f"Generating {human_size(total_bytes)} → {args.output}", file=sys.stderr)
    print(
        f"  Line length: {args.min_line}–{args.max_line} chars, "
        f"buffer: {human_size(buffer_size)}",
        file=sys.stderr,
    )

    generate(
        path=args.output,
        total_bytes=total_bytes,
        min_line=args.min_line,
        max_line=args.max_line,
        buffer_size=buffer_size,
        seed=args.seed,
    )


def parse_size(s: str) -> int:
    """Parse a human-readable size string like '50G', '500M', '10K' into bytes."""
    s = s.strip().upper()
    if not s:
        raise ValueError("empty size string")

    multipliers = {
        "B": 1,
        "K": 1024,
        "KB": 1024,
        "M": 1024**2,
        "MB": 1024**2,
        "G": 1024**3,
        "GB": 1024**3,
        "T": 1024**4,
        "TB": 1024**4,
    }

    for suffix, mul in sorted(multipliers.items(), key=lambda x: -len(x[0])):
        if s.endswith(suffix):
            try:
                num = float(s[: -len(suffix)]) if len(s) > len(suffix) else float(s)
                return int(num * mul)
            except ValueError:
                raise ValueError(f"invalid size: {s!r}") from None

    # Plain number (bytes)
    try:
        return int(s)
    except ValueError:
        raise ValueError(f"invalid size: {s!r}") from None


if __name__ == "__main__":
    main()
