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
Quoted identifier tokens are decoded through the shared string-literal escape
decoder before interning: bare, quoted, byte-escaped and Unicode-escaped names
identify the same binding. Empty/NUL-containing names are diagnosed. Declaration
name/source metadata is collected once and reused when lowering bodies.
Unquoted primitives retain priority over declarations with quoted primitive
names, for value, address and assignment paths; quoted tokens do not implicitly
name primitives.

Shadowing is an error, as in Zig. Matching declarations in two enclosing
namespace scopes make an unqualified reference ambiguous; qualified member
lookup checks only the selected namespace, not its parents.

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

## Fieldless struct namespaces

`this` has no payload and returns the current lexical namespace's nominal type.
Blocks and function aliases do not change that owner. Captured enclosing locals
are not supported: immutable captures report an explicit NYI, and mutable
cross-namespace references report Zig's accessibility error instead of leaking
foreign instruction refs into a declaration's operand map. `field_val` / `field_ref`
on a type value, and `field_ptr` on its address-oriented binding, resolve a
namespace declaration through the same lazy path as `decl_val` / `decl_ref`.
Mutable globals retain their original storage; constants retain declaration
address identity and alignment; function values retain their lexical namespace.
A pointer to a type binding can be loaded for namespace member lookup.

Root exports are reachable immediately. A nested namespace's exports become
reachable when its nominal type is resolved. Sema scans new namespace types
alongside the function/global worklists, after publishing the containing type
binding; unused namespaces and their non-exported declarations remain lazy.
There are no new runtime namespace or `this` instructions. Field-bearing struct
values, initialization, instance fields and layouts remain unsupported.

`in_comptime` has no payload and resolves to a comptime bool from the current
semantic block's comptime state. It never reaches runtime TIR. Irgen rejects
its use in syntactically known comptime scopes with the official redundant-use
error. Type expressions, array lengths and pointer/array type sentinels preserve
that lexical generation context; their existing semantic consumers still enforce
known values. A generation guard restores the caller context on every return.
Slice sentinel assertions are ordinary expressions, not type sentinels. TypeOf
query bodies deliberately reset the context even under a comptime parent.
This is not general comptime function-call or mutable-memory interpretation.

`typeof_builtin` carries `IrTypeOf { body, arg_count }` followed by
`arg_count` operand refs. Its body has ordinary (not forced-comptime) expression
analysis even when the query appears in a type/global constant initializer.
Sema uses an isolated operand map/instruction body and a transactional suffix
of the enclosing TIR builder so captured runtime refs remain valid. It rolls
back both instruction and extra-data lengths: the backend scans the whole
instruction table for argument/allocation slots, not just the executable body.
Non-local control targets are cloned with private exit lists, and discarded
aggregate slots do not consume the enclosing function's stack budget. Nested
queries inherit this isolation. Declaration/global/function worklists are NOT
rolled back; referenced invalid bodies/initializers still diagnose as in Zig.

A single query operand returns its exact type. Multiple fixed-width integer
peers use the official n-ary largest-signed/largest-unsigned strategy, including
its legacy known-value tie rule and original operand order (typed undefined
counts as known; undefined comptime_int cannot refine to a fixed integer).
Fixed-float peers preserve the widest float type and check runtime integer
precision without coercing/evaluating query values. Existing pointer/function
peer rules retain qualifiers/sentinels. There is no new typed-IR opcode or
runtime query evaluation. Optional-result peer synthesis reports explicit NYI;
this does not add optionals, tuples, generics, general comptime function calls
or floating-point computation/lowering.

`has_decl` / `has_field` / `field_type` / `offset_of` carry
`IrHasMember { container, name_body, type_src, name_src }`.
The name body has a contextual `[]const u8` result and is generated/analyzed in
an isolated comptime context, with an operand-map parent for outer constants.
Sema resolves the container type, then all logical name bytes, then validates
that the type supports the selected query. Declaration lookup tests only `pub` declarations in
the selected namespace: Zig 0.17 returns false for private declarations even
inside the same namespace. It never resolves a found member's initializer or
function body. Empty names and unmatched interior-NUL names miss; single-final-
NUL normalization is described below. Names are not identifier tokens and are
not truncated at the first NUL for namespace lookup. Constant strings,
byte arrays, subranges and concatenations use the existing pointer provenance.
Runtime backing, undefined descriptors/pointers and integer-address backing
cannot supply names, even for empty ranges. Name decoding charges the shared
constant-array work budget and has an explicit 16 MiB per-name ceiling.
No runtime TIR instruction or backend lowering is needed. General comptime
function calls/mutable-memory evaluation, struct values, other user-declared
container kinds and imports retain their existing unsupported diagnostics.

`field_named_ptr` / `field_named_val` carry
`IrNamedField { base, name_body, name_src }`. Irgen evaluates the base once as
an expression address, then generates the same isolated contextual comptime
name body used by `has_decl`. Sema projects the address through existing
namespace/array/slice rules; the value form then loads it. This preserves global
storage identity, constant alignment and slice descriptor const/volatile/allowzero
qualifiers without new runtime TIR or backend operations. Assignment, compound
assignment and address-taking use the pointer form. Undefined array values and
array pointers can expose their type-only length without reading memory, while
runtime base argument effects are retained.

Name resolution reads every logical byte, then consumes exactly one final NUL,
matching `Value.arrayToIpString`/`getOrPutTrailingString`. This is not first-NUL
truncation: two final NULs leave one, and an interior NUL is retained. Namespace
lookup compares all normalized name bytes. Built-in array `len` and slice
`len`/`ptr` instead mirror Zig 0.17's `NullTerminatedString.eqlSlice`: a literal
prefix followed by NUL also matches when more logical bytes follow. This
predicate is deliberately not used for namespace lookup. Missing-member
diagnostics preserve long names; presentation stops at an embedded NUL, as
verified against the official binary, without changing lookup semantics.
Builtin `anyopaque` has no namespace: `has_decl` returns false and field lookup
reports its opaque missing-member diagnostic. General opaque declarations are
still unsupported.

Discarding a function value does not make its body reachable. Taking or
coercing an internal, non-generic function's code address does, even if that
pointer is discarded; `ref`, `decl_ref`, `field_ptr`, `field_named_ptr` and `as`
queue such addresses. The temporary address used internally by
`field_named_val` does not force body analysis. Extern ABI liveness continues
to wait for runtime use; generic instantiation retains its existing rules.

`has_field` returns a comptime bool for supported instance-field metadata:
arrays have `len`, slices have `len` and `ptr`, and ordinary structs expose
exact declared field names, never namespace declarations. The root and other
fieldless structs have no instance fields.
This is distinct from accessing a declaration on a type with `field_named_*`.
No pointer auto-dereference applies to membership queries; thin/many pointers,
including pointers to arrays/slices, and other unsupported target kinds receive
the official `type '...' does not support '@hasField'` primary diagnostic.
The type resolves before the name, and the name resolves before target-kind
validation. Both membership builtins share contextual constant-name decoding,
its work/provenance limits and NUL rules; no runtime TIR or backend operation
is introduced. `field_type` requires a supported struct and returns the selected
field's type, or diagnoses a missing field. Both struct queries resolve the full
supported layout before lookup, even for an absent name.

## Ordinary struct field types and layout

Each nominal namespace retains stable `SemaField` records with the original IR
field body, resolved type, ABI size/alignment and checked byte offset. Deferred field bodies analyze
in that namespace using arena-owned snapshots of lexical operand maps; no stack
map or block escapes. Existing cross-namespace local capture rejection remains:
this ownership mechanism does not enable runtime captures or foreign TIR refs.

Layout has explicit unresolved/in-progress/resolved/failed states. Pointer and
slice fields require only their own representation, allowing recursive pointees;
by-value recursion (also through zero-length arrays) fails. Failure never
publishes a zero-size/align-one layout. The public read-only size API fails for
unresolved structs without changing its output; alignment is zero until resolved.
Sema consumers demand layout before deriving implicit pointer alignment, strides,
pointer differences or resized slice lengths. Equal-pointee/equal-annotation
qualifier coercions need no layout demand and avoid false recursive cycles.

Ordinary auto layout uses a separate declaration-index permutation, sorted by
exact pinned Zig PDQ descending effective ABI alignment in `struct_layout.c`.
Equal-alignment ordering is intentionally unstable, including above thresholds
24/50; stable sorting or an arbitrary alignment tiebreak changes field addresses.
Declaration order remains the identity of fields and constant-value positions.
Physical-order traversal checks aligned offsets, size addition and final tail
rounding; zero-byte fields still participate in the permutation and alignment.
The final struct size must fit UINT32_MAX, matching the official implementation;
this bound is enforced before size/alignment/order publication. Existing uint64
arithmetic overflow guards remain earlier recovery boundaries. The read-only
`sema_struct_layout` / `sema_struct_field_layout` APIs expose only resolved
layouts and preserve outputs on failure, including bounds errors. The order array
maps physical positions to declaration indices and is arena-owned. `offset_of`
resolves type, then name, then layout (also through zero-length arrays), then
checks struct kind and finds the field. It returns the stored byte offset as
comptime_int; missing fields do not bypass layout validation. No runtime TIR is
introduced. The sorter is a C specialization of the actual Zig std sources,
not a replacement std; MIT attribution is in `third_party/LICENSE-ZIG`.

Scalar integers (including wide layout-only types), bool, void, f32/f64, pointers,
slices, nested supported structs and arrays can be field types. Other field
forms/layouts report explicit NYI: tuples, comptime fields, defaults, explicit
field alignment, comptime-only/uninstantiable fields, error layouts and unsupported
float widths. Defaults are not silently ignored; demanding such a struct's
layout is unsupported, while unused types, `has_decl` and pointer-size queries
retain existing laziness. Declaration/field duplicates share normalized-name
validation, but fields are never lexical namespace bindings.

Physical struct values remain separate work: no struct initialization, instance
field projection, storage, runtime materialization, by-value function transport,
struct bitcasts or struct bit-size queries are added. Struct stores,
including undefined/volatile stores, and direct/indirect/variadic runtime
transport have explicit gates. The array/slice aggregate predicate and private
ABI are intentionally unchanged. Thin pointers and slice descriptors referring
to supported structs may use existing pointer operations without loading a struct.


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
- Source `for` evaluates range starts/ends once in source order as usize. Bounded
  inputs produce checked `sub end, start` lengths. `for_len` has an extra-data
  payload of count followed by bounded length refs; unbounded companions do not
  contribute lengths. Sema selects a known bound when available, rejects known
  mismatches, and emits void `TIR_FOR_LEN_CHECK` comparisons for runtime bounds.
  QBE guards length equality before iteration with the dedicated mismatch panic.
  An anonymous usize index allocation starts at zero. The loop compares it to the
  common length, binds immutable captures as checked start+index, and increments
  after the body-exit block before repeat. Sequence operands evaluate once as
  values: arrays snapshot, array pointers borrow and slices snapshot descriptors.
  Their `.len` refs enter the same for_len list; captures use array_index or
  array_index_ptr instead of start+index. Semantic-only for_ptr_check rejects
  pointer captures of array values. For reuses while's outer result block
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
  Panic argument expressions now receive a []const u8 result type, so differently
  sized branch values coerce to descriptors before joining. Runtime panic extracts
  both words; comptime messages read bytes through provenance-aware element loads.
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

## Type layout queries

`size_of` and `align_of` take a type ref and yield a comptime integer; there is no
TIR or runtime query. Size queries distinguish empty one-possible-value arrays
from nonempty comptime-only backing. Alignment queries use child alignment even
for empty arrays. On x86_64-linux integer storage rounds ceil(bits/8) up to the
integer alignment (power of two, capped at 16), not an unbounded power of two.
Pointer indexing/arithmetic and array layout share this exact byte stride.

`bit_size_of` queries representation bits rather than ABI bytes. Its arrays
multiply child representation bits (u257 -> 257 bits, ABI size 48 bytes). Child
representation eligibility is checked even for empty arrays: [0]type has byte
size zero but no bit size. A zero array dimension suppresses multiplication
overflow only after this eligibility check. Pointer representations remain 64 bits.

## Slice descriptors

`ptr_type` now carries IR_POINTER_SLICE independently of single/many pointer flags.
VALUE_SLICE stores interned pointer/usize components. Runtime descriptor values
are OPERAND_AGGREGATE snapshots with 16-byte backing: offset 0 is the many pointer,
offset 8 the usize length, ABI alignment 8. ARRAY_COPY is an overlap-safe aggregate
copy shared by arrays and slices. SLICE_LEN_PTR addresses the second word;
INDEX_CHECK guards index < length before many-pointer address calculation. Slice
values must never materialize as scalar pointer temporaries. Internal arguments
use descriptor pointers and callee snapshots; results use the trailing destination
parameter. Elements remain borrowed and mutable through mutable-element slices.

### Inferred slice joins and comparison boundaries

Peer resolution of different-sized unsentinelled array pointers can choose a slice
with compatible element representation and merged pointer qualifiers/alignment.
Existing aggregate join storage copies both descriptor words; it never copies
borrowed elements or materializes a slice as a scalar pointer. Common sentinel
slices remain NYI unless an explicit unsentinelled result type coerces the arms.
After peer resolution, slice equality is rejected before pointer-root comparison,
including when two array-pointer operands infer a slice as their common type.


### Borrowed slicing expressions

IR_SLICE contains an addressable base and usize start/end references plus bound
source locations. End may be NONE. Evaluate the operand address before both
bounds, but load a pointer/slice descriptor at SLICE analysis after bound effects,
matching Zig AstGen/Sema. Do not replace this with an early descriptor snapshot.

Known start/end produce a single pointer to an array of end-start elements;
runtime bounds produce a descriptor, even for repeated runtime `n..n`. Omitted
ends use logical array/slice length; an open many-pointer tail stays a many pointer.
Single-item pointers require compile-time bounds [0..0], [0..1] or [1..1].
SLICE_START_CHECK enforces unsigned start <= end before SLICE_END_CHECK enforces
end <= available length; the latter includes a physical array sentinel when present.
Known out-of-bounds ends are diagnosed before reversed known bounds. Runtime
panics use the minimal simple_panic messages rather than FullPanic's numeric detail.

Pointer offset/cast values retain the original root and cumulative byte offset.
Constant view dereference reconstructs only its own array extent; element reads
follow the underlying many-pointer projection without copying the entire view.
Byte views honor their offset and physical sentinel boundary. Runtime windows
borrow storage and reuse descriptor/aggregate ABI transport; no backing copy is made.
A zero sentinel is inherited only at the original logical end. Known-length
sentinel array views work; runtime sentinel descriptors and explicit sentinel
slicing deliberately reject until their type/checking support is implemented.


### Known slice dereference and address cancellation

IR_DEREF and IR_REF_DEREF accept a comptime-known descriptor. Runtime descriptors
reject with "index syntax required to access runtime-known slice", including a
runtime slice whose bounds happened to be equal. `&s.*` yields an array pointer
with the descriptor's known length and element pointer qualifiers/alignment;
it borrows the same storage, not a new immutable descriptor snapshot.

Direct `s.*` follows actual Zig 0.17.0 analyzeLoad/maybeDerefSliceAsArray rules:
- A supported one-possible-value element (void, u0, empty/nested zero-bit array)
  folds to that element value before conversion to an array. Dense construction
  remains subject to the existing element budget; it is never represented as undef.
- Otherwise known immutable backing produces an array value of the slice length.
- Undefined data pointers do not become valid by having zero length.
- Known descriptors of mutable global backing (even empty views) follow
  analyzeLoad's runtime fallback type: a load of the element, rather than an
  array of the slice length.
  To explicitly copy the whole array view, use `(&s.*).*`.

Two official backend failures are scoped separately: Zig 0.17.0 crashes (-11)
for `&undefined_zero_bit_slice.*`, so szc emits an undefined-dereference error;
the reference semantic matrix never treats that crash as an accepted case.
For nonempty mutable backing, its sema infers an element value but native emission
reports "genSetReg called with a value larger than dst_reg". The inferred type is
reference-checked without native emission; native reload behavior is separately
covered by tests/run_szc/slice_deref_mutable.zig, never claimed as a successful
reference execution. Known immutable/address/explicit-copy cases execute against Zig.


### Slice descriptor field lvalues

IR_FIELD_PTR projects an address from an addressable base. Direct slice storage
and a single pointer to a descriptor are supported; one implicit pointer load is
allowed, not arbitrary recursive autodereference. Array `.len` remains a type-only
constant and its address is immutable, never an invented field in array backing.

VALUE_SLICE_FIELD_PTR records its descriptor pointer, selected word and cumulative
root-relative byte offset. Hashing/interning, pointer equality, address conversion,
constant loads, function dependency walking and QBE/static-data emission retain
that path. Nested array projections therefore preserve field address identity and
PIE-safe relocations. Mutable-global roots stay runtime contents even when the
field's address is constant. Constant field reads select an interned descriptor
component without reading a copied array or fabricating a scalar pointer value.

Runtime projection reuses PTR_CAST for `.ptr` and SLICE_LEN_PTR (+8) for `.len`.
Field pointer attributes follow descriptor storage, independently of element
pointer attributes. Field alignment is min(storage alignment, field ABI alignment),
including word zero of over-aligned storage, as Type.fieldPtrType specifies.
Borrowed field stores reuse scalar STORE/STORE_VOLATILE and existing coercion/
const checks; no descriptor snapshot is used as an assignment destination.
Pointer-to-slice field reads now project/read a single word, allowing volatile
fields without allowing volatile whole-descriptor copies. Forming a field address
performs no external memory access; this is verified by the page-fault observer.

## Scalar-sentinel many-pointers (M5.4a)

IR_PTR_TYPE now optionally names a sentinel value and its source position. Sema
coerces it to the element type and requires a defined comptime integer or bool.
TypePointer.sentinel is an interned ValueId (VALUE_NONE means absent); type
hashing/equality/names include it. This does not change thin-pointer ABI/layout.
Implicit coercions can remove a sentinel but cannot invent/change one; writable
nested pointees require matching promises. Literal zero-sentinel backing can
supply the zero promise. Peer inference retains a promise only when compatible.
Pointer arithmetic/open tails preserve it, while element addresses and bounded
unsentinelled slicing clear it. Existing root/offset values and codegen need no
new representation. Sentinel slices, explicit sentinel slicing, and
non-integer/bool pointer sentinel values remain separate work.

## Scalar-sentinel arrays (M5.4b)

IR_ARRAY_TYPE_SENTINEL carries length, child, sentinel and sentinel source refs;
IR_ARRAY_TYPE remains the compact unsentinelled form. TypeArray.sentinel is a
ValueId, not a boolean or element count. VALUE_NONE means no sentinel. Type
identity includes the coerced value; names, layout, bounds, constant elements,
aggregate snapshots and codegen all use that same value. Integer/bool sentinels
may be nonzero or negative. u8 nonzero-sentinel arrays are not VALUE_BYTES:
that representation always implies zero-terminated backing.

Explicit `[n:s]T` and `[_:s]T` literals initialize logical elements followed by
the physical sentinel. IR_ARRAY_INIT_SENTINEL similarly finalizes anonymous
in-place initialization after evaluating/storing every ordinary element; this
preserves self-observation and initializer side-effect order. Copies/returns
include the extra physical element; `.len` excludes it. Concatenation coerces
sentinels to the peer child type, retaining a lone/equal sentinel and dropping
incompatible promises. Empty sentinel arrays still have their physical slot and are not treated as
empty one-possible-value arrays unless their child itself has one possible value.

In-memory array compatibility requires equal length and identical sentinels.
Pointer coercion may discard an array sentinel after that check only when the
containing slot is not writable. This permits direct `*[n:s]T -> *[n]T` and
immutable nested slots without making writable `**[n:s]T` slots covariant.
Different sentinel array-pointer arms can infer an unsentinelled array pointer
of the same extent, or an unsentinelled slice when their lengths differ.

Maximal zero-bit array types are representable. Zig 0.17.0 wraps their physical
usize length when checking indexing and explicit slice ends: `[max:0]u0`
indexing is rejected and only explicit end zero fits the wrapped bound. Omitted
slice ends use the logical length instead. Keep these upstream boundary rules
rather than treating the mathematical 2^64 physical elements as addressable.
Sentinel slice descriptors, explicit `[a..b:s]` checks and non-integer/bool
sentinel values remain separate milestones.


## Scalar-sentinel slices (M5.4c)

POINTER_SLICE carries TypePointer.sentinel as a ValueId while retaining the same
owned sixteen-byte pointer/length backing. The `.ptr` type is a many-pointer with
the descriptor's promise; descriptor word addresses retain storage qualifiers.
Joins preserve equal array/slice promises and drop incompatible ones. Slice
coercion cannot invent or change a promise, especially in writable nested slots.

IR_SLICE inherits the sentinel for open tails and known original logical ends.
A known result length produces a single pointer to `[len:s]T`; dynamic lengths
produce descriptors. Physical runtime bounds use wrapping usize `len + 1`;
known descriptors with dynamic-result paths instead use Zig's logical-bound
behavior. Sentinel indexing accepts `index <= len`; non-sentinel indexing uses
`index < len`. Zero-bit elements do not suppress runtime slice index guards.

An empty array may discard const/alignment only under Zig's physical-empty or
unsentinelled-empty destination rules. Known zero-bit array view loads fold the
requested view type, not the canonical cast root's extent. Explicit assertions
and non-integer/bool sentinel values remain separate work.


## Explicit sentinel assertions (M5.4d)

IrSlice carries optional sentinel/sentinel_src refs. IR_SLICE_ELEM_TYPE extracts
the borrowed base's element type without an actual descriptor word access, so
assertion expressions get a result type. An open many-pointer assertion checks
its existing promise, rather than scanning; a bounded assertion creates the
requested promise only after validating any known immutable end element.

TIR_SENTINEL_CHECK has actual/expected scalar refs and void type. For runtime
memory, start <= end and physical-end <= available guards precede the end load;
comparison failure calls the sentinel-mismatch panic. Known-length results retain
Zig's constant guard elision. Known mutable global backing does not supply a
comptime memory value; follow Zig's runtime-load bypass in that path. Never treat
an inability to read known-address memory as a known sentinel value.

Constant array-pointer views reconstruct the requested logical elements and
physical sentinel via borrowed many-pointer element loads; canonical offset-zero
casts need not have their containing root's extent. Runtime assertion volatile
loads are retained once by szc. Zig ReleaseFast removes safety reads entirely;
that distinction is explicit in the observer fixture, while Debug checks one.


## Integer-to-pointer conversion (M5.5b)

IR_PTR_FROM_INT is binary: contextual destination type and address expression.
Sema coerces the address to usize, rejects slice/non-pointer destinations and
validates defined constants against allowzero and the effective pointer alignment.
TIR_PTR_FROM_INT preserves a runtime usize value after ordered null/alignment
checks, then a QBE long-word copy. Panic messages match actual Zig.

VALUE_INT_PTR is a typed uint64 absolute address, not a relocation, immutable
object or fabricated backing allocation. Numeric casts and subobject addresses
canonicalize to the same form. PointerAddress.offset carries the numeric address
for this root kind, so comparison/difference use the common numeric address space.
Runtime loads, stores and aggregate snapshots use the numeric pointer directly;
comptime memory cannot be read from an address alone. Known numeric-backed
sentinel views therefore follow Zig's known runtime-memory bypass. Runtime-length
explicit assertions still read/check the end element after bounds guards.

Numeric addition wraps at usize width; subtraction must not precede address zero.
Known absolute `@intFromPtr` values fold, while symbolic roots retain relocations
and runtime address conversion. None of this implements general `@ptrCast` memory
reinterpretation or optional/vector/C-pointer types.


## Outer qualifier casts (M5.5c)

IR_CONST_CAST and IR_VOLATILE_CAST infer a pointer type from their unary operand;
result context applies an ordinary coercion afterward. The new type clears only
its own is_const/is_volatile bit, preserving nested qualifiers and promises.
Known thin values keep their address identity (byte/subobject casts preserve
backing wrappers, numeric/function/object pointers retain their value kind).
Runtime thin casts use TIR_PTR_CAST. Slices retag the borrowed element pointer
and rebuild the same pointer/length pair with owned descriptor semantics; neither
cast reads pointee memory or recursively alters pointee element types.


### Contiguous qualifier-cast chains

Irgen rejects repeated `@constCast` or `@volatileCast` flags in a contiguous
pointer-cast chain, matching Zig 0.17.0 AstGen. Redundant parentheses remain
inside the same chain. An intervening `@as` expression or a named intermediate
value starts a distinct chain and may legitimately restore a qualifier before
another explicit cast removes it. Distinct qualifier flags remain independent;
the existing typed pointer casts and owned slice descriptors are unchanged.


## Alignment casts (M5.5d)

IR_ALIGN_CAST uses IrPointerCast: destination type, operand, IrPointerCastFlags
(ALIGN/CONST/VOLATILE) and operand source offset. Irgen folds adjacent casts and
parentheses into one contextual operation, diagnosing repeated flags. Ordinary
qualifier-only chains retain their inferred-type operations.

Sema validates unchanged child memory representation, shape (or array-pointer to
slice), sentinel, allowzero and explicit qualifier permissions, then retags the
same borrowed address. Slice retagging rebuilds an owned descriptor with unchanged
length; it does not access pointee memory. Constant numeric addresses are checked
against the target alignment. Known symbolic roots remain relocations and do not
silently strengthen allocation alignment. Allowzero undefined remains undefined.

TIR_ALIGN_CHECK is binary: pointer plus boolean empty-slice exemption. Its type
carries the required pointer alignment. Codegen guards masked low address bits
unless exempt, using PANIC_ALIGNMENT. Only runtime slice-to-slice casts derive the
exemption from a zero length; thin and array-pointer-to-slice casts never do.
Known numeric empty slices are still checked during sema, matching Zig.
General pointer reinterpretation and changed child types are not implemented here.


## Scalar bit casts (M5.5e)

IR_BIT_CAST is binary: destination type and an operand evaluated without numeric
result coercion. Equal representation widths are required for integer/bool/void;
comptime_int, pointer and untyped undefined operands are not implicit bit sources.
Sema wraps known integer bits to destination signedness using BigInt, handles the
single bool bit and zero-bit void/u0, and preserves typed undefined values.

TIR_BIT_CAST is unary and uses scalar copies with destination canonicalization,
never numeric range checks. Signed i1 true is -1; bool/u1 true is 1. Native widths
1..64 work at runtime, while wider constants use arbitrary-precision values.
Aggregates/floats/vectors/enums and physical memory casts are not implied by this
operation; logical representation width is not an ABI-stride serialization rule.


## Constant aggregate bit casts (M5.5f)

IR_BIT_CAST still carries destination type and uncoerced source. Sema computes
logical widths recursively through integer/bool/void arrays, including scalar
sentinels, and uses comptime.c PackedBits buffers to serialize low element first.
This is not ABI-memory serialization: e.g. [2]u5 has ten packed bits, not two
storage bytes. Constant destination sentinels come from the declared type.

All-undefined aggregates canonicalize only when every stored element, including
any sentinel, is undefined. Whole undefined is preserved; partial undefined is
zero in the packed writer. Zero-bit results have their unique defined value.
Packed buffer capacity and dense array construction are separately budgeted.
Runtime aggregate bit casts still have an explicit limitation at this milestone.


## Runtime aggregate bit casts (M5.5g)

For aggregate casts with at most 64 logical bits, sema uses a native unsigned word
as an intermediate representation. Typed ARRAY_ELEM_PTR/LOAD operations read ABI
storage, scalar BIT_CAST/INT_FROM_BOOL/INT_CAST normalize and widen fields, and
SHL/BIT_OR pack them. SHR/TRUNCATE/BIT_CAST normalize destination scalars before
STORE or nested ARRAY_COPY into owned slots. The aggregate value remains an owned
pointer-backed operand, never a scalar QBE temporary. Zero-bit results are known
unique values. Wider runtime aggregates retain a deliberate diagnostic.

Source sentinel fields participate in packing; runtime destination storage holds
the extracted encoded bits. Tests use valid sentinel representations. Unlike
comptime reconstruction, this path does not overwrite the encoded runtime tail
with the type's sentinel. Neither path is a physical pointer-memory cast. The reference generic AIR
scalarizer currently omits sentinel elements from its loop bounds; a bool-sentinel
native regression is explicitly scoped under tests/run_szc rather than copying
that upstream defect. Numeric-sentinel native comparisons remain enabled.


## Scalar memory copies (M9.4g)

IR_MEMCPY is binary: destination and source indexable pointers, evaluated once.
Neither operand is contextually coerced into an element value. Sema derives
logical lengths from slices/array pointers, and can infer a many-pointer extent
from the other operand. Element types require in-memory compatibility. A separate
TIR_COPY_LEN_CHECK checks runtime length equality, including zero-bit copies.

TIR_MEMCPY uses TirMemcpy { destination, source, length }. Both data operands are
thin pointers retaining independent qualifiers/alignment. The backend checks
non-overlap using byte extents (logical length times ABI stride), then copies
scalar storage in a bounded loop. Volatile reads/writes use outlined helpers;
unused high integer storage bits are not numerically normalized by the copy.
Top-level sentinels are outside the logical extent. Internal TIR_ARRAY_COPY still
means overlap-safe whole-object copying; it is not the source @memcpy operation.


## Nonvolatile aggregate memory copies (M9.4h)

The same TIR_MEMCPY payload can address supported aggregate elements. Length and
alias validation are unchanged; the backend uses ABI byte extents and calls libc
memcpy only for nonempty copies. Nested arrays include their own sentinel storage
in the stride. Slice descriptors copy two words and retain their borrowed backing.
No aggregate is put into a scalar QBE temporary, and no deep copy is implied.
Nonempty volatile aggregate copies remain explicit NYI rather than using libc.

Zero-bit copy elision requires both a proven one-possible-value element type and
a runtime (not comptime-only) layout. A zero returned by the partial ABI layout
helper is not proof: unsupported types and nested comptime-only children must not
silently become no-ops. The recursive layout predicate intentionally exempts empty
arrays, whose elements need no runtime representation.


## Nonvolatile aggregate memory fills (M9.4i)

IR_MEMSET still evaluates destination and its contextually typed fill once.
TIR_MEMSET uses TirMemset { pointer, length, value }: scalar elements carry a
scalar value, while supported array/slice elements carry a pointer to their
owned runtime snapshot or immutable constant backing. The fill pointer never
borrows mutable destination storage. Slice descriptors retain borrowed backing
pointers; filling descriptors is not cloning their elements.

The backend loops over logical elements at ABI strides and calls libc memcpy
from that one fill snapshot in each iteration. A zero iteration count skips all
calls, including runtime empty allowzero ranges. Nested scalar sentinels are
part of each element's ABI storage. Nonempty volatile aggregates and unsupported
runtime layouts remain explicit errors. Const/unknown-length checks and fill
coercion precede empty/one-value elision. Known zero lengths do not require defined
destination data; nonzero-bit nonempty fills do. Nonvolatile undefined fills may
leave prior memory representations; no Debug poisoning behavior is promised.


## Empty runtime array storage (M5.2c5)

The shared sema_type_array_storage_allowed predicate tracks unsentinelled empty
array dimensions before inspecting the leaf. Such a dimension removes all leaf
values, including nested comptime-only, wide integer and f32/f64 leaves, but
retains their known ABI alignment. Locals, globals, snapshots, internal aggregate
calls/returns and constant backing use the same storage decision. Codegen still
uses the existing minimal address anchor for zero-byte objects; no unsupported
leaf value is emitted or lowered.

A zero reported by the partial size helper is not enough. Unresolved layouts
(such as anyerror), occupied wide/float/comptime-only leaves and f16/f80/f128
storage remain rejected. A zero-length array with a sentinel is not empty for
this proof. Logical slice lengths remain meaningful: zero-byte memcpy still
checks their equality. Empty array indexing rejects before inspecting a defined
index or undefined pointer, using the reference's distinct value/pointer messages.

## Immutable named struct initialization and logical reads

`STRUCT_INIT` stores `IrStructInit { type, field_count, flags }`, followed by
source-ordered `IrStructInitField { name, src, body }` records. Each body starts
with `STRUCT_FIELD_TYPE`, evaluates the contextual expression, and breaks with
`IR_BODY_RESULT`. `IR_STRUCT_INIT_TYPED` means type/expression analysis occurs in
source order and final field coercions happen after all bodies. Without it,
virtual result-location initialization coerces each supplied field sequentially.
This is not an all-name prepass: eager expression failures beat later unknown
names, while bare literal final coercions may not. Missing fields are checked last.
Zero-field syntax is shared by arrays/structs; sema dispatches the resolved type.
`INIT_EMPTY_PTR` preserves array destination/qualifier/sentinel validation and
returns the loaded initialized aggregate. Runtime struct destinations use the
separate sequential `STRUCT_INIT_PTR` protocol described below.

`FIELD_READ_REF` and `FIELD_NAMED_READ_REF` are internal read-reference operations.
Known struct field values receive logical immutable references; namespace/array/
slice fields retain existing borrowed pointers. Recursive static/dynamic/indexed
read chains evaluate each base/index once. True lvalues still use `FIELD_PTR` or
`FIELD_NAMED_PTR`; the read-reference protocol must not bypass field-address validation or
silently snapshot volatile descriptor storage.

Known structs use a distinct nominal `VALUE_STRUCT`, with arena-owned IDs in
**declaration order**, never physical-layout order or array-value aliasing. Runtime
local storage uses owned snapshots; struct-by-value ABI remains gated. Runtime
emission/global publication checks
all immutable backing dependencies so pointer casts or nested relocations cannot
hide unsupported struct storage. Pure logical references are not runtime escapes.

## Ordinary struct bit-layout queries

`bit_offset_of` uses the same `IrHasMember` payload and deferred comptime name
body as `offset_of`. Sema resolves type, name, complete layout, struct kind and
field in that order. For the supported ordinary structs the result is the checked
byte offset multiplied by eight in uint64, returned as comptime_int; no new TIR
operation or runtime transport is introduced. Zero-byte fields retain their actual
physical offset. Packed/extern/comptime-field offset extensions remain unsupported.

Ordinary structs have no bit representation. `bit_size_of` rejects the original
requested struct or array type before resolving its field layout, even for `[0]S`.
It is not an alias for ABI byte size times eight. Scalar/array logical bit counts,
sentinels and pointer behavior retain their existing rules.

## Completed immutable struct backing and typed field addresses

`VALUE_STRUCT_FIELD_PTR` records `StructFieldPointer { base, index, offset }`:
index is declaration-indexed, offset is cumulative physical bytes from the root.
Typed field provenance is retained through casts and array-element composition;
it is not interchangeable with the same physical root plus a byte offset. Known
comparison uses canonical enclosing field paths and field-relative displacement,
so distinct overlapping zero-byte fields compare unequal at comptime. Runtime
numeric addresses may coincide. Same-field subtraction folds; different field
origins lower to runtime even when their physical root is shared. Subtraction
before a field's start rejects at pointer formation, not only on dereference.
The provenance walker charges all flattening/prefix work to a 4096-step limit.

`FIELD_PTR`/`FIELD_NAMED_PTR` may now form addresses rooted in completed immutable
compiler-owned objects. Qualifiers are preserved; ordinary field alignment is
min(base alignment, field natural alignment), never offset-strengthened. Loads
follow the typed declaration-index path, preserving typed undefined and array
field bounds. Exact typed array-field decay retains the field origin. Whole
volatile struct loads remain unsupported, but rooted scalar field and borrowed
slice-descriptor field accesses emit real volatile operations.

`sema_type_static_storage_allowed` is read-only and distinct from runtime array
storage/aggregate ABI admission. It requires resolved ordinary struct fields,
bounded recursive type work and supported occupied leaves, while preserving the
legacy empty-array whitelist. Actual by-value struct coercions establish layout
before same-type/undefined shortcuts; pointer-type construction remains lazy.
Runtime-backing graph validation follows field paths and stored struct values
without effectful resolution while holding queue/state buffers. It retains its
cumulative 1048576 node/edge limit and explicit failure diagnostic.

Codegen closes immutable dependencies iteratively, including REF contents rather
than relying on object creation order. Static serialization visits declaration
values in physical field order, checks types/extents, writes zero inter-field and
tail padding, emits nested arrays/sentinels/slices and rooted relocations, and
provides a one-byte anchor for zero-sized objects. Physical undefined-zero policy
does not define logical comptime values. Aggregate objects/total storage retain
the 16777216-byte limit; static traversal/serialization is bounded by 4194304
node/edge steps and depth512. Relocation-bearing immutable objects use RELRO;
alignment above64 uses outlined assembly with ELF OBJECT size metadata.

Mutable struct globals and struct-bearing by-value ABI remain closed. Runtime
local storage and dynamic/absolute typed field addresses are described below.
Known physical reinterpret loads, early struct-address cycles, and packed/extern/
default/comptime/tuple/aligned-field extensions remain closed.


## Runtime local struct storage and actual result locations

`STRUCT_INIT_PTR` has `IrStructInitPtr { pointer, field_count }` followed by the
same source-ordered field records as `STRUCT_INIT`. Each deferred body derives
`FIELD_PTR` from the actual destination and evaluates its RHS with that field as
the result location. Sema stores the completed field before analyzing the next
body. Missing fields are checked last; a noreturn body stops all subsequent
stores and missing-field validation. `INIT_EMPTY_PTR` dispatches empty structs
and arrays without changing array sentinel/qualifier behavior.

This is distinct from typed `STRUCT_INIT`: already-valued RHS operands own their
snapshots before later effects, and the finished value is copied afterward.
For `s={a=1,b=2}`, contextual `s=.{.a=s.b,.b=s.a}` yields22; explicit `S{...}`,
`@as(S,.{...})`, and temporary-then-assignment yield21. A contextual earlier
field write remains observable if a later field returns from the function.

Runtime structs are `OPERAND_AGGREGATE`, not scalar QBE values. Owned slots,
loads/snapshots, nested arrays/slices, copies and branch/block merges use the
same checked aggregate storage helpers. `TIR_ARRAY_COPY` retains its existing
opcode and overlap-safe memmove lowering; it is an owned aggregate copy, not an
array-only representation. Runtime local admission uses
`sema_type_runtime_storage_allowed`, independently of immutable static admission,
mutable-global admission, builtin element admission and by-value ABI admission.
The 16777216-byte stack cap, alignment, zero-size anchors and TypeOf isolation
remain in force. Whole volatile aggregate operations are still rejected.

`TIR_STRUCT_FIELD_PTR` stores `TirStructFieldPtr { base, offset }` and returns a
typed pointer: copy the base for offset0, otherwise add the checked byte offset.
The result preserves const/volatile/allowzero and min(base,natural) ordinary
field alignment. Nested/indexed read-reference chains borrow actual storage so
scalar volatile fields do not require a whole aggregate read. Aggregate-valued
reads still snapshot at their evaluation point.

Known absolute roots retain typed `VALUE_STRUCT_FIELD_PTR` provenance too.
Payload offsets are root-relative; numeric flattening adds the root address
exactly once, including composed array/slice/field paths. Numeric conversion may
fold, while logical comparison preserves the nominal aggregate field base and
index—even at an offset-zero or overlapping physical address. QBE/static data
emission handles numeric roots without inventing symbolic relocations.

Recursive struct-bearing array signatures, direct/indirect/internal/C aggregate
transport, mutable struct globals, and occupied struct-element memory builtins
remain explicitly gated. Pointer and slice descriptors do not transport their
pointee structs by value and retain their existing ABI.


## Internal struct-bearing value transport (M6.1h)

Ordinary supported structs and recursively struct-bearing arrays may be fixed
parameters/results of internal Zig-convention functions. This reuses the private
aggregate ABI: each argument is a backing pointer, incoming pointers promise only
byte alignment, and callee entry takes a naturally aligned private snapshot.
The caller allocates a fresh trailing result pointer for aggregate returns; the
callee copies into it and returns void. Direct and typed indirect calls use the
same transport. No struct is classified as a scalar QBE value.

Argument expression evaluation owns aggregate snapshots before subsequent
argument effects. Return expression evaluation owns its snapshot before deferred
mutations. Result storage never aliases an assignment's actual destination;
contextual literal initialization retains its separate sequential RLS semantics.

Live function signatures resolve/check struct-bearing storage in the defining
namespace/IR, including address/relocation-driven liveness. Indirect calls also
validate their signature in the caller context. Pointer/slice descriptor types
remain lazy with respect to pointee layout. Layout work never runs inside cached
backing graph walks. Unreferenced declarations remain lazy.

C/SysV aggregate transport remains explicitly closed, including typed indirect
calls without a FuncId. Known C declarations retain declaration-site diagnostics;
indirect signatures reject at the call. Variadic aggregate arguments, mutable
struct globals, whole volatile aggregate copies, unsupported field layouts and
occupied struct-element memory builtins remain gated independently.
