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
- [x] **M3.6 Arithmetic semantics**: wrapping/saturating ops, division, shifts, safety checks → panic stub.
  - [x] **M3.6a Checked arithmetic and panic stub**: overflow, cast, unsigned division and shift guards.
  - [x] **M3.6b Runtime saturation and explicit division builtins**.

## Phase 4: Control flow

- [x] **M4.1 if/else, comparisons, `and`/`or` short-circuit.**
- [x] **M4.2 while/break/continue, continue expressions, labeled loops.**
- [x] **M4.3 Blocks as expressions, labeled break with value.**
- [x] **M4.4 for over ranges, `defer`.**
  - [x] **M4.4a Runtime range-based for loops**: captures, multiple inputs, length safety, labels and else/values.
  - [x] **M4.4b defer**: scope-exit execution across fallthrough and control transfers.
- [x] **M4.5 unreachable, noreturn, `@panic` minimal runtime.**
  - [x] **M4.5a Safe unreachable and noreturn checks**: safety panic, return/cast diagnostics, terminating calls/joins.
  - [x] **M4.5b Explicit @panic**: first-class literal values and minimal fixed-length message runtime.

## Phase 5: Memory

- [~] **M5.1 Single pointers, `&`, `.*`, `const` pointers.**
  - [x] **M5.1a Runtime single pointers**: mutable-local addresses, dereference reads/writes, nested const coercions.
  - [x] **M5.1b Addressable constants/parameters/temporaries and pointer identity**: definition-anchored storage, immutable objects and equality.
  - [~] **M5.1c Remaining pointer integration**: mutable globals, function addresses/calls and pointer attributes.
    - [x] **M5.1c1 Internal mutable scalar globals**: lazy type/initializer resolution and writable pointer relocations.
    - [~] **M5.1c2 Function addresses/calls and pointer attributes**.
      - [x] **M5.1c2a Function types, addresses and indirect calls**.
      - [~] **M5.1c2b Pointer attributes**.
        - [x] **M5.1c2b1 Allowzero pointer qualification**.
        - [~] **M5.1c2b2 Pointer alignment**.
          - [x] **M5.1c2b2a Alignment-qualified pointer types and unaligned memory**.
          - [~] **M5.1c2b2b Explicit declaration/storage alignment**.
            - [x] **M5.1c2b2b1 Aligned mutable locals**.
            - [x] **M5.1c2b2b2 Aligned local constants** (known values and runtime initializers).
            - [x] **M5.1c2b2b3a Internal mutable global alignment**.
            - [x] **M5.1c2b2b3b Aligned global constants** (internal, supported runtime-representable types).
            - [x] **M5.1c2b2b4 Aligned function declarations/addresses** (local explicitly aligned bare-function aliases remain NYI).
        - [x] **M5.1c2b3 Volatile pointer accesses** (supported scalar/bool/single-pointer values).
          - [x] **M5.1c2b3a Volatile pointer qualification/coercion**.
          - [x] **M5.1c2b3b Observable volatile loads/stores**.
- [ ] **M5.2 Arrays, indexing, array literals, `++` at comptime** (0.17.0 has no `**`).
- [ ] **M5.3 Slices, many-pointers, slicing, `.len`, `.ptr`, for over slices.**
- [~] **M5.4 String literals, sentinels, `[*:0]const u8`.** Literal byte values landed in M4.5b; general string/memory operations and sentinel pointer syntax remain pending.
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

### CI follow-up: differential compiler process lifecycle
- **Evidence**: 72914b5 push CI passed; scheduled run 38675456100 later passed
  GCC/Clang/fuzz and all reference safety cases, then timed out after 30 seconds
  in the nested-pointer reference comparison. Its old subprocess.run cleanup only
  killed an immediate child, and no case/source context was retained. No compiler
  acceptance disagreement was reported; hosted-runner stalls are not fully diagnosed.
- **What**: extracted the already-tested group-cleanup runner into
  tools/compiler_process.py, shared by safety and all ten semantic matrices. Added
  per-actor/case progress and source/command/status/output failure reports; a timeout
  still fails immediately after whole-group cleanup, without retries or skipping.
  Reference matrix compilations use -j2, preserving every existing timeout budget
  (30s nested-pointer, 120s other compilation matrices). Parser comparisons unchanged.
- **Tests**: 17 compiler-wrapper/adapter regressions, including real matrix CLI
  crashes outside the repo cwd and mocked wiring checks for all ten adapters,
  plus the existing eight shared process-cleanup tests.
  e2e-test and safety-test run both. Compiler sources/corpora are unchanged.
  All ten actual matrices pass 4,428 Zig comparisons. GCC/Clang -Werror sanitizer
  suites pass 558 units, 726 goldens, executable/driver/safety checks; GCC's full
  e2e is reference-enabled. Four 60-second fuzz targets and 20,000 standalone
  mutations per stage pass. Exact coverage is in HANDOFF.md; check new push CI
  separately rather than claiming hosted stalls have been solved.
- **Language follow-up**: actual Zig 0.17 semantic-only compilation accepts an
  aligned local bare-function alias, but emitted compilation SIGSEGVs; a function-
  declaration-aligned control succeeds. Keep the explicit alias-address NYI rather
  than fabricating backend guarantees. No language checkbox advanced here; next
  scoped language work is typed immutable address dependencies. Q6 stays at M10.2.

### M5.1c2b3b Observable volatile loads/stores
- **What**: TIR_LOAD_VOLATILE/TIR_STORE_VOLATILE distinguish runtime observable
  accesses from ordinary memory. Sema keeps volatile reads even when discarded,
  carries the flag through compound assignments/joins/calls, and preserves the
  usual const-store, undefined-pointer and comptime diagnostics. Known immutable
  values and one-possible-value void/u0 still fold as in Zig's analyzeLoad/storePtr2.
- **Backend**: QBE has no volatile load/store instruction. Each access calls a
  private outlined QBE helper containing exactly one width-correct load or store;
  QBE calls are opaque side effects and cannot be removed, merged or reordered as
  ordinary loads. No interprocedural inlining/LTO is introduced. Load results are
  normalized for signedness/subword integer widths; x86_64 handles weak alignment.
  Standalone emit-qbe includes helper definitions and needs no assembly companion.
  These calls conservatively inhibit optimization; they are not atomic operations
  or a promise of cross-thread memory ordering.
- **Physical tests**: a Linux/x86_64 mprotect/single-step observer counts actual
  memory faults and records access order. Forty-four sequences cover 17 scalar/
  bool/pointer/callback-slot types at aligned and offset-one addresses, discarded
  reads, consecutive stores, loops, compound assignment, &p.* cancellation and
  void/u0 no-access behavior. The same oracle passes with Zig Debug/ReleaseFast.
  Compound assignment accepts either separate read/write instructions or LLVM's
  single read-modify-write instruction (reported as a write fault). Undefined
  stores have no cross-compiler access-count assertion: ReleaseFast can omit them;
  szc currently emits an arbitrary zero store. No undefined contents are read.
- **Regression coverage**: IR/TIR/QBE goldens, five reference-rejected diagnostics,
  an executable with globals, defer/loop/compound effects, indirect callback slots,
  known volatile comptime pointers and OPVs. Removed temporary access-NYI goldens;
  their fuzz inputs now exercise successful accesses. CI e2e includes the observer.
  Corpus grows to irgen 33/sema 79. GCC/Clang -Werror ASan/UBSan pass 558 units,
  726 goldens, complete native executable/memory/166 safety suites; GCC's full e2e
  is reference-enabled. Optimized GCC also passes goldens/executables/observer,
  final Clang retains all 317 new function/qualifier reference comparisons, and
  four 60-second fuzz targets plus 20,000 standalone mutations per stage pass.
  Exact totals and hosted CI status are recorded in HANDOFF.md.
- **Limits/next**: only existing supported value layouts; no atomics, fences,
  memory-mapped-device-specific access protocol, aggregate/vector/wide-int/float
  expansion or non-single pointers. The three-checkpoint batch is complete; return
  to small milestones. Function-alias alignment and constant address cycles remain
  explicit gaps in enclosing M5.1 alignment integration. Q6 stays at M10.2.

### M5.1c2b3a Volatile pointer qualification/coercion
- **What**: single-pointer syntax lowers IR_POINTER_VOLATILE. Interning already
  includes TypePointer.is_volatile; sema now populates and prints it. Coercion can
  add but not discard volatile, while mutable outer pointees require bidirectional
  compatibility. Joins combine const/allowzero/volatile and retain the weakest
  alignment only when both operands coerce. Function parameter variance uses the
  same memory-compatibility checks. Pointer identity and storage layout do not change.
- **Grounding/tests**: Sema.zig coerceInMemoryAllowedPtrs and peer resolution
  confirm the qualifier rules. A 240-case Zig matrix covers direct qualifiers,
  alignment interactions, nested mutable invariance, joins and function variance.
  Added three stage goldens, seven reference-rejected diagnostics, executable
  identity/slot/address-cancellation coverage and two temporary access-NYI fixtures.
  Removed the old blanket volatile-pointer rejection. CI runs the new matrix.
  GCC -Werror ASan/UBSan passes 558 units, 720 goldens and full executable/memory/
  safety suites. Clang sanitizer checks and all 240 Zig comparisons pass; the new
  executable passes with Zig too. Four 60-second fuzz targets and 20,000 standalone
  mutations per stage pass. Exact test counts are in HANDOFF.md.
- **Safe boundary**: this checkpoint supports qualification only. Volatile loads
  and stores fail explicitly rather than silently becoming ordinary QBE memory
  operations. The next checkpoint removes these temporary guards and supplies
  observable access lowering. Atomicity/fences, casts, non-single pointers and
  unsupported pointee types remain separate work. Q6 is still deferred to M10.2.

### M5.1c2b2b4 Function declaration/address alignment
- **What**: function declarations have isolated alignment bodies/source locations.
  SemaFunc stores validated code alignment independently of the interned function
  signature; taking/decaying the address preserves it in the pointer type. Alias
  values retain their function identity. Pointer weakening and joins reuse the
  existing memory-coercion rules. Function *types* cannot carry an alignment,
  matching std.zig.AstGen; Sema's u29/nonzero/power-of-two checks are shared.
- **Backend**: QBE functions are already 16-byte aligned. Larger guarantees put
  each function at offset zero of a private executable .text section; the assembly
  companion establishes its ELF section alignment before the QBE body. The driver
  concatenates complete assembly streams without parsing or patching QBE output.
  This avoids padded wrappers, changed function identity and huge trailing padding.
  Standalone emit-qbe explicitly rejects code alignment above 16; build-exe/run work.
  Exported definitions are emitted aligned; extern declarations trust their supplier.
- **Tests**: 27 physical internal/export/extern address checks through 4096 bytes,
  indirect calls via immutable pointer slots, ELF executable-section/no-TEXTREL and
  bounded-size assertions, 77 reference address/alias/join comparisons, three stage
  goldens, twelve reference-rejected diagnostics and executable regression. CI runs
  the physical and differential tests; corpus grows to irgen 31/sema 70 inputs.
  GCC/Clang -Werror sanitizer checks pass; GCC's full reference-enabled suite passes
  558 units, 709 goldens and all executable/memory/166 safety cases. Four 60-second
  fuzz targets and 20,000 standalone mutations per stage pass. See HANDOFF.md for
  exact Clang coverage; hosted CI is not implied by local success.
- **Limits/next**: explicit alignment on a local bare-function alias remains an
  honest NYI rather than a metadata-only promise; inline addresses/generic calls,
  explicit callconv/addrspace/linksection and other existing restrictions remain.
  Constant type-only address cycles remain unsupported. Enclosing memory/alignment
  milestones stay partial. This turn batches volatile qualification then volatile
  loads/stores next; Q6 remains deferred to M10.2.

### M5.1c2b2b3b Aligned internal global constants
- **What**: aligned global constants retain their original comptime Operand and
  unique immutable object identity. IrDecl has a separate type body for explicitly
  typed aligned constants and an isolated alignment body; SemaDecl stores validated
  byte alignment separately. Declaration references pass it to analyze_ref, rather
  than manufacturing runtime variables or discarding known values.
- **Reference semantics**: explicit type resolves before alignment/initializer;
  inferred type follows initializer resolution before alignment. Zig 0.17
  Zcu/PerThread.zig and Sema.zig source/probes confirm this ordering and the global
  rejections for comptime-only types and bare function aliases. Ordinary copied
  globals/locals use their own natural storage alignment. Unused declarations stay
  lazy; invalid attributes and invalid initializers keep diagnostic ordering.
- **Storage/interactions**: existing aligned immutable objects emit QBE data up to
  64-byte alignment, or outlined .rodata/.data.rel.ro assembly above that. Real
  addresses, nested pointers, mutable pointees, callback slots, void/u0 and
  zero/undefined storage work without changing backend representation. Standalone
  emit-qbe retains its above-64 static-data limitation; build-exe/run supply assembly.
- **Tests**: 153 new physical address/readback checks with PIE/RELRO assertions,
  332 comparisons against Zig, IR/TIR/QBE goldens, and 21 diagnostic fixtures.
  Twenty diagnostics are reference-rejected; the typed constant self-address cycle
  records an existing szc-only limitation which the reference accepts. Sanitized
  replay matches all 332 new cases plus 300 mutable-global and 626 local-constant
  cases. Full validation totals are recorded in HANDOFF.md; CI runs both the new
  physical tests and reference matrix. GCC/Clang -Werror ASan/UBSan pass 558 unit
  checks, 694 goldens, all executable/memory suites and 166 safety cases. GCC also
  passes the full reference-enabled suite. Four 60-second fuzz targets and 20,000
  standalone mutations per stage pass. Corpus grows to irgen 30/sema 65 inputs.
- **Limitations/next**: internal constants of supported runtime-representable types
  only; exported/extern/threadlocal aligned constants remain explicit NYI. Typed
  constant address-dependency cycles remain unsupported, not silently resolved with
  placeholder values. Function declaration/address alignment is next; existing
  aggregate/float/backend restrictions remain. All enclosing alignment and memory
  milestones stay partial. Q6 remains deferred until M10.2.

### CI follow-up: procfs reaping race in cleanup regression
- da77561 CI exposed ESRCH while reading a just-reaped descendant's procfs file.
  The probe now recognizes both ENOENT and ESRCH as disappearance, without hiding
  live processes or changing production timeout handling. Deterministic tests cover
  both errors and zombie/sleeping states; all eight cleanup checks pass locally.
  No language milestone was advanced. Check the new push before claiming CI green.

### CI follow-up: safety subprocess-group timeout cleanup
- **What**: safety commands now start in a new session. On a compilation/execution
  deadline, the harness kills the whole process group, drains captured output and
  raises TimeoutExpired with the command, original budget and captured bytes.
  Successful/nonzero exits and signal statuses retain subprocess.run semantics.
- **Why**: fc00389/run 38611434252 passed GCC/Clang/fuzz, but differential safety
  completed 159/166 cases with seven timeouts: four native compilations, two
  reference compilations and one reference execution. The -j2 reference cap alone
  did not prevent stalls. The old subprocess.run timeout path only killed its
  immediate child; inherited pipes from descendants could keep cleanup waiting.
  This fixes that reproducible harness defect, not a claim that all hosted-runner
  stalls have been diagnosed or that remote CI has recovered.
- **Regression coverage**: tests/run_process_timeout.py checks exact stdout/stderr
  and nonzero status, signal status, a waiting parent with a long-lived child,
  and an already-exited parent whose child still owns the output pipes. Timeout
  cases must return promptly with the expected partial output and no live child.
  e2e-test and safety-test both run these four checks before compiler safety cases.
- **Validation**: GCC/Clang -Werror ASan/UBSan pass 558 unit checks, 671 goldens,
  53 ordinary executable inputs, four process-cleanup checks and 166 safety cases.
  The complete GCC reference-enabled e2e suite also passes existing driver, storage,
  ABI and local/constant/global alignment checks; all 166 reference safety programs
  pass with the new helper. Four 60-second fuzz targets and 20,000 standalone
  mutations per stage pass. No compiler semantics,
  reference backends, expected panics, -j2 job cap, 120-second compilation budget
  or 15-second execution budget changed. No timeouts are ignored or retried.
- **Next**: check push CI; if stalls persist, inspect its new evidence rather than
  raising budgets blindly. Language work remains aligned global constants, then
  function alignment; all enclosing memory milestones stay partial.

### M5.1c2b2b3a Internal mutable global alignment
- **What**: internal mutable globals accept explicit alignment with declared or
  inferred types. IrDecl carries a separate isolated alignment body/source location.
  Type and alignment resolve before publishing a typed global's address; inferred
  declarations resolve the initializer/type first, then alignment. SemaGlobal keeps
  byte alignment independently of its value type, and the published pointer retains
  explicit natural alignment as well as weak/strong guarantees.
- **Lazy resolution/identity**: DECL_TYPED reuses the already validated address and
  alignment while deferred initialization drains. Forward references, address cycles
  and invalid initializers reached only through an address remain checked. An
  unused invalid alignment remains lazy. Alignment expressions cannot load mutable
  global contents at comptime or leak runtime execution into attribute evaluation.
- **Codegen/storage**: ordinary QBE data records use the actual explicit alignment.
  Above QBE's static limit of 64, the existing assembly companion emits writable
  .data with .balign; zero/undefined storage uses .zero. Pointer/function-pointer
  relocations stay writable, not in the immutable .data.rel.ro section. Global
  address identity is unchanged by weakening a pointer type.
- **Reference grounding**: Zig 0.17 Zcu/PerThread.zig analyzeNavType resolves declared
  type/layout then pointer modifiers; analyzeNavVal handles modifiers after inferred
  initializer/type resolution. Typed/inferred error-order fixtures, pointer type
  probes and executable behavior were checked against the actual reference binary.
- **Tests**: added IR/TIR/QBE goldens, 15 reference-rejected diagnostic cases and an
  explicit global-constant NYI fixture. The physical matrix checks 153 global
  addresses/readbacks across weak/natural/over-alignment, including zero and
  undefined storage, pointers, callbacks and OPVs, with no TEXTREL. The reference
  coercion matrix covers 300 typed/inferred global cases and is in CI. Executable
  coverage includes forward address cycles, lazy invalid declarations, defer writes
  and mutations through aligned pointer/function-pointer slots. GCC/Clang -Werror
  ASan/UBSan pass 558 unit checks, 671 goldens, 53 ordinary executable inputs plus
  two szc-only inputs, 16 driver, 11 storage, four ABI, seven unaligned, 135 local,
  270 constant, 153 global address and 166 safety cases. GCC reference integration
  and optimized GCC checks pass. The 300 global comparisons match Zig and sanitized
  replay; existing 626 constant cases retain sanitizer parity. Four 60-second fuzz
  targets and 20,000 standalone mutations per stage pass. Corpus: irgen 29, sema 58.
- **CI follow-up**: prior 8bf59f5 passed GCC/Clang/fuzz, but reference Zig timed out
  after 120 seconds on shl_size_33/shr_size_33 (164/166 safety cases completed).
  Safety reference compilations now pass -j2 to bound worker parallelism, retaining
  the 120-second compilation and 15-second execution budgets and all assertions.
  Both affected reference programs compile in under one second locally with -j2
  and produce the expected panic; remote success still requires checking push CI.
- **Limitations/next**: only internal mutable globals. Global const alignment,
  exported/extern/threadlocal variables and function declaration/address alignment
  retain their explicit limitations. Do not reuse global-variable storage for known
  constants or silently promise alignment on code symbols. Enclosing alignment and
  memory milestones stay partial; Q6 remains deferred until M10.2.

### M5.1c2b2b2 Aligned local constants
- **What**: explicit local const alignment preserves the immutable binding's value,
  including comptime integers/types and known pointer values. IR_CONST_BIND carries
  the initializer and validated IR_ALIGN ref without converting reads into runtime
  loads. Cached IR_REF instructions remain anchored after the binding, so runtime
  values spill once at the definition even when first addressed in one branch.
- **Known storage**: ConstObject records alignment alongside value and identity;
  hashing/equality/rehashing include it. Compatible pointer casts retain the same
  object. Known dereferences still recover the original value. Codegen uses the
  object's real storage alignment, not the alignment of the pointer value itself.
  Pointer/function-pointer objects remain in .data.rel.ro; scalars remain in .rodata.
- **QBE/driver interaction**: the pinned QBE data parser accepts powers of two only
  through 64 (its alignment field is bounded by CHAR_MAX). Larger immutable objects
  are emitted as ELF assembly data with .balign, correct scalar widths and symbol
  relocations. Codegen writes an optional assembly companion; the driver keeps it
  in an anonymous temporary, runs external QBE normally, appends the data to QBE's
  assembly and invokes cc. No text rewrite, fake alignment promise or runtime
  address rounding substitutes for a known object's relocatable aligned address.
  --keep-temps retains the complete .s; success/failure cleanup remains unchanged.
- **Standalone IL**: emit-qbe reports a compile diagnostic when a materialized
  constant requires alignment above 64; use build-exe/run for those programs.
  Ordinary standalone IL is unchanged. The executable harness explicitly tests
  this limitation for marked outlined-data inputs, then tests the full pipeline.
  The sema fuzzer exercises both codegen output streams through its discard sink.
- **Tests**: added IR/TIR/QBE goldens, reference-rejected alignment diagnostics,
  interning/rehashing unit checks, retained-assembly and tool-failure driver checks.
  Assembly checks cover 270 physical known/runtime constant addresses across 15
  types and nine alignments, plus readbacks and PIE/RELRO checks. Executable tests
  cover runtime branch dominance, recursion, loop initialization, returned known
  addresses, nested object dependencies and function-pointer storage. The reference
  matrix covers 626 coercion/comptime cases, including copied binding alignment.
  GCC/Clang -Werror ASan/UBSan pass 558 unit checks, 652 goldens, 52 ordinary
  executable inputs plus two szc-only inputs, 16 driver, 11 storage, four ABI,
  seven unaligned memory, 135 mutable-local alignment, 270 constant alignment and
  166 safety cases. Full GCC reference checks and optimized GCC checks pass.
  All 626 new comparisons match Zig and sanitized replay; existing 300 mutable
  cases retain sanitizer parity. Four final 60-second fuzz targets and 20,000
  standalone mutations per stage pass. Corpus: irgen 28, sema 52 inputs.
- **Limitations/next**: aligned global declarations and function declarations remain
  pending. Taking the address of an explicitly aligned local bare-function alias
  is an explicit NYI: metadata alone must not promise stronger code alignment.
  Calling that alias and addressing an aligned function-pointer *slot* work.
  General comptime interpretation, floats and wider/aggregate runtime storage keep
  their existing limits. All enclosing memory/alignment milestones stay partial.

### M5.1c2b2b1 Aligned mutable locals
- **What**: mutable local declarations accept explicit integer alignment, including
  inferred types, weak alignment and explicit natural alignment. A new IR_ALIGN
  instruction evaluates an isolated comptime body before the initializer; this
  keeps invalid attributes observable even when initialization fails or terminates.
  Pointer types and declarations share alignment-body generation and validation:
  defined u29, nonzero power of two, no captured runtime execution.
- **Storage/codegen**: IrAlloc carries the validated alignment ref; sema retains it
  in the slot pointer type instead of changing the child's ABI layout. QBE uses
  alloc4/8/16 for alignments up to 16. Larger alignments privately over-allocate
  max(size, 1) + alignment - 1 bytes and round the address upward with add/and;
  the entire object remains inside its allocation. Slots remain per activation,
  hoisted to function entry; initializers still execute at their declaration sites.
- **Interactions**: explicit natural alignment remains type-distinct from implicit
  alignment, and address coercions can weaken, never strengthen, the guarantee.
  Pointer/function-pointer slot alignment is independent of pointee alignment.
  Recursive activation identity, loop reinitialization, aliases, defer writes,
  void/u0 storage and weak-alignment scalar accesses have executable coverage.
- **Reference grounding**: Zig 0.17 Sema.zig zirAllocExtended resolves declaration
  alignment before initialization and retains explicit alignment in pointer flags.
  Acceptance/type-identity probes and invalid-attribute fixtures were checked
  against the actual 0.17.0 binary, not inferred from QBE's allocation primitives.
- **Tests**: added IR/TIR/QBE goldens, 12 reference-rejected diagnostic cases and an
  explicit aligned-local-constant NYI fixture. The runtime assembly helper checks
  actual addresses for 15 value types at nine alignments (135 cases); the local
  alignment/coercion differential matrix has 300 cases and runs in CI. GCC/Clang
  -Werror ASan/UBSan pass 554 unit checks, 636 goldens, 51 ordinary executable
  inputs plus two szc-only inputs, 12 driver, 11 storage, four callback ABI, seven
  unaligned memory, 135 local alignment and 166 safety cases. GCC reference checks
  and optimized GCC goldens/executables/address checks pass. All 300 new cases
  match Zig and sanitized replay; the existing 1,035 pointer-alignment comparisons
  also match Zig. Four 60-second fuzz targets and 20,000 standalone mutations per
  stage pass. Corpus: irgen 26, sema 47 inputs.
- **Limitations/next**: this completes mutable locals only. Aligned local constants,
  globals and functions remain separate storage work; do not implement constants
  as runtime variables and lose comptime values/reference identity. Float alignment
  expressions, general comptime interpretation and other pointer attributes remain
  pending. All enclosing M5.1/M5.1c/M5.1c2/M5.1c2b/M5.1c2b2/M5.1c2b2b stay partial.

### M5.1c2b2a Alignment-qualified pointer types and unaligned memory
- **What**: single-pointer types accept `align(integer_expression)`. IrPointerType
  holds a separate comptime body and source location for the alignment expression;
  sema evaluates it with a fresh operand-map overlay, no runtime TIR and no enclosing
  function/control targets. Values must be defined u29, nonzero powers of two.
  Captured runtime values, unreachable/panic and runtime stores cannot escape into
  runtime code from a type attribute. Calls/mutable comptime interpretation still
  have their existing explicit NYI limits.
- **Type identity/coercion**: TypePointer.alignment stores zero for implicit ABI
  alignment and byte units for explicit alignment. Explicit natural alignment is
  memory-compatible but not type-identical to implicit alignment in Zig 0.17; do
  not canonicalize those types together. Implicit effective alignment comes from
  the child ABI layout. Coercion may weaken alignment, never strengthen it; mutable
  outer pointees require equal effective alignment. Function parameter/return
  variance includes these rules, and typed undefined cannot bypass them.
- **Joins**: use the minimum effective alignment, retaining explicit alignment when
  either input was explicit, plus the existing const/allowzero union. Both inputs
  must coerce to the selected pointer. Bare function bodies decay before an aligned
  const-function-pointer join; mutable function-pointer peers remain rejected,
  matching reference behavior. Differential testing caught and fixed an initially
  over-permissive mutable-peer merge.
- **Backend/interactions**: existing x86_64 QBE scalar loads/stores operate on
  unaligned addresses; no invented alignment promises or dummy backing storage.
  Seven assembly-backed regressions supply addresses at offset one from aligned
  data and check signed/unsigned 16/32/64-bit loads/stores, an unaligned pointer slot,
  and untouched prefix/suffix guard bytes. They also execute with official Zig.
  Alignment of a pointer's pointee is separate from the pointer value's own ABI
  alignment (eight bytes); existing local/global slots and immutable objects retain
  natural storage alignment. Weakening a pointer never changes object identity.
- **Comptime safety**: auditing attribute evaluation found missing runtime-emission
  guards in coercions, integer casts, unary/bool operations, conditional branches
  and stores. These now reject runtime work in a comptime-only body instead of
  emitting through a null TIR. Runtime paths and OPV folding remain intact.
- **Reference**: actual 0.17 Sema.zirPtrType, validateAlign,
  coerceInMemoryAllowedPtrs, pointer peer merging and Type.abiAlignment, plus binary
  probes. Alignment expressions use the existing supported comptime integer subset;
  float literals such as align(4.0), which Zig accepts, remain explicit float NYI.
- **Validation**: GCC/Clang -Werror ASan/UBSan and optimized GCC: 554 unit checks,
  620 goldens, 50 ordinary/reference executable inputs plus two szc-only inputs,
  12 driver checks, 11 PIE/storage checks, four callback ABI checks, seven unaligned
  memory cases and 166 safety cases pass. Four 60-second fuzz targets passed, with
  final irgen/sema each re-fuzzed for 60 seconds after the comptime fixes.
  All 1,035 alignment comparisons match Zig 0.17.0 and sanitized replay matches
  debug output for every case. Existing 168 const-only pointer, 486 function-pointer
  and 864 allowzero comparisons also match. Standalone 20,000 mutations per stage
  pass, with irgen corpus 25 and sema corpus 43 inputs.
- **Limitations/next**: this is pointer type alignment, not explicit storage alignment.
  Local/global/constant/function declaration alignment remains M5.1c2b2b; do not
  claim over-aligned storage can already be declared. @alignCast and integer/pointer
  casts remain later casting work. Volatile memory access, address spaces and packed
  pointer fields are still pending. M5.1/M5.1c/M5.1c2/M5.1c2b/M5.1c2b2 remain partial.

### M5.1c2b1 Allowzero pointer qualification
- **What**: single-pointer syntax accepts `allowzero`, independent of `const`.
  IR_PTR_TYPE uses an IrPointerType payload with a child ref and qualifier flags;
  the text dump preserves existing const notation and appends allowzero. Type
  interning and diagnostic names retain the qualifier, including nested and function
  pointers. Ordinary address-of results and storage addresses remain nonzero types.
- **Coercion/joins**: nonzero pointers may widen to allowzero; implicit conversion
  cannot discard allowzero. Mutable outer pointees require bidirectional compatibility,
  preventing a nullable pointer from being stored through an alias of a nonzero slot.
  The same rule applies to typed undefined pointers. Branch joins combine top-level
  const/allowzero flags only if both inputs can coerce to the resulting pointer,
  selecting an existing memory-compatible child rather than inventing a new pointee.
  Function parameter contravariance and return covariance include allowzero.
- **Runtime/interactions**: existing pointer casts, scalar loads/stores, comparisons,
  writable globals, immutable backing objects and direct/indirect calls carry the
  qualification without changing the x86_64 representation. Void/u0 accesses remain
  no-ops, even through undefined pointers; other undefined dereferences still error.
  Runtime tests cover both branch orders, nested qualification, globals, immutable
  object identity, callback signatures and qualifier-sensitive type identity.
- **Reference**: verified actual 0.17 Sema.coerceInMemoryAllowedPtrs,
  resolvePeerTypes pointer merging, resolvePairInMemoryCoercible and Type.ptrAllowsZero;
  nine negative diagnostic fixtures were also checked with the official binary.
- **Validation**: GCC/Clang -Werror ASan/UBSan and optimized GCC: 554 unit checks,
  590 goldens, 49 ordinary/reference executable inputs plus two szc-only inputs,
  12 driver checks, 11 PIE/storage checks, four callback ABI checks and 166 panic
  cases pass. All 864 allowzero comparisons match Zig 0.17.0; sanitized replay
  matches debug output for every case. Existing 168 const-only pointer and 486
  function-signature comparisons also match. Four 60-second fuzz targets and
  standalone 20,000 mutations per stage pass (irgen corpus 24, sema corpus 34).
- **Limitations**: this checkpoint implements allowzero qualification, not integer
  pointer construction. @ptrFromInt/@intFromPtr/@ptrCast remain M5.5; there is no new
  null-pointer value or optional representation. Alignment, volatile memory semantics,
  address spaces, packed pointer fields and non-single pointer kinds remain pending.
  M5.1/M5.1c/M5.1c2/M5.1c2b remain partial; next is M5.1c2b2 pointer alignment.

### M5.1c2a Function types, addresses and indirect calls
- **What**: IR_FN_TYPE represents a function signature as a type value, without
  creating a function declaration or consuming a FuncId. Function declarations and
  type expressions share prototype lowering. TYPE_FN is comptime-only; function
  addresses are VALUE_FUNCTION values typed as single const pointers, pointing at
  the original function symbol rather than a scalar constant backing object.
  Function aliases therefore share address identity with their original declaration.
- **Calls/reachability**: callable signatures come from either function bodies or
  single pointers to functions. Known targets retain direct calls; runtime targets
  use TirCall.indirect and QBE `call %temporary`. Signature-driven argument coercion,
  arity checks, void/u0 erasure and noreturn termination are shared. Function symbols
  used in runtime values or nested immutable/global initializers queue their bodies;
  the existing function/global drain resolves newly reachable definitions. Callees
  are evaluated before arguments, preserving the old target when an argument changes
  a global callback. Known pointer dereference recovers the body; runtime dereference
  rejects loading a comptime-only function body instead of pretending to load bytes.
- **Signature identity/coercion**: parameter noalias flags and auto/C/inline convention
  identity are interned. Extern/export declarations infer C convention; explicit
  callconv expressions remain NYI. Non-pointer noalias parameters and auto variadic
  types diagnose. Memory-compatible function returns are covariant, parameters are
  contravariant, noalias/convention/arity are invariant, and mutable outer pointers
  require bidirectional compatibility. A noreturn return can widen only when the
  enclosing memory is not mutable. Same-representation integer aliases also match;
  integer-width widening is not function-pointer covariance.
- **Backend/ABI**: symbolic function initializers work in writable globals and
  immutable .data.rel.ro backing objects, with PIE/RELRO regression coverage. All
  integer/bool call results are normalized, including narrow C callback results whose
  upper return-register bits are unspecified. An assembly-backed test reproduced
  the old incorrect result, then checks unsigned/signed byte and boolean returns
  through both direct and indirect calls against Zig. Eleven QBE snapshots gained
  the expected canonical-return operations; no unrelated snapshots were regenerated.
- **Tests**: GCC/Clang -Werror ASan/UBSan, optimized GCC: 554 unit checks, 579 goldens,
  48 ordinary/reference executable inputs, two szc-only linkage inputs, 12 driver
  checks, 11 PIE/storage checks, four callback ABI checks and 166 panic cases. All
  486 function-signature/qualifier pairs and 168 existing pointer-qualifier pairs
  match Zig 0.17.0; function comparison is now in CI. Generated indirect-call tests
  cover all 128 signed/unsigned 1–64-bit integer types. Four 60-second fuzz targets
  and standalone 20,000 mutations per stage pass; final sema/codegen was re-fuzzed.
- **Reference/interactions**: checked actual Sema.analyzeRef, checkCallArgumentCount,
  coerceInMemoryAllowedFns/Ptrs and callconvCoerceAllowed, plus binary probes. Existing
  integer/defer safety and mutable-global lazy initialization remain intact. Imported
  libc callbacks use szc's cc linkage; their reference fixture was separately checked
  with Zig -lc rather than silently adding libc to every reference test.
- **Limitations**: pointer attributes remain M5.1c2b. Inline function addresses,
  runtime generic function addresses, comptime calls/specialization, explicit calling
  convention expressions and broader C ABI/aggregate/variadic support remain NYI/later
  work. Known generic function addresses may be discarded without forcing emission.
  M5.1/M5.1c/M5.1c2 are still partial; this is not a bootstrap-completion claim.

### CI follow-up: separate compilation and execution timeouts
- M5.1c1 CI run 38548134671 passed compiler and fuzz jobs; differential safety failed
  only because seven Zig reference compilations exceeded the 15-second execution
  budget. Safety compilation now has 120 seconds, while execution stays at 15 seconds.
  Compiler failures, runtime hangs and panic/message mismatches remain hard failures.
  Follow-up run 38549587896 was stopped by runner shutdown during the reference safety
  loop; its compiler/fuzz jobs passed. Per-case CI progress and failures now flush
  immediately, preserving the active case on interruption; no CI success is claimed.

### M5.1c1 Internal mutable scalar globals
- **What**: internal container-level `var` declarations now have stable GlobalId
  records, typed comptime initializers and writable scalar storage. VALUE_GLOBAL
  represents a known address, never a constant snapshot of the stored value.
  Explicit types have a separate IR type body; IR_DECL_TYPE supplies the initializer
  result context. DECL_TYPED publishes the address without forcing initialization.
  Executable analysis drains pending initializers and functions to a fixed point.
  Forward references, self-address initializers and address cycles work independently
  of first-use order, including an inferred global pointer into a typed cycle.
- **Memory/backend**: direct and indirect reads/stores reuse TIR_LOAD/STORE with
  materialized global symbols. Integer, bool, pointer, void and u0 globals are
  supported; zero-sized accesses remain no-ops. QBE emits internal writable data,
  zero-filled objects with `z`, and static relocations to mutable globals, immutable
  backing objects and literal bytes. Dependency marking includes global initializers
  before immutable-object closure; immutable pointer backing remains in .data.rel.ro.
  Unsupported wide integer storage diagnoses at the global declaration location.
- **Language behavior**: mutable global reads are runtime even when the initializer
  is known. Comptime reads/writes of non-OPV mutable storage fail, including undefined
  stores and indirect access; self-reads use Zig's declaration-dependency diagnostic.
  Void/u0 accesses need no memory and remain valid at comptime, with direct declaration
  resolution still detecting a self-dependent initializer. Unreferenced declarations
  stay lazy; exported declarations remain eager roots and explicitly diagnose NYI.
- **Tests**: 549 unit checks, 557 goldens, 45 ordinary/reference executable inputs
  plus one szc-only input, 12 driver checks, eight PIE/RELRO/storage checks and 163
  panic cases. The global generator covers all 128 signed/unsigned 1–64-bit integer
  types and grows global/pointer records. Runtime tests cover recursion, defer,
  pointer reassignment, live const pointers, address cycles and reference snapshots.
  Diagnostic probes agree with actual Zig 0.17.0; 168 qualifier cases still match.
  GCC/Clang -Werror ASan/UBSan, optimized GCC, four 60-second fuzz targets and standalone
  20,000 mutations per stage pass; the final lazy-resolution change was re-fuzzed.
- **Reference/interactions**: checked Sema.analyzeNavRefInner/ensureNavResolved's
  type-versus-fully distinction, declaration loads, OPV handling and actual binary
  probes. Existing reference dominance, const-object identity, arithmetic safety,
  panic byte storage and pointer qualification remain unchanged. Initializers are
  validated even when the global is only referenced by address.
- **Limitations**: this is internal scalar storage only. Extern/export/TLS globals,
  custom global align/linksection/addrspace, function pointers/calls, pointer attributes,
  aggregate globals and general mutable comptime memory remain explicit NYI/later work.
  Comptime calls needed for richer global initializers remain M7. M5.1 and M5.1c are
  still partial; next checkpoint is M5.1c2, not a claim of full bootstrap support.

### M5.1b Definition-anchored references and immutable object identity
- **What**: `&` now accepts local constants, parameters, ordinary temporaries and
  named global constants. A dense irgen reference table caches addresses by value
  instruction. Body finalization inserts IR_REF immediately after its definition,
  with parameter references at function entry. This preserves identity for aliases
  and dominance across branches, captures and deferred bodies without speculative
  use-site spills. Runtime storage is per invocation, not mutable static memory.
- **Comptime/storage**: VALUE_REF points to an interned ConstObject, not a raw host
  pointer. Anonymous immutable objects deduplicate by value; named declarations
  have separate identities, even for equal initializers. Pointer qualification
  preserves object identity. Known dereferences keep their comptime values,
  including type values; runtime references use existing TIR_ALLOC/STORE/LOAD.
  Codegen marks transitive object dependencies and emits only referenced storage.
  Scalar objects live in .rodata; pointer objects use .data.rel.ro so PIE relocation
  does not introduce TEXTREL and GNU_RELRO protects them after loader relocation.
- **Language behavior**: `==`/`!=` compare runtime pointers or known immutable object
  identities; single-pointer ordering stays invalid. Undefined pointer comparisons
  produce an undefined bool, accepted when discarded but rejected when used as a
  compile-time condition. Context can type temporary literals (`read(&1)`), but
  does not retype a named comptime_int binding (`read(&constant)` still diagnoses
  `*const comptime_int`). Writes through const references are rejected normally.
- **Tests**: GCC/Clang -Werror ASan/UBSan: 542 unit checks, 538 goldens, 42 ordinary/
  reference executable inputs plus one szc-only input, 12 driver checks, four new
  PIE/RELRO storage checks and 159 panic cases. The all-width generator now covers
  parameter/alias addresses and materialized min/max constants for all 1–64-bit
  integer types. Tests cover recursion, skipped-branch first uses, loop captures,
  cleanup reads, global identity, nested backing objects, context and type values.
  All 168 qualifier comparisons still match Zig. Optimized GCC, four 60-second fuzz
  targets and standalone 20,000 mutations per stage pass; final sema was re-fuzzed.
- **Reference**: checked actual 0.17 AstGen.ref_table/rvalue/body fixups, plus
  Sema.analyzeRef/analyzeNavRef/cmpSelf and binary probes. Global declarations and
  anonymous constant addresses are deliberately not conflated. Returning a runtime
  stack pointer is not diagnosed by the reference; tests never dereference one
  after its lifetime ends. Known pointer comparison poison is preserved, not
  eagerly diagnosed merely because a comparison expression exists.
- **Limitations/interactions**: mutable container variables, function addresses and
  calls through function pointers, pointer attributes and non-single pointer kinds
  remain explicitly unsupported (M5.1c/other memory work). General array/aggregate
  loads and mutable comptime memory remain M5.2/M6/M7. Comptime-only objects can be
  referenced/folded, but unsupported runtime object layouts produce hard errors.
  Existing integer/call/defer lowering and literal byte storage remain unchanged;
  four previous NYI error cases are now positive TIR snapshots.

### M5.1a Runtime single pointers and const qualification
- **What**: source `*T`/`*const T` types use structural pointer interning. Address-of
  mutable locals reuses their existing allocation; `p.*` reads/writes integer, bool
  or pointer pointees. Grouped lvalues and `&p.*` work, including nested pointers,
  function arguments/results, conditional/block joins and deferred writes.
  IR_DEREF and IR_REF_DEREF validate source dereferences separately from compiler
  loads. TIR_PTR_CAST represents compatible pointer qualification, emitted as a
  QBE long copy rather than an integer cast with unrelated safety checks.
- **Const rules**: a mutable pointer can become read-only. Nested qualifications
  follow 0.17's in-memory coercion rules: a mutable outer pointee makes inner
  qualifiers invariant. `**u8 -> *const *const u8` is allowed;
  `**u8 -> **const u8` is not. Scalar pointee widening is never pointer covariance.
- **Diagnostics/safety**: writes through const pointers, non-pointer dereferences,
  known undefined non-OPV dereferences (including `&p.*`), pointer-to-noreturn and
  single-pointer ordering are rejected. Void/u0 dereferences and stores require no
  memory access, including through known undefined pointers, as in the reference.
  Runtime pointee arithmetic/casts/defer preserve existing checked panic behavior.
- **Tests**: GCC/Clang warning-clean ASan/UBSan suites; 533 unit checks, 526 goldens,
  40 ordinary/reference executable inputs plus one szc-only input, 12 driver checks
  and 157 panic cases. The new generator covers signed/unsigned widths 1–64 with
  load/store, nested const arguments, pointer reassignment and defer snapshots.
  A CI differential compares all 84 qualifier pairs at pointer depths 1–3 in both
  runtime and typed-undefined form (168 cases). Typed undefined no longer bypasses
  const/pointer compatibility or implicitly becomes an integer. Error fixtures
  compare rejection/acceptance with actual 0.17, not a guessed model. Optimized GCC,
  four 60-second fuzz targets and standalone 20,000-mutation replays per stage pass.
- **Limitations**: M5.1 remains partial. Addresses of constants, parameters, globals
  and temporary values explicitly fail until declaration-backed storage/lifetime
  handling in M5.1b; no lazy branch-local spills or static temporary hacks.
  Pointer equality is explicitly unsupported until identity handling; `<` ordering
  of single pointers is a language error. Pointer attributes, non-single sizes,
  aggregate/array dereference and general comptime memory remain later work.
- **Interactions**: alloc/load/store still use existing QBE stack slots and long
  pointer ABI; address taking introduces no new allocator or global storage.
  Dereference result types feed existing assignment coercions, and pointer peers
  choose a compatible existing type without inventing an unsafe nested conversion.
  M4.5b literal byte pointers still support `&text.*` identity, but loading whole
  literal arrays awaits M5.2. General pointer casts remain M5.5.

### M4.5b Explicit panic and first-class literal byte values
- **What**: ordinary quoted/multiline string expressions lower to IR_STR, typed
  as pointers to immutable zero-sentinel u8 arrays. Escapes decode to bytes;
  Unicode escapes encode valid scalar values as UTF-8. VALUE_BYTES interns owned,
  length-aware bytes (including embedded NULs), independent of AST lifetime.
  Aliases, known-condition selection, block exits, local pointer variables and
  same-length runtime branch joins flow through the usual expression/value pipeline.
- **Panic**: @panic takes a normal operand and validates the byte-array-to-const-byte-
  slice conversion. Known array lengths lower directly as pointer/length operands
  to TIR_PANIC and an outlined libc write/abort helper. Literal storage is emitted
  once per referenced ValueId in ELF read-only .rodata, including its zero sentinel.
  Panic writes exactly the message bytes plus newline; it never unwinds defer.
  Comptime panic reports "encountered @panic at comptime: ...". Nested panic and
  syntactically unreachable arguments terminate before the outer panic, as in 0.17.
- **Tests**: warning-clean GCC/Clang ASan/UBSan: 533 unit checks, 500 goldens,
  36 ordinary/reference executable inputs plus one szc-only input, 12 driver checks
  and 154 panic cases. Optimized GCC is clean. Sixteen new panic cases compare
  Unicode boundaries, all 256 byte values, NULs, raw UTF-8, empty/escaped/multiline
  messages, aliases, variables, both runtime choices and cleanup suppression.
  Unit checks exercise byte interning ownership/deduplication/rehashing and binary
  diagnostic storage/printing. Readelf confirms literal .rodata has no write flag.
  Four 60-second fuzz targets and standalone 20,000-mutation replays pass.
- **Reference**: checked actual 0.17 string_literal.parseWrite/parseEscapeSequence,
  AstGen.strLitNodeAsString/simpleUnOp/panic and Sema.zirPanic, plus reference
  execution and diagnostics. Runtime binary messages match byte-for-byte. The
  official diagnostic renderer truncates a comptime panic message at embedded NUL;
  szc intentionally preserves the complete diagnostic bytes and records this difference.
- **Limitations**: general arrays/indexing/slices/string operations remain Phase 5.
  Runtime choices between different-sized literal arrays require slice joins and
  are explicitly unsupported, not falsely labeled incompatible source types.
  Runtime undefined panic slices are explicitly unsupported (reference accepts
  that undefined program). User panic handlers, stack traces, safety modes and
  retries for failed/partial writes remain absent. Quoted identifier canonicalization
  is unchanged; decoding literals does not automatically normalize identifier names.
- **Interactions**: IR_PANIC stores the operand and its argument source location;
  there is no panic-specific literal AST shortcut. TYPE_ARRAY carries element,
  length and zero-sentinel layout. Codegen materializes VALUE_BYTES as read-only
  global addresses and accepts ordinary pointer storage/joins. Diagnostic now stores
  a byte length; diag_report_bytes allows embedded-zero comptime messages, while
  existing formatted diagnostics use the same collector unchanged. Slice ABI and
  more general pointer/aggregate coercions can extend this foundation in Phase 5.

### M4.5a Safe unreachable and noreturn checks
- **What**: explicit source unreachable now uses the existing outlined safety
  runtime, printing "reached unreachable code" and aborting, rather than executing
  raw hlt. Constant-evaluation unreachable remains a compilation error. A distinct
  TIR_TRAP terminates the backend after statically known noreturn calls and for
  internal fallback bodies; it does not claim a source safety violation.
- **Diagnostics**: explicit value/void/undefined returns from noreturn functions
  report "function declared 'noreturn' returns". Implicit return points at the
  signature type. Ordinary casts (including nested result-location coercions)
  report "cannot cast to noreturn". A syntactically noreturn return operand,
  including direct unreachable, is unreachable-code error, like AstGen.reachableExpr.
- **Tests**: warning-clean GCC/Clang ASan/UBSan: 518 unit checks, 479 goldens,
  35 ordinary/reference execution inputs plus one szc-only input, 12 driver checks,
  138 panic cases. Optimized GCC builds cleanly. Generated noreturn joins check all
  128 signed/unsigned 1–64-bit result types, dead panic arms and noreturn cleanup.
  Eight new panic cases cover entry points, calls, branches, loops and cleanup
  suppression. Four 60-second libFuzzer runs and standalone 20,000-mutation replays
  per target pass. Only affected return-IR and short-circuit QBE goldens changed.
- **Reference**: actual 0.17 Sema.zirUnreachable/analyzeAs/coerceExtra, AstGen.ret
  and std.debug.FullPanic.unreach, plus binary execution and diagnostic comparisons.
  Known direct noreturn calls end in backend unreach, not a source-unreachable
  panic; indirect noreturn-returned checks await function-pointer support.
- **Limitations**: explicit @panic remains M4.5b. Its slice-message requirement
  needs proper read-only byte/string value support, not a literal-only AST shortcut.
  General arrays/slices/strings remain Phase 5; user panic overrides, stack traces,
  runtime-safety toggles and other optimization modes are not implemented.
  Comptime function calls and richer coercion diagnostics retain the existing
  Phase 7/aggregate limitations.
- **Interactions**: IR_AS_RETURN distinguishes the outermost direct return-result
  coercion from ordinary IR_AS propagated into branches. Sema uses return-specific
  diagnostics only for the former; return operands are still checked before defer.
  Explicit unreachable alone uses TIR_UNREACHABLE's safety panic. Existing control
  join logic discards noreturn exits, and noreturn cleanup suppresses pending
  transfers. No changes to parser, external QBE invocation or panic unwinding.

### M4.4b Scope-exit defer
- **What**: expression/block defer, reverse registration order, nested cleanup,
  lexical names resolved at registration, and delayed evaluation of cleanup loads
  and call arguments. Defers execute for block fallthrough, implicit void return,
  explicit return, and named/unnamed break/continue. Return/break operands are
  evaluated before cleanup. Continue cleanup precedes the loop increment or
  continue expression; only scopes crossed by the transfer are cleaned up.
- **Control rules**: return from a defer body and break/continue escaping it are
  rejected, including dead conditional paths. Local block/loop control within
  cleanup is allowed. Cleanup can itself be noreturn; subsequent cleanup and the
  pending transfer are then omitted by sema. Panic/unreachable do not unwind defers.
  Non-void discarded cleanup expressions (including cleanup block statements) are
  errors. Errdefer remains an explicit unsupported feature until error unions.
- **Tests**: 518 unit checks, 466 goldens, 33 ordinary/reference executable inputs
  plus one szc-only edge input, 12 driver checks and 130 panic cases. GCC/Clang
  warning-clean sanitizer validation and optimized GCC builds pass. Generated
  snapshots exercise all 128 signed/unsigned 1–64-bit types on both exit paths.
  Stress coverage grows cleanup operand maps beyond their initial capacity;
  panic cases verify each exit kind, cleanup order and operand-before-cleanup.
  Four 60-second fuzz runs, final irgen/sema fuzz rechecks and standalone
  20,000-mutation replays per target pass.
- **Reference**: checked against Zig 0.17 AstGen.deferStmt/genDefers/ret and
  Sema.ensureResultUsed, with execution and diagnostic comparisons using its binary.
- **Limitations**: errdefer/error return traces await Phase 6; mutable comptime
  storage and calls await Phase 7 (constant-only empty cleanup already works).
  Full diagnostic notes and ordinary unused-result/local checks outside cleanup
  remain incomplete, as does quoted identifier canonicalization.
- **Interactions**: arena DeferScope registrations retain one untyped cleanup
  body, while a dedicated defer-body scope blocks escaping control transfers.
  Each scope exit emits IR_DEFER with that reusable body. Sema expands it into
  ordinary typed instructions using a fresh operand-map overlay whose parent
  exposes captured outer refs; cleanup-local operands never leak between exits.
  Map growth preserves its parent. IR_ENSURE_RESULT validates ignored cleanup
  values. Existing TIR/QBE control flow needs no new operation or runtime stack.

### M4.4a Runtime range-based for loops
- **What**: bounded integer ranges, multiple range inputs, unbounded companion
  counters, immutable usize captures/discards, nested loops, labeled break/continue,
  value-producing breaks and exhaustion else. Bounds are evaluated once in source
  order; continue advances the common index and break skips else. Captures do not
  leak into else. Existing result-type propagation and peer joins apply to values.
- **Safety**: endpoints coerce to usize; checked subtraction rejects reversed ranges.
  Known length mismatches report "non-matching for loop lengths". Runtime mismatches
  panic before iteration, including empty inputs, with "for loop over objects with
  non-equal lengths". Unbounded capture addition is checked for overflow.
- **Tests**: 518 unit checks, 449 goldens, 29 ordinary/reference executable inputs
  plus one szc-only edge input, 12 driver checks and 124 panic cases pass with
  warning-clean GCC/Clang ASan/UBSan builds. Optimized GCC also builds cleanly.
  The range generator covers 128 integer types; fixtures exercise endpoint order,
  bound snapshots, labels, else, contextual casts and boundary counters. Four
  60-second libFuzzer runs and standalone 20,000-mutation replays per target pass.
- **Reference**: verified using Zig 0.17.0 AstGen.forExpr, Sema.zirForLen,
  std.debug.forLenMismatch and the official executable, not older semantics.
- **Limitations**: non-range array/slice inputs await Phase 5; inline/comptime for
  awaits Phase 7. Defer is M4.4b; errdefer awaits error unions. Full diagnostic
  notes, unused captures/locals and quoted identifier canonicalization remain
  incomplete. Pointer range captures are invalid in Zig, not a supported future mode.
- **Interactions**: irgen emits bounded lengths through IR_FOR_LEN and lowers the
  loop with existing result/body-exit blocks and explicit repeat targets. Sema
  prefers a known bound and emits TIR_FOR_LEN_CHECK for runtime comparisons;
  codegen uses the existing panic guard infrastructure. No new parser node or
  static control state is introduced. Array/slice for can extend this shared-index
  lowering with memory element access in Phase 5.

### M4.3 Labeled blocks as expressions and named break values
- **What**: labeled block expressions/statements, value/void breaks, nested exits
  to outer blocks and loops, implicit void fallthrough, branch-local results and
  peer-typed joins. Result types propagate from returns, declarations, assignments
  and call arguments into break operands (including integer cast builtins).
  Blocks with no local exit are noreturn. A sole constant exit stays known;
  multiple runtime exits of comptime-only type are rejected like Zig 0.17.0.
- **Comptime**: constant-only block bodies work in container-level declarations,
  including nested named exits, conditional selection, arithmetic, type-valued
  blocks and function aliases. When typed IR is absent, breaks record known
  operands without allocating/emitting TIR; an inner exit to an outer target
  propagates noreturn until that target yields its value.
- **Scoping/diagnostics**: block and loop labels share ControlScope and a label
  redefinition checker. Unnamed jumps skip labeled blocks and find an enclosing
  loop; named continue targeting a block is rejected. Labels are a distinct
  namespace from locals. Unused/missing/redefined labels, fallthrough/type errors,
  comptime-only joins and unreachable break operands are reference-checked.
- **Testing**: IR/TIR/QBE goldens; runtime/comptime/nested-loop examples; all 128
  signed/unsigned 1–64-bit block joins via tools/generate_block_tests.py in CI.
  GCC/Clang sanitizer suites: 518 unit checks, 427 goldens, 25 ordinary e2e inputs
  plus one szc-only edge input, 12 driver checks and 119 panic cases. New safety
  cases exercise checked arithmetic and integer cast guards in break operands;
  ordinary programs and diagnostic messages are checked against actual Zig 0.17.0.
  Four 60-second fuzz runs and standalone corpus replay (20,000 mutations per
  target) are clean, including final irgen/sema replay after updating block seeds.
- **Limitations**: mutable comptime storage, comptime calls/loops and explicit
  comptime expressions remain Phase 7 work. Identifier/label matching still uses
  exact source spelling: quoted labels work with identical spelling, but aliases
  such as foo vs @"foo" and equivalent escapes are not yet normalized. Unused
  locals/results and full secondary diagnostic notes remain unsupported.
- **Interactions**: no new IR/TIR/codegen operation is needed. Source labeled
  blocks use the existing IR_BLOCK/BREAK and deferred BreakExit coercion bodies
  introduced by M4.2. ControlScope has independent flags for unnamed loop jumps
  and continue support. Namespace boundaries still stop label lookup.

### M4.2 Runtime while, break/continue and labeled loops
- **What**: boolean `while`, block/unbraced bodies, continue expressions, nested
  loops, named and unnamed break/continue, loop `else` and value-producing breaks.
  Loop conditions are reevaluated each iteration. Continue branches to a body-exit
  block, then executes the continue expression before repeating; break skips both
  the continue expression and else. Else-side unnamed jumps target an outer loop;
  a named break may target the exhausted loop, but named continue may not.
  Duplicate/unused/missing labels and invalid jump operands are diagnosed.
- **Compound assignments**: checked/wrapping/saturating arithmetic, bitwise and
  shift assignments use load/operation/store. Ordinary shifts provide the RHS's
  Log2Int result type (including @intCast); saturating shifts accept arbitrary
  integer amounts, matching the actual 0.17 AstGen assignment rules.
- **Result joins**: sema tracks explicit control targets. Break operands are
  gathered before peer typing, then coerced in deferred noreturn exit bodies;
  this preserves branch-local operands without inserting a widening cast outside
  its dominating branch. A sole constant exit remains known. Nonreturning loops
  stop sema analysis, and the existing QBE loop/repeat/block/br operations suffice.
- **Testing**: IR/TIR/QBE goldens, reference-checked diagnostics, nested labeled
  loops, early return/break from continue expressions, zero iterations, skipped
  runtime division guards, stack-local reinitialization and typed loop values.
  `tools/generate_loop_tests.py` exercises all 128 signed/unsigned 1–64-bit types,
  included in e2e and CI. Two new panic cases verify checked overflow in a loop
  body and continue expression. GCC/Clang sanitizer suites: 518 unit checks,
  409 goldens, 21 ordinary e2e inputs plus one szc-only edge input, 12 driver
  checks and 117 panic cases; ordinary programs match the official 0.17 binary.
  Four 60-second fuzz runs, final irgen/sema 60-second runs and standalone
  corpus replay are clean.
- **Limitations**: inline/comptime loop evaluation is deferred to Phase 7; even
  a zero-iteration loop in a container-level comptime initializer is not evaluated
  yet. Optional/error-union while captures await M6.3/M6.5. Labeled value blocks
  are M4.3, for/defer M4.4; unused locals/results and full diagnostic notes remain
  unsupported. Arithmetic safety remains always enabled.
- **Interactions**: IR_REPEAT now carries an explicit loop ref. Source while
  lowers to an outer result IR_BLOCK, IR_LOOP, condition IR_CONDBR, and a separate
  body-exit IR_BLOCK for continue. ControlTarget chains are analysis-local (no
  static state); nested IR_IF/boolean branches inherit them. The same block-merge
  infrastructure can be reused by M4.3's labeled blocks.

### M4.1 Source if/else, comparisons and boolean short-circuit
- **What**: source statement/expression `if`, optional `else`, else-if chains,
  unbraced plain assignments, branch-local declarations, integer/bool results,
  result-type propagation into branch casts (return/local/assignment/call argument),
  integer peer-type joins and early returns. Known conditions analyze only the
  selected arm. Runtime arms lower to existing TIR block/condbr/br; a branch that
  returns has no merge operand. A sole constant exit stays comptime-known while
  preserving control effects; two runtime exits reject comptime-only result types,
  even if their values are identical, like Zig 0.17.0.
- **Found on the way**: result coercions must not be appended after direct branch
  terminators. Noreturn result blocks must not emit a dangling QBE join label.
  Known-lhs boolean short-circuiting now propagates a noreturn RHS. Zig accepts
  syntax after an if whose two arms return, then sema skips it; do not add an
  irgen unreachable-code error based on inspecting both arms.
- **Testing**: IR/TIR/QBE goldens and diagnostic cases; executable tests cover
  nested branches, both/one returning arms, void/u0 results, skipped division
  guards, branch assignments, signed/unsigned comparisons and short-circuit
  composition. `tools/generate_conditional_tests.py` adds branch/merge checks for
  all 128 signed/unsigned 1–64-bit types, included in `make e2e-test` and CI.
  GCC/Clang sanitizer suites pass 518 unit checks, 389 goldens, 16 ordinary e2e
  inputs, 12 driver checks and 115 panic cases. Four 60-second libFuzzer runs,
  final sema/codegen 60-second run and standalone corpus replay are clean.
  Diagnostics checked against the actual 0.17.0 binary and Sema.zig's
  zirCondbr/zirBlock implementations. Both runtime arms are analyzed before
  collecting results, so independent branch errors are collected.
- **Limitations**: optional/error-union payload captures are explicitly rejected
  until M6.3/M6.5. Labeled value blocks, loops, break/continue and defer remain
  upcoming milestones. Compound assignments and unused-result/local diagnostics
  remain unsupported. Explicit unreachable still emits hlt until M4.5.
- **Interactions**: IR_IF is a value instruction with an IrCondbr payload and
  break_inline BODY_RESULT arm exits; IR_CONDBR remains a terminator. No new TIR
  operation or backend executable is needed. Noreturn results stop sema body
  analysis; void branches use TIR_REF_NONE and dump without a fake numeric ref.

### M3.6b Runtime saturation and explicit division builtins
- **What**: runtime +|/-|/*| clamp overflow to integer bounds; <<| checks the
  original (possibly u64) amount before narrowing it, handles zero/large shifts,
  and detects truncation by shifting the normalized result back. Added @divTrunc,
  @divFloor, @divCeil, @divExact, @rem and @mod with peer typing and comptime
  evaluation. Signed division guards min/-1; floor/ceil correct truncating results
  according to operand signs and nonzero remainder; exact division guards remainder.
  Modulo follows floor division, including negative denominators as in Zig 0.17.0.
  `comptime.c` owns rounded BigInt quotient/remainder computation, with alias-safe
  output descriptors; zero divisors leave outputs untouched. Known u0 divisors
  and undefined operands are rejected during sema. Matched the reference's special
  max-bound result for nonzero comptime saturating shifts with an amount beyond u64.
  New typed-IR/QBE/error goldens and sema fuzz seeds. Generated all-width tests now
  include saturation and explicit division; panic coverage grows to 115 cases.
- **Validation**: 518 unit checks (including 2,000 randomized BigInt round/alias
  trials), 377 goldens, 12 ordinary e2e inputs, one szc-only edge input, 12 driver
  checks and 115 panic cases pass under GCC/Clang sanitizers. Ordinary e2e and
  panic cases match official Zig 0.17.0; three additional generated seeds also match.
  Four local libFuzzer targets complete 60 seconds each without failures.
- **Limitations / reference discrepancy**: the official x86_64 runtime backend
  traps on @rem/@mod(i64.min, -1), although its comptime evaluator returns zero and
  Sema adds no division-overflow guard for remainder. szc returns zero without
  issuing a trapping hardware division. This deliberate edge difference is covered
  in `tests/run_szc/remainder_minimum.zig`, run with ZIG unset; it is not silently
  included in the ordinary differential suite. Runtime integers wider than 64 bits,
  floats, vectors, optimization-mode safety switches and full panic/std integration
  remain later milestones.
- **Interactions**: builtin names lower through the irgen table to distinct IR/TIR
  operations; codegen shares overflow predicates between checked and saturating
  arithmetic, and emits only needed libc-backed panic helpers. Existing CI
  test/e2e targets include the new goldens, width cases and panic checks.

### M3.6a Checked runtime arithmetic and panic stub
- **What**: typed IR preserves checked vs wrapping add/sub/mul/negation. QBE guards
  detect unsigned carry/borrow and signed overflow after canonical normalization;
  multiplication checks unsigned operand magnitudes against a division threshold,
  including signed minima and zero operands, without relying on wider native integers.
  Checked negation rejects the signed minimum. Integer casts enforce destination
  bounds and signedness, including u0; truncate and wrapping operations remain unchecked.
  Unsigned division/remainder guard zero divisors; shifts guard non-power-of-two
  widths. Panic stubs emit the matching Zig 0.17.0 message to stderr and abort.
  Fixed comptime left shifts to truncate to their fixed width, and reject `i0` as
  Zig does. Added safe boundary regressions, generated checked-op cases over all
  128 integer types, error goldens and runtime panic checks against official Zig.
  The safety harness runs in `make e2e-test` (and independently with `make safety-test`).
  Fresh GCC/Clang `-Werror` sanitizer builds pass 515 unit checks, 369 goldens,
  ten e2e inputs, 12 driver checks and 77 panic cases; reference validation passes
  against official Zig. Standalone fuzz replay is also clean, and all four
  libFuzzer targets completed 60 seconds each without failures.
- **Limitations**: runtime saturation and explicit signed division/remainder builtins
  remain M3.6b; integers wider than 64 bits are still rejected. Runtime checks are
  always enabled: no optimization-mode switch or `@setRuntimeSafety` yet. The panic
  stub has no stack trace, custom panic handler or std integration; explicit
  `unreachable` still uses QBE `hlt` until M4.5. A panic's single stderr write does
  not retry short/interrupted writes.
- **Interactions**: codegen alone lowers safety checks and emits only needed panic
  helpers (libc write/abort through the separate system-cc link). Typed-IR wrapping
  op names differ from checked ops so later optimization modes can preserve semantics.
  The safety harness compiles each source separately, checks SIGABRT and exact szc
  stderr, and checks official Zig's signal/message without depending on stack traces.

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
  The new GitHub CI configuration is pushed; check Actions for the latest status.
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
