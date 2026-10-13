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
- Next: **M4.3** labeled blocks as expressions and break with value.
- Run `PATH=/data/tools/bin:$PATH ZIG=/data/zig/zig-x86_64-linux-0.17.0/zig make e2e-test`
  locally. `tools/build_qbe.sh` builds the pinned QBE into `build/qbe-src` on CI.
- Tools: `SZC_QBE`/`qbe`, `SZC_CC`/`cc`; pinned QBE revision
  `e786f06032fefa2e3790d6b1c9e31ed138f475a6`. Local QBE: `/data/tools/bin/qbe`.
- Restored sandbox needed libasan/libubsan/clang/compiler-rt reinstalled. Git
  credentials are not persisted; use transient authentication for future pushes.
- M3.4 (`1882ced`) and M3.5 (`7b36edd`) are pushed to `main`.
- CI through M4.1 (`957fd22`): completed successfully.

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
- Phase 4: parser has for/labels/defer AST nodes; for/defer and labeled blocks still need irgen/sema.
  Source if is now IR_IF with IrCondbr payload and break_inline BODY_RESULT arm
  exits. It lowers to TIR block/condbr/br. IR_CONDBR is still a terminator.
  Known conditions select one arm; a sole surviving constant stays known, while
  two runtime exits of comptime-only type are rejected even for equal values.
  Noreturn blocks omit QBE join labels. Reuse the new block/loop control targets
  for labeled value blocks, checking the actual 0.17.0 reference first.
- `make safety-test` checks generated runtime panic cases; setting `ZIG` enables
  reference validation. Checks are always enabled in szc for now. Panic stubs
  print one line via libc write, then abort; explicit unreachable still uses hlt.
- Pending user decision: how to build the Zig compiler source (M10.2).
