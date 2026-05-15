#!/usr/bin/env bash
# Build the embed_demo binary that links against perfetto's trace_processor
# static library.
#
# Layout (cnperf repo):
#   perfetto_demo/perfetto/          Perfetto upstream tree
#   perfetto_demo/embed_demo/        this folder (main.cpp + build.sh)
#   perfetto_demo/perfetto/out/release/   GN output (created on first run)
#
# Prerequisites:
#   Prebuilt gn + ninja live under perfetto/third_party/{gn,ninja}/ after running
#   once (from perfetto root):  tools/install-build-deps --no-dev-tools
#   (Do not pass --ui unless you need the web UI; newer Perfetto removed --no-ui.)
#
# Usage:
#   ./build.sh                    # incremental build
#   ./build.sh clean              # nuke out/ and rebuild from scratch
#   PERFETTO_SRC=/path/to/perfetto ./build.sh   # override tree location
#   PERFETTO_GN_ARGS='is_debug=false is_clang=false extra_cxxflags="-Wno-dangling-reference"' ./build.sh

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PERFETTO_DIR="$(cd "${PERFETTO_SRC:-$HERE/../perfetto}" && pwd)"
OUT_DIR="$PERFETTO_DIR/out/release"

PYTHON="${PYTHON:-python3}"

gn_bin() {
  if [[ -x "$PERFETTO_DIR/third_party/gn/gn" ]]; then
    echo "$PERFETTO_DIR/third_party/gn/gn"
  elif [[ -x "$PERFETTO_DIR/buildtools/linux64/gn" ]]; then
    echo "$PERFETTO_DIR/buildtools/linux64/gn"
  else
    command -v gn
  fi
}

ninja_bin() {
  if [[ -x "$PERFETTO_DIR/third_party/ninja/ninja" ]]; then
    echo "$PERFETTO_DIR/third_party/ninja/ninja"
  elif [[ -x "$PERFETTO_DIR/buildtools/linux64/ninja" ]]; then
    echo "$PERFETTO_DIR/buildtools/linux64/ninja"
  else
    command -v ninja
  fi
}

ensure_host_tools() {
  local have_gn=0 have_nj=0
  if [[ -x "$PERFETTO_DIR/third_party/gn/gn" ]]; then have_gn=1; fi
  if [[ -x "$PERFETTO_DIR/third_party/ninja/ninja" ]]; then have_nj=1; fi
  if (( have_gn && have_nj )); then
    return 0
  fi

  echo ">>> Prebuilt gn/ninja not found under $PERFETTO_DIR/third_party/"
  echo "    Run Perfetto's dependency bootstrap (one-time, needs network):"
  echo "    $PYTHON $PERFETTO_DIR/tools/install-build-deps --no-dev-tools"
  echo ""
  if [[ "${PERFETTO_FETCH_DEPS:-1}" == "0" ]]; then
    echo "PERFETTO_FETCH_DEPS=0: aborting. Install deps manually and retry."
    return 1
  fi
  (cd "$PERFETTO_DIR" && exec "$PYTHON" tools/install-build-deps --no-dev-tools)
}

default_gn_args() {
  if [[ -n "${PERFETTO_GN_ARGS:-}" ]]; then
    echo "$PERFETTO_GN_ARGS"
    return
  fi
  echo "is_debug=false is_clang=false extra_cflags=\"-Wno-comment -Wno-array-bounds\" extra_cxxflags=\"-Wno-dangling-reference -Wno-array-bounds -Wno-comment\""
}

if [[ "${1:-}" == "clean" ]]; then
  echo ">>> clean: removing $OUT_DIR"
  rm -rf "$OUT_DIR"
fi

ensure_host_tools || exit 1

GN="$(gn_bin)"
NJ="$(ninja_bin)"
if [[ -z "$GN" || ! -x "$GN" ]]; then
  echo "error: gn not found. Install with:" >&2
  echo "  (cd \"$PERFETTO_DIR\" && $PYTHON tools/install-build-deps --no-dev-tools)" >&2
  exit 1
fi
if [[ -z "$NJ" || ! -x "$NJ" ]]; then
  echo "error: ninja not found. Install with:" >&2
  echo "  (cd \"$PERFETTO_DIR\" && $PYTHON tools/install-build-deps --no-dev-tools)" >&2
  exit 1
fi

cd "$PERFETTO_DIR"

# 1) Regenerate ninja files (applies updated default_gn_args, e.g. GCC -Wno flags).
echo ">>> gn gen $OUT_DIR  ($(default_gn_args))"
mkdir -p "$OUT_DIR"
"$GN" gen "$OUT_DIR" --args="$(default_gn_args)"

# 2) Build embed demos (pulls in libtrace_processor.a).
echo ">>> ninja embed_demo query_http_demo query_local_demo (first run can take several minutes)"
"$NJ" -C "$OUT_DIR" embed_demo query_http_demo query_local_demo

# 3) Symlink next to this script for convenience.
ln -sfn "../perfetto/out/release/embed_demo" "$HERE/embed_demo"
ln -sfn "../perfetto/out/release/query_http_demo" "$HERE/query_http_demo"
ln -sfn "../perfetto/out/release/query_local_demo" "$HERE/query_local_demo"

echo
echo "=== build OK ==="
echo "binary:  $OUT_DIR/embed_demo"
echo "binary:  $OUT_DIR/query_http_demo"
echo "binary:  $OUT_DIR/query_local_demo"
echo "symlink: $HERE/embed_demo"
echo "symlink: $HERE/query_http_demo"
echo "symlink: $HERE/query_local_demo"
echo
echo "try:    $HERE/embed_demo ../my_trace.perfetto-trace"
echo "http:   $HERE/query_http_demo ../my_trace.perfetto-trace 8765"
echo "local:  $HERE/query_local_demo ../my_trace.perfetto-trace \"SELECT 1\""
