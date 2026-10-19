#!/bin/sh
# Complete original protocol plus correction-specific owner/source tests; no sema.
set -eu
mkdir -p build/switch-next-frontend
${CC:-cc} -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
  ${CFLAGS:--O0 -g} ${EXTRA_CFLAGS:-} -Isrc \
  src/arena.c src/source.c src/diag.c src/intern.c src/lexer.c src/ast.c src/parser.c src/irgen.c \
  tests/frontend/switch_next_owner_protocol.c -o build/switch-next-frontend/owner-protocol
build/switch-next-frontend/owner-protocol
