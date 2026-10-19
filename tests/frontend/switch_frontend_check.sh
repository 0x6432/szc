#!/bin/sh
# Run from repository root; isolated frontend binary, no sema/backend dependencies.
set -eu
mkdir -p build/switch-frontend
${CC:-cc} -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
  ${CFLAGS:--O0 -g} ${EXTRA_CFLAGS:-} -Isrc \
  src/arena.c src/source.c src/diag.c src/intern.c src/lexer.c src/ast.c src/parser.c src/irgen.c \
  tests/frontend/switch_frontend_protocol.c -o build/switch-frontend/protocol
build/switch-frontend/protocol
