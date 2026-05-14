#!/usr/bin/env python3
"""HTTP client: POST SQL to query_http_demo (optional; prefer demo_local_client.py for no-HTTP)."""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--url",
        default="http://127.0.0.1:8765/query",
        help="query_http_demo /query endpoint",
    )
    parser.add_argument(
        "sql",
        nargs="?",
        default="SELECT name, ts, dur FROM slice ORDER BY ts LIMIT 10",
        help="SQL to send as raw POST body",
    )
    args = parser.parse_args()

    req = urllib.request.Request(
        args.url,
        data=args.sql.encode("utf-8"),
        method="POST",
        headers={"Content-Type": "text/plain; charset=utf-8"},
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", errors="replace")
        print(raw, file=sys.stderr)
        return 1
    except urllib.error.URLError as e:
        print(f"request failed: {e}", file=sys.stderr)
        return 2

    try:
        obj = json.loads(raw)
    except json.JSONDecodeError:
        print(raw)
        return 0

    print(json.dumps(obj, indent=2, ensure_ascii=False))
    if isinstance(obj, dict) and obj.get("error"):
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
