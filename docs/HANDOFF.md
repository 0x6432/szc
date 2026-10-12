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

- Done: M0.1–M0.5 (infrastructure), M1.1–M1.6 (lexer).
- Next: **M2.1** AST node structs + parser lookahead buffer + `szc ast` dumper.

## Notes for the next steps

- Parser: pull tokens with `lexer_next` into a small ring buffer for lookahead.
  The grammar needs at most 2–3 tokens of lookahead (e.g. `IDENTIFIER COLON` for labels,
  `DOT IDENTIFIER EQUAL` in init lists). Doc comments are tokens: the parser
  must skip or attach them.
- Each parse function begins with the PEG rule it implements, copied from `docs/grammar.peg`.
- Number literals are lexed loosely (like the reference). Validate them when decoding (M3.x).
- Pending user decision: how to build the Zig compiler source (M10.2).
