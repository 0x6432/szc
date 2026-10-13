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

- [x] **M2.1 AST node structs + `szc ast` dumper skeleton**; token lookahead buffer in the parser.
- [x] **M2.2 Top level**: ContainerMembers, `const`/`var` decls, FnProto, TestDecl, ComptimeDecl.
- [x] **M2.3 Binary expressions (Pratt)**: one `parse_expr_precedence(min_prec)` driven by an operator table covering BoolOr … MultiplyExpr (the grammar rules are listed in its comment), non-associative comparisons, `catch |e|` payload; PrefixExpr stays recursive descent.
- [x] **M2.4 Primary/suffix expressions**: literals, identifiers, calls, field access, `.*`, `.?`, indexing, slicing, builtin calls.
- [x] **M2.5 Type expressions**: pointers, slices, arrays, optionals, error unions, `fn` types, ptr modifiers.
- [x] **M2.6 Statements + blocks**: var decls, assignments (incl. destructuring), defer/errdefer, labeled blocks.
- [x] **M2.7 Control flow**: if/while/for with payloads, else branches, break/continue/return, labels.
- [x] **M2.8 switch**: prongs, ranges, `inline`, payloads, labeled switch.
- [x] **M2.9 Containers**: struct/enum/union/opaque, fields, `extern`/`packed`, error sets, init lists.
- [x] **M2.10 asm expressions, callconv/align/addrspace/linksection.**
- [x] **M2.11 Error recovery**: keep going after parse errors and collect them all.
- [x] **M2.12 Parser fuzzer + parse the whole of Zig 0.17.0 `lib/std` without errors.**
- [x] **M2.13 Differential tests against `std.zig.Ast`**: node spans over all Zig sources, accept/reject on mutated sources (CI job `differential`).

## Phase 3: First executable (`irgen.c`, `sema.c`, `codegen.c`)

- [x] **M3.1 Untyped IR (ZIR-like) design + `szc ir` dump.**
- [x] **M3.2 irgen for functions, integer literals, `return`, params, locals.**
- [x] **M3.3 Typed IR + minimal sema**: `void`, `bool`, `i32`/`u8`/`usize` etc., coercion of comptime_int literals.
- [x] **M3.4 QBE codegen**: functions, arithmetic, calls; driver runs `qbe` and `cc`.
- [x] **M3.5 End-to-end test harness** (`tests/run/*.zig` check exit code/stdout).
- [ ] **M3.6 Arithmetic semantics**: wrapping/saturating ops, division, shifts, safety checks → panic stub.

## Phase 4: Control flow

- [ ] **M4.1 if/else, comparisons, `and`/`or` short-circuit.**
- [ ] **M4.2 while/break/continue, continue expressions, labeled loops.**
- [ ] **M4.3 Blocks as expressions, labeled break with value.**
- [ ] **M4.4 for over ranges, `defer`.**
- [ ] **M4.5 unreachable, noreturn, `@panic` minimal runtime.**

## Phase 5: Memory

- [ ] **M5.1 Single pointers, `&`, `.*`, `const` pointers.**
- [ ] **M5.2 Arrays, indexing, array literals, `++` at comptime** (0.17.0 has no `**`).
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

### M3.5 End-to-end test harness (`tests/run_e2e.sh`, `tests/run_driver.sh`)
- **What**: `make e2e-test` compiles each `tests/run/*.zig` program with szc,
  verifies its first-line `// exit: N` and optional byte-exact `.stdout` sidecar,
  rejects unexpected stderr,
  checks retained IL/assembly, and exercises `szc run` independently. If `ZIG` is
  set, every source is also compiled and executed by official Zig (compilation
  success is checked separately from program exit). Deterministic generated tests
  cover all 128 signed/unsigned 1–64-bit integer types: wrapping ops, bitwise ops,
  shifts, comparisons, locals and unsigned division/remainder. Driver boundary tests
  check tool overrides/failures, no subprocess after a compile error, paths with
  spaces, argument forwarding, temporary cleanup, invalid CLI and output failures.
  `tools/build_qbe.sh` fetches and builds the pinned official QBE revision.
  CI gcc/clang jobs run native sanitizer e2e tests; the differential job additionally
  checks exit/stdout with official Zig 0.17.0. Existing fuzz schedules remain intact.
  Validation: fresh GCC and Clang `-Werror` ASan/UBSan builds pass all 366
  goldens, nine e2e inputs (including the generated 128-type suite) and 12 driver
  boundary tests. Negative harness checks reject compile failures and stdout
  mismatches. The QBE clone/build script succeeds using the canonical Git transport.
  New GitHub CI runs await restoring Git push authentication.
- **Limitations**: current programs produce no stdout (string/runtime output support
  awaits M5); sidecars support future output tests. Only successful programs with
  deterministic exits are covered; panic tests arrive in M3.6. Generated tests use
  one deterministic seed by default (the generator accepts `--seed`). Ten-second
  execution timeouts prevent a bad compiled program from hanging the suite.
- **Interactions**: `make test` stays independent of QBE; `make e2e-test` requires
  QBE on PATH or `SZC_QBE`. Reference validation is optional locally, enabled in CI
  by `ZIG`. Generated source stays in ignored `build/`, not in the checked-in corpus.

### M3.4 QBE codegen (`src/codegen.[ch]`, `src/driver.[ch]`)
- **What**: typed IR to QBE IL, integer/bool functions and direct calls, arithmetic,
  comparisons, casts, local alloc/load/store, branch/block/loop instructions and `hlt`.
  Integers up to 32 bits use `w`, up to 64 use `l`; narrow integers stay sign- or
  zero-extended after operations. Stack slots are hoisted to entry. A C `main` wrapper
  handles Zig `main` returning u8/void/noreturn. `emit-qbe`, `build-exe` (`-o`,
  `--keep-temps`) and `run` commands; driver executes separate QBE and system cc
  processes without a shell (`SZC_QBE`/`SZC_CC`). Integer builtins `@as`, `@intCast`,
  `@truncate`, `@intFromBool` support runtime lowering and comptime evaluation.
  Checked Zig 0.17.0 `Sema.zig`: truncate permits widening and requires matching
  signedness for typed operands. Fixed runtime `or` branch ordering and nested-body
  local lookup. Golden QBE/cast tests; sema fuzzer also emits QBE after successful
  analysis; eight executable tests compared against the official Zig binary.
  GCC and Clang `-Werror` sanitizer suites pass (366 goldens), all four fuzzers
  complete 60 seconds each, and a generated 128-function integer-width check
  (signed/unsigned 1–64 bits) exits zero with both szc and Zig.
- **Limitations**: runtime integers wider than 64 bits and floats are rejected;
  arithmetic and integer-cast safety checks are deferred to M3.6. Full C ABI handling,
  globals, indirect calls and std startup are not implemented. Driver intermediates
  are named `<output>.ssa`/`.s`; concurrent builds must use different output paths.
  Source-level loops/if still await Phase 4; short-circuit bool control flow works.
- **Interactions**: codegen consumes analysed `SemaFunc`/`TirFunc` and interned values;
  `br` assigns the target block result before jumping. `run` uses a temporary directory
  and propagates program exit status; a failed compiler stage never invokes QBE/cc.
  M3.5 will automate executable exit/stdout and reference-compiler checks.

### M3.3 Typed IR + minimal sema (`src/tir.h`, `src/sema.[ch]`, `src/comptime.[ch]`)
- **What**: typed IR (`tir.h`): per-function instruction array, every instruction typed,
  bodies as in the untyped IR, comptime-known operands as `const` instructions holding a
  `ValueId`. `comptime.c`: arbitrary-precision `BigInt` (add/sub/mul/truncating divmod/shifts/
  two's-complement bitwise ops/wrap/fits/decimal) and an interned value table (void, bool,
  int, type, function, undefined). `sema.c`: interned structural types (builtin types have the
  TypeId of their well-known IR ref; structs are nominal), namespaces from `struct_decl`,
  lazy declaration resolution with dependency-loop detection, function signatures (`func`
  instruction), function bodies analysed from a queue starting at `pub fn main` (and exported
  functions), instruction map from IR instruction to operand (comptime value or runtime typed
  instruction). Operations with comptime-known operands are folded (overflow and division by
  zero are compile errors; wrapping ops wrap; saturating ops clamp); comptime_int coerces to
  integer types with range checks, runtime integers widen with `int_cast`; peer types for
  integer binary ops; comparisons against out-of-range constants are folded; signed `/`/`%`
  errors like Zig; shift amounts coerce to `Log2Int`; `and`/`or` become `block`+`condbr`
  only when the left side is runtime-known; noreturn calls end the block. `szc tir`
  dumper, goldens `tests/tir/*.tir` and `tests/tir/errors/*.err`, BigInt unit tests against
  128-bit arithmetic, `fuzz_sema` target (parse → irgen → sema → dump).
- **Limitations**: no generic functions, comptime calls, comptime variables, container-level
  variables, struct fields, callconv, floats, pointers other than local `alloc`s, function
  pointers (all report "not supported by szc yet"). Runtime arithmetic has no safety checks
  yet (M3.6). Declaration lookup scans the namespace linearly. Error locations of a few
  errors differ from Zig (e.g. implicit return is reported at the closing brace).
- **Interactions**: codegen reads `SemaFunc` (symbol, type, `tir`) for every function in
  state `FUNC_ANALYSED` (or extern + queued) and `sema_type_size/align`; `TIR_REF_NONE` is
  the operand of a `ret` in a `void` function; `br` targets a `block` instruction;
  `condbr` payload is `TirCondbr`; `call` payload is `TirCall` + argument refs. Zig's `main`
  gets an internal symbol (`szc.main.N`); codegen must emit the C `main` wrapper.

### M3.1–M3.2 Untyped IR + irgen basics (`src/ir.h`, `src/irgen.c`, `src/intern.[ch]`)
- **What**: IR design in `docs/IR.md`: per-file instruction array + `extra` words, refs with
  well-known constants (primitive types and values), bodies as instruction lists ending in a
  terminator, payload structs read with `IR_READ` (memcpy, no aliasing). String interner
  (`StringId`). irgen handles structs (fields with type/default bodies; const/var/fn/test/
  comptime declarations with lazily analysed value bodies; duplicate-declaration check),
  functions (param/`anytype` instructions with type bodies that see earlier params, return
  type/callconv bodies, body with `ret_implicit`), integer literals (decoded, big ones as
  32-bit limbs, underscore/leading-zero/digit validation), identifiers (locals, params,
  declarations, primitives, `iN`/`uN`), arithmetic/bitwise/comparison/unary operators,
  short-circuit `and`/`or`, calls with per-argument bodies whose result type is `arg_type`,
  `return` with `ret_type` result type, local `const`/`var` (alloc), plain assignment through
  `elem_type` result type, `_ = x`, unreachable-code and shadowing errors. `szc ir` dumper,
  IR golden tests (`tests/ir/`), `fuzz_irgen` target; irgen runs crash-free (ASan) on all
  1373 Zig sources (reporting unsupported constructs).
- **Limitations**: everything else reports "not supported by szc yet" (control flow, builtins,
  pointers, strings, floats, containers other than auto-layout structs, compound
  assignment, labeled blocks, …). Captures of outer function locals by nested containers are
  not implemented (M7.3). Unused-local errors are not reported.
- **Interactions**: sema walks bodies via `ir_body_count/ir_body_insts`; a declaration's value
  body ends in `break_inline result, value`; `arg_type %call, i` is resolved while sema
  analyses the call's arguments; `elem_type`/`ret_type` give assignment/return result types.

### M2.1–M2.13 AST + parser (`src/ast.[ch]`, `src/parser.[ch]`)
- **What**: one struct per AST node kind (`AstNode` header with kind, file, main-token offset
  and `start`/`end` byte span), `ast_visit_children`, indented dumper (`szc ast`), span
  dumper (`szc ast-spans`). Recursive-descent parser, one function per grammar rule with the
  PEG rule in its comment; binary operators use one Pratt loop over an operator table
  (`parse_expr_precedence`), comparisons are non-associative, operator whitespace symmetry
  and `&&` are checked like the reference. 4-token lookahead ring buffer. Doc comments are
  accepted only where the reference accepts them (before members, error-set names and
  params; `//!` at the start of a container) with the same errors (same-line, unattached,
  on tests/comptime blocks); declarations between fields are reported.
  Error recovery: setjmp/longjmp at member and statement level, skipping like
  `findNextContainerMember`; a stray `}` at the root ends the parse like the reference.
  Nesting limit 1024.
- **Testing**: parses all 1373 `.zig` files of Zig 0.17.0 (`lib/`, `src/`, `test/behavior`,
  `test/standalone`) without errors; node spans of every file equal those of `std.zig.Ast`
  (`tools/ast_spans.zig` + `tools/diff_ast_spans.sh`); accept/reject agrees with the reference
  on >10k mutated sources (`tools/diff_parse_errors.py`); golden tests `tests/parser/*.ast`
  and `tests/parser/errors/*.err`; libFuzzer + standalone fuzzing clean.
- **Found on the way**: the lexer had been ported from an older tokenizer; fixed to 0.17.0
  (no `**` operator, no `.**` token, control characters invalid after `\\` in strings) and the
  reference tests re-ported (336 cases). 0.17.0 also has no `errdefer |e|` payload, and local
  `const`/`var` declarations require an initializer.
- **Limitations**: doc comment text is not stored. Enum fields and untyped union fields are
  stored as tuple-like fields whose `type` is the name identifier (`has_name == false`, like
  the reference AST); irgen decides by container kind. Literals are kept as source spans
  (decoded later). Diagnostic wording is close to, not identical with, the reference, and
  recovery may report a different number of follow-up errors.
- **Interactions**: irgen walks the tree via the node structs (`AST_CAST` checks the kind).
  `start`/`end` spans are only used for tests and diagnostics; `offset` is the main token
  (operator, keyword, name) for error positions.

### M1.1–M1.6 Lexer (`src/lexer.c`, `src/lexer.h`)
- **What**: on-demand state-machine tokenizer matching Zig 0.17.0 `std.zig.Tokenizer`
  token for token: same kinds (`TK_*`, named after `Token.Tag`), boundaries, BOM skip,
  and recovery after an invalid token (resume at the next newline). Keywords are looked up by
  first letter + length (no strcmp scan). `szc tokens` / `szc tokens-kinds` dump commands.
  All `testTokenize` cases (336) from the 0.17.0 reference are ported (`tools/port_tokenizer_tests.py`
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
  lookup binary-searches a per-file line-start table (added in M2 after a slow error flood).
- **Interactions**: every later stage allocates from an `Arena` and reports through `Diag`.
