# szc

A Zig 0.17.0 compiler written in C11, used to **bootstrap the official Zig
compiler** on x86_64-linux. It emits [QBE](https://c9x.me/compile/) IL, then
runs `qbe` and the system `cc` to assemble and link.

It isn't a full Zig toolchain: it only implements the core language and the
builtins the Zig compiler and its std need. There are no SPIR-V, wasm or other backends.

## Status

See [docs/MILESTONES.md](docs/MILESTONES.md). The lexer and parser are complete. Minimal integer/bool programs compile
through typed IR and QBE to native executables with checked arithmetic and
integer casts, saturating operations and explicit division/remainder builtins.
Source-level `if`/`else`, comparisons and boolean short-circuiting work, including
value-producing branches and early returns. Runtime `while`, labeled break/continue,
continue expressions, loop else/values and compound assignments also work.
Labeled blocks support named value breaks, nested exits, typed joins and
constant-only comptime evaluation. Runtime range/array/slice `for` supports multiple
inputs, unbounded companion counters, captures, labels and loop else/values.
`defer` runs in reverse registration order across scope fallthrough, return,
break and continue. Inline/comptime loops and `errdefer`
remain pending. Explicit `unreachable` aborts through the safety panic runtime;
noreturn return/cast diagnostics are checked against Zig 0.17. `@panic` accepts
first-class string/byte-array values, byte slices, aliases, variables and runtime
choices with dynamic message lengths. Literal bytes, escapes, Unicode and multiline
strings are supported; general slicing and string operations remain pending.

Fixed-size arrays support typed literals, checked indexing, mutable/immutable backing,
owned snapshots, function arguments/returns and scalar-element mutation. Single/many
pointers support qualifiers, array-to-many coercion, borrowed indexing, element offsets,
differences and `@intFromPtr` addresses. `@sizeOf`/`@alignOf`/`@bitSizeOf` query supported layouts and representations;
unsentinelled slices have pointer/length descriptors, `.ptr`/`.len`, indexing,
array/string coercions, storage and internal calls/returns. Differently sized
unsentinelled array pointers can infer a slice at runtime joins. Array/slice loops
support values and pointer captures; slicing syntax remains pending. Integer ABI sizes and pointer strides include
widths beyond 64 bits, but runtime
wide-integer values/arithmetic are still unsupported. Structs, sentinel slices, modules/imports,
generic functions and the full comptime interpreter remain pending; szc does not yet
bootstrap Zig.

## Build & test

```sh
make                 # build/szc
make test            # unit + golden tests under ASan/UBSan
make e2e-test        # executable + driver + panic tests (needs qbe)
make safety-test     # runtime panic checks only (needs qbe)
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

Aligned local variables/constants and internal mutable/constant globals are supported
for the currently implemented value types. Function declarations and their addresses
also preserve explicit alignment; code alignment above 16 needs the assembly companion.
Static data alignment above 64 bytes needs an assembly companion because of QBE's data-alignment
limit: use `build-exe` or `run`; standalone `emit-qbe` reports this limitation.
`--keep-temps` retains the complete assembly, including any outlined data.

Volatile single-pointer qualifiers and accesses work for supported scalar/bool/pointer
values. Accesses use opaque outlined QBE helpers so unused reads and consecutive
stores survive optimization. Volatile is not atomic or a synchronization primitive.

```sh
tools/build_qbe.sh
PATH="$PWD/build/qbe-src:$PATH" make e2e-test
# Also compare every program's exit status/stdout with official Zig:
PATH="$PWD/build/qbe-src:$PATH" ZIG=/path/to/zig make e2e-test
```

Executable tests start with `// exit: N`; an optional matching `.stdout` file
specifies byte-exact stdout (otherwise it must be empty). Tests distinguish
compilation failures from nonzero program exits. Generated tests exercise all
signed/unsigned widths from 1 to 64 bits, including conditional/loop/block result joins; driver tests cover tool failures,
intermediate retention/cleanup and paths containing spaces. Runtime safety tests
check panic signals/messages; when `ZIG` is set they also validate behavior against
the official compiler. Safety timeouts terminate the command's entire process group,
including compiler descendants; `e2e-test` and `safety-test` first verify cleanup
and captured-output behavior. `tests/run_szc` separately records known reference-backend
edge discrepancies (currently signed minimum remainder by -1).

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

Planned standard-library resolution uses the `lib/` that ships with Zig 0.17.0, via
`ZIG_LIB_DIR`, `<szc dir>/../lib/zig` or `<szc dir>/../lib`.

`tools/reference/tokenizer.zig` and `docs/grammar.peg` are from the Zig project (MIT license).
