# szc

A Zig 0.17.0 compiler written in C11, used to **bootstrap the official Zig
compiler** on x86_64-linux. It emits [QBE](https://c9x.me/compile/) IL, then
runs `qbe` and the system `cc` to assemble and link.

It isn't a full Zig toolchain: it only implements the core language and the
builtins the Zig compiler and its std need. There are no SPIR-V, wasm or other backends.

## Status

See [docs/MILESTONES.md](docs/MILESTONES.md). The lexer and parser are complete. Minimal integer/bool programs compile
through typed IR and QBE to native executables; arithmetic safety is next.

## Build & test

```sh
make                 # build/szc
make test            # unit + golden tests under ASan/UBSan
make fuzz-standalone # replay fuzz corpus + random mutations (gcc ok)
make fuzz fuzz-run   # libFuzzer (clang)
```

```sh
build/szc tokens file.zig
build/szc tir file.zig
build/szc emit-qbe file.zig
build/szc build-exe file.zig -o program --keep-temps
build/szc run file.zig
```

QBE must be on `PATH` (or set `SZC_QBE`); `SZC_CC` selects the system C compiler.
Both variables name executables, not shell commands. QBE revision used for testing:
`e786f06032fefa2e3790d6b1c9e31ed138f475a6`.

## Layout

| Path | Purpose |
|---|---|
| `src/lexer.c` | on-demand tokenizer |
| `src/parser.c`, `src/ast.h` | recursive descent parser, one struct type per AST node |
| `src/irgen.c` | AST → untyped IR |
| `src/sema.c` | semantic analysis → typed IR |
| `src/comptime.c` | comptime interpreter, big integers |
| `src/codegen.c` | typed IR → QBE IL |
| `src/arena.c`, `src/diag.c`, `src/source.c` | memory, diagnostics, files |
| `docs/` | design, style, milestones, handoff, grammar |
| `tests/` | unit and golden tests |
| `fuzz/` | fuzz targets and seed corpora |

Standard library: szc uses the `lib/` that ships with Zig 0.17.0, found via
`ZIG_LIB_DIR`, `<szc dir>/../lib/zig` or `<szc dir>/../lib`.

`tools/reference/tokenizer.zig` and `docs/grammar.peg` are from the Zig project (MIT license).
