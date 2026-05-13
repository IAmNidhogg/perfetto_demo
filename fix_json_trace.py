#!/usr/bin/env python3
"""Normalize a Chrome-trace-style JSON so Perfetto Trace Processor can parse it.

Problem:
  Perfetto's JSON parser requires ``pid`` / ``tid`` to be integers. Some tools
  (e.g. cnperf-cli) emit human-readable strings such as
  ``"Device0 task[Sync]"`` for pid or ``"[PID 4587] Ctx:1 Q:0"`` for tid.

This script:
  1. Maps each unique string pid/tid to a stable, deterministic integer ID
     (collision-free with existing integer pids/tids in the file).
  2. Emits ``process_name`` / ``thread_name`` metadata events so the original
     human-readable name still shows up in Perfetto UI and ``thread`` /
     ``process`` SQL tables.
  3. Drops events that are unusable after fixing (missing ``ph``, etc.).
"""

import json
import sys
from pathlib import Path


def normalize(in_path: Path, out_path: Path) -> None:
    with in_path.open() as f:
        doc = json.load(f)

    events = doc.get("traceEvents", [])
    print(f"input events: {len(events)}")

    # Collect existing integer ids to avoid collisions when allocating new ones.
    int_pids = {e["pid"] for e in events if isinstance(e.get("pid"), int)}
    int_tids = {e["tid"] for e in events if isinstance(e.get("tid"), int)}

    # Allocate IDs starting well above anything we've seen.
    next_pid = max(int_pids, default=0) + 1_000_000
    next_tid = max(int_tids, default=0) + 1_000_000

    pid_map: dict[str, int] = {}   # original string pid -> int pid
    tid_map: dict[tuple[int, str], int] = {}   # (resolved_pid, original tid) -> int tid

    def resolve_pid(raw):
        nonlocal next_pid
        if isinstance(raw, int):
            return raw
        if raw is None:
            return 0  # unknown process
        if raw not in pid_map:
            pid_map[raw] = next_pid
            next_pid += 1
        return pid_map[raw]

    def resolve_tid(raw, pid):
        nonlocal next_tid
        if isinstance(raw, int):
            return raw
        if raw is None:
            return pid  # use process main-thread convention
        key = (pid, raw)
        if key not in tid_map:
            tid_map[key] = next_tid
            next_tid += 1
        return tid_map[key]

    fixed: list[dict] = []
    dropped = 0
    for e in events:
        if not isinstance(e, dict) or "ph" not in e or e["ph"] is None:
            dropped += 1
            continue
        new = dict(e)
        new_pid = resolve_pid(e.get("pid"))
        new_tid = resolve_tid(e.get("tid"), new_pid)
        new["pid"] = new_pid
        new["tid"] = new_tid
        fixed.append(new)

    # Synthesize metadata so the UI/SQL still shows the original names.
    metadata = []
    for name, pid in pid_map.items():
        metadata.append({
            "name": "process_name", "ph": "M", "pid": pid,
            "args": {"name": name},
        })
    for (pid, name), tid in tid_map.items():
        metadata.append({
            "name": "thread_name", "ph": "M", "pid": pid, "tid": tid,
            "args": {"name": name},
        })

    out_doc = dict(doc)
    out_doc["traceEvents"] = metadata + fixed

    with out_path.open("w") as f:
        json.dump(out_doc, f, separators=(",", ":"))

    print(f"output events: {len(out_doc['traceEvents'])}"
          f" (= {len(metadata)} metadata + {len(fixed)} data; dropped {dropped})")
    print("string pid -> int mapping:")
    for k, v in pid_map.items():
        print(f"  {v}\t<- {k!r}")
    print("string tid -> int mapping (showing first 10):")
    for (pid, k), v in list(tid_map.items())[:10]:
        print(f"  pid={pid} tid={v}\t<- {k!r}")


if __name__ == "__main__":
    src = Path(sys.argv[1] if len(sys.argv) > 1 else "timechart_data.json")
    dst = Path(sys.argv[2] if len(sys.argv) > 2 else src.with_suffix(".fixed.json"))
    normalize(src, dst)
    print(f"wrote {dst}")
