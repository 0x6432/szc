# Untyped IR (irgen.c → sema.c)

The untyped IR plays the role of Zig's ZIR. `irgen.c` turns one file's AST into
one `IrFile`; it knows nothing about types. `sema.c` analyses it lazily (only the
declarations that are referenced) into typed IR, calling `comptime.c` to evaluate
anything comptime-known.

## Shape

- An `IrFile` has one array of instructions (`insts`), one `uint32_t` array of
  extra data (`extra`) for variable-length payloads, and the file's root
  instruction (a `struct_decl`: a file is a struct).
- An instruction is `{op, src, data}`: `op` is an `IrOp`, `src` is the byte offset
  of the AST node's main token (for diagnostics) and `data` is a small union whose
  meaning depends on `op`.
- Operands are `IrRef`s. A ref below `IR_REF_FIRST_INST` names a well-known
  constant (`void` value, `true`, `u8` type, …: the `IR_WELL_KNOWN` list in
  `ir.h`); any other ref `r` names instruction `r - IR_REF_FIRST_INST`.
  `IR_REF_NONE` (0) means "absent".
- A **body** is a list of instructions, stored in `extra` as `count, inst...`
  and referenced by its extra index (`IrBody`). Instructions of all bodies live in
  the same `insts` array; a body lists the ones that belong to it, in execution
  order. Every body ends with a terminator (`break`, `break_inline`, `ret`,
  `repeat`, `condbr`, `unreachable`, …).
- Names (identifiers, field names, string literal contents) are interned
  `StringId`s (`intern.c`), so sema compares names by id.

## Scopes and names

irgen resolves every identifier lexically:
- a local constant or parameter → the ref of the instruction that produced it;
- a local variable → `load` of its `alloc` (or the alloc itself as an lvalue);
- a primitive (`u8`, `bool`, `true`, `null`, `undefined`, `i7`, …) → a
  well-known ref or an `int_type` instruction;
- a declaration of an enclosing container → `decl_val name` / `decl_ref name`.
  sema looks the name up in the namespace of the body being analysed and its
  parents;
- anything else is reported as "use of undeclared identifier".
Shadowing is an error, as in Zig.

## Result types

Many Zig expressions need the type they are coerced to (`@intCast(x)`, `.{...}`,
`.enum_literal`, `undefined`…). irgen threads a `ResultInfo` through expression
generation, like AstGen: "no result type", "coerce to type ref T", or "the
pointee type of pointer ref P" (assignment). When an expression needs its result
type, irgen materializes it (`elem_type` of the pointer's type, the param type
of a call argument, the function's `ret_type`, a field's type, …).
Ordinary shift assignments use `log2_int_type value` for the RHS result type.

## Declarations

`struct_decl` (also enum/union/opaque) payload in `extra`: kind and layout,
optional argument body (backing integer / tag type), then for each field its
name, flags and type / alignment / default-value bodies, then for each
declaration its name, flags (pub, export, extern, threadlocal, var, test,
comptime) and value body. A declaration's value body is analysed when the
declaration is first referenced; it ends with `break_inline` of the value.

Functions are a `func` instruction inside the declaration's value body. The
parameters are `param` instructions (name, comptime flag, type body); the
function body refers to them, and sema binds them to arguments (runtime
parameters) or values (comptime parameters of a generic instantiation). The
return type is a body too, so it may depend on comptime parameters.

## Control flow

- `block` has a body; `break block, value` leaves it with a value. Source labeled
  blocks use this pair; fallthrough contributes void. Unnamed jumps skip block
  labels to find enclosing loops; named continue cannot target a block.
  In a comptime context, block exits record known values without emitting typed
  IR, including nested exits to an outer label. `break_inline` is the body-result
  variant used by declarations, types, call arguments and source if arms.
- `condbr cond, then_body, else_body` ends a body.
- Source `if` uses `if cond, then_body, else_body` (`IrCondbr` payload).
  Each arm ends with `break_inline BODY_RESULT, value`, or a terminator such as
  `ret`/`unreachable`. This is a value-producing instruction, not a syntactic
  terminator. Sema selects a known arm or lowers runtime arms to a typed result
  block with `condbr` and `br`; an arm that returns does not contribute a value.
  Two runtime exits cannot merge comptime-only types. One constant exit remains
  known while a void result block preserves the runtime control effects.
- `loop` has a body that ends with `repeat loop_ref`; leaving the loop is a
  `break` of an enclosing block. Source while wraps the loop in a result block;
  its condition body ends with condbr. The true arm has a body-exit block (the
  continue target), followed by the continue expression and repeat; the false
  arm yields the else result to the outer result block. Break skips else.
- Source range `for` evaluates starts/ends once in source order as usize. Bounded
  inputs produce checked `sub end, start` lengths. `for_len` has an extra-data
  payload of count followed by bounded length refs; unbounded companions do not
  contribute lengths. Sema selects a known bound when available, rejects known
  mismatches, and emits void `TIR_FOR_LEN_CHECK` comparisons for runtime bounds.
  QBE guards length equality before iteration with the dedicated mismatch panic.
  An anonymous usize index allocation starts at zero. The loop compares it to the
  common length, binds immutable captures as checked start+index, and increments
  after the body-exit block before repeat. For reuses while's outer result block
  and separate continue target; break skips increment/else. Else cannot see captures.
- Source `defer` registers a reusable cleanup body without executing it. Lexical
  scopes after registration retain its body and source position. Scope exits
  emit `defer` payload instructions in reverse order, but only for crossed scopes.
  Return/break values are evaluated before cleanup. The body ends in
  `break_inline BODY_RESULT, void` or a noreturn terminator; escaping control is
  forbidden during irgen. `ensure_result` requires discarded cleanup values to
  be void/noreturn. Sema expands each body in a fresh operand-map overlay whose
  parent resolves captured refs. Cleanup-local operands stay private to that
  expansion; typed instructions append to the exit's current body. No new TIR
  cleanup operation is needed, and panic/unreachable do not unwind defers.
- Sema collects block exits using an analysis-local control-target chain. Each
  exit reserves a noreturn typed block whose body is filled after peer typing
  with any needed coercion and br. This keeps runtime operands in their branch.
- `and`/`or` are `bool_and`/`bool_or` with a right-hand body (short-circuit).
- `ret value`, `ret_implicit` (falling off the end of a `void` function).

## Instruction set

The authoritative list is `IR_OP_LIST` in `ir.h`, with the meaning of each
instruction's operands next to it. It grows milestone by milestone; `szc ir`
prints a file's IR.
