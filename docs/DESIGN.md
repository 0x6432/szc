# szc design

szc is a Zig 0.17.0 compiler written in C. Its only goal is to bootstrap the
official Zig compiler on x86_64-linux. It emits QBE IL and runs `qbe` (1.3 or
master) and the system `cc` (as assembler and linker) as separate programs.

## Pipeline (one file per stage)

```
source ──lexer.c──> tokens (produced on demand, one at a time)
       ──parser.c─> AST (ast.h: one struct type per node kind)
       ──irgen.c──> untyped IR (ZIR-like: one instruction stream per declaration)
       ──sema.c───> typed IR (types resolved, generics instantiated, lazy decls)
            └── comptime.c: comptime interpreter + big integers, called by sema
       ──codegen.c> QBE IL text
       ──driver───> qbe -o x.s x.ssa ; cc x.s rt.s -o x
```

Support files: `arena.c` (memory), `diag.c` (error collection), `source.c`
(file loading), `main.c` (command line).

## Decisions

| Topic | Decision |
|---|---|
| Language | C99, with C11/C23 features only where they make code much simpler |
| Lexer | Hand-written state machine. The parser pulls one token at a time (`lexer_next`). Tokens are `{kind, start, end}`: no copied text |
| Keywords | Recognized once, in the lexer (switch on length, then a compare). Parser only compares `TokenKind` values, e.g. `TK_PUB`; it never compares strings |
| AST | Separate struct per node kind (`AstFnDecl`, `AstBinary`, …), all with a common `AstNode` header holding the kind and token. Children are typed pointers |
| Parser | Recursive descent. Each function has a comment with the PEG rule it implements (from `docs/grammar.peg`). One function per precedence level |
| Lookahead | Small ring buffer of tokens filled on demand from the lexer |
| Errors | Collected (not fatal); the parser resyncs at `;`, `}` or next declaration. Exit status is non-zero if any error was reported |
| Memory | Arena allocators. Nothing is freed individually; arenas are freed whole. Must be ASan/LSan clean |
| IR | irgen produces untyped IR (like Zig's ZIR); sema analyses it lazily into typed IR; comptime.c interprets IR for comptime |
| Backend | QBE IL text, `qbe` run as a separate process. Thread-locals use QBE's `thread` |
| Target | x86_64-linux only |
| std | The std library shipped with Zig: `ZIG_LIB_DIR`, else `<dir of szc>/../lib/zig`, else `<dir of szc>/../lib` |
| f16/f80/f128 | Hard compile error for now ("unsupported by szc") |
| Type safety | Enums are passed as their enum type, booleans as `bool`, sizes as fixed-width unsigned types |

## Features QBE lacks

How other QBE-based compilers handle it:
- **cproc** (C on QBE) has no inline assembly (it needs QBE support), no atomic types and no `long double`.
- **Hare** has no inline assembly. Its runtime puts syscalls and other machine code in separate `.s`
  files that the build driver assembles and links in.

What szc does:
- **Inline asm**: each `asm` expression is *outlined*. szc generates a small
  assembly function in a separate `.s` file. Inputs are passed as ordinary
  SysV arguments, and the stub moves them into the requested registers
  (`{rax}`, `{rdi}`, …), runs the template, then moves the output to `rax`.
  QBE just sees a normal call. This covers how std uses asm on
  x86_64-linux (syscalls, `start.zig`, cpuid, etc.).
- **Atomics**: calls to hand-written helpers in `rt/rt_x86_64.s`
  (`lock cmpxchg`, `xchg`, `lock xadd`, `mfence`).
- **u128/i128**: lowered to pairs of 64-bit values; mul/div go through helpers.
- **@Vector**: lowered to scalar operations on an array.
- **f16/f80/f128**: compile error for now.

## Deferred decisions

- How to build the Zig compiler source (build options, module list): decide at M10.2 with the user.
