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
6. Implement/test/fix/document/commit/push each coherent milestone, then continue
   directly to the next without pausing for user approval. Periodically inspect prior
   CI runs and fix actual failures; ask only for decisions/blockers (Q6 at M10.2).
   Push after every milestone (and more often during long ones).
   The GitHub token is **never** committed. Restore authenticated Git access if needed.

## Current state

- Latest language checkpoint: M5.4a scalar-sentinel many-pointers is complete.
  `[*:value]T` supports defined comptime integer/bool sentinels. ValueId promises
  participate in type interning/names; root/offset pointers and thin ABI remain
  unchanged. Qualifier/coercion/inference rules cannot invent/change promises;
  writable nested slots are invariant. Arithmetic/open tails preserve promises;
  scalar element addresses and bounded unsentinelled windows drop them.
  Literal zero backing supplies the zero promise. Other sentinel types/sentinel
  slices/explicit array sentinel syntax remain separate work.
- /data/sentinel-many-final GCC/Clang Werror ASan/UBSan: 666 units/940 goldens,
  86 ordinary/generated + three scoped native cases, 16 driver checks, all C/ABI/
  alignment suites, 48 volatile sequences and 216 safety cases. GCC full e2e
  runs Zig; both builds separately reference-check the new native program,
  five C many-pointer variants and volatile Debug/ReleaseFast. Actual matrices:
  209 sentinel pointers + 168 fields + 87 dereference + 204 slicing + 252 slices.
  Optimized GCC passes 86 native/216 safety cases; 949 unchanged array/layout
  oracle replays retain verified source hashes (not fresh Zig runs). Four 60s
  fuzzers execute 1487814/47977/51638/67325 inputs, both builds replay 20000
  mutations/stage (55 irgen/111 sema seeds). All scripts exit zero; 1673 source/
  test files match both frozen builds. Evidence uses /data/sentinel-many-final-*.
- Latest language checkpoint: M5.4b scalar-sentinel arrays is complete.
  General integer/bool ValueIds replace the zero-only flag. Explicit type/literal
  syntax, contextual anonymous literals, physical layout/initialization, copies,
  concat and exact nested pointer-slot rules are covered. Empty logical arrays
  with physical sentinels are not incorrectly folded as empty one-value objects.
- /data/sentinel-arrays-final3 GCC/Clang Werror ASan/UBSan sources match the
  integrated source/test/config tree: 692 units/950 goldens; 87 ordinary/generated
  + three scoped native programs; 16 driver checks; all C/ABI/alignment suites;
  48 volatile sequences; 220 safety cases. GCC full e2e runs Zig; both compilers
  additionally reference-check sentinel_arrays.zig, four C storage variants and
  all volatile sequences in Debug/ReleaseFast. Fresh actual matrices pass
  313 array sentinels + 209 sentinel pointers + 168 fields + 87 dereference +
  204 slicing + 252 slices. Optimized GCC passes 87 native/220 safety cases;
  949 unchanged array/layout checks replay the source-hash-verified prior actual
  reference oracle (not 949 new Zig runs). Four 60s fuzzers execute
  949201/24630/26297/41516 inputs; both builds replay 20000 mutations/stage
  (56 irgen/112 sema seeds). All final processes exit zero. Older final/final2
  array snapshots are superseded. Logs use /data/sentinel-arrays-final3-*.
- M5.4c scalar-sentinel slices has completed local validation. Final immutable
  /data/sentinel-slices-final2 GCC/Clang sources/test/config match development:
  718 units/961 goldens, 88 ordinary/generated + three scoped native programs,
  16 driver checks, all C/ABI/alignment suites, 52 volatile sequences, 226 safety.
  Fresh matrices: 256 sentinel slices + 313 sentinel arrays + 209 sentinel
  pointers + 168 fields + 87 dereference + 204 slicing + 252 slices. Both builds
  separately run new native/four C variants/volatile Debug+ReleaseFast against
  actual Zig; GCC full e2e also runs Zig. Optimized GCC passes 88 native/226 safety;
  949 unchanged array/layout cases replay the source-hash-verified reference
  oracle, not fresh Zig. Four 60s fuzzers: 1306173/33229/36889/42163; 20000
  mutations/stage on both builds, 57 irgen/113 sema seeds. Every final wrapper
  exits zero. Older final is superseded. Logs: /data/sentinel-slices-final2-*.
- Integration into the main checkout is awaiting approval after an environment
  safety block on the bulk-copy operation. No C changes are integrated or pushed.
  Do not retry the blocked integration without user approval. Development is
  preserved separately in /data/sentinel-slices-next.
- M5.4d explicit assertions is locally complete in /data/sentinel-assertions-next.
  Frozen /data/sentinel-assertions-final GCC/Clang source/test/config match:
  726 units/972 goldens; 89 ordinary/generated + three scoped native programs,
  16 driver checks, all C/ABI/alignment suites, 54 volatile sequences, 243 safety.
  Both builds separately reference new native/four C descriptor variants/volatile
  Debug+ReleaseFast; GCC full e2e runs Zig. All eight fresh matrices pass:
  178 assertions + 256 sentinel slices + 313 arrays + 209 sentinel pointers +
  168 fields + 87 dereference + 204 slicing + 252 slices. Optimized GCC passes
  89 native/243 safety. The 949 old array/layout cases are exact-source-hash
  oracle replays, not fresh Zig runs. Four 60s fuzzers:
  1592533/52095/49368/68597; both builds 20000 mutations/stage, 58 irgen/114 sema
  seeds. All final processes exit zero. Logs: /data/sentinel-assertions-final-*.
  This milestone, like M5.4c, is not integrated or pushed while approval is pending.
- Next M5.5b integer-to-pointer conversion is isolated in /data/int-pointer-next.
  Numeric VALUE_INT_PTR addresses are not compiler-owned memory; runtime null/
  alignment checks, constants, pointer arithmetic and storage are under test.
  Initial 217 actual semantic comparisons, one reference native, three mapped-C
  storage variants and five reference panic cases pass. Follow-on numeric-backed
  descriptor/source probes are being integrated and the expanded matrix is
  running. Do not claim the milestone complete yet. See /data/ptr-from-int-*.
- CI 768cdb9/run 38790363308 completed failure in safety; its annotation reports
  hosted runner lost communication, not a proven semantic regression. M5.4b
  09e535e/run 38796178970 failed in the old safety job (annotation:
  operation canceled); safety partitioning 51372be/run
  38796715747 completed success (all jobs/aggregate green). The older array run
  failed and is not counted green; the successor includes its compiler changes.
- CI 7aeb3e5/run 38786030355 failed with runner shutdown/exit143 after native
  run/build timeouts; both reported cases pass with the original frozen compiler
  and unchanged deadlines (/data/ci-slice-fields-native-reproduction.log).
  b3f0dc3/run 38786459559 failed safety: 210 passed/six build timeouts across both
  Zig and szc. All six pass locally (/data/ci-b3-safety-reproduction.log).
  dbe17c3/run 38786103135 also failed with eight reported safety case names timing
  out before runner shutdown; nine variants of those names pass locally using
  the original compiler/deadlines (/data/ci-dbe-safety-reproduction.log).
  768cdb9/run 38790363308 is still in progress at the latest check. None of these
  failed/pending runs is counted as green. M0.5c four-way safety CI partitioning
  is complete: 22 partition/CLI proofs, exact preservation of all old Make/CI
  commands, and four actual native/Zig shards with 55 passing cases each.
  Maximum reference concurrency stays four; aggregate gating, process deadlines,
  tool pins and fuzz budgets are unchanged. Local full safety remains unsharded.
  M5.4b is pushed as 09e535e (new run 38796178970 was queued at last check).
  No new remote aggregate success is claimed.

- Latest language checkpoint: M5.3e writable/addressable slice descriptor fields
  is complete. /data/slice-fields-final immutable GCC/Clang snapshots match all
  1659 checked source/test files. Werror ASan/UBSan: 640 units/930 goldens;
  85 ordinary/generated + three scoped native programs, 16 driver checks,
  all C/ABI/alignment suites, 48 observed volatile sequences and 216 safety cases.
  GCC full e2e also checks Zig; both compilers separately reference-check the
  new native program, four C descriptor layouts and volatile Debug/ReleaseFast.
  Actual semantic matrices: 168 fields + 87 dereference + 204 slicing + 252 old
  slices. Optimized GCC passes 85 native/216 safety cases. The 949 array/layout
  cases are source-hash-verified replays of the previously actual-reference
  validated oracle, not fresh Zig compilations. Four 60-second fuzzers execute
  1430533/44280/53821/66966 inputs; both builds replay 20000 mutations/stage
  (54 irgen/110 sema seeds). All final scripts exit zero; logs use
  /data/slice-fields-final-*.
- IR_FIELD_PTR/VALUE_SLICE_FIELD_PTR borrow the descriptor's original word,
  preserving selection, root and cumulative offset through casts/nested arrays.
  Field address alignment is capped at the word ABI alignment, even for an
  over-aligned descriptor. Descriptor storage controls field const/volatile/
  allowzero qualifiers independently of element pointer qualifiers. Individual
  undefined descriptor words may be initialized without loading the descriptor.
  Volatile field loads/stores are observable; address formation does no access.
  Whole-descriptor volatile copies and sentinel descriptors/slicing remain NYI.
- CI follow-up: M0.5b pushed as dbe17c3 (run 38786103135 queued), M5.3e
  is 7aeb3e5 (run 38786030355 queued). Previous 657b0d7/run 38778220976
  completed with GCC/Clang/fuzz successful but serial differential failed:
  check-run 116571298839 reports "hosted runner lost communication with the
  server". Its job-log download redirects to a missing blob (HTTP404), so no
  test-level cause is claimed. Do not call this green or a semantic regression.
  The split suites retain all checks/deadlines; inspect their actual results.
- CI run 38778400121 (8dfaab9): 18 jobs successful, native reference suite still
  running at last check; no aggregate success claimed. M5.3e pushed as 7aeb3e5.
  M0.5b splits native/storage/alignment/safety into independent reference suites,
  preserving all 24 original e2e commands exactly once and max concurrency four.
  The matrix now has twenty suites; make e2e-test retains the complete local
  entry point. All four partitioned local suites passed; unchanged original GCC
  reference coverage also passed. Proof: /data/ci-native-coverage.log and
  /data/ci-native-{native,storage,alignment,safety}.log. Check new remote runs.
  Next language work: sentinel descriptors/slicing, remaining memory integration;
  no build-Zig question before M10.2.

- CI follow-on M0.5a: reference checks now run in sixteen independent suites
  (14 semantic tools + native/parser), max four concurrent, fail-fast disabled.
  The final `differential` status requires all matrix suites to succeed. GCC/Clang
  and fuzz jobs, parser 1000-case/run-seed checks and process deadlines are
  unchanged. /data/ci-sharding-validation.log verifies YAML, exact coverage and
  aggregate status wiring. Inspect remote jobs; static validation is not green CI.

- Latest follow-on: M5.3d2 known slice dereference/address cancellation complete.
  Corrected /data/slice-deref-guard-final GCC/Clang Werror ASan/UBSan snapshots
  match integrated compiler sources exactly: 632 units/920 goldens; full native
  84 ordinary/generated + three scoped programs; 16 driver checks, all C/ABI/
  alignment/volatile suites, 213 safety cases. Both compilers reference-check
  slicing.zig, slice_deref.zig and four C descriptor variants. Actual-reference
  matrices pass 87 dereference + 204 slicing + 252 previous slice cases.
  Optimized GCC passes 84 + three native/213 safety cases; 949 unchanged array/
  layout cases replay against the source-hash-verified actual-reference oracle.
  Four fresh 60-second fuzzers execute 1825141/56471/76535/88773 inputs;
  both builds replay 20000 mutations/stage (53 irgen/109 sema seeds). Logs use
  /data/slice-deref-guard-final-*. Earlier /data/slice-deref-final is superseded.
- Dereference caveats: &s.* gives a borrowed array pointer only when the descriptor
  is known. Direct s.* follows actual analyzeLoad behavior: OPV child -> element,
  immutable contents -> array, mutable-global contents (even empty views) ->
  runtime element fallback. Use (&s.*).* for explicit array copies. Runtime
  descriptors reject. /data/slice-deref-official-quirks.log proves upstream's
  SIGSEGV on undefined-zero-bit address cancellation and genSetReg backend error
  on mutable element fallback. The 87-case matrix excludes the crash cases;
  only sema types are reference-checked for the latter, with native execution
  in tests/run_szc/slice_deref_mutable.zig. Never claim these reference bugs pass.
- M5.3d1 was pushed as db49ca9; CI run 38775924855 has passed GCC/Clang/fuzz,
  with differential still running at last check. Next: sentinel descriptors/
  slicing (writable descriptor fields completed in M5.3e), then remaining memory integration;
  no build-Zig question before M10.2. Bounded reference CI sharding follows
  as M0.5a; coverage validation is /data/ci-sharding-validation.log.

- Latest checkpoint: M5.3d1 borrowed slicing is complete. /data/slicing-final
  immutable GCC/Clang Werror ASan/UBSan snapshots match integrated compiler
  sources exactly: 632 units/912 goldens, 83 ordinary/generated + two scoped
  native programs, 16 driver checks, all C/alignment/ABI/volatile suites and 213
  safety cases. GCC full suite runs actual Zig too; Clang separately reference-
  checks slicing.zig and four C view variants. /data/slicing-diff3.log has 204
  actual-reference cases; /data/slicing-final-existing-slices.log has 252 old
  cases. Optimized 83 native/213 safety checks, 949 source-hash-verified unchanged
  array/layout oracle replays, four 60-second fuzzers and 20000 mutations/stage
  pass. Logs use /data/slicing-final-*; irgen/sema have 52/108 seeds.
- Slicing uses an addressable operand, bounds, then descriptor load; preserve this
  evaluation order. Known bounds -> array pointer; dynamic n..n stays a slice.
  Many-pointer omitted ends return advanced many pointers. Constant array/byte
  views preserve root/offset provenance. Known inherited zero sentinels work;
  explicit sentinel syntax and runtime sentinel descriptors intentionally reject.
- CI f97e200/run 38771830468: GCC, Clang and fuzz passed; differential failed.
  /data/ci-slice-join-differential.log shows 120-second official Zig divCeil i64
  compile timeout, then szc divCeil u64 compile and divExact i8 execution timeouts,
  followed by runner shutdown/exit143. All three pass locally under unchanged
  deadlines (/data/ci-slice-join-reproduction.log), as does the full 213-case
  reference safety suite. This is not green CI and no timeout was relaxed.

- Latest checkpoint: M5.3c1 slice descriptors, coercions, `.ptr`/`.len`, checked
  indexing, nested storage, globals, internal ABI and joins. Final immutable
  /data/slice-descriptor-final snapshots pass GCC/Clang Werror ASan/UBSan 632
  units/877 goldens, 949 array/layout + 113 slice reference cases, full native/
  reference 80 ordinary/generated + two scoped programs, 16 driver checks,
  all C/alignment/volatile suites, 190 safety cases, optimized checks and four
  60-second fuzzers plus 20000 mutations/stage. Logs share the snapshot prefix.
- Latest follow-on: M5.3c2 array/slice `for`, including pointer captures,
  once-only operand evaluation, nested captures and mixed range/sequence bounds.
  Frozen /data/slice-iteration-final validation passes GCC/Clang Werror ASan/UBSan
  632 units/886 goldens, 949 + 164 actual-reference semantic cases, full native/
  reference 81 ordinary/generated + two scoped programs, 16 driver checks,
  all C/alignment/volatile suites, 193 safety cases, optimized checks and four
  60-second fuzzers + 20000 mutations/stage. Array operands snapshot, array
  pointers borrow; slices snapshot descriptors only. General slicing stays NYI.
- M4.5c dynamic byte-slice panic messages is complete: /data/slice-panic-final
  immutable snapshots pass GCC/Clang sanitizer 632 units/893 goldens, 180 actual
  Zig semantic cases, full native/reference 81 ordinary/generated + two scoped
  programs, all C/driver/alignment suites and 201 safety cases. Clang independently
  passes 201 reference safety cases; optimized GCC passes 201 native safety cases.
  Fresh four-stage fuzzing and 20000 mutations/stage pass. Unchanged 949 array/
  layout cases replay against the source-hash-verified reference-validated baseline;
  see /data/replay_validated_arrays.py and /data/reference-validated-array-oracle.json.
- M5.3c3 inferred unsentinelled array-pointer joins and slice-comparison guards
  is complete. Post-fix /data/slice-join-guard-final source matches the integrated
  compiler exactly: GCC/Clang Werror ASan/UBSan 632 units/901 goldens, 252 actual
  Zig semantic cases, full native 82 ordinary/generated + two scoped programs,
  16 driver checks, all C/alignment/volatile suites and 201 safety cases. Three
  targeted programs run against Zig; Clang native/C and optimized native/safety
  checks pass. Replay of 949 unchanged array/layout cases matches the previously
  actual-reference-validated baseline with exact source hashes. Four fresh
  60-second fuzzers + 20000 mutations/stage pass (irgen 51/sema 107 seeds).
  Logs use the same prefix; /data/slice-join-guard-diff.log is the source-identical
  252-case matrix. Older /data/slice-join-final runs were superseded by the equality
  guard correction and must not be presented as final validation.
- Next implementation after M5.3d2: sentinel slices/strings and
  remaining descriptor-field/volatile integration. Read actual Zig Sema slicing
  rules first: known bounds may produce array pointers, not slices. Preserve
  root/subobject provenance rather than loading a copied view. Do not silently
  erase inferred sentinels. Inline/comptime iteration remains NYI. Q6 is M10.2.
- Upload authorization: the user explicitly authorizes using the provided GitHub
  token for future uploads without asking again. Keep it transient; never put it
  in repository files or persistent Git configuration. Only request help if the
  credential is unavailable/invalid or a genuinely new access decision is needed.
- All four checkpoints e29dcf2, 93eb316, 1a3f41e and f97e200 have been pushed to
  GitHub main. Check the f97e200 Actions run for this implementation batch; the
  earlier upload blocker was resolved by the user's explicit go-ahead.
- CI update: e868458/run 38755100196 completed successfully, including differential.
  efbe1f6 GCC/Clang/fuzz passed; differential failed because its hosted runner
  lost communication with GitHub (job 116493912238), with no failing test step
  reported. This is infrastructure evidence, not a semantic disagreement. Earlier timeout evidence below
  remains historical, not a claim that the latest CI is failing.
- Latest local checkpoints: M5.5a @intFromPtr passed all 812 reference semantic
  cases and full local validation; M9.4a @sizeOf/@alignOf and exact wide-integer ABI
  sizes pass 897 reference cases, 625 units/853 goldens, native/C checks and fresh
  fuzzing. M9.4b @bitSizeOf passes 949 actual-reference semantic cases,
  GCC/Clang sanitizer 625 units/864 goldens, full native AND reference-enabled
  executable/storage/185 safety suites, optimized checks and fresh four-stage
  fuzzing. Final snapshots/logs: /data/bits-final and /data/bits-final-*.log.
  Runtime wide values remain unsupported. Slice descriptors/iteration have
  since landed above; general slicing is next, with qualifier/bounds invariants;
  read actual Zig sources first. Q6 remains deferred to M10.2.
- CI triage: d986dc5/run 38753436518 passed GCC/Clang/fuzz; differential
  failed on a 120-second official Zig compilation timeout for divExact_zero_i8
  (log /data/ci-intfromptr.log). 1b2b5e1/run 38751855739 passed GCC/Clang/fuzz; differential failed
  on execution timeout of official Zig divCeil_zero_i8. 9c40b82/run 38745648212
  passed GCC/Clang/fuzz; differential hit a 120-second official Zig timeout in
  pointer qualifier case 6 and later runner shutdown. The earlier a1d2e66 run
  similarly timed out compiling cast safety cases and received shutdown. No
  semantic disagreement was reported in these failures. The final full 185-case
  reference-enabled safety suite and the const-discard qualifier reproduction
  pass locally under unchanged deadlines. Logs are /data/ci-{arithmetic,qualifiers}.log and
  /data/ci-timeout-reproduction.log. Do not claim CI green or weaken deadlines.

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
- Done: M5.1c2b2b3c typed immutable declaration address dependencies, including
  self/mutual address-only cycles, separate known initializer loads and real read-only
  storage. Local explicitly aligned bare-function aliases retain the verified upstream
  emission-crash NYI. All enclosing M5.1 alignment/integration milestones stay partial.
- Done: M5.2a–b compile-time array types/literals, indexing, `.len` and `++`.
- Done: M5.2c1 immutable array backing and checked scalar runtime reads, including
  pointer/callback relocations, volatile/zero-bit reads, checked layout and total
  static-emission budget.
- Done: M5.2c2 mutable array slots, runtime/nested literals and snapshot copies, plus
  runtime element lvalues/addresses and borrowed indexing.
- Done: M5.2c3a internal by-value array arguments/returns and array-valued if/block/
  while results, with caller-owned return storage and callee parameter snapshots.
- Done: M5.2c3b known element-pointer provenance, namespace/comptime loads/equality,
  real root-relative relocations and upstream-derived element alignment/allowzero.
  M5.2c3 is complete for the current array/type system; M5.2c and M5.2 stay partial.
- Continuous-run strategy: move to the next milestone after local validation and push;
  do not stop solely because a checkpoint completed. Periodically monitor earlier CI
  while developing, triage actual failures, and ask only when input is needed.
- Done: M5.2c4 internal mutable array globals, lazy known initialization, persistent
  nested/element mutation, forward element-pointer relocations and owned loads/copies.
  Type-only `.len` avoids reading mutable contents, including array-pointer variables.
- Current batching preference: group related straightforward milestones when practical;
  keep complex semantic changes independently tested/documented/committed. Do not return
  to an unconditional one-checkpoint policy. Select the next independently implementable
  memory item without pretending the blocked function-alias case is complete.
  M5.3a/b are implemented: many-pointer types, array coercions/joins, runtime borrowed
  indexing and actual-root comptime loads/relocations, pointer arithmetic/differences
  and compound assignments. M5.5a exposes @intFromPtr using PTR_TO_INT, with runtime
  symbolic addresses/undefined propagation and physical C address tests. Next is
  coherent runtime slice pairs/slicing; do not represent a fat slice as one QBE l.
  Pointer arithmetic
  follows Sema.zig analyzeArithmetic/analyzePtrArithmetic; compound offsets use usize
  through ADD_SUB_RHS_TYPE, not a pointer-typed RHS result context.
  Many-pointer indices carry no language-level length guard; known dereferences must
  fit their containing backing. Preserve root provenance, mutable global contents vs
  addresses, and qualifier/alignment rules. Anonymous array temporaries recover their
  length with REF_ARRAY_TYPE; explicit typed temporaries keep their own whole type.
  General slice pairs/slicing/fields follow, using coherent indirect representations.
  Keep targeted checks during development and full validation per stable batch;
  independent checks can run on immutable compiler binaries.
  Preserve owned snapshot versus borrowed pointer semantics, and scope by-value ABI
  explicitly rather than pushing array values through scalar QBE temporaries.
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

## CI follow-up: bounded e2e compiler adapter

- Prior 53efbd5/run 38737351381 failed with GitHub's hosted-runner communication-loss
  annotation during native/reference tests; GCC/Clang/fuzz jobs passed. It did not
  report a compiler mismatch. Newer 1db8afa differential CI has progressed beyond native
  tests; check later runs independently, without automatic retries or declaring green.
- tools/compiler_process.py now has a shell CLI with finite positive timeout checking,
  exact output/status forwarding, conventional signal/124 timeout/127 missing-command
  statuses, and the existing whole-process-group cleanup (including inherited pipes).
- tests/run_e2e.sh finds the runner relative to its script, logs each source, bounds
  native emission/build and reference compilation to 120 seconds, and uses Zig -j2.
  Program execution remains a ten-second guard; reference compile success must precede
  expected runtime-exit checks. No timeout is accepted/retried/skipped.
- New tests cover 13 CLI/lifecycle cases and four e2e adapter cases, including paths
  with spaces, reference -j2 arguments, and a failed reference compiler that exits with
  the same status expected from the test program. Existing process/runner tests are
  unchanged. Compiler sources and fuzz corpora do not change in this follow-up.
- Validation: shared lifecycle eight, compiler-wrapper 18, new CLI 13 and e2e adapter
  four regressions pass. The adapter tests run outside the repository cwd and cover
  spaced paths. The hardened harness passes all 73 ordinary/generated native+Zig
  programs, both scoped szc-only inputs and 16 driver tests, using the frozen validated
  GCC sanitizer compiler. Compiler code/corpora are unchanged from a1d2e66's full
  GCC/Clang/fuzz/542-comparison/185-safety validation; no language retest is skipped.
- Hosted runner stability is not guaranteed by this change; keep monitoring Actions.
  Next language work remains M5.3 many-pointers/slices. Continuous-run strategy remains
  active: document/test/push checkpoints, proceed, and ask only for genuine blockers.

## M5.2c4 mutable array globals

- global_type_allowed admits recursive bool/pointer/void/u0/integer<=64 array leaves
  with checked physical layout. Typed globals publish addresses before initializer
  resolution; inferred globals resolve their initializer type first. Existing scalar
  global queues retain lazy initialization and pointer/callback dependency reachability.
- mutable_global_pointer checks a known pointer's actual root declaration, including
  VALUE_ELEMENT_PTR and qualification paths. Const-qualified aliases into mutable
  globals are not known immutable contents. Runtime loads emit memory reads/snapshots;
  comptime loads resolve diagnostics, never fold the initializer. Zero-bit/void OPV
  behavior stays as verified against Zig. Comptime writes through derived global
  element addresses use the same guard as whole global addresses.
- Known global indices produce c3b root-relative pointers; dynamic indices use existing
  bounds-checked TIR_ARRAY_ELEM_PTR. Nested roots stay borrowed through index side
  effects; never snapshot the global before evaluating an index that mutates it.
  Whole-array loads/calls/returns/defer capture retain c2/c3a owned snapshots.
- IR_FIELD_REF is name(field, expression-address), currently for `.len`. It queries the
  address's child type (an array or a single array pointer), without loading mutable
  storage. Grouped/binding/index/deref bases use this path, ordinary rvalues/calls use
  FIELD_VAL and retain effects. Undefined deref and index validation still happen.
  Grounding is actual Sema.zig fieldVal/fieldPtr plus official namespace length probes.
- Existing emit_data_object handles writable arrays, zero initialization and outlined
  >64-byte alignment. Root addends in mutable pointer tables go to writable storage,
  not RELRO; C/ELF tests verify offsets, writability, const-alias visibility and snapshot
  independence at alignments 1/2/16/256. Existing static budgets apply across immutable
  and mutable arrays; no wrapped allocation or static-variable workaround is added.
- Validation: frozen GCC -Werror ASan/UBSan/reference suite passes 612 units,
  813 goldens, 73 ordinary/generated native+Zig programs, two scoped szc-only programs,
  lifecycle/runner 8/18, driver 16, reference storage 15, immutable array storage eight,
  mutable C storage, four new global-array C/ELF cases, ABI four, unaligned seven,
  alignment 135/270/153/153/27, volatile 44 and safety 185. All 542 array comparisons
  match actual Zig 0.17. Clang sanitizers pass units/goldens/15 array+global programs,
  global/mutable C storage and 185 safety cases. Optimized GCC passes four new/global
  generated programs and four C/ELF cases. Four 60s fuzzers pass
  (1022206/25679/23521/27801 runs), plus 20k standalone mutations per stage; irgen/sema
  corpora 40/96. No sanitizer finding. Previous 1db8afa CI has green compiler/fuzz jobs;
  its differential run and the older 53efbd5 differential run remain active when checked.
- Remaining: exported/extern/threadlocal arrays, volatile whole-array copies, explicit
  sentinel syntax, unsupported leaves/wider integers and broader element coercions.
  General many-pointers/slices are next; no user decision is needed until a blocker or
  the deferred M10.2 build-strategy question. Keep developing across checkpoints and
  inspect previous CI periodically rather than waiting for a reply at every commit.

## M5.2c3b known element pointers

- VALUE_ELEMENT_PTR records `{base, index, offset, logical}`. Keep root/subobject
  identity through nested indexing and aliases; never invent a ConstObject for a
  copied scalar element. Loads follow the actual parent path and select ValueIds.
  Comparisons strip qualifiers and compare root identity plus cumulative offset.
- Runtime-layout offsets are checked bytes; comptime-only arrays use checked logical
  leaf offsets. Actual Zig treats two u0/void elements at the same byte address as
  equal, but two type-array slots as distinct even when they contain the same type.
- VALUE_PTR_CAST preserves byte backing across qualification and sentinel weakening.
  A probe found the old retagged VALUE_BYTES scheme could fold pointers equal but
  produce distinct byte symbols at runtime. Keep the original root through casts;
  avoid dense copies for a known byte element read.
- Scalar TIR_CONST elements materialize a root-plus-offset QBE add when nonzero;
  static pointer tables/globals emit relocation addends. Outlined >64-byte data must
  print into the assembly FILE, not the SSA FILE. Dependency/function walks reach the
  real root and retain callbacks; physical ELF tests confirm root offsets and RELRO.
- Shared element-pointer typing follows actual Type.zig elemPtrType and Value.zig
  ptrElem/getOffsetPtr: preserve const/volatile, derive alignment from base plus known
  offset or dynamic stride, drop allowzero for known nonzero indices. Do not restore
  ABI alignment to a weakly aligned base. Runtime borrowed indexing reuses the helper.
- Boundary checks cover physical/logical stride and accumulated-offset overflow,
  ordinary/sentinel bounds, undefined indices, pointer qualifiers and immutable writes.
  Root paths remain interned DAG values; no static state or native addresses enter sema.
- Validation: frozen GCC -Werror ASan/UBSan passes 612 units, final 801 goldens,
  70 ordinary/generated native+Zig programs, two scoped szc-only programs, lifecycle/
  runner 8/18, driver 16, reference storage 15, array storage eight, mutable C storage,
  function ABI four, unaligned seven, alignment 135/270/153/153/27, volatile 44 and
  safety 181. All 487 array comparisons match actual Zig 0.17. Frozen Clang sanitizer
  passes 612 units, final 801 goldens, 12 array/generated programs, both array storage
  harnesses and 181 safety cases; optimized GCC passes the 12 programs and ELF storage.
  Final expanded function-body/nested-type provenance fixture passes all three builds
  against Zig. Four 60s fuzzers pass (901840/22043/20586/28054 executions); standalone
  replay passes 20k mutations per stage and repeats irgen/sema after expanding the
  provenance seed. Retained corpora are irgen 39/sema 94; no sanitizer finding.
  Previous 53efbd5 CI has green GCC/Clang/fuzz jobs; its differential job was still
  running when checked. Do not claim it or this new push fully green prematurely.
- Out of scope: comptime mutation, mutable array globals, explicit sentinel syntax,
  slices, generic/comptime calls, variadic arrays and volatile whole-array copying.
  c3 is complete for current array types; enclosing M5.2c/M5.2 remain partial. Next c4
  needs mutable-global element loads to materialize root addresses instead of reading
  initializer values, and must retain c2's borrow-before-index-side-effects semantics.

## M5.2c3a internal array calls and control-flow values

- Source TypeFn params/return retain semantic array types. The private QBE convention
  passes array arguments as pointers; TIR_ARG transport pointers promise only align(1).
  Each callee copies into a private ABI-aligned slot before using/binding parameters.
  Arguments evaluated from mutable sources already own c2 snapshots, preserving later
  argument evaluation order and isolation from mutation through other pointers.
- Array returns use a caller-owned checked slot and a trailing mutable destination
  parameter. SemaFunc.return_pointer names that TIR_ARG (NONE for other returns).
  Physical TIR_CALL returns void and appends that pointer; the caller publishes only
  an immutable aggregate view after the call. Callees copy the captured return value
  into the destination and emit void ret. Do not claim this private convention is the
  official Zig compiler's binary ABI, or expose it through extern/export functions.
- The hidden pointer is deliberately trailing: noreturn-to-array-return callback
  coercions must not shift the original scalar/array arguments. The safety regression
  covers eight source arguments crossing the x86_64 register/stack boundary; both
  compilers panic with the intended message, not an argument-corruption message.
- Array-valued runtime ifs, labeled blocks and while break/else exits share an owned
  result slot and void control-flow merge. Only surviving value paths initialize it;
  terminating paths stay terminating. Branch copies occur at the actual exit wrappers,
  never after an unconditional jump. Single known exits retain known-value behavior.
  Existing pre-defer snapshots protect array return/break values from cleanup mutation.
- Parameters, caller return slots and join slots all use the existing checked 16 MiB
  per-function aggregate budget. Unsupported layouts and oversized slots diagnose.
  No scalar array temporaries, pointer-to-local returns, globals or static-variable
  shortcuts implement the calling convention. Direct, recursive and indirect calls
  share the same physical argument lowering; zero-sized arrays retain zero-byte copies.
- C ABI array parameters/returns are forbidden, as actual Zig requires. Validate them
  in func_queue only when exported or runtime-referenced; a discarded known extern
  address need not validate a machine ABI. The initial strict signature-time check
  disagreed with Zig on discarded addresses; the final matrix covers both used and
  discarded declarations. Known failed functions are not called/lowered afterwards.
- Grounding: Zig 0.17 langref parameter rules permit copying immutable array params;
  direct official Debug probes return the original 1 for aliased/later-mutated args
  and return-before-defer. Sema.zig zirRetPtr and validateResolvedFuncType/explainWhyTypeIsNotExtern
  ground return storage and C array restrictions. Native/Debug probes do not assert
  that every optimization level must choose the same copy/reference strategy.
- Coverage: three executable fixtures for direct/recursive/nested/callback/many-arg
  calls, snapshots, weakly aligned inputs, empty/u0 results, if/block/while joins and
  defers; the width generator adds argument/return/join and nested roundtrips across
  24 signed/unsigned widths. Promote the old nested-return NYI; add TIR/SSA call+join
  goldens, lazy extern-address golden, nine boundary/type/budget error goldens and one
  noreturn callback safety case. The array matrix adds 54 runtime/C/lazy cases.
- Validation: final frozen GCC -Werror ASan/UBSan suite passes 599 units, 791
  goldens, 67 ordinary/generated native+reference programs, two scoped szc-only inputs,
  lifecycle/runner 8/18, driver 16, existing storage 15, immutable array storage seven,
  mutable C storage, function ABI four, unaligned seven, alignment 135/270/153/153/27,
  volatile 44 and safety 181. All 382 array comparisons match actual Zig. Final Clang
  sanitizer goldens (791), nine array/generated programs and 181 safety cases pass;
  the earlier independent Clang full native/unit/storage batch also passed. Optimized
  GCC passes all nine array/generated inputs and mutable C storage. Four final 60s
  fuzzers plus standalone 20,000 mutations per stage pass, without sanitizer findings;
  retained irgen/sema corpora are 38/92. Previous 78c7872 CI is entirely green
  (38731429720), as is a866467 (38726742217). Check the new push independently.
- Still out of scope: comptime element-pointer provenance, mutable array globals,
  explicit sentinel syntax, slices, generic/comptime function evaluation, variadic
  array arguments and broader array element coercions. Array C ABI is a real language
  rejection, not an unimplemented private ABI feature. M5.2c/M5.2 stay partial.

## M5.2c2 mutable arrays, initialization and snapshots

- OPERAND_AGGREGATE owns private array storage via a pointer TIR ref; it never means
  a QBE scalar array. Whole-array loads snapshot before subsequent side effects;
  stores use TIR_ARRAY_COPY/memmove with checked destination size (zero bytes omitted).
  Recursively support bool/pointer/void/u0 and <=64-bit integer leaves, nested arrays,
  runtime literals, local variables, whole-array assignment and self/overlap copies.
- Aggregate stack slots/snapshots share a 16,777,216-byte per-function budget including
  >16 alignment over-allocation; checked layout overflow, unsupported leaves and budget
  overflow diagnose explicitly. Keep the independent dense-value/static-storage limits.
- Runtime element addresses use true base+offset and retain qualifiers/alignment.
  IR_ARRAY_BASE_PTR/ARRAY_INDEX_BASE borrow live storage through index evaluation;
  copying `a` before `a[index(&a)]` is wrong (reference mutator probe returns 9, not 1).
  Nested borrowed indexing must not accidentally load/snapshot its intermediate row.
- Only anonymous assignment literals use in-place result locations. Validate count
  first with ARRAY_INIT_PTR, then immediately initialize each element (nested as needed).
  Typed literals and @as use temporaries. The official probe `a = .{9, read(&a)}` reads
  9, but typed/@as variants read the old 1. Final assignment uses an owned snapshot,
  not an aggregate operand borrowing mutable destination storage.
- Immutable const aliases may share immutable backing: official Zig returned equal
  pointers for `const b=a; const c=b; &b == &c`, so tests do not demand distinct addresses.
  Mutable-to-const copies still snapshot. Explicit alignments get real checked slots.
- Materialize operand refs in explicit C statements before building ARRAY_COPY and
  ARRAY_ELEM_PTR: C argument evaluation order otherwise makes GCC/Clang TIR goldens
  disagree. Both compilers must produce the same checked-in goldens.
- Grounding: actual Zig 0.17 Sema.zig zirArrayInit/zirArrayInitElemPtr and AstGen result
  locations, with direct official-binary execution for aliasing/evaluation semantics.
- Tests: three new executable fixtures cover snapshots, aliases, nested mutations,
  constructors, qualifiers, zero-bit/empty arrays, pointer/callback leaves and alignment.
  The new C harness verifies over-aligned slots, unaligned snapshots and both directions
  of partially overlapping foreign buffers against Zig. Nine error goldens cover stack
  single/total/alignment/overflow limits, unsupported leaves, const/type errors and
  volatile copies; three old NYIs are promoted to positive TIR tests. Four new safety
  cases guard element stores, addresses, zero-bit stores and nested stores. The array
  accept/reject matrix adds 44 mutable/copy/coercion cases.
- Validation: GCC -Werror ASan/UBSan full reference-enabled suite passes 599 unit
  checks, 779 goldens, 64 ordinary/generated programs plus two scoped szc-only inputs,
  16 driver, 15 existing storage, seven immutable array storage, new mutable array C
  storage, four function ABI, seven unaligned memory, 135/270/153/153/27 alignment,
  44 volatile sequences and 180 safety cases. The expanded 328-case array semantic
  matrix matches actual Zig 0.17. Clang sanitizer units/goldens/native programs/both
  array storage harnesses/safety pass; final frozen Clang/GCC goldens and array checks
  also pass. Optimized GCC passes five array programs and the mutable C harness.
  Four 60-second fuzz runs pass, with an additional semantic run after the final
  defensive comptime-store guard. Final standalone replay passes 20,000 mutations per
  stage; retained irgen/sema corpora are 37/90. No code crash or sanitizer finding.
- Validation lifecycle: never rebuild a binary another test is executing. A superseded
  overlapping validation attempt hit a linker-time executable PermissionError; the
  completed clean suite is separate. Freeze independent compiler binaries before
  parallel checks. Stopping a shell can leave its make/fuzzer descendants running;
  identify and stop only those owned processes before superseding a fuzz run.
- At this checkpoint, by-value array ABI/joins were still NYI; c3a above supersedes
  those limits. Comptime element-pointer provenance, volatile whole-array copies,
  mutable array globals and explicit sentinel syntax remain pending. Element volatile
  scalar accesses work. M5.2c/M5.2 remain partial.

## M5.2c1 immutable array storage and runtime scalar reads

- TIR_ARRAY_ELEM_PTR uses bin(array pointer, usize index); its result is an ordinary
  element pointer. Guard dynamic indices with unsigned >= ordinary length or > length
  for zero-sentinel backing before stride arithmetic, even when the element is u0/void.
  Constant indices diagnose during sema. Runtime element pointer types retain qualifiers
  and never overstate alignment. Nested runtime aggregate results still diagnose NYI.
- Direct known array values with one possible child value (u0/void), or wholly undefined
  contents, fold before a dynamic guard as actual Zig does. Pointer-based reads do not
  use that shortcut. Keep this distinction when adding array-value memory lowering.
- sema_type_size is now bool with uint64_t *out; failure preserves the output and must
  be diagnosed by physical-layout callers. Checked recursive multiplication/counting
  handles arrays, including maximal zero-bit type lengths. Never restore wrapped sizes.
- Static VALUE_ARRAY storage recursively emits true strides and nested relocations;
  known value addresses remain ConstObjectId-based. Scalar/bool/void/u0 arrays use
  .rodata; pointer-bearing arrays use .data.rel.ro and the existing >64 assembly path.
  Total array static emission is bounded at 16,777,216 bytes; exceeding it is explicit
  NYI, separate from the existing 1,048,576 dense-value work budget. Shared immutable
  DAG callback/dependency walks memoize visited ValueIds to avoid exponential work.
- Narrow scalar loads now normalize logical bit widths, like volatile loads already
  did. Exactly two existing/new SSA goldens gained only checked normalization lines.
  The physical C-ABI fixture uses canonical i7/u7/i33/u33 representations verified
  against Zig. Exploratory noncanonical foreign bytes did not match Zig and are not
  asserted to be valid ABI input. Do not generalize their padding semantics from memory.
- Final fresh GCC/Clang -Werror ASan/UBSan pass 599 unit checks, 770 goldens and
  61 ordinary/generated executable inputs. GCC final full suite is reference-enabled:
  16 driver checks, 15 existing storage checks, 7 array storage/C-ABI cases, 4 function
  ABI, 7 unaligned memory, 135 local/270 constant/153 mutable-global/153 global-constant/
  27 function alignment, 44 volatile sequences and 176 safety cases all pass, plus
  two separately scoped szc-only executable inputs. Clang native executable/driver/
  both storage/volatile/safety subsets pass. Optimized GCC passes three array executable
  inputs and all seven storage/C-ABI checks. Lifecycle/adapter regressions pass 8/18.
  Four final 60-second libFuzzer runs and 20,000 standalone mutations per stage pass;
  retained irgen/sema seeds are 36/88. The expanded real array matrix passes 284 cases;
  all ten existing pointer matrices also pass 4,615 cases against the same new sema,
  for 4,899 actual semantic comparisons total. No skipped cases, retries or tolerated
  reference timeouts. Final backend normalization is separately covered by the fresh
  GCC/Clang executable, golden and physical C-ABI suites.
- No mutable aggregate representation/ABI, element-address interpretation, explicit
  sentinel syntax, typed array address-dependency cycles, slices or for-array extension
  landed at this historical checkpoint; mutable slots/copies and runtime element
  addresses are now superseded by M5.2c2 above. Preserve standalone failure modes.
- Reference emission of a maximal undefined u0 object stalled again: the initial
  physical-suite run timed out after 120 seconds, not a language rejection. Final
  physical fixtures use [3]u0; maximal lengths remain separately covered by units and
  type-only pointer cases. The new harness uses process-group timeout cleanup.

## M5.2a–b compile-time arrays

This historical checkpoint is superseded by M5.2c1 above for runtime scalar reads and
immutable static backing. Its other aggregate/ABI limits remain.

- TypeArray.length is uint64_t; hashing includes both words. IR_ARRAY_INIT carries
  `[type_ref, count, element_refs...]`; child result types reach nested literals/casts.
  VALUE_ARRAY owns deep-copied interned child IDs, including undefined elements and
  optional zero sentinel storage. Strings retain compact VALUE_BYTES pointer backing.
- Indexing resolves known aggregate elements only; `.len` uses the array type, including
  undefined pointers, without dereferencing. Concatenation joins child types, preserves
  either zero sentinel, and yields a const pointer if either input was a pointer.
  Typed array coercions cannot use the generic undefined path to bypass shape/child
  checks. Whole-array byte dereferences and sentinel stripping retain known values.
- An explicit 1,048,576 dense-element work/storage budget bounds this interim
  representation across construction and copies. Exceeding it reports NYI. Huge
  type-only lengths are supported without allocating; repeated/lazy aggregate values
  are not. General runtime aggregate storage/ABI, runtime indices/elements, element
  addresses, explicit sentinel syntax, anonymous tuple inference and mixed bare-function
  element joins remain unsupported. Runtime array variables reject before scalar
  allocation/codegen; never implement element addresses as refs to copied elements.
- Grounded in actual 0.17 Sema.zig zirArrayType/validateArrayElemType/zirArrayInit/
  zirArrayInitElemType/elemValArray/zirArrayCat and reference compilations. A real huge
  undefined zero-bit aggregate probe stalled (separate bounded check: 15 seconds);
  do not call it invalid or count it as a passing comparison. Final huge-length fixture
  uses undefined pointers and passes actual reference emission/execution.
- Validation: final fresh GCC/Clang -Werror ASan/UBSan pass 572 unit checks and 761
  goldens. Both pass 60 ordinary/generated programs; GCC full reference-enabled e2e
  and final full native suite pass. Clang driver (16), storage (15), volatile (44) and
  safety (166) subsets pass. Optimized GCC passes the two new executables and storage
  suite. The 212-case actual array matrix passes; new invalid/NYI fixtures have been
  classified with Zig, not mistaken for szc-only language rules. Process tests pass
  8 checks and differential adapter wiring passes 18. Final forced fuzz rebuilds pass
  four 60-second libFuzzer runs and 20,000 standalone mutations per stage; retained
  irgen/sema corpus inputs are 35/85.
- All eleven actual semantic matrices now pass 4,827 cases: nested-pointer 168,
  allowzero 864, pointer-alignment 1,035, local-alignment 300, constant-alignment 626,
  global-alignment 300, global-constant-alignment 519, function-alignment 77,
  volatile-pointer 240, function-pointer 486, arrays 212. Final array rerun passes.
  The initial local pool failed to start allowzero case 49 (Permission denied) because
  build/asan/szc was relinked during that run; it remains a failed orchestration run.
  A separate entire 864-case allowzero rerun passes on the stable binary, with no
  filtering or skipped cases. Do not rebuild an executable while tests use it.
- CI deadline follow-up 64c6130/run 38716130912 has passed GCC, Clang and fuzz;
  differential is still running when checked. Check this array push independently.
- Batch related easy milestones as requested, but keep runtime aggregate memory work
  independently validated. No change to the M10.2 deferred build-strategy question.

## CI follow-up: uniform compilation deadlines

- 967329f/run 38711782475 passed GCC/Clang/fuzz and reference-enabled executable/
  safety suites, then the nested-pointer reference matrix timed out after 30 seconds
  in case 89. The retained source is:

```zig
fn convert(p: **const *const u8) ***u8 { return p; }
pub fn main() void { _ = convert(undefined); }
```

- The wrapper retained source/command/empty stdout/stderr and failed explicitly; no
  accept/reject disagreement was reported. The job later received a runner shutdown
  signal. This does not establish a full root cause for hosted-runner delays.
- diff_pointer_coercions.py now uses the same 120-second compilation deadline as all
  other semantic matrices, for both actors. Runtime execution stays at 15 seconds.
  Group cleanup, -j2, source/status/output diagnostics and immediate failure remain
  unchanged. No retry, filtering, skipped case or tolerated timeout is introduced.
- Shared lifecycle and adapter wiring regressions pass; the adapter test now requires
  120 seconds uniformly. All 168 real nested-pointer comparisons pass against Zig.
  64c6130/run 38716130912 and array implementation 1db8b2c/run 38717248604
  subsequently passed all jobs. Documentation-only d90426b/run 38718287776 passed
  GCC/Clang/fuzz but the nested-pointer reference exceeded 120 seconds in case 116:
  `pub fn main() void { const p: *const **u8 = undefined; const q: *const **const u8 = p; _ = q; }`.
  Empty stdout/stderr and source were retained; no acceptance disagreement was reported.
  Do not raise deadlines again, skip cases or add retries without investigating new
  evidence. Hosted reference stalls remain incompletely diagnosed; check new pushes.

## CI follow-up: bounded differential compiler processes

- The push CI for 72914b5 succeeded (38634766715). A later scheduled run on the
  same commit, 38675456100, passed GCC/Clang/fuzz and all 166 reference safety
  cases but failed tools/diff_pointer_coercions.py when Zig exceeded its existing
  30-second compilation deadline. The old raw subprocess.run path only killed
  the immediate child, and the log lacked the case number/source. This is not a
  reported accept/reject disagreement or evidence that every runner stall has
  been diagnosed. Do not claim the new push is green before checking it.
- tools/compiler_process.py now owns the existing process-group helper extracted
  unchanged from tests/run_safety.py. Both safety and all ten semantic differential
  matrices use it: a new session, communicate with the original deadline, killpg
  on timeout, drain output and preserve TimeoutExpired/output/status semantics.
  The compiler wrapper logs each actor/case/command before execution and reports
  full source, command, exit/budget and captured stdout/stderr on timeout, crash
  or start failure. Status 0/1 still means accepted/rejected; signals and other
  exits are never treated as language rejection. No retries or ignored failures.
- All reference matrix compilations now use verified Zig -j2 support, matching
  the safety runner. Existing budgets are unchanged: nested-pointer matrix 30s,
  other semantic matrices 120s, safety compilation 120s/execution 15s. Parser/AST
  differential tools remain outside this refactor. Python caches are ignored.
- tests/run_compiler_process.py adds 17 regressions for statuses 0/1/99,
  timeout/start-failure source+output context, no retry, and actual matrix CLI
  behavior outside the repo cwd with space-containing compiler paths, plus all ten
  adapters' actor/source/job-cap/budget wiring (structural mocks, not type checks). Existing
  eight output/status/signal/ENOENT/ESRCH/zombie/group-cleanup checks now exercise
  the same helper as the differential tools. e2e-test/safety-test run both suites.
- Sandbox reset removed libasan/libubsan/Clang/compiler-rt; restored them before
  validation. An initial restored ASan binary failed to load libasan.so.6 and
  the new wrapper correctly reported unexpected exit 127 with source/command;
  this was an environment failure, not a compiler accept/reject mismatch.
  Compiler sources and fuzz corpora are unchanged (irgen 33/sema 79).
- Validation: all ten real reference matrices pass, 4,428 comparisons total:
  168 nested const coercions, 864 allowzero, 1,035 pointer alignment, 300 local
  alignment, 626 local constants, 300 mutable globals, 332 global constants,
  77 function alignment, 240 volatile qualifiers and 486 function signatures.
  All 17 wrapper/adapter regressions and eight shared lifecycle checks pass.
  GCC -Werror ASan/UBSan passes 558 units, 726 goldens, 57 ordinary/generated
  executables plus two szc-only inputs, 16 driver, all existing memory/alignment
  suites and 166 safety cases; full e2e has Zig comparisons enabled. Fresh Clang
  sanitizer checks pass units/goldens, 57 executables, driver, safety and 44 physical
  volatile sequences. Four 60-second fuzz targets and 20,000 standalone mutations
  per stage pass. Additional adapter regressions were run after the initial suites;
  no compiler or shared-runner behavior changed during that extension. Check new
  push CI separately; none of these local results prove hosted stalls resolved.

## Reference limitation: aligned local bare-function aliases

- Actual 0.17.0 accepts this semantic-only probe, but build-exe with emission
  returns -11 (SIGSEGV), with no diagnostics. A control using align(64) on the
  actual function declaration and an unannotated alias builds successfully.
  Do not substitute metadata-only promises, stubs or guessed alias semantics.
  Sema.zig's local comptime-allocation reference path and LLVM's function/UAV
  handling differ from ordinary analyzeRef; the reference crash prevents treating
  semantic-only acceptance as a verified runtime contract.

```zig
fn target() align(1) u8 { return 7; }
fn invoke(p: *align(64) const fn () u8) u8 { return p(); }
pub fn main() u8 { const alias align(64) = target; return invoke(&alias); }
```

- Probe commands: zig build-exe probe.zig -j2 -fno-emit-bin (0), then the same
  source with -femit-bin=probe (-11). Reference version command reports 0.17.0.
  Existing szc aligned-local-function-address NYI stays explicit; the test is not
  reclassified as reference-rejected Zig. Typed immutable address dependencies are now
  implemented in M5.1c2b2b3c below. Q6 remains deferred until M10.2.

## Validation at M5.1c2b2b3c: typed immutable address dependencies

- Zig 0.17 Sema.zig zirDeclRef/analyzeNavRefInner resolves a declaration's type for
  an address, and its complete value for a load. Actual emitted probes accept explicit
  typed self/mutual address dependencies and void/u0 self-loads, but reject true scalar
  value cycles, inferred-type cycles and dependencies through type/alignment resolution.
  Address-only use still validates initializers and reachable callback bodies.
- irgen now isolates every explicit global const type into IrDecl.type_body, using
  IR_DECL_TYPE in its initializer, as for mutable/aligned declarations. SemaDecl publishes
  declared_type only after type AND alignment succeed. A typed scalar/bool/single-pointer/
  void const address can then receive a stable GlobalId without evaluating its contents.
  SemaGlobal.is_const distinguishes immutable backing from mutable storage; VALUE_GLOBAL
  is a declaration address, not an initializer. No interned Value/ConstObject is mutated,
  no undefined placeholder is used, and const decl.value remains its known Operand.
- Global records are allocated only when an immutable address is requested. Their
  initializers drain even for address-only use, using the existing function/global queue.
  Reading a known immutable declaration pointer resolves/folds its contents, including
  through volatile-qualified aliases; a pointer to mutable storage still cannot fold.
  Runtime pointer parameters retain real loads. Pending initializer function references
  queue callback bodies, and codegen emits immutable globals in .rodata/.data.rel.ro.
  Anonymous/local/inferred value references retain the existing immutable-object DAG;
  named backedges use stable declaration IDs, so no cyclic interner traversal is needed.
- Coverage: old reference-accepted self-address NYI fixture becomes a positive TIR test.
  New IR/TIR/QBE and executable coverage exercises self/mutual dependencies, value-first/
  address-first resolution, known pointer chains, callback slots, signed/bool/void/u0
  storage, mutable interaction and lazy unused invalid declarations. Six new error
  goldens are all rejected by reference Zig, including an invalid address-only callback.
  Physical ELF checks inspect all eight named immutable symbols, real 256-byte alignment,
  local binding, section selection, full RELRO and no TEXTREL under PIE; the over-aligned
  variant is also compiled/executed by Zig when ZIG is set. Reference storage suite: 15.
- Validation: 519 global-constant comparisons (187 new, 332 retained), 558 unit checks
  and 735 goldens pass. Fresh GCC/Clang -Werror ASan/UBSan pass 58 ordinary/generated
  executables, 16 driver, 15 storage, 166 safety and 44 physical volatile sequences.
  GCC's full e2e has Zig comparisons enabled, also passing two szc-only cases, four ABI,
  seven unaligned, 135 mutable-local, 270 local-constant, 153 mutable-global, 153 global-
  constant and 27 function-alignment checks. Eight lifecycle and 17 adapter regressions
  pass. All four 60-second libFuzzer targets and 20,000 standalone mutations per stage
  pass. All ten actual differential matrices pass 4,615 comparisons: 168 nested const,
  864 allowzero, 1,035 pointer alignment, 300 local alignment, 626 local constants,
  300 mutable globals, 519 global constants, 77 function alignment, 240 volatile and
  486 function signatures. An initial local orchestration typo named a nonexistent
  allowzero tool (exit 2); the correct diff_pointer_allowzero.py completed all 864.
  No comparison was skipped or failure ignored. GCC -O2 -Werror also passes the new
  executable and all 15 storage checks. New retained corpus inputs: irgen 34/sema 81. The restored sandbox again needed
  libasan/libubsan/Clang/compiler-rt.
- Scope: only explicitly typed supported runtime storage gets early immutable addresses;
  inferred type cycles and genuine value/type/alignment loops remain errors, not guessed
  values. Other aggregates/floats, exported/extern/threadlocal aligned constants and
  aligned local bare-function aliases retain prior limitations. Dependency diagnostics
  retain the existing primary-only message, not Zig's complete dependency trace.
  Enclosing M5.1/M5.1c/M5.1c2/M5.1c2b/M5.1c2b2/M5.1c2b2b remain partial.
- Next: ground small M5.2 array-type/literal/indexing checkpoints in the 0.17 sources;
  batch related straightforward parts when practical. Do not block independent array
  work on the upstream aligned-local-function-alias crash, or mark M5.1 complete.
- CI before this push: 1e0dfde/run 38705005917 has successful GCC/Clang/fuzz jobs,
  but differential still reports in_progress at nested pointer comparisons, so it is
  not a green run. Check this checkpoint's new push separately.

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
  At that checkpoint typed global-constant address dependency cycles reported the existing
  dependency loop even where Zig accepts them. That szc-only diagnostic fixture
  is now promoted to a positive regression in M5.1c2b2b3c above.
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
