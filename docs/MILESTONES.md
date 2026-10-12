# szc milestones

Every milestone is small enough to finish, test and commit in one pass.
Each finished milestone records what it can't do yet ("Limitations") and how it
depends on or feeds other stages ("Interactions").

Status: `[x]` done, `[~]` in progress, `[ ]` not started.

## Phase 0: Infrastructure

- [x] **M0.1 Repo skeleton**: Makefile, `.editorconfig`, README, docs (design, style, handoff).
- [x] **M0.2 Arena allocator**: chunked bump allocator, overflow-checked, ASan poisoning; unit tests.
- [x] **M0.3 Diagnostics**: collect errors (file, byte offset, message), print sorted with line:col and a caret.
- [x] **M0.4 Source loading**: read file, NUL-terminate, file table for diagnostics.
- [x] **M0.5 CI**: GitHub Actions: gcc + clang builds with `-Werror`, ASan/UBSan tests, short libFuzzer run, nightly long fuzz.

## Phase 1: Lexer (`src/lexer.c`)

- [x] **M1.1 Token kinds + punctuation/operators**: every 0.17.0 operator token.
- [x] **M1.2 Identifiers, keywords, builtins, `@"..."` identifiers**: keyword lookup by length and first byte (no linear strcmp scan).
- [x] **M1.3 Literals**: number, char, string and multiline string tokens (same rules as the reference tokenizer).
- [x] **M1.4 Comments**: line, doc (`///`) and container doc (`//!`) comments, CRLF, error recovery up to the next newline.
- [x] **M1.5 `szc tokens` dump + golden tests** (port of the reference tokenizer test-suite).
- [x] **M1.6 Lexer fuzzer** (libFuzzer and standalone driver) + seed corpus.

## Phase 2: AST + parser (`src/ast.h`, `src/parser.c`)

- [ ] **M2.1 AST node structs + `szc ast` dumper skeleton**; token lookahead buffer in the parser.
- [ ] **M2.2 Top level**: ContainerMembers, `const`/`var` decls, FnProto, TestDecl, ComptimeDecl.
- [ ] **M2.3 Expression precedence ladder**: BoolOr … MultiplyExpr, PrefixExpr (one function per level).
- [ ] **M2.4 Primary/suffix expressions**: literals, identifiers, calls, field access, `.*`, `.?`, indexing, slicing, builtin calls.
- [ ] **M2.5 Type expressions**: pointers, slices, arrays, optionals, error unions, `fn` types, ptr modifiers.
- [ ] **M2.6 Statements + blocks**: var decls, assignments (incl. destructuring), defer/errdefer, labeled blocks.
- [ ] **M2.7 Control flow**: if/while/for with payloads, else branches, break/continue/return, labels.
- [ ] **M2.8 switch**: prongs, ranges, `inline`, payloads, labeled switch.
- [ ] **M2.9 Containers**: struct/enum/union/opaque, fields, `extern`/`packed`, error sets, init lists.
- [ ] **M2.10 asm expressions, callconv/align/addrspace/linksection.**
- [ ] **M2.11 Error recovery**: keep going after parse errors and collect them all.
- [ ] **M2.12 Parser fuzzer + parse the whole of Zig 0.17.0 `lib/std` without errors.**

## Phase 3: First executable (`irgen.c`, `sema.c`, `codegen.c`)

- [ ] **M3.1 Untyped IR (ZIR-like) design + `szc ir` dump.**
- [ ] **M3.2 irgen for functions, integer literals, `return`, params, locals.**
- [ ] **M3.3 Typed IR + minimal sema**: `void`, `bool`, `i32`/`u8`/`usize` etc., coercion of comptime_int literals.
- [ ] **M3.4 QBE codegen**: functions, arithmetic, calls; driver runs `qbe` and `cc`.
- [ ] **M3.5 End-to-end test harness** (`tests/run/*.zig` check exit code/stdout).
- [ ] **M3.6 Arithmetic semantics**: wrapping/saturating ops, division, shifts, safety checks → panic stub.

## Phase 4: Control flow

- [ ] **M4.1 if/else, comparisons, `and`/`or` short-circuit.**
- [ ] **M4.2 while/break/continue, continue expressions, labeled loops.**
- [ ] **M4.3 Blocks as expressions, labeled break with value.**
- [ ] **M4.4 for over ranges, `defer`.**
- [ ] **M4.5 unreachable, noreturn, `@panic` minimal runtime.**

## Phase 5: Memory

- [ ] **M5.1 Single pointers, `&`, `.*`, `const` pointers.**
- [ ] **M5.2 Arrays, indexing, array literals, `++`/`**` at comptime.**
- [ ] **M5.3 Slices, many-pointers, slicing, `.len`, `.ptr`, for over slices.**
- [ ] **M5.4 String literals, sentinels, `[*:0]const u8`.**
- [ ] **M5.5 Casting builtins**: `@intCast`, `@truncate`, `@ptrCast`, `@intFromPtr`, `@ptrFromInt`, `@bitCast`, `@as`.

## Phase 6: Aggregates and errors

- [ ] **M6.1 Structs**: fields, init, field access, methods, default values.
- [ ] **M6.2 Enums**: tag values, `@intFromEnum`, `@enumFromInt`, enum literals.
- [ ] **M6.3 switch** on ints/enums with ranges.
- [ ] **M6.4 Optionals**: `?T`, `orelse`, `.?`, if-payloads, optional pointers.
- [ ] **M6.5 Error sets + error unions**: `try`, `catch`, `errdefer`, error set merging, inferred error sets.
- [ ] **M6.6 Unions**: bare and tagged, switch payload capture.
- [ ] **M6.7 extern/packed struct layout.**

## Phase 7: Comptime (`src/comptime.c`)

- [ ] **M7.1 Big integers for comptime_int.**
- [ ] **M7.2 Comptime interpreter over IR**: const decls, arithmetic, control flow.
- [ ] **M7.3 Types as values**: `type`, `@TypeOf`, returning types from functions.
- [ ] **M7.4 Generic functions**: comptime params, `anytype`, monomorphization + memoization.
- [ ] **M7.5 `inline for` / `inline while` / inline switch prongs.**
- [ ] **M7.6 `@typeInfo` / `@Type` / `@This` / `@hasDecl` / `@field` / `@hasField`.**
- [ ] **M7.7 comptime_float (f64-backed for now; f16/f80/f128 = hard error).**

## Phase 8: Modules and std

- [ ] **M8.1 `@import` of files (file = struct), lazy decl analysis.**
- [ ] **M8.2 Lib dir resolution**: `ZIG_LIB_DIR`, `<exe>/../lib/zig`, `<exe>/../lib`; `@import("std")`.
- [ ] **M8.3 `@import("builtin")` generation** (x86_64-linux target info).
- [ ] **M8.4 `start.zig` entry and `std.os.linux` syscalls** (asm strategy, see DESIGN.md).
- [ ] **M8.5 std hello world** (`std.debug.print`, `std.fs.File`).

## Phase 9: Features needed to bootstrap

- [ ] **M9.1 Threadlocal (QBE `thread`), atomics via runtime helpers.**
- [ ] **M9.2 u128/i128 lowering via helpers.**
- [ ] **M9.3 `@Vector` lowered to scalar loops.**
- [ ] **M9.4 Labeled switch `continue :sw`, `@fieldParentPtr`, `@memcpy`/`@memset`, remaining builtins.**
- [ ] **M9.5 Inline asm for x86_64-linux std usage.**

## Phase 10: std tests and bootstrap

- [ ] **M10.1 Selected std unit tests pass (`szc test`).**
- [ ] **M10.2 Build options / build strategy for the Zig compiler**: ASK THE USER first (deferred decision Q6).
- [ ] **M10.3 Compile the Zig compiler (no LLVM) → zig2.**
- [ ] **M10.4 zig2 rebuilds itself.**

---

## Completed milestone log

(newest first; each entry: what, limitations, interactions)

### M1.1–M1.6 Lexer (`src/lexer.c`, `src/lexer.h`)
- **What**: on-demand state-machine tokenizer matching Zig 0.17.0 `std.zig.Tokenizer`
  token for token: same kinds (`TK_*`, named after `Token.Tag`), boundaries, BOM skip,
  and recovery after an invalid token (resume at the next newline). Keywords are looked up by
  first letter + length (no strcmp scan). `szc tokens` / `szc tokens-kinds` dump commands.
  All 339 `testTokenize` cases from the reference are ported (`tools/port_tokenizer_tests.py`
  → `tests/lexer/ref/`) and pass. Unit tests: every fixed lexeme round-trips, all 46 keywords,
  EOF is sticky, embedded NUL, BOM. Fuzz target checks ordering/progress/bounds properties;
  2.3M libFuzzer runs and the standalone mutator run clean.
- **Limitations**: number literals are lexed loosely like the reference (e.g. `0xg` is one
  token); validating and decoding them is done at M3.x. String/char escapes are
  not validated or decoded yet. UTF-8 is not validated (same as the reference tokenizer).
- **Interactions**: the parser calls `lexer_next()` on demand and keeps its own lookahead
  buffer. Doc-comment tokens (`TK_DOC_COMMENT`, `TK_CONTAINER_DOC_COMMENT`) reach the
  parser, which must accept them where the grammar allows them. Token text is
  `source[start..end]` and is never copied. Diagnostics take the byte offset (`Token.start`).

### M0.1–M0.5 Infrastructure
- **What**: Makefile (debug/release/asan/test/fuzz), `.editorconfig` (tabs, width 2),
  arena allocator (chunked, overflow-checked, ASan poisoning + red zones), diagnostics
  collector (file:line:col + caret, keeps going), source table (NUL-terminated buffers),
  GitHub Actions CI (gcc+clang `-Werror`, ASan/UBSan tests, 60s libFuzzer per push,
  30 min nightly).
- **Limitations**: diagnostics print in the order they were reported (not sorted). Line/column
  is computed by scanning from the start of the file (fine for errors only).
- **Interactions**: every later stage allocates from an `Arena` and reports through `Diag`.
