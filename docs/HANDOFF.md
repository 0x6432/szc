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
- Next: **M3.6** arithmetic safety, saturating operations and panic stub.
- Run `PATH=/data/tools/bin:$PATH ZIG=/data/zig/zig-x86_64-linux-0.17.0/zig make e2e-test`
  locally. `tools/build_qbe.sh` builds the pinned QBE into `build/qbe-src` on CI.
- Tools: `SZC_QBE`/`qbe`, `SZC_CC`/`cc`; pinned QBE revision
  `e786f06032fefa2e3790d6b1c9e31ed138f475a6`. Local QBE: `/data/tools/bin/qbe`.
- Restored sandbox needed libasan/libubsan/clang/compiler-rt reinstalled. Saved Git
  credentials did not survive; push fails without restoring authentication.
- CI for previous M3.3 commit `2b0d0ca`: completed successfully.

## Validation at M3.5

- GCC and Clang fresh `-Werror` ASan/UBSan: 366 goldens, nine executable inputs
  checked with official Zig, 12 driver boundary tests, standalone fuzz replay clean.
- Four libFuzzer targets ran 60 seconds each locally without failures; sema fuzzing
  now also exercises QBE emission. Existing CI push/nightly schedules are retained.
- New CI configuration is committed but not yet run: GitHub push authentication
  must be restored first. M3.3's existing CI passed.

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
- M3.6 needs to distinguish checked vs wrapping arithmetic in typed IR (currently
  both lower to `TIR_ADD/SUB/MUL/NEGATE`); range-check narrowing/signedness-changing
  `TIR_INT_CAST`, add saturating runtime ops and division/shift safety as appropriate.
  Always verify these semantics against the 0.17.0 Sema/arith sources and binary.
- Pending user decision: how to build the Zig compiler source (M10.2).
