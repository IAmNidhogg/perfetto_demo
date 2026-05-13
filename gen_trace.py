#!/usr/bin/env python3
"""Generate a small but realistic ``my_trace.perfetto-trace`` file.

This script encodes the Perfetto trace protobuf format by hand using only
the Python standard library, so you don't need to install anything.

The resulting file can be:
  * Opened in https://ui.perfetto.dev (drag-and-drop)
  * Queried with ``trace_processor_shell my_trace.perfetto-trace``

Reference of the proto fields used below:
  Trace.packet                                = 1   (repeated TracePacket)
  TracePacket.timestamp                       = 8   (uint64, nanoseconds)
  TracePacket.trusted_packet_sequence_id      = 10  (uint32)
  TracePacket.track_event                     = 11  (TrackEvent)
  TracePacket.track_descriptor                = 60  (TrackDescriptor)
  TrackDescriptor.uuid                        = 1   (uint64)
  TrackDescriptor.name                        = 2   (string)
  TrackDescriptor.process                     = 3   (ProcessDescriptor)
  TrackDescriptor.thread                      = 4   (ThreadDescriptor)
  TrackDescriptor.parent_uuid                 = 5   (uint64)
  TrackDescriptor.counter                     = 8   (CounterDescriptor)
  ProcessDescriptor.pid                       = 1   (int32)
  ProcessDescriptor.process_name              = 6   (string)
  ThreadDescriptor.pid                        = 1   (int32)
  ThreadDescriptor.tid                        = 2   (int32)
  ThreadDescriptor.thread_name                = 5   (string)
  TrackEvent.type                             = 9   (enum)
  TrackEvent.track_uuid                       = 11  (uint64)
  TrackEvent.categories                       = 22  (repeated string)
  TrackEvent.name                             = 23  (string)
  TrackEvent.double_counter_value             = 44  (double)
"""

import struct
from io import BytesIO


# ---------- minimal protobuf wire-format helpers ---------------------------

def _varint(value: int) -> bytes:
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value & 0x7F)
    return bytes(out)


def _tag(field_number: int, wire_type: int) -> bytes:
    return _varint((field_number << 3) | wire_type)


def f_varint(field_number: int, value: int) -> bytes:
    return _tag(field_number, 0) + _varint(value)


def f_string(field_number: int, s: str) -> bytes:
    data = s.encode("utf-8")
    return _tag(field_number, 2) + _varint(len(data)) + data


def f_bytes(field_number: int, b: bytes) -> bytes:
    return _tag(field_number, 2) + _varint(len(b)) + b


def f_double(field_number: int, value: float) -> bytes:
    return _tag(field_number, 1) + struct.pack("<d", value)


# ---------- TrackEvent.Type enum -------------------------------------------

TYPE_SLICE_BEGIN = 1
TYPE_SLICE_END = 2
TYPE_INSTANT = 3
TYPE_COUNTER = 4


# ---------- builders --------------------------------------------------------

def process_descriptor(pid: int, name: str) -> bytes:
    return f_varint(1, pid) + f_string(6, name)


def thread_descriptor(pid: int, tid: int, name: str) -> bytes:
    return f_varint(1, pid) + f_varint(2, tid) + f_string(5, name)


def td_process(uuid: int, pid: int, name: str) -> bytes:
    return f_varint(1, uuid) + f_bytes(3, process_descriptor(pid, name))


def td_thread(uuid: int, parent_uuid: int, pid: int, tid: int, name: str) -> bytes:
    return (f_varint(1, uuid)
            + f_varint(5, parent_uuid)
            + f_bytes(4, thread_descriptor(pid, tid, name)))


def td_counter(uuid: int, parent_uuid: int, name: str) -> bytes:
    # An empty CounterDescriptor is enough to mark the track as a counter.
    counter_desc = b""
    return (f_varint(1, uuid)
            + f_string(2, name)
            + f_varint(5, parent_uuid)
            + f_bytes(8, counter_desc))


def packet_track_descriptor(td: bytes, seq_id: int = 1) -> bytes:
    return f_bytes(60, td) + f_varint(10, seq_id)


def packet_slice_begin(ts: int, track_uuid: int, category: str, name: str,
                       seq_id: int = 1) -> bytes:
    te = (f_string(22, category)
          + f_varint(9, TYPE_SLICE_BEGIN)
          + f_varint(11, track_uuid)
          + f_string(23, name))
    return f_varint(8, ts) + f_bytes(11, te) + f_varint(10, seq_id)


def packet_slice_end(ts: int, track_uuid: int, seq_id: int = 1) -> bytes:
    te = f_varint(9, TYPE_SLICE_END) + f_varint(11, track_uuid)
    return f_varint(8, ts) + f_bytes(11, te) + f_varint(10, seq_id)


def packet_instant(ts: int, track_uuid: int, category: str, name: str,
                   seq_id: int = 1) -> bytes:
    te = (f_string(22, category)
          + f_varint(9, TYPE_INSTANT)
          + f_varint(11, track_uuid)
          + f_string(23, name))
    return f_varint(8, ts) + f_bytes(11, te) + f_varint(10, seq_id)


def packet_counter(ts: int, track_uuid: int, value: float,
                   seq_id: int = 1) -> bytes:
    te = (f_varint(9, TYPE_COUNTER)
          + f_varint(11, track_uuid)
          + f_double(44, value))
    return f_varint(8, ts) + f_bytes(11, te) + f_varint(10, seq_id)


def wrap(packet: bytes) -> bytes:
    # Trace.packet = 1, wire-type 2 (length-delimited)
    return f_bytes(1, packet)


# ---------- main scene ------------------------------------------------------

def main() -> None:
    out = BytesIO()

    # Stable UUIDs for every track we emit.
    PROC_UUID = 0x1001
    MAIN_UUID = 0x2001
    WORKER_UUID = 0x2002
    COUNTER_UUID = 0x3001

    PID, MAIN_TID, WORKER_TID = 1234, 1234, 1235

    # 1) Track descriptors.
    out.write(wrap(packet_track_descriptor(
        td_process(PROC_UUID, PID, "my_app"))))
    out.write(wrap(packet_track_descriptor(
        td_thread(MAIN_UUID, PROC_UUID, PID, MAIN_TID, "main"))))
    out.write(wrap(packet_track_descriptor(
        td_thread(WORKER_UUID, PROC_UUID, PID, WORKER_TID, "worker"))))
    out.write(wrap(packet_track_descriptor(
        td_counter(COUNTER_UUID, PROC_UUID, "mem_usage_mb"))))

    # 2) Main thread: do_request { parse_input; cache_miss (instant); db_query }
    out.write(wrap(packet_slice_begin(1_000_000, MAIN_UUID, "app", "do_request")))
    out.write(wrap(packet_slice_begin(1_100_000, MAIN_UUID, "app", "parse_input")))
    out.write(wrap(packet_slice_end(1_400_000, MAIN_UUID)))
    out.write(wrap(packet_instant(1_500_000, MAIN_UUID, "app", "cache_miss")))
    out.write(wrap(packet_slice_begin(1_600_000, MAIN_UUID, "app", "db_query")))
    out.write(wrap(packet_slice_end(2_200_000, MAIN_UUID)))
    out.write(wrap(packet_slice_end(2_500_000, MAIN_UUID)))

    # 3) Worker thread: compute_task { fft } running in parallel.
    out.write(wrap(packet_slice_begin(1_200_000, WORKER_UUID, "worker", "compute_task")))
    out.write(wrap(packet_slice_begin(1_300_000, WORKER_UUID, "worker", "fft")))
    out.write(wrap(packet_slice_end(1_800_000, WORKER_UUID)))
    out.write(wrap(packet_slice_end(2_000_000, WORKER_UUID)))

    # 4) Memory counter samples.
    for ts, mb in [
        (1_000_000, 120.5),
        (1_300_000, 180.2),
        (1_600_000, 240.8),
        (2_000_000, 210.0),
        (2_500_000, 150.7),
    ]:
        out.write(wrap(packet_counter(ts, COUNTER_UUID, mb)))

    data = out.getvalue()
    path = "my_trace.perfetto-trace"
    with open(path, "wb") as f:
        f.write(data)
    print(f"wrote {path} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
