#!/usr/bin/env python3
"""Local client: run query_local_demo (no HTTP), pretty-print JSON."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    here = Path(__file__).resolve().parent
    default_bin = here / "query_local_demo"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bin",
        type=Path,
        default=default_bin,
        help="path to query_local_demo binary",
    )
    parser.add_argument("trace", type=Path, help="trace file for Trace Processor")
    parser.add_argument(
        "sql",
        nargs="?",
        default="SELECT name, ts, dur FROM slice ORDER BY ts LIMIT 10",
        help="SQL (omit to use default)",
    )
    args = parser.parse_args()

    if not args.bin.is_file():
        print(f"missing binary: {args.bin} (run ./build.sh in embed_demo/)", file=sys.stderr)
        return 2

    proc = subprocess.run(
        [str(args.bin), str(args.trace), args.sql],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    raw = proc.stdout.strip()
    if not raw:
        return proc.returncode or 1
    try:
        obj = json.loads(raw)
    except json.JSONDecodeError:
        print(raw)
        return proc.returncode
    print(json.dumps(obj, indent=2, ensure_ascii=False))
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
