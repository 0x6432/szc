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
- `as_return type, value` marks the outermost direct return-value coercion;
  ordinary `as` instructions propagated into branches remain casts. This preserves
  the reference distinction between invalid noreturn returns and casts. Source
  return operands must be syntactically reachable before scope-exit cleanup.
- `ret value`, `ret_implicit` (falling off the end of a `void` function).
- Explicit source `unreachable` is a comptime error or runtime TIR_UNREACHABLE,
  which aborts through the safety panic helper. Internal TIR_TRAP after statically
  known noreturn calls/fallback bodies emits only QBE hlt. Noreturn exits contribute
  no value to branch/block joins. Neither termination path unwinds defer cleanup.

## Instruction set

The authoritative list is `IR_OP_LIST` in `ir.h`, with the meaning of each
instruction's operands next to it. It grows milestone by milestone; `szc ir`
prints a file's IR.

## Literal bytes and explicit panic

- `str` holds interned decoded bytes. Quoted literals decode Zig 0.17 escapes and
  UTF-8 Unicode scalar values; multiline tokens join raw contents with newlines,
  with no implicit trailing newline. Sema produces a const single pointer to a
  zero-sentinel u8 array, with structural TYPE_ARRAY length/element information.
- VALUE_BYTES owns and interns bytes with an explicit length; that length excludes
  the zero sentinel. Empty strings and embedded NULs are not strlen-based values.
  Aliases, variables and fixed-array pointer joins use existing operand machinery.
  Codegen materializes constants as references to read-only globals keyed by ValueId.
- `panic` has IrPanic payload: message ref and argument source position. Sema
  validates the fixed-array pointer -> const-byte-slice conversion, then lowers to
  TIR_PANIC(pointer, length). The outlined helper writes bytes/newline and aborts.
  At comptime it records an error; binary message lengths survive diagnostic printing.
  Different-length runtime joins/general slice representations await memory lowering.
  Panic's noreturn control flow never causes scope-exit cleanup to run.

## Single-pointer operations

- `ptr_type` uses an IrPointerType payload with a child type ref, const/allowzero
  flags, and an optional alignment comptime body/source offset. Its text dump retains
  the old `child, @true/@false` const notation, appends `allowzero` when set and adds
  an `align { ... }` body. The source location identifies the child type. Alignment
  bodies run in an isolated comptime context with captured outer operands but no TIR
  or enclosing function/control target. The resulting defined u29 must be a nonzero
  power of two. Zero in TypePointer.alignment means implicit ABI alignment; explicit
  natural alignment remains a distinct type. Alignment can weaken in a coercion,
  while mutable outer pointees require equal effective alignment.
  Pointer identity includes allowzero; coercion may add it but cannot discard it,
  and mutable outer pointees require bidirectional compatibility. A branch join
  can combine top-level const/allowzero flags only when both inputs can coerce to
  the resulting pointer. Existing references and storage addresses remain nonzero.
- `align` is IR_ALIGN with an isolated comptime body, using the same body generator
  and validation as pointer type alignment. It yields a validated u29 before a
  mutable local's initializer is analyzed. IrAlloc.alignment references that value,
  or NONE for implicit ABI alignment. The alloc dump appends `align=%ref` only for
  an explicit attribute; TIR_ALLOC records it in the slot pointer type. Child ABI
  layout is unchanged. QBE slots above alignment 16 use private over-allocation
  and upward address rounding; initialization is not hoisted with the allocation.
  Aligned local constants use CONST_BIND below; mutable globals and global
  constants use IrDecl alignment bodies. Function declaration alignment remains pending.
- Address-of mutable locals reuses ALLOC refs. Source `deref(pointer)` validates
  the pointer and produces a typed load, or an OPV void/u0 constant without memory.
  `ref_deref(pointer)` performs the same validation and returns the pointer itself
  for assignment targets and `&p.*`. Neither cancellation nor ignored results
  bypass undefined/non-pointer validation.
- Internal LOAD remains distinct from source dereference. ELEM_TYPE only needs a
  valid pointer type; stores check const qualification and coerce the value before
  emitting memory access. Zero-storage void/u0 stores emit no typed memory op.
- Compatible qualification uses TIR_PTR_CAST, a long QBE copy. Nested coercions
  must be invariant below any mutable outer pointee; scalar pointee widening is
  not permitted. Peer resolution can choose either compatible existing pointer
  type. Addressable constants/temporaries, identity and general comptime memory
  are intentionally deferred, not simulated with conditional stack spills.

## Definition-anchored references and immutable objects

- IR_REF takes a value and returns a const single pointer. Irgen caches one
  reference per defining instruction and inserts it immediately after the value
  when finalizing its body; parameter reference chains appear at function entry.
  Taking an address only in one branch still yields a dominating initialization.
  References of references are appended in dependency order. Mutable locals keep
  their existing allocation refs, and named constants/params keep their value refs.
- IR_CONST_BIND wraps an explicitly aligned local const initializer and its
  validated alignment ref. Copies of such bindings also get a wrapper, with NONE
  for an unannotated destination: source storage alignment must not leak through
  a copied value. It returns the original operand, not a runtime load,
  and is a separate definition for anchored references. IR_REF reads this binding's
  alignment when constructing its const pointer. Runtime values get aligned stack
  storage; known values keep their comptime payload and aligned immutable objects.
  Taking an explicitly aligned bare-function alias address is NYI until code-symbol
  alignment is implemented; aligned function-pointer slots are ordinary objects.
- IR_REF_TYPE extracts the child of an expected address result type. It applies
  result context to temporary values, not named bindings; `&1` can receive u8
  context while a named comptime_int constant retains its original pointee type.
- Sema lowers runtime references to stack allocation plus initialization, and known
  ones to VALUE_REF/ConstObjectId. ConstObject records an immutable ValueId and an
  identity and byte alignment (0 means natural): anonymous value addresses with
  the same alignment deduplicate; each named global constant has
  its own identity. Retyping a compatible pointer does not change its object.
- Known dereferences recover the stored value without losing comptime knowledge.
  Pointer equality compares object IDs or runtime address bits. A known undefined
  pointer comparison yields undefined bool, diagnosed only when a defined result
  is needed (for example a compile-time branch condition).
- Codegen marks transitive immutable object dependencies and emits scalar backing
  objects in .rodata. Pointer-valued objects use .data.rel.ro, preserving PIE/RELRO
  without text relocations. Literal bytes retain their separate read-only storage.
  Runtime local/parameter values never become static mutable temporary objects.

## Outlined immutable data

- The pinned QBE parser limits data alignment to powers of two through 64. Codegen
  accepts an optional assembly companion stream for immutable objects above that
  limit, using ELF .balign and scalar/symbol relocation directives. Dependencies
  are still marked transitively before either data stream is emitted. The driver
  appends the companion to the external QBE output before invoking system cc.
- Standalone emit-qbe rejects materialized static alignment above 64 rather than
  emitting invalid or insufficiently aligned IL. build-exe/run provide the complete
  pipeline, and retained .s output includes the outlined data. Pointer objects use
  .data.rel.ro in both streams; there are no text relocations or mutable statics
  standing in for runtime local values.

## Mutable global declaration alignment

- IrDecl adds alignment_body/alignment_src for a mutable global's optional isolated
  comptime attribute. Declared type/layout resolves first, then alignment, before
  publishing the address; inferred globals resolve initializer/type before alignment.
  Re-entering a typed declaration reuses its pointer modifiers without reevaluation.
- SemaGlobal stores byte alignment (0 means implicit ABI); its VALUE_GLOBAL address
  has the corresponding pointer type. TIR dumps append align(N) only when explicit.
  Existing lazy initializer draining and declaration dependency diagnostics remain.
- Static alignment through 64 uses ordinary QBE data; larger mutable objects use
  outlined writable .data with .balign, including .zero for zero/undefined bytes.
  Immutable data sections do not change. Global constants use the path below;
  function alignment and threadlocal/export/extern variable limitations remain.

## Global constant declaration alignment

- IrDecl.alignment_body/alignment_src also describe a global constant attribute.
  Explicitly typed aligned constants use type_body and IR_DECL_TYPE, resolving
  type then alignment before initializer analysis. Inferred constants resolve
  initializer/type first, then alignment. Unannotated constants retain their
  existing inline type-body representation.
- SemaDecl.alignment stores validated bytes (0 implicit ABI) independently of the
  known value. IR_DECL_REF passes this and the declaration's address_identity to
  analyze_ref; ConstObject retains immutable storage alignment without losing the
  original operand's comptime value. Copying the value does not inherit alignment.
- Global comptime-only types and bare function aliases cannot carry alignment;
  function-pointer objects can. Aligned exported/extern/threadlocal constants are
  explicit NYI. Type-only publication for constant address-dependency cycles is
  not implemented; unlike typed mutable globals, these keep dependency-loop errors.
- Existing immutable static codegen handles explicit alignment and relocations,
  including the outlined assembly companion above QBE's 64-byte data limit.

## Function declaration alignment

- IrFuncHeader.align_body/align_src describe a declaration attribute, evaluated
  with the shared isolated u29 validation. Function type syntax cannot carry it.
  SemaFunc.alignment stores code alignment independently of TypeFn identity;
  function_address uses it in the returned const pointer, including alias/decay.
- QBE guarantees 16-byte function alignment. Larger requests select a unique
  executable .text.szc.fn.ID section, with the sole body at offset zero. The
  assembly companion establishes section alignment before the QBE stream. Driver
  combination copies streams unchanged, companion first. Standalone emit-qbe
  diagnoses alignment above 16 because it cannot carry this ELF prelude.
- Exported bodies use the same mechanism; extern alignment is supplier-guaranteed.
  Explicitly aligned local bare-function alias addresses remain NYI.

## Volatile pointer qualification

- IrPointerType.flags adds IR_POINTER_VOLATILE; TypePointer retains it independently
  of const, allowzero and alignment. Names/dumps show it, and type interning includes
  the bit. Top-level conversion can add volatile, never discard it. Nested mutable
  pointers remain invariant; function parameter variance reuses compatibility rules.
- Pointer joins union const/allowzero/volatile and choose weakest alignment, then
  validate both operands against the resulting type. Qualifiers do not alter size,
  storage identity or the ABI. Observable accesses are lowered as described below.

## Volatile memory accesses

- Runtime dereferences/stores through volatile pointers emit TIR_LOAD_VOLATILE or
  TIR_STORE_VOLATILE. They share operand shapes with LOAD/STORE but are effectful
  even for unused read results. Compound assignment retains both operations.
  Known immutable values and void/u0 still fold; runtime global contents do not.
- QBE has no native volatile opcode. Codegen emits opaque calls to private outlined
  QBE helpers, each containing exactly one width-correct load or store. Helpers
  return load values, receive store pointers/values, and cannot be interprocedurally
  inlined by QBE. No purity/LTO assumptions are allowed at this boundary. Calls
  conservatively barrier memory; this is not atomicity or inter-thread ordering.
  Load results normalize narrow signed/unsigned representations. Standalone IL
  includes all used helper definitions and needs no separate assembly for them.
- No memory operation is needed for void/u0. Undefined stores use an arbitrary
  zero in szc, but no stable value/access-count guarantee is made for undefined
  values; actual Zig ReleaseFast may omit such a store.

## Compile-time array types and values

- IR_ARRAY_TYPE uses bin(length-ref, child-type-ref); length is known usize with full
  64-bit range. IR_ARRAY_ELEM_TYPE extracts a child type for literal result locations.
  `[_]T` receives the positional literal count during irgen, not a fake standalone type.
- IR_ARRAY_INIT extra words are `[type_ref, count, element_refs...]`. Each element
  receives the child result type, including nested anonymous literals and integer casts.
  Sema checks exact count/coercion. Known elements use VALUE_ARRAY, which interns
  immutable, deep-copied ordered ValueIds, preserving undefined elements; zero-sentinel
  arrays include their sentinel value after the ordinary elements.
- IR_ARRAY_INDEX uses bin(base, usize-index), accepting arrays or single pointers to
  arrays. Known indexing checks the ordinary/sentinel bounds and extracts a known
  element; it does not create runtime aggregate storage or approximate element refs.
  IR_FIELD_VAL uses name(field, base); array `.len` returns known usize from type alone.
- IR_ARRAY_CAT uses bin(lhs, rhs), joining child types and checking length overflow.
  Either zero sentinel is retained; either pointer operand makes the result a const
  single pointer. Defined u8 pointer results reuse VALUE_BYTES, otherwise immutable
  VALUE_ARRAY/VALUE_REF backing is used. Whole-array byte loads produce VALUE_ARRAY.
- Dense array construction/copies share a 1,048,576 element-work budget per Sema.
  This explicit NYI resource limit is separate from full-width type lengths. There
  is a private internal array ABI described below; C by-value arrays are forbidden.

## Immutable array backing and runtime scalar element reads

- TIR_ARRAY_ELEM_PTR is bin(array pointer, usize index), with an element-pointer type.
  Parent array type supplies ordinary length, sentinel flag and checked child stride.
  Runtime unsigned guards use >= ordinary length or > sentinel length, without adding
  one to a possibly maximal usize. Constant indices are validated before TIR lowering.
- Immutable VALUE_ARRAY data uses recursive element emission and dependency/callback
  reachability over the interned DAG. Read-only pointer leaves select .data.rel.ro;
  other supported leaves select .rodata. Data alignment and physical byte sizes are
  real, including outlined >64-byte alignment. Runtime values use private snapshots below.
- sema_type_size now returns bool and writes a uint64_t result only on success. Physical
  callers diagnose overflow instead of consuming a wrapped/truncated size. Zero-bit
  array layout remains zero; total static array bytes have a 16,777,216-byte NYI budget.
- Element loads reuse LOAD/LOAD_VOLATILE; qualifiers propagate and ordinary narrow
  loads also normalize logical width. Known u0/void or wholly undefined array *values*
  can fold before a dynamic bounds guard, matching Zig; runtime pointer accesses
  preserve the guard. This is not permission to skip pointer bounds checks for OPV.
- Mutable slots/snapshots, source element addresses and the private by-value ABI are
  described below. Known element addresses use the provenance model following this
  section; never replace them with refs to copied element constants.

## Comptime element-pointer provenance

- VALUE_ELEMENT_PTR holds ElementPointer `{base, index, offset, logical}`. `base` is
  an interned pointer to the actual parent array, not a value ref to a copied element;
  nested element paths keep the same root. `index` drives known loads through the
  parent array value. `offset` is a checked cumulative root-relative byte offset for
  runtime-layout children, or logical leaf index for comptime-only children.
- value_pointer_address strips element paths and VALUE_PTR_CAST qualification aliases
  to return `{root, offset, logical}`. Sema equality compares root object/global/byte
  identity and offset, ignoring qualifiers. Zero-sized runtime children therefore share
  addresses, while distinct slots of type-only arrays keep logical identity.
- VALUE_PTR_CAST retains original byte backing when a known string pointer is qualified
  or loses its sentinel. Retagging VALUE_BYTES itself could otherwise emit a new symbol
  and make runtime equality disagree with comptime equality. Known byte reads access
  compact backing directly; whole-array loads still construct ordinary VALUE_ARRAY.
- Known constant indices in ARRAY_INDEX_PTR and borrowed ARRAY_INDEX_BASE create these
  values after bounds/undefined-index checks. Known undefined base pointers propagate
  undefined addresses; undefined *array values* still have real immutable backing.
  Strides, physical layouts and cumulative offsets never silently wrap. Comptime-only
  paths cannot materialize machine addresses; they are for semantic loads/equality.
- Scalar TIR_CONST element pointers use a root symbol directly at offset zero and a
  QBE `add` temporary otherwise. Static pointer initializers emit symbol-plus-addend
  relocations, in QBE data or outlined assembly as appropriate. Root dependency and
  function-address walks preserve backing/callback reachability.
- Element-pointer type derivation follows Zig 0.17 Type.elemPtrType: constant index zero
  preserves base alignment; other known offsets and dynamic strides constrain it by
  divisibility. Const/volatile survive, known nonzero indices drop allowzero, and a
  weakly aligned root stays weak. Borrowed runtime indexing shares this helper.

## Mutable array slots and owned runtime values

- OPERAND_AGGREGATE carries the semantic array type and a TIR pointer to an owned
  private snapshot. It is never a scalar array temporary. Whole-array loads copy
  into fresh storage before later side effects; immutable binding aliases may share
  immutable storage, as Zig permits. Explicitly aligned bindings get their own slots.
- TIR_ARRAY_COPY is bin(destination pointer, source pointer), type void. The destination
  array type determines checked byte size; codegen emits overlap-safe libc memmove,
  with no operation for zero bytes. Whole-array stores coerce exact child/length and
  may drop a sentinel, never widen scalar elements. Undefined stores need no bytes.
- Mutable slots, snapshots and runtime literal temporaries use existing TIR_ALLOC.
  Supported recursive leaves are bool, pointers, void and integers up to 64 bits.
  Checked sizes and over-alignment padding count against 16,777,216 aggregate stack
  bytes per function; overflow and unsupported leaves diagnose instead of wrapping.
- IR_ARRAY_BASE_PTR normalizes an array slot or a slot containing a single array
  pointer. IR_ARRAY_INDEX_BASE preserves borrowed nested array storage until the next
  index is evaluated. These avoid incorrectly snapshotting `a` before side effects in
  `a[index(&a)]`. Borrowed storage always has a pointer operand, not an aggregate value.
- IR_ARRAY_INDEX_PTR produces a checked real element address, preserving qualifiers
  and conservative alignment. Scalar reads/writes reuse existing volatile/narrow-width
  helpers. Runtime nested-array reads create snapshots when used as array values.
- Anonymous assignment literals inherit result storage. IR_ARRAY_INIT_PTR validates
  destination/count before children; each child pointer/store is lowered immediately,
  recursively for nested anonymous literals. A final owned snapshot keeps the existing
  assignment interface safe. Explicitly typed literals and @as create temporaries
  instead: `a = .{9, read(&a)}` and `a = [_]u8{9, read(&a)}` intentionally differ.
- Ordinary IR_ARRAY_INIT still has `[type_ref, count, element_refs...]`; mixed/runtime
  elements initialize private storage only after their expressions have been evaluated.
  Known literals keep existing interning/comptime behavior. Dense element-work limits
  remain independent of stack/static byte limits.
- Scalar materialization still rejects arrays; calls and joins use the indirect
  lowering below. General comptime element-pointer provenance, volatile whole-array
  copies, mutable array globals, explicit sentinel syntax and slices remain NYI.

## Internal array calls, returns and joins

- Source TypeFn keeps logical array parameter/return types. Physical TIR_ARG array
  parameters are const align(1) single pointers; a fresh ABI-aligned snapshot binds
  each immutable source parameter. TIR_CALL arguments carry array backing addresses,
  not scalar array instructions, and preserve source evaluation order/snapshots.
- Array results allocate caller-owned storage; the lowered argument list appends its
  pointer after every source argument. The call instruction has type void and the
  caller obtains an immutable aggregate operand only after it completes. Callee
  SemaFunc.return_pointer names the matching trailing ARG; array returns copy to it
  then ret void. The hidden pointer is trailing so noreturn-to-array-result function
  pointer coercions do not shift any original arguments, including stack arguments.
- Codegen emits void headers for array-returning internal functions and derives all
  physical parameters from pointer/scalar TIR_ARG instructions. TIR_CALL.arg_count is
  the lowered count, including the optional trailing destination, not the source arity.
  C scalar returns/arguments and pointer-to-array C interoperability are unchanged.
- Array-valued if/block/while exits copy into an owned shared result slot before a
  void BR; terminating exits do not initialize or read it. A single constant exit
  stays known. Result storage is ALLOC-hoisted and still counts against the checked
  aggregate budget. Captured values precede defer effects as in scalar lowering.
- Exported/runtime-referenced C declarations reject array params/results in func_queue
  and become FUNC_FAILED. Discarding a known extern address is not runtime use and
  does not force ABI validation. Private array transport is never silently used as a
  C convention or claimed to match another Zig compiler's object-file ABI.

## Mutable array globals and type-only length fields

- Internal array globals use existing VALUE_GLOBAL/SemaGlobal identities, lazy typed
  address publication and initializer/dependency queues. Runtime loads of whole arrays
  create owned aggregate snapshots; stores copy through ARRAY_COPY. Nested indexing
  borrows live global storage through index side effects, exactly like mutable locals.
- mutable_global_pointer strips element/qualification paths to inspect the root
  declaration's mutability. A `*const` alias into a mutable global still needs runtime
  loads; it cannot expose the initializer as a comptime value. Zero-bit/void OPV loads
  retain existing behavior, with actual bounds validation at indexing boundaries.
- Derived global element pointers may be materialized for loads/stores and writable
  initializer relocations; the same root-global guard rejects comptime mutation.
- IR_FIELD_REF uses name(field, expression-address), unlike FIELD_VAL's value operand.
  `.len` derives the expression type from the address's child and returns known usize
  for arrays/single array pointers without reading storage. Undefined dereference and
  index checks still run. General call/rvalue field bases keep normal evaluation.
- Writable aggregate .data/zero storage reuses checked recursive data emission, real
  alignment and the shared 16 MiB static array budget. Pointer tables are writable
  rather than RELRO; over-aligned data uses the assembly path. Unsupported global
  flags, leaves and volatile whole-array operations remain explicitly rejected.

## Many-pointer indexing and array result contexts

- IR_POINTER_MANY selects `[*]T`; size, qualifiers and alignment are interned in the
  existing TypePointer. Array-to-many coercion preserves actual pointer provenance
  through VALUE_PTR_CAST, not a reference to the array value's first copied element.
- REF_ARRAY_TYPE takes an address-of result type and anonymous literal element count.
  A single pointer uses its pointee type; a many pointer synthesizes `[count]child`.
  Explicit typed literals supply their own whole type before final pointer coercion.
- PTR_ELEM_PTR takes a many pointer and usize index and returns a single element
  pointer, with alignment weakened by offset/stride and allowzero preserved only when
  needed. Unlike ARRAY_ELEM_PTR it has no invented array-length safety check.
- Known many-pointer elements use cumulative physical bytes or logical comptime slots
  relative to real backing. Loads select the requested child within that root;
  nonzero-size known out-of-backing loads diagnose, while address formation is allowed.
  Mutable global roots use runtime loads/snapshots, even through const pointer views.
- Nested lvalues borrow storage; array-valued reads still own snapshots. Scalar,
  callback and pointer leaves retain existing physical ABI/relocations/volatile paths.
  Runtime slices are not scalar pointers and are not implemented by this milestone.

## Pointer arithmetic and differences

- PTR_ADD/PTR_SUB take a many pointer and usize element count; the result remains a
  many pointer with stride/offset-derived alignment and allowzero qualification.
  Codegen scales by child ABI bytes and adds/subtracts without a length check.
- ADD_SUB_RHS_TYPE takes the loaded lhs value of a compound assignment. It yields
  usize for many-pointer offsets, otherwise the original type, preserving integer
  cast result contexts without trying to coerce integer offsets into pointers.
- PTR_TO_INT is a pointer-address copy to usize. Pointer differences use two copies,
  SUB_WRAP and DIV_EXACT by the effective element size; single array-pointer children
  mean the array element, as in Zig 0.17. Known same-root subtraction folds relative
  offsets; negative results reject, distinct roots require runtime addresses.
- The reused DIV_EXACT debug guard diagnoses non-integral differences in szc.
  Upstream raw AIR div_exact does not guarantee a panic on that illegal behavior;
  only valid pointer differences are execution-compared with Zig.
- Arithmetic preserves real roots and cumulative offsets for static relocations and
  subsequent known loads. Zero-bit runtime children preserve the base pointer;
  comptime-only additions use logical slots, while known subtraction follows
  upstream ABI-byte (zero) subtraction. Unsupported layout/offset overflow rejects.

## Pointer-to-integer builtin

- IR_INT_FROM_PTR takes a pointer expression and produces usize. Single/many pointer
  types are accepted independently of const/volatile/allowzero/alignment qualifiers.
  Nonpointer and slice operands reject, following Type.isPtrAtRuntime's scalar rule.
- Undefined known pointers yield undefined usize. Other symbolic pointers require a
  runtime context and materialize through existing constant/root/function dependency
  paths, then PTR_TO_INT copies the actual address bits. No linker address is guessed
  at comptime. Runtime addresses of comptime-only backing remain explicitly NYI.
- The builtin does not introduce numeric pointers, integer-to-pointer conversion,
  vector/optional pointer handling or slice extraction. Those need separate semantics.
