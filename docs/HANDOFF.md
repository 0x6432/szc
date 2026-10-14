# Handoff

Read this first when resuming work (the sandbox resets often).

## Resume checklist

1. `git clone https://github.com/0x6432/szc && cd szc` (or `git pull`).
2. Git identity for commits: `git config user.name 0x42 && git config user.email noreply@localhost`.
3. Tools: `gcc`/`clang`, `make`, `python3`. For sanitizers on Amazon Linux:
   `sudo dnf install -y libasan libubsan clang compiler-rt`.
   QBE: `git clone git://c9x.me/qbe.git && make -C qbe` (needed from M3.4).
4. `make test && make fuzz-standalone` must pass before new work.
5. Check `docs/MILESTONES.md` for the next unchecked milestone.
6. Push after every milestone (and more often during long ones).
   The GitHub token is **never** committed. Restore authenticated Git access if needed.

## Current state

- Done: M0 (infrastructure), M1 (lexer), M2 (AST + parser, differential-tested against
  `std.zig.Ast`).
- Done: M3.1–M3.2 (untyped IR + irgen basics, `szc ir`).
- Done: M3.3 (typed IR + minimal sema, `szc tir`).
- Done: M3.4 QBE codegen + driver (`emit-qbe`, `build-exe`, `run`), integer casts.
- Done: M3.5 executable/driver harness + generated integer-width tests and CI.
- Done: M3.6a checked arithmetic, casts, unsigned division/shift guards and panic stub.
- Done: M3.6b runtime saturation and explicit division/remainder builtins.
- Done: M4.1 source-level if/else, typed branch joins, early returns and short-circuit composition.
- Done: M4.2 runtime while/break/continue, labels, loop else/values and compound assignments.
- Done: M4.3 labeled block expressions, nested named exits and constant-only comptime blocks.
- Done: M4.4a runtime range-based for, multi-input length checks and usize captures.
- Done: M4.4b scope-exit defer across fallthrough, return, break and continue.
- Done: M4.5a safe explicit unreachable, noreturn return/cast diagnostics and terminating joins.
- Done: M4.5b explicit @panic and first-class string-literal byte values.
- Done: M5.1a runtime single pointers, mutable-local addresses, dereference and nested const rules.
- Done: M5.1b definition-anchored references, immutable constant objects and pointer equality.
- Done: M5.1c1 internal mutable scalar globals, lazy type/init resolution and writable relocations.
- Done: M5.1c2a function types, symbolic addresses, signature variance and indirect calls.
- Next: **M5.1c2b** pointer attributes. M5.1/M5.1c/M5.1c2 remain partial.
- Run `PATH=/data/tools/bin:$PATH ZIG=/data/zig/zig-x86_64-linux-0.17.0/zig make e2e-test`
  locally. `tools/build_qbe.sh` builds the pinned QBE into `build/qbe-src` on CI.
- Tools: `SZC_QBE`/`qbe`, `SZC_CC`/`cc`; pinned QBE revision
  `e786f06032fefa2e3790d6b1c9e31ed138f475a6`. Local QBE: `/data/tools/bin/qbe`.
- Restored sandbox needed libasan/libubsan/clang/compiler-rt reinstalled. Git
  credentials are not persisted; use transient authentication for future pushes.
- M3.4 (`1882ced`) and M3.5 (`7b36edd`) are pushed to `main`.
- CI through M5.1b (`4e31f1e`): completed successfully (run 38545269536).
  M5.1c1 run 38548134671 passed GCC/Clang/fuzz and all 45 native/reference inputs,
  but seven reference safety compilations hit the old 15-second harness timeout.
  Compilation now has a separate 120-second budget; execution still has 15 seconds.
  Its follow-up 1731927/run 38549587896 passed compiler/fuzz jobs but the differential
  runner received a shutdown signal during safety testing (no completed safety result).
  CI now flushes per-case safety progress so interruptions retain the active case.

## Validation at M5.1c2a

- GCC/Clang -Werror ASan/UBSan and optimized GCC: 554 unit checks, 579 goldens,
  48 ordinary/reference executable inputs plus two szc-only inputs, 12 driver checks,
  11 PIE/storage checks, four assembly-backed ABI checks and 166 panic cases. All 486
  function-signature/qualifier comparisons and 168 existing pointer-qualifier cases
  match Zig; both differential scripts are in CI. Four 60-second fuzz targets and
  standalone 20,000 mutations per stage pass; final sema/codegen was re-fuzzed.
  Corpus: irgen 23, sema 32 inputs. Check this checkpoint's push CI run.
- IR_FN_TYPE shares function prototype lowering, but returns a TYPE_TYPE value and
  must not allocate a function record. VALUE_FUNCTION can be a body or pointer value;
  its TypeId distinguishes them while FuncId keeps original symbol identity. Function
  addresses and aliases are symbols, not immutable objects containing function bytes.
- TypeFn interns parameter noalias flags and FunctionConvention (auto/C/inline).
  Preserve memory_coercion_allowed's function return covariance, reversed parameter
  coercion and mutable-outer invariance. Noreturn return widening is not valid through
  mutable outer pointers; integer-width changes are not memory-compatible coercions.
  The 486-case checker caught pointer-parameter variance that exact matching misses.
- Runtime function pointers call via TirCall.indirect; direct callees use FuncId and
  indirect=TIR_REF_NONE. Argument result contexts use the signature TypeId, not a
  presumed declaration. Keep callee loads before argument effects. Known function
  pointer dereference recovers a body; runtime `p.*` is a comptime-only-load error.
- queue_value_functions follows nested VALUE_REF objects before queuing a symbol.
  Materialized function addresses and global pointer initializers must emit bodies
  even if the function is never directly called. Do not queue discarded known generic
  addresses; runtime emission of generic function bodies still gives explicit NYI.
- Normalize integer/bool results after every non-zero-sized TIR_CALL. C only defines
  meaningful low bits for narrow returns; unnormalized upper bits broke callbacks.
  tests/run_function_abi.py links deterministic .s helpers and optionally Zig to check
  direct/indirect u8/i8/bool results. This complements PIE/RELRO function-address checks.
- Explicit callconv expressions, inline addresses and generic runtime addresses are
  still NYI; comptime specialization belongs to M7. C convention identity inferred
  from extern/export declarations is preserved, but full C/aggregate/variadic ABI
  support is not claimed. The szc-only libc callback fixture matches Zig with -lc.
- Next: M5.1c2b pointer attributes; inspect actual 0.17 alignment/volatile/allowzero
  rules before implementing. Keep every attribute checkpoint tested/committed/pushed.
  Continue to defer the compiler build/bootstrap-options question until M10.2.

## Validation at M5.1c1

- GCC/Clang -Werror ASan/UBSan: 549 unit checks, 557 goldens, 45 ordinary/reference
  executable inputs plus one szc-only input, 12 driver checks, eight PIE/storage
  checks, 163 panic cases and 168 pointer-qualification differential cases. Optimized
  GCC is clean. Four 60-second fuzz targets and standalone 20,000 mutations per stage
  pass; final lazy sema was separately re-fuzzed. Corpus: irgen 19, sema 28 inputs.
- VALUE_GLOBAL is a typed symbolic address into stable Global records. It must never
  fold a mutable global's initializer into a read. Use materialize() before runtime
  LOAD/STORE: a known pointer operand has no valid `.inst` until materialized.
- Explicit mutable global types live in IrDecl.type_body; IR_DECL_TYPE supplies the
  initializer's result context. DECL_TYPED means address known, initializer pending.
  Taking an address resolves only its type, not its initializer; a full read resolves
  both. Drain globals/functions before codegen. Preserve this distinction so inferred
  pointer/address cycles do not depend on first-use order. Do not eagerly initialize
  every unrelated declaration: unused invalid globals must remain lazy.
- Global output is internal writable QBE data with scalar/zero-fill initializers or
  relocations to globals, immutable objects and bytes. Mark global initializer
  dependencies before immutable-object closure. Immutable pointer objects still use
  .data.rel.ro and runtime globals must not be placed in read-only sections.
- Non-OPV comptime reads/writes fail, with self-read dependency diagnostics even for
  indirect reads. Void/u0 memory accesses remain no-ops at comptime; direct `var z:u0=z`
  still diagnoses a declaration dependency. Global checked operations and cleanup use
  existing safety panics, and runtime reference snapshots retain definition dominance.
- Internal integers/bools/pointers/void/u0 only. Extern/export/TLS globals, custom global
  attributes, aggregate storage and mutable comptime interpretation remain NYI/later
  work. Exported variables are eager roots, so even an unreferenced export errors.
- At this checkpoint, next was M5.1c2: function address/type/call integration and pointer attributes. Confirm
  each against the actual Zig 0.17 sources/binary; keep checkpoints small. Do not ask
  the deferred compiler build/bootstrap-options question until M10.2.

## Validation at M5.1b

- GCC/Clang -Werror ASan/UBSan: 542 unit checks, 538 goldens, 42 ordinary/reference
  executable inputs plus one szc-only input, 12 driver checks, four PIE/RELRO checks,
  159 panic cases and 168 coercion differential cases. Optimized GCC is clean.
  Four 60-second fuzz targets and standalone 20,000 mutations per stage pass.
  Unit tests cover immutable object ownership/identity and hash-table growth.
- IR_REF is cached by defining instruction and inserted after that definition when
  its containing body is finalized. Parameter reference chains are prepended to
  the function body after sema's TIR_ARG setup. Keep this dominance/identity model;
  do not move allocation initialization back into the first address-taking branch.
  Known constants remain values, so address taking does not break comptime folding.
- VALUE_REF uses ConstObjectId from ValueTable.objects. Object identity 0 means a
  deduplicated anonymous value address; named global constants use stable nonzero
  declaration identities. Const coercions preserve object IDs. Distinct globals
  with equal values must not alias each other or an anonymous local constant.
- Known reference loads recover their stored ValueId and coerce to the pointee
  type. Runtime references use activation-local TIR_ALLOC/STORE, including params,
  captures and reusable defer expansions. No mutable static temporary storage.
- Codegen emits reachable immutable scalar objects in .rodata, pointer objects in
  .data.rel.ro, and marks nested object/byte dependencies. The latter section has
  loader relocations but is protected by GNU_RELRO; tests require no TEXTREL in a
  PIE. `tests/run_reference_storage.py` is part of make e2e-test/CI (needs readelf).
- Temporary literals use result pointee context; named bindings retain their type.
  Pointer equality handles both runtime and immutable objects. An undefined pointer
  comparison propagates undefined bool; discarding it is legal, a known condition
  use is not. Actual Zig 0.17 probes/sources verify these distinctions.
- After M5.1c1, function-pointer/address integration and pointer attributes remain. Arrays/aggregate memory are later phases. Runtime storage of unsupported
  comptime-only objects is a hard NYI, not a dummy zero object. Escaping a runtime
  local pointer is not diagnosed by Zig; do not add tests that use expired pointers.

## Validation at M5.1a

- GCC/Clang -Werror ASan/UBSan: 533 unit checks, 526 goldens, 40 ordinary/reference
  executable inputs plus one szc-only input, 12 driver checks, 157 panic cases.
  `tools/generate_pointer_tests.py` exercises all signed/unsigned 1–64-bit pointees.
  `tools/diff_pointer_coercions.py SZC ZIG` checks 84 depth-1/2/3 const conversions
  in runtime and typed-undefined form (168 cases); the differential CI job runs it
  after e2e. Optimized GCC is clean. Four 60-second fuzz targets and standalone
  20,000-mutation replays per target pass.
- IR_PTR_TYPE uses child type and well-known true/false refs; its location is the
  child expression. IR_DEREF reads, IR_REF_DEREF validates and preserves the pointer
  for lvalues and &p.*. Keep internal IR_LOAD separate from source validation.
  Typed undefined pointers still obey pointer type/const coercion rules; only
  untyped undefined is freely typed.
- TIR_PTR_CAST is a QBE long copy, not an integer cast. Nested qualifier coercions
  must remain invariant below mutable outer pointees (actual Sema.zig rules).
- OPV void/u0 dereferences/stores do not access memory, even through known undefined
  pointers. Other known undefined source dereferences are errors. Existing pointer
  storage uses allocation-backed runtime addresses, never static mutable state.
- At this checkpoint constants/parameter/global/temporary addresses and pointer
  equality were still NYI; M5.1b above supersedes those limitations. Pointer
  attributes/many/slice/C pointer syntax and aggregate reads remain pending.

## Validation at M4.5b

- GCC/Clang warning-clean sanitizer suites: 533 unit checks, 500 goldens,
  36 ordinary/reference executable inputs plus one szc-only input, 12 driver checks
  and 154 panic cases. Optimized GCC is clean. Four 60-second fuzz targets and
  standalone 20,000 mutations per target pass. New seeds cover literal value flow.
- IR_STR now holds decoded bytes; TYPE_ARRAY represents u8 length and zero sentinel,
  with a const single pointer as the literal type. VALUE_BYTES owns/interns its
  complete byte sequence, not a strlen-based C string. Byte lengths exclude the
  sentinel, while storage includes it. Keep this invariant for future string memory.
- IR_PANIC has a normal message operand plus argument source location. Sema validates
  the fixed-array pointer -> const-byte-slice conversion and decomposes it directly
  into TIR_PANIC pointer/length. There is no panic-only AST literal special case.
- Codegen emits referenced bytes once per ValueId in read-only .rodata; data uses
  numeric byte initializers to avoid QBE quoting/escape ambiguity. TIR_PANIC calls
  the outlined pointer/length write+newline+abort helper. Panic never unwinds defer.
- Runtime aliases, local pointer variables and same-length runtime choices work.
  Different-length runtime choices require true slice joins and explicitly fail
  as unsupported. Runtime undefined panic messages are explicitly unsupported.
  General slice values/ABI, arrays and pointer operations are still Phase 5.
- String escapes/multiline joining were checked against actual 0.17 sources and
  binary. Sixteen new runtime cases include all byte values and Unicode boundaries.
  Nested panic/unreachable arguments are valid in 0.17 and stop before outer panic.
- Diagnostic stores message_length and prints bytes, preserving embedded zeroes.
  The 0.17 renderer truncates comptime panic diagnostics at NUL; szc intentionally
  preserves them (new binary golden/unit tests). Existing text diagnostics are unchanged.
- M5.4 is partly implemented, not complete: literal bytes exist, but general string
  operations/sentinel pointer syntax await memory work. Quoted identifiers still
  use exact spelling; don't assume literal decoding fixed their canonicalization.

## Validation at M4.5a

- GCC/Clang warning-clean sanitizer suites: 518 unit checks, 479 goldens,
  35 ordinary/reference executable inputs plus one szc-only input, 12 driver checks
  and 138 panic cases. Optimized GCC is warning-clean. The noreturn generator
  exercises 128 integer result types and both value/noreturn cleanup paths.
- Four 60-second libFuzzer targets and standalone 20,000 mutations per target pass.
- TIR_UNREACHABLE is now exclusively the explicit source safety violation: it
  calls the outlined "reached unreachable code" panic helper and aborts. TIR_TRAP
  is the backend-only terminator after known noreturn calls/internal fallback.
  Don't replace every backend hlt with a source safety panic. Indirect calls have
  different reference noreturn-returned rules; check them when adding pointers.
- IR_AS_RETURN marks only the final direct return-result coercion; nested result
  coercions remain IR_AS. This distinguishes "function declared 'noreturn' returns"
  from "cannot cast to noreturn". Undefined returns do not bypass the former.
  A syntactically terminating return operand reports unreachable code.
- Noreturn callees/cleanup stop sema before pending transfers; value joins omit
  terminating exits. The official 0.17 binary accepts source after a statically
  typed noreturn call in the probe; don't add speculative rejection from memory.
- Explicit @panic expects a slice message in actual Sema.zirPanic. Implement the
  necessary first-class read-only byte/string values before lowering it; do not
  add an AST-only literal shortcut. General memory features remain Phase 5.
  No user panic overrides, stack traces or alternate safety/optimization modes yet.

## Validation at M4.4b

- GCC/Clang warning-clean sanitizer suites: 518 unit checks, 466 goldens,
  33 ordinary/reference executable inputs plus one szc-only edge input,
  12 driver checks and 130 panic cases. Optimized GCC is warning-clean.
  The defer generator checks all 128 signed/unsigned 1–64-bit types, both
  runtime exit paths and operand snapshots before cleanup mutations.
- Four 60-second fuzz targets, final irgen/sema rechecks and standalone replay
  (20,000 mutations per target) pass. Cleanup map-growth regression checks
  parent preservation and repeated expansion at different exits.
- DeferScope retains a reusable IrBody lowered once with registration-time
  lexical names. IR_DEFER expands it per exit; sema uses a fresh InstMap overlay
  with captured outer operands visible through parent lookup. Never share the
  cleanup's local map between exits or lose parent when growing it.
- emit_defers traverses from the current lexical scope to the transfer target,
  newest first. Block fallthrough stops at the original block scope; return
  stops at the namespace; break/continue stops at ControlScope. Return/break
  operands are generated before cleanup. Panic/unreachable do not unwind.
- SCOPE_DEFER_BODY blocks escaping break/continue and any return, but allows
  internal labeled blocks and loops. IR_ENSURE_RESULT enforces ignored-result
  rules for cleanup expressions and statements. Outside cleanup, full ordinary
  unused-result checking remains incomplete; don't claim it is implemented.
- No new TIR/QBE operation is required: normal instructions and existing control
  joins represent cleanup. Noreturn cleanup stops pending transfers in sema.
- Errdefer and error-return tracing await Phase 6, mutable comptime storage/calls
  Phase 7. Constant-only empty cleanup works. Check actual Zig 0.17 sources for
  future panic semantics; current safety panics already have outlined helpers.

## Validation at M4.4a

- GCC/Clang warning-clean sanitizer suites: 518 unit checks, 449 goldens,
  29 ordinary/reference e2e inputs plus one szc-only edge input, 12 driver checks
  and 124 panic cases. Optimized GCC is warning-clean. The range-width generator
  covers all 128 signed/unsigned 1–64-bit bound types and contextual casts.
- Four 60-second libFuzzer runs and standalone corpus replay with 20,000 mutations
  per target pass. New irgen/sema seeds exercise multi-input and labeled for loops.
- Bounds are evaluated once, in input/source order, and coerced to usize. Each
  bounded length is checked end-start. IR_FOR_LEN prefers a known bound, diagnoses
  known mismatches and emits TIR_FOR_LEN_CHECK for runtime inputs before iteration.
- A shared zero-based usize index drives captures as start+index. For reuses the
  while result block / body-exit continue block structure; continue increments,
  break skips increment and else. Else has no captures; unnamed jumps target outer
  loops, while its own label supports break but not continue.
- Runtime length mismatch panics with "for loop over objects with non-equal lengths".
  Reversed ranges and overflowing unbounded companion captures use integer overflow.
  Messages/behavior were checked against 0.17 AstGen.forExpr, Sema.zirForLen and
  std.debug.forLenMismatch, plus the official executable.
- Array/slice iteration is Phase 5; inline/comptime for is Phase 7. Range pointer
  captures are invalid Zig and rejected. Defer is the next small milestone.
  Quoted identifier/label canonicalization remains incomplete (see M4.3).

## Validation at M4.3

- GCC/Clang warning-clean sanitizer suites: 518 unit checks, 427 goldens,
  25 ordinary/reference e2e inputs plus one szc-only edge input, 12 driver
  checks and 119 panic cases. `tools/generate_block_tests.py` checks all 128
  signed/unsigned 1–64-bit types, including container-level constant blocks.
- Four 60-second libFuzzer runs pass. Standalone replay covers all targets
  with 20,000 mutations each; final irgen/sema corpus replay is clean.
- Source block/loop labels now share irgen ControlScope. Named breaks target
  the block; unnamed jumps skip block labels and find outer loops. Named continue
  to a block is rejected. Reuse check_label for new labeled control constructs.
- Runtime blocks reuse IR_BLOCK/BREAK and deferred BreakExit coercion bodies.
  Comptime blocks record operands with wrapper=TIR_REF_NONE, emit no TIR and
  yield their sole known exit; nested exits to an outer label propagate noreturn.
- Explicitly noreturn break operands are unreachable-code errors, matching the
  actual 0.17 AstGen reachableExpr rule. Dead conditional exits can mark a label
  used without contributing a sema merge value, matching reference behavior.
- Constant arithmetic, nested exits, type-valued blocks and function aliases work.
  Mutable comptime storage/calls/loops and explicit comptime syntax remain Phase 7.
- Quoted label/identifier canonicalization is still incomplete: identical spelling
  works, but foo vs @"foo" and equivalent escapes do not match yet. Keep this
  visible when implementing string/identifier decoding in later milestones.

## Validation at M4.2

- GCC/Clang warning-clean sanitizer suites: 518 unit checks, 409 goldens,
  21 ordinary e2e inputs plus one szc-only edge input, 12 driver checks and
  117 panic cases. Reference checks use the official Zig 0.17.0 binary/source.
- Four 60-second libFuzzer runs plus final irgen/sema 60-second runs pass;
  standalone replay covers all targets with 20,000 mutations each.
- `tools/generate_loop_tests.py` covers 128 integer types in e2e/CI. Tests cover
  nested/labeled jumps, loop else/values, typed joins, skipped division guards,
  continue-expression exits and local reinitialization. New panic cases cover
  checked overflow in a loop body and continue expression.
- While lowering uses outer result IR_BLOCK -> IR_LOOP -> condition IR_CONDBR;
  the true arm has a body-exit IR_BLOCK, continue expression and explicit
  IR_REPEAT(loop_ref). Continue breaks the body-exit block. Break targets the
  outer result block. Else unnamed jumps go to an outer loop; its own label
  supports break only. Keep these scopes distinct when adding for/defer.
- Sema's analysis-local ControlTarget chain and deferred BreakExit bodies support
  peer-typed joins without escaping branch-local operands. Reuse IR_BLOCK/BREAK
  and this merge infrastructure for M4.3; source labeled blocks still need irgen.
- Ordinary shift compound assignments use IR_LOG2_INT_TYPE for contextual casts.
- Inline/comptime loops and optional/error-union captures remain unsupported;
  container-level comptime while (even false) currently reports an evaluation
  error. Comptime loops belong to Phase 7, captures to Phase 6.

## Validation at M4.1

- GCC/Clang warning-clean sanitizer suites: 518 unit checks, 389 goldens,
  16 ordinary e2e inputs (including arithmetic and conditional-width generators),
  one szc-only edge input, 12 driver checks and 115 panic cases.
- Ordinary/reference inputs pass with official Zig 0.17.0; all 128 signed/unsigned
  1–64-bit conditional joins are exercised. Diagnostic messages are reference-checked.
- Four 60-second libFuzzer runs and final sema/codegen 60-second run pass;
  standalone replay covers all four targets with 20,000 mutations each.
- `tools/generate_conditional_tests.py` is part of e2e/CI. New IR_IF arm bodies
  yield via break_inline BODY_RESULT; do not treat IR_IF as an irgen terminator.
- Both runtime arms are analyzed before gathering merge results, preserving
  independent errors. A single constant exit is represented by a void control
  block plus the known operand; a two-exit comptime-only result is an error.

## Validation at M3.6b

- GCC/Clang sanitizer suites: 518 unit checks, 377 goldens, 12 ordinary e2e inputs,
  one szc-only edge input, 12 driver checks, 115 panic cases. Ordinary/reference
  runs pass with the official Zig 0.17 binary; additional generated seeds 1/42/65535
  match as well. Four 60-second local fuzz runs pass.
- Known reference discrepancy: native @rem/@mod(i64.min, -1) traps in official Zig;
  its comptime evaluator returns zero. szc returns zero. The separate `tests/run_szc`
  case is intentionally executed without ZIG; do not fold it into reference e2e tests.
- `comptime.c` now has alias-safe rounded BigInt division (trunc/floor/ceil).
  @mod accepts negative denominators in 0.17; do not apply older Zig restrictions.

## Validation at M3.6a

- Fresh GCC/Clang `-Werror` sanitizer builds: 515 unit checks, 369 goldens,
  ten e2e inputs, 12 driver checks, 77 generated panic cases; reference validation
  passes with official Zig 0.17.0. GCC optimized build is warning-clean too.
- Standalone fuzz replay and four 60-second libFuzzer runs pass.
- Generated width tests now also exercise non-overflowing checked arithmetic;
  `tests/run/safety_boundaries.zig` covers minima/maxima, zero products, narrowing
  casts and u0. The panic harness checks widths 1/5/8/32/33/64 and compiler success
  separately from SIGABRT/message validation.

## Validation at M3.5

- GCC and Clang fresh `-Werror` ASan/UBSan: 366 goldens, nine executable inputs
  checked with official Zig, 12 driver boundary tests, standalone fuzz replay clean.
- Four libFuzzer targets ran 60 seconds each locally without failures; sema fuzzing
  now also exercises QBE emission. Existing CI push/nightly schedules are retained.
- New CI configuration is pushed; check GitHub Actions for its latest status.
  M3.3's existing CI passed.

## Notes for the next steps

- Reference Zig 0.17.0: source tarball from codeberg (ziglang/zig tag 0.17.0) and the binary
  `https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz`. Always check behavior
  against **these** sources (`lib/std/zig/{tokenizer,Parse,Ast}.zig`), not memory of older Zig.
- Parser differential tests (need the official binary `$ZIG`):
  `$ZIG build-exe tools/ast_spans.zig -OReleaseFast -femit-bin=build/ast_spans`, then
  `tools/diff_ast_spans.sh build/szc build/ast_spans <file-list>` and
  `tools/diff_parse_errors.py build/szc build/ast_spans <file-list> [count] [seed]`.
  `zig fmt --check file.zig` shows the reference's parse error for a mutant.
- Number literals are lexed loosely (like the reference). Validate them when decoding (M3.x).
- Phase 4: runtime range for and scope-exit defer are implemented.
  Explicit @panic and literal byte values are implemented; single pointers are next.
  Array/slice inputs and comptime loops remain pending.
  Source if is now IR_IF with IrCondbr payload and break_inline BODY_RESULT arm
  exits. It lowers to TIR block/condbr/br. IR_CONDBR is still a terminator.
  Known conditions select one arm; a sole surviving constant stays known, while
  two runtime exits of comptime-only type are rejected even for equal values.
  Noreturn blocks omit QBE join labels. Reuse the block/loop control targets
  for future array/slice loops and error cleanup, checking the actual 0.17.0
  reference first.
- `make safety-test` checks generated runtime panic cases; setting `ZIG` enables
  reference validation. Checks are always enabled in szc for now. Panic stubs
  print one line via libc write, then abort; explicit unreachable uses this runtime.
  Only backend-only trap/known noreturn-call terminators use hlt.
- Pending user decision: how to build the Zig compiler source (M10.2).
