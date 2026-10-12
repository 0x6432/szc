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
   The GitHub token is **never** committed. Ask the user for it.

## Current state

- Done: M0 (infrastructure), M1 (lexer), M2 (AST + parser, differential-tested against
  `std.zig.Ast`).
- Next: **M3.1** untyped IR design in `src/irgen.c` + `szc ir` dump.

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
- Pending user decision: how to build the Zig compiler source (M10.2).
