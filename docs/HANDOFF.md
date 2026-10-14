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
- Done: M5.1c2b1 allowzero pointer qualification, nested coercions and qualifier joins.
- Done: M5.1c2b2a alignment-qualified pointer types and actual unaligned memory tests.
- Done: M5.1c2b2b1 explicit alignment on mutable locals, including over-aligned slots.
- Done: M5.1c2b2b2 aligned local constants, preserving comptime values and addresses.
- Done: M5.1c2b2b3a explicit alignment on internal mutable global objects.
- Done: M5.1c2b2b3b explicit alignment on internal global constants of supported
  runtime-representable types, preserving comptime values and immutable identities.
- Done: M5.1c2b2b4 function declaration/address alignment (internal/exported
  definitions and supplier-guaranteed extern declarations).
- Done: M5.1c2b3a volatile single-pointer qualification/coercion.
- Done: M5.1c2b3b observable volatile scalar/bool/single-pointer loads/stores.
  This completes the user-requested three-checkpoint batch; return to small
  checkpoints next. Remaining alignment gaps: local bare-function alias alignment
  and typed immutable address-dependency cycles. Plan/ground one of these next;
  do not claim all M5.1 integration complete.
  M5.1/M5.1c/M5.1c2/M5.1c2b/M5.1c2b2/M5.1c2b2b remain partial.
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
  M5.1c2a 54fcbb4/run 38554387430 also passed GCC/Clang/fuzz; its differential
  job lost communication with the hosted runner during native/reference tests.
  The check annotation confirms runner communication loss, not a completed test
  result. The next checkpoint, M5.1c2b1 55ad2cd/run 38564405059, passed all
  GCC/Clang/fuzz/differential jobs, restoring green CI. M5.1c2b2a 8b1d3cf
  also passed all jobs (run 38590520673), as did M5.1c2b2b1 ecfb6ab
  (run 38593742928). Check each new push's run.

## Validation at M5.1c2b3b

- TIR_LOAD_VOLATILE/TIR_STORE_VOLATILE preserve runtime access effects. Sema chooses
  them from TypePointer.is_volatile; no discarded-read pruning is introduced.
  Stores of undefined currently materialize arbitrary zero rather than silently
  using a normal store. Known immutable pointer contents and OPVs still fold,
  matching Sema.zig analyzeLoad/storePtr2; runtime global contents stay unknown.
  Existing undefined-pointer, const-store, comptime-only load and coercion checks
  apply before emitting memory operations. Casts/non-single pointers remain NYI.
- Codegen emits opaque calls to private QBE functions, one per used width/sign for
  loads and width for stores. Each helper has exactly one memory instruction with
  a returned load value or pointer-argument store. QBE optimizes functions separately
  and has no volatile opcode or cross-function inlining; calls preserve otherwise
  discardable reads/consecutive stores and conservatively barrier memory operations.
  Do not add purity annotations, inlining or LTO without redesigning this boundary.
  Load results normalize narrow integer values. emit-qbe is self-contained for
  these helpers. Volatile is not atomic and supplies no cross-thread synchronization.
- tests/run_volatile_access.py links tests/helpers/volatile_observer.c. The observer
  uses Linux/x86_64 PROT_NONE faults plus the single-step flag to re-arm protection
  after each actual memory instruction; unrelated faults terminate the test. It
  verifies 44 sequences for 17 types at offsets zero/one, unused reads, consecutive
  stores and order, loops, compound operations, &p.* cancellation and void/u0.
  Compound LLVM addb memory instructions fault once as writes, so that dedicated
  oracle accepts exact wrwr or wwr event sequences; ordinary wwrrwr tests remain
  exact. Zig Debug and ReleaseFast both pass. A proposed undefined-store count was
  rejected by actual ReleaseFast behavior (it can omit that store); no test asserts
  counts for undefined stores or reads their contents. szc's arbitrary-zero choice
  is covered by TIR/QBE goldens, not claimed as a Zig-guaranteed observable write.
- Three stage goldens, five reference-rejected errors and executable integration
  cover globals, immutable comptime pointers, defer/loop/compound sequencing and
  callback slots. Removed temporary access-NYI fixtures; retained fuzz inputs now
  exercise valid accesses. Corpus: irgen 33/sema 79. GCC/Clang -Werror ASan/UBSan
  both pass 558 units, 726 goldens, eight cleanup probes, 57 ordinary/generated
  executables plus two szc-only inputs, 16 driver, 11 storage, four ABI, seven
  unaligned, 135 mutable-local, 270 local-constant, 153 mutable-global,
  153 global-constant, 27 function-alignment, 44 physical volatile sequences and
  166 safety checks. GCC's full e2e suite is reference-enabled. The observer also
  passes against Zig Debug/ReleaseFast for Clang and optimized GCC szc builds;
  optimized GCC passes goldens and all 57 ordinary/generated executables. Final
  Clang compiler still matches the 77 function-alignment and 240 volatile-qualifier
  reference cases. Four 60-second fuzz targets and 20,000 standalone mutations
  per stage pass. Do not infer remote success from local tests.
- Batch commits before this: 2cc8303 function alignment; 59e3d53 volatile qualifiers.
  0fc49d8's race-fix CI and 9ee0e66's global-constant CI (38629629954) both
  completed successfully. At last check 2cc8303/run 38631694505 and
  59e3d53/run 38633098367 passed GCC/Clang/fuzz, with differential still running.
  Each newer push needs its own status check. Return to small milestones after this third checkpoint. Existing
  local aligned bare-function-alias addresses and typed constant address cycles
  remain unsupported; choose a scoped, reference-grounded follow-up before arrays.
  Keep enclosing alignment/memory milestones partial. Q6 only at M10.2.

## Validation at M5.1c2b3a

- IR_POINTER_VOLATILE and pointer_type populate the existing TypePointer flag;
  IR/TIR diagnostics retain it. memory_coercion_allowed permits adding volatile,
  disallows discarding it and enforces nested invariance through mutable pointers.
  peer_type unions volatile with const/allowzero and verifies both conversions;
  function variance reuses these rules. Pointer representation/identity unchanged.
- Sema.zig coerceInMemoryAllowedPtrs and peer type resolution ground the behavior.
  tools/diff_volatile_pointers.py has 240 acceptance cases covering qualifiers,
  alignment, nested pointers, joins and function parameter variance. Three stage
  goldens, seven reference-rejected errors, two temporary access-NYI fixtures and
  executable pointer/alias/slot/&p.* tests replace blanket pointer-volatile NYI.
  Corpus: irgen 32/sema 75. GCC -Werror ASan/UBSan passes 558 units,
  720 goldens, eight cleanup probes, 56 ordinary/generated executables plus two
  szc-only inputs, 16 driver, all existing memory/alignment and 166 safety cases.
  The new executable also passes with Zig. Clang sanitizers pass units/goldens,
  46 ordinary executables, driver and safety checks, and all 240 Zig comparisons.
  Four 60-second fuzz targets and 20,000 standalone mutations per stage pass.
- This intermediate commit rejects volatile memory accesses explicitly; it does
  not claim ordinary QBE loads/stores are volatile. Next is M5.1c2b3b, still within
  this user-requested three-checkpoint batch. Non-single pointers, casts, atomics
  and unsupported value kinds remain NYI. Keep Q6 deferred until M10.2.
- Earlier process-reaping race fix 0fc49d8/run 38627706294 completed all CI jobs
  successfully. Later checkpoint CI must still be checked independently.

## Validation at M5.1c2b2b4

- Function address alignment is SemaFunc metadata, not part of TypeFn. IrFuncHeader
  carries isolated align_body/align_src. analyze_alignment validates after the
  signature; function_address preserves the declaration guarantee for direct
  references, aliases and function-to-pointer decay. std.zig.AstGen rejects an
  alignment on function type syntax, and szc now emits the same primary error.
- QBE uses fixed alignment 16 for functions. For larger requests emit_function
  selects .text.szc.fn.ID with exactly one body at offset zero. An assembly prelude
  creates that executable section and raises its alignment while still empty.
  combine_assembly copies the complete companion then complete QBE output; there
  is no text interpretation or instruction rewriting. Immutable/global data still
  uses existing sections and all retained .s content includes both streams.
- Standalone emit-qbe rejects function alignment above 16 with an explicit
  build-exe recommendation. Physical tests verify internal/export/extern targets
  at nine alignments (27 cases), indirect slots, no TEXTREL, executable sections
  and bounded output size. Extern alignment is a promise supplied by external asm,
  not code emitted by szc. tests/run_function_alignment.py accepts optional ZIG.
- tools/diff_function_alignment.py checks 77 acceptance/coercion/join cases with
  Zig. Added IR/TIR/QBE goldens, 12 reference-rejected errors, executable coverage
  and corpus inputs (irgen 31/sema 70). GCC -Werror ASan/UBSan passes 558
  units, 709 goldens, eight cleanup probes, 55 generated/ordinary executables plus
  two szc-only inputs, 16 driver, 11 storage, four ABI, seven unaligned, 135 local,
  270 local-constant, 153 mutable-global, 153 global-constant, 27 function alignment
  and 166 safety cases, with Zig reference checks enabled. Clang sanitizers pass
  units/goldens, 45 ordinary executables, function alignment (also Zig), driver,
  storage, global constants and safety. All 77 new comparisons match both debug
  and sanitized builds. Four 60-second fuzz targets and 20,000 standalone
  mutations per stage pass. Check push CI separately.
- Explicitly aligned local bare-function alias addresses remain NYI, as do inline
  addresses, generic calls, explicit callconv/addrspace/linksection and existing
  constant address cycles. Do not mark enclosing memory/alignment complete.
- Next in this user-authorized batch: volatile qualification then observable
  volatile accesses; keep separate validated commits. Ask Q6 only at M10.2.

## Validation at M5.1c2b2b3b

- Global const declarations now use isolated IrDecl alignment bodies. Explicit
  types resolve before alignment and initialization; inferred declarations resolve
  initializer/type first. SemaDecl.alignment is separate from the original known
  Operand, so dereferencing an aligned constant still yields its comptime value.
  IR_DECL_REF combines the declaration's unique identity and storage alignment.
  Ordinary copied bindings get their own natural storage, not the source's alignment.
- Actual Zig 0.17 sources (Zcu/PerThread.zig analyzeNavType/analyzeNavVal and
  Sema.zig resolveNavPtrModifiers) and reference probes ground ordering and errors.
  Aligned global comptime-only types and bare function aliases are rejected, unlike
  local comptime constants. Function-pointer slots are ordinary supported objects.
- Existing immutable storage codegen handles QBE data through 64 and outlined ELF
  assembly for larger alignments, including .rodata/.data.rel.ro and relocations.
  build-exe/run supply that companion; standalone emit-qbe retains its explicit
  above-64 limitation. No new backend workaround or mutable const backing was added.
- Forward acyclic constants, nested pointers, mutable pointees, callable pointer
  slots, void/u0, lazy invalid declarations and copied binding guarantees are tested.
  Typed global-constant address dependency cycles still report the existing
  dependency loop even where Zig accepts them. The address-cycle diagnostic is an
  szc-only limitation fixture, not a reference-rejected conformance example.
  Exported/extern/threadlocal aligned global constants remain NYI; functions,
  aggregates and other pre-existing type/backend limitations are not completed.
- New coverage: 153 physical global-constant address/readback checks (15 types at
  nine alignments, plus zero/undefined objects), 332 reference acceptance/coercion
  comparisons, IR/TIR/QBE goldens and 21 diagnostic fixtures. Sanitized replay
  matches all 332 new, 300 mutable-global and 626 local-constant cases.
- Clang -Werror ASan/UBSan passes 558 units, 694 goldens, eight cleanup probes,
  54 ordinary executable inputs plus two szc-only inputs, 16 driver, 11 storage,
  four ABI, seven unaligned-memory, 135 mutable-local, 270 local-constant,
  153 mutable-global, 153 global-constant and 166 safety checks. Optimized GCC
  passes goldens, ordinary executables and the new address checks with Zig enabled.
- GCC -Werror ASan/UBSan passes the same complete suite with Zig reference checks
  enabled. Four 60-second libFuzzer targets and 20,000 standalone mutations per
  stage pass; corpus is irgen 30/sema 65. Check the new push's CI; the prior
  0fc49d8/run 38627706294 has passed GCC/Clang/fuzz, with differential still running
  (allowzero comparisons at last check). Do not declare runner stalls solved.
- Next: function declaration/address alignment, without promising code-symbol
  alignment through metadata alone. Keep enclosing memory/alignment milestones
  partial and Q6 deferred until M10.2.

## CI follow-up: procfs reap-race regression

- da77561/run 38626759145 passed GCC/fuzz, but Clang/differential stopped in the
  cleanup probe: a descendant disappeared during reading /proc/PID/stat and raised
  ProcessLookupError (ESRCH), not FileNotFoundError (ENOENT). This is a test race,
  not evidence that the process-group termination failed.
- descendant_exited now treats both disappearance errors as a successful exit and
  still rejects a live/sleeping process. Deterministic mocked ENOENT/ESRCH/zombie/
  sleeping cases plus the existing output/status/signal/group checks all pass:
  eight cleanup checks. Production timeout behavior and compiler sources unchanged.
- 0fc49d8 carries this fix; check its push CI. The following checkpoint implements
  aligned global constants (see above).

## CI follow-up after M5.1c2b2b3a

- GCC/Clang -Werror ASan/UBSan pass 558 unit checks, 671 goldens, 53 ordinary
  executable inputs, four process-cleanup regressions and 166 safety cases.
  GCC's complete e2e suite also passes two szc-only inputs, 16 driver checks,
  11 storage, four ABI, seven unaligned, 135 local, 270 constant and 153 global
  alignment checks, with the ordinary/memory/safety reference comparisons enabled.
  All 166 safety cases pass against Zig 0.17.0 using the new process-group helper.
  Four 60-second fuzz targets and 20,000 standalone mutations per stage pass.
  Corpus remains irgen 29/sema 58; compiler sources are unchanged. Reinstalled
  sanitizer/Clang dependencies after the sandbox reset. Check new push CI before
  claiming the hosted-runner stalls are resolved.
- fc00389/run 38611434252 passed GCC/Clang/fuzz but differential safety completed
  159/166 with seven timeouts: native exact_remainder_i8/exact_remainder_i64/
  shr_size_5/shr_size_33 compilations; reference shl_size_5/shl_size_33 compilations;
  reference exact_remainder_u64 execution. Reference -j2 did not eliminate stalls.
  Do not describe this as a proven reference-only problem or a recovered green CI.
- tests/run_safety.py now uses Popen(start_new_session=True), communicate with the
  existing deadline, killpg(SIGKILL) on timeout, then output draining and re-raising
  TimeoutExpired. The old immediate-child cleanup could leave descendants running
  or blocked on inherited output pipes. Ordinary stdout/stderr/status/signal
  behavior is unchanged. Compilation stays at 120s; execution stays at 15s; no
  retries, skipped cases or relaxed expected diagnostics were added.
- tests/run_process_timeout.py covers four outcomes, including a waiting driver and
  an exited driver whose descendant retains stdout/stderr. Both timeout cases use
  a 60-second descendant and a 3-second deadline; cleanup must finish within 8s,
  preserve ready output, and leave no live descendant. A killed zombie awaiting
  PID 1 reaping is allowed. e2e-test and safety-test run this test before safety.
- This is a harness-hardening checkpoint, not completed language work. Aligned
  global constants (M5.1c2b2b3b) remain next. The actual Zig source probes confirm
  comptime-only globals cannot carry alignment and bare function aliases have a
  specific rejection, unlike aligned local comptime values. The implementation
  still has its explicit aligned-global-constant NYI. Keep Q6 deferred to M10.2.
- Check the new push's CI before asserting that hosted-runner stalls are resolved.

## Validation at M5.1c2b2b3a

- GCC/Clang -Werror ASan/UBSan pass 558 unit checks, 671 goldens, 53 ordinary
  executable inputs plus two szc-only inputs, 16 driver checks, 11 storage cases,
  four callback ABI cases, seven unaligned-memory cases, 135 mutable-local,
  270 constant and 153 mutable-global address checks, and 166 safety cases.
  GCC's full executable/memory/safety suite also passes against Zig 0.17.0 with
  bounded safety compiler jobs. Optimized GCC passes goldens, ordinary executables
  and the 153 global-address checks (also run with the reference binary). All 300
  new global coercions match Zig and sanitized replay; the existing 626 constant
  cases retain sanitizer parity. Four 60-second fuzz targets and 20,000 standalone
  mutations per stage pass. Corpus: irgen 29, sema 58 inputs. Final style-only
  cleanup was rebuilt and rechecked through units/goldens/reference address tests.
  Remote success still requires checking this checkpoint's push CI.
- The sandbox restored ecfb6ab; fast-forwarded from GitHub to the pushed 8bf59f5
  before this work. Reinstalled Clang/libasan/libubsan/compiler-rt. Credentials
  remain transient, not in files/config. The prior 8bf59f5 CI run 38605337173 passed
  GCC/Clang/fuzz but failed differential safety on reference compiler timeouts:
  shl_size_33 and shr_size_33 exceeded 120 seconds (164/166 completed). No szc
  compiler failure was reported. Safety reference commands now use verified Zig
  -j2 support; budgets/assertions are unchanged. Focused reference probes compile
  in 0.92s/0.67s locally and confirm expected panics. Check this push's remote run.
- IrDecl.alignment_body/alignment_src describe an isolated mutable-global attribute;
  zero means absent. global_var_body fills them, leaving aligned global const as
  explicit NYI. Explicit mutable type resolves before alignment/address publication;
  inferred type comes from the initializer, so attribute validation follows it.
  This matches Zcu/PerThread.zig analyzeNavType/analyzeNavVal ordering, not local
  allocation ordering. Diagnostic fixtures preserve both paths.
- SemaGlobal.alignment stores 0 for implicit ABI, otherwise byte units. create_global
  uses it in the published pointer type; DECL_TYPED reuses that exact address/type
  on later initialization. Do not publish a pointer before its alignment is known.
  Deferred initialization still drains when only the address was requested, and
  unreferenced invalid attributes stay lazy.
- Codegen emits explicit alignment for QBE data through 64. Larger mutable data
  uses the existing assembly companion, .data/.balign and .zero for zero/undefined
  storage. Immutable objects still use .rodata/.data.rel.ro. Test real mutations,
  not just pointer metadata, when changing section selection.
- tests/run_global_alignment.py checks 153 physical addresses/readbacks, including
  nine zero and nine undefined objects, plus writable pointer/callback relocations
  and no TEXTREL. tools/diff_global_alignment.py covers 300 typed/inferred pointer
  conversions in CI. The integration fixture exercises typed forward cycles,
  inferred globals, defer writes, weak/natural metadata, OPVs and lazy bad alignment.
- Next: aligned global constants, then function declaration/address alignment.
  Preserve comptime values and immutable identities; bare-function aliases still
  need honest code-symbol alignment. Threadlocal/exported/extern mutable globals
  remain NYI. Keep Q6 deferred until M10.2.

## Validation at M5.1c2b2b2

- GCC/Clang -Werror ASan/UBSan pass 558 unit checks, 652 goldens, 52 ordinary
  executable inputs plus two szc-only inputs, 16 driver checks, 11 PIE/storage
  cases, four callback ABI cases, seven unaligned-memory cases, 135 mutable-local
  alignment cases, 270 constant-address cases and 166 safety cases. GCC's full
  executable/memory/safety suite also passes against Zig 0.17.0. Optimized GCC
  passes goldens, ordinary executables and both local-address matrices; constant
  addresses also match the official binary. The 602 base constant coercion/comptime
  cases and 24 copied-binding cases match Zig; all 626 replay identically under
  ASan/UBSan. Existing 300 mutable-local cases also retain sanitizer parity.
  Four final 60-second fuzz targets and 20,000 standalone mutations per stage pass.
  Corpus: irgen 28, sema 52 inputs. Check this checkpoint's push CI before claiming
  remote success. The prior ecfb6ab CI passed all jobs (run 38593742928).
- IR_CONST_BIND is a binop (initializer value, validated alignment ref); it only
  wraps explicitly aligned const bindings and copies of those bindings. A copied
  binding with no attribute records NONE, so it cannot inherit source alignment.
  Other ordinary IR is unchanged. Sema returns the original operand, including
  comptime knowledge. IR_REF consults the
  binding's alignment; runtime references use the existing definition-anchored
  allocation path, known values retain immutable ConstObject storage.
- ConstObject.alignment is 0 for implicit ABI alignment, otherwise byte units. It
  participates in object hash/equality and rehashing. value_ref now takes alignment
  as its final argument; ordinary references pass zero. Pointer casts must keep
  object identity/alignment, not re-intern its stored value at weaker alignment.
- codegen_qbe now accepts a second FILE* for optional outlined assembly. The pinned
  QBE parse.c parsedat rejects alignment > CHAR_MAX (largest power of two 64).
  Known objects above 64 use real .balign assembly data with relocations; the driver
  writes a tmpfile companion, runs QBE as a separate executable, appends the data
  to the generated .s, then runs cc. Runtime slots still use the previous private
  over-allocation/rounding strategy. Keep .rodata / .data.rel.ro placement intact.
- Standalone emit-qbe cannot carry the companion and rejects *materialized static*
  alignment above 64 with a diagnostic directing users to build-exe. Values not
  materialized need no backing data. tests/run_e2e.sh recognizes an explicit
  outlined-data marker and tests the diagnostic plus complete build/run pipeline;
  ordinary programs still compare standalone and retained IL byte-for-byte.
- tests/run_const_alignment.py checks 270 known/runtime addresses and readbacks,
  plus no TEXTREL and GNU_RELRO. tools/diff_const_alignment.py checks 626 accepted
  or rejected coercion/comptime cases in CI. @TypeOf is still NYI; reflection probes
  were checked separately against actual Zig, not added to szc's supported matrix.
- Next: aligned globals, then function declaration/address alignment, in small
  checkpoints. Bare-function aliases with explicit alignment cannot yet be
  addressed (honest NYI); do not propagate metadata onto an unaligned code symbol.
  Calling the alias and aligned *function-pointer slot* storage work. Constants
  retain values/reference identity; no mutable-variable substitution was used.
  Keep Q6 deferred until M10.2.

## Validation at M5.1c2b2b1

- GCC/Clang -Werror ASan/UBSan: 554 unit checks, 636 goldens, 51 ordinary
  executable inputs plus two szc-only inputs, 12 driver checks, 11 PIE/storage
  checks, four callback ABI cases, seven unaligned-memory cases, 135 local-address
  alignment checks and 166 safety cases pass. GCC also runs ordinary inputs,
  assembly-backed memory checks and safety cases against Zig 0.17.0. Optimized
  GCC passes goldens, ordinary executables and the 135 address checks (also run
  with Zig). All 300 new local-alignment/coercion cases match Zig and replay
  identically under ASan/UBSan; all 1,035 existing pointer-alignment comparisons
  still match Zig. Four 60-second fuzz targets and 20,000 standalone mutations
  per stage pass. Corpus: irgen 26, sema 47 inputs. Check this push's remote CI;
  local success does not imply the new GitHub run has completed.
- IR_ALIGN yields a validated comptime u29 from an isolated body before local
  initialization; IrAlloc.alignment is NONE for implicit ABI alignment, otherwise
  that ref. Pointer type attributes reuse the same irgen alignment_body helper.
  type_pointer now takes an explicit final alignment argument; ordinary address
  constructors pass zero, analyze_alloc passes the declaration's alignment.
- Local mutable storage retains explicit alignment in its pointer type. Child ABI
  alignment is unchanged. For A > 16, codegen allocates max(size, 1) + A - 1 bytes
  at QBE alignment 16, then rounds upward with add/and. This is real per-activation
  storage, not a static object or an unsupported QBE alloc variant. Ordinary slots
  retain their previous emission. Initialization stays at the source declaration.
- tests/run_local_alignment.py checks 135 physical addresses (15 types x nine
  alignments through 4096), mutations and readbacks through linked assembly;
  optional ZIG runs the same source/helpers. tools/diff_local_alignment.py covers
  300 typed/inferred locals and address coercions and is in differential CI.
  tests/run/local_alignment.zig additionally covers recursion/identity, aliases,
  defer, loop reinitialization, aligned pointer/function-pointer slots and OPVs.
- At this checkpoint aligned constants were pending; M5.1c2b2b2 above implements
  them without losing comptime values or definition-anchored references. Global
  and function declaration attributes still need separate storage/code alignment.
  Keep Q6 deferred until M10.2.

## Validation at M5.1c2b2a

- GCC/Clang -Werror ASan/UBSan and optimized GCC pass 554 unit checks, 620 goldens,
  50 ordinary/reference executable inputs plus two szc-only inputs, 12 driver checks,
  11 PIE/storage checks, four callback ABI checks, seven unaligned memory cases and
  166 safety cases. Four 60-second fuzz targets pass; final irgen/sema were re-fuzzed
  for 60 seconds each after the comptime evaluation fixes. All 1,035 alignment,
  168 const-only pointer, 486 function-pointer and 864 allowzero comparisons match
  Zig 0.17.0; the complete alignment matrix also matches debug output under ASan/UBSan.
  Standalone 20,000 mutations per stage pass. Corpus: irgen 25, sema 43 inputs.
  Check this checkpoint's push CI before claiming remote success.
- IrPointerType.alignment_body is a separate comptime body, not a runtime ref. It
  has a fresh InstMap overlay capturing the parent operands, is_comptime=true,
  tir/func/control=NULL. This prevents align(unreachable) from compiling as a runtime
  panic and stops runtime casts/stores from emitting through a null TIR. Existing
  runtime coercion/cast/unary/bool/control/store paths gained require_runtime guards.
- TypePointer.alignment is byte alignment of the pointee: 0 means implicit child
  ABI alignment. Keep 0 distinct from explicit natural alignment for type identity;
  memory coercions compare effective alignment instead. A pointer value/slot still
  has ABI alignment eight, regardless of its pointee qualification.
- Pointer peer joins take minimum effective alignment, explicit if either input
  was explicit. Do not restore the old early "one pointer coerces to the other"
  returns: they lose explicit natural alignment in the inferred type. Bare function
  plus const-function-pointer peers use address decay before merging; mutable peers
  must not gain acceptance simply by OR-ing a const flag onto both pointer types.
- tests/run_pointer_alignment.py links real offset-one .s storage to test seven
  unaligned scalar/pointer reads/writes with guard bytes; optional ZIG executes the
  same source and helpers. make e2e-test runs it. tools/diff_pointer_alignment.py is
  in differential CI, alongside existing qualifier/function comparisons.
- At this checkpoint explicit storage alignment remained pending. Mutable locals
  are now implemented in M5.1c2b2b1 above. Global/immutable/function alignment still
  needs real backing-storage guarantees and preserved reference identity.
- Integer alignment expressions work within existing comptime support. Zig accepts
  align(4.0), but floats are still explicit NYI here; do not add a literal-only hack.
  @alignCast/@ptrCast/@intFromPtr/@ptrFromInt remain later casting work. Address
  spaces/packed fields/volatile remain pending. Keep Q6 deferred until M10.2.

## Validation at M5.1c2b1

- GCC/Clang -Werror ASan/UBSan and optimized GCC are clean: 554 unit checks,
  590 goldens, 49 ordinary/reference executable inputs plus two szc-only inputs,
  12 driver checks, 11 PIE/storage checks, four callback ABI checks and 166 panic
  cases pass. All 864 new allowzero, 168 existing const-only pointer and 486
  function-signature comparisons match Zig 0.17.0; the entire allowzero matrix also
  matches debug output under ASan/UBSan. Four 60-second fuzz targets and standalone
  20,000 mutations per stage pass. Corpus: irgen 24, sema 34 inputs. Check push CI.
- Restored libasan/libubsan/Clang/compiler-rt after this sandbox reset; these are
  test environment dependencies, not repository changes.
- IR_PTR_TYPE now reads IrPointerType from extra, not data.bin. All new payload
  consumers must use IR_READ; its text dump intentionally preserves old const
  snapshots. TypePointer.is_allowzero participates in type hash/equality/names.
- type_pointer takes explicit const/allowzero booleans. Ordinary references and
  local/global storage addresses pass allowzero=false; qualifier widening retags
  existing values instead of creating different backing objects or symbols.
- memory_coercion_allowed may add allowzero only outside mutable outer memory;
  retain function parameter contravariance and nested mutable invariance. Typed
  undefined does not bypass either qualifier rule. Peer merging ORs top-level
  qualifiers and verifies both input conversions; do not recursively synthesize
  a child type without checking actual Zig peer rules.
- tools/diff_pointer_allowzero.py covers 864 const/allowzero conversions and joins:
  depth-one/two cross-products, depth-three allowzero, typed undefined, scalar
  branch joins and nullable function-pointer parameter/return variance. CI runs it
  alongside the existing const-only and function-signature comparisons.
- No new pointer representation or null constant. @ptrFromInt/@intFromPtr/@ptrCast
  are still M5.5. Next inspect actual 0.17 alignment rules and x86_64/QBE unaligned
  memory behavior for M5.1c2b2; volatile needs real access semantics, not a type-only
  flag. Address spaces/packed offsets/non-single pointer kinds remain later work.
- Keep the compiler build/bootstrap-options question deferred until M10.2.

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
