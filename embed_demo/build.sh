#!/usr/bin/env bash
# Build the embed_demo binary that links against perfetto's trace_processor
# static library.
#
# Layout:
#   ../perfetto/                 perfetto source tree (clone of google/perfetto)
#   ../perfetto/embed_demo/      BUILD.gn + symlink to this folder's main.cpp
#   ../perfetto/out/release/     GN build output (created on first run)
#
# Usage:
#   ./build.sh           # incremental build
#   ./build.sh clean     # nuke out/ and rebuild from scratch

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PERFETTO_DIR="$(cd "$HERE/../perfetto" && pwd)"
OUT_DIR="$PERFETTO_DIR/out/release"

if [[ "${1:-}" == "clean" ]]; then
  echo ">>> clean: removing $OUT_DIR"
  rm -rf "$OUT_DIR"
fi

cd "$PERFETTO_DIR"

# 1) Generate ninja files (idempotent; fast on no-op).
if [[ ! -f "$OUT_DIR/build.ninja" ]]; then
  echo ">>> gn gen $OUT_DIR"
  tools/gn gen "$OUT_DIR" --args='is_debug=false is_clang=true'
fi

# 2) Build only the embed_demo target. Ninja will transitively build
#    libtrace_processor.a and all its deps.
echo ">>> ninja embed_demo (this also builds libtrace_processor.a; first time can take several minutes)"
tools/ninja -C "$OUT_DIR" embed_demo

# 3) Drop a convenience symlink next to the source. We point to a path
#    relative to the workspace root, not absolute, so the symlink keeps
#    working if the whole tree is moved.
ln -sfn "../perfetto/out/release/embed_demo" "$HERE/embed_demo"

echo
echo "=== build OK ==="
echo "binary:  $OUT_DIR/embed_demo"
echo "symlink: $HERE/embed_demo"
echo
echo "try:    $HERE/embed_demo ../my_trace.perfetto-trace"
