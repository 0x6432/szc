#!/bin/sh
# Direct-metadata renderer vectors, no Zig programs or native fixtures.
# 2026-10-22 raw-byte parity replaces the obsolete quoted ASCII-only policy.
# Usage: tests/error_set_type_name_20261019.sh [SOURCE_ROOT [FROZEN_SRC_OVERLAY]]
# Optional immutable overlay is copied into a PRIVATE temporary source tree;
# original sources are never changed. Caller records base/overlay source hashes.
# Sema is opaque: unit includes sema.c ONCE; exclude it from the source list.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=${1:-$(dirname "$HERE")}
ROOT=$(CDPATH= cd -- "$ROOT" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/szc-error-set-name-20261019.XXXXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir "$TMP/src"
cp "$ROOT"/src/*.[ch] "$TMP/src/"
if [ "$#" -ge 2 ]; then
    OVERLAY=$(CDPATH= cd -- "$2" && pwd)
    # The authorized frozen implementation owns exactly these four files.
    for FILE in ir.h irgen.c sema.h sema.c; do
        cp "$OVERLAY/$FILE" "$TMP/src/$FILE"
    done
fi
# Existing bounded process helper owns finite execution and cleanup deadlines.
bounded() {
    PYTHONPATH="$ROOT/tools${PYTHONPATH:+:$PYTHONPATH}" python3 -c '
import subprocess, sys
from compiler_process import run
try:
    result = run(sys.argv[2:], timeout=float(sys.argv[1]))
except subprocess.TimeoutExpired as error:
    sys.stdout.buffer.write(error.output or b"")
    sys.stderr.buffer.write(error.stderr or b"")
    raise
sys.stdout.buffer.write(result.stdout)
sys.stderr.buffer.write(result.stderr)
sys.exit(result.returncode if result.returncode >= 0 else 128 - result.returncode)
' "$@"
}
CC=${CC:-cc}
# Intentional splitting of conventional CC/CFLAGS variables; paths are quoted.
SOURCES="arena source diag intern lexer ast parser irgen comptime struct_layout codegen driver"
set --
for SOURCE in $SOURCES; do set -- "$@" "$TMP/src/$SOURCE.c"; done
FAILED=0
for MODE in normal sanitized; do
    SAN=''
    if [ "$MODE" = sanitized ]; then
        SAN='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all'
    fi
    echo "BUILD $MODE internal renderer (CC=$CC)"
    bounded 180 $CC -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
        -Wmissing-prototypes -Werror ${CFLAGS:--O0 -g} $SAN \
        -I"$TMP/src" "$HERE/error_set_type_name_20261019.c" "$@" -o "$TMP/$MODE"
    echo "RUN $MODE internal renderer; no compiler fixture execution"
    if bounded 20 "$TMP/$MODE"; then :; else
        STATUS=$?
        echo "FAIL $MODE renderer status=$STATUS" >&2
        FAILED=1
    fi
done
exit "$FAILED"
