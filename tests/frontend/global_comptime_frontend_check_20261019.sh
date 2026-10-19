#!/bin/sh
# Isolated frontend-only namespace statement policy; no sema/backend needed.
set -eu
mkdir -p build/global-comptime-frontend
${CC:-cc} -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror \
  ${CFLAGS:--O0 -g} ${EXTRA_CFLAGS:-} -Isrc \
  src/arena.c src/source.c src/diag.c src/intern.c src/lexer.c src/ast.c src/parser.c src/irgen.c \
  tests/frontend/global_comptime_frontend_policy_20261019.c -o build/global-comptime-frontend/policy
build/global-comptime-frontend/policy
