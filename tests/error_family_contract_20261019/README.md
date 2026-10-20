# Shared error-family compile-only reference contract

Exactly sixty unique final programs TOTAL across three related foundations:
- S16 symbolic error.Name and finite E.Name, membership, consumers and laziness;
- M12 finite coercion/equality/error-set merge, query and nonlocal control;
- U16 explicit error-union TYPE identity/composition/invalid operands;
- F16 all representation/ABI/layout/zero/concat/alignment/producer fences plus
  ordinary discard controls. F11 includes zero-array layout; F12 contains runtime
  if/u8-switch/labeled-block error joins. No supplementary new programs.

Pinned reference is actual Zig0.17.0, build-obj -j2 -fno-emit-bin. Candidate uses
tir (partial TIR output on errors is intentionally retained). Neither compiler's
fixture is ever executed. There is no external QBE requirement, source wrapper,
filter, skipped boundary, numeric error ID, try/catch, error-union value
construction or new reflection builtin. ANYERROR is exercised as a TYPE only,
never value-widening/runtime error numbering. Indirect-call/pointer-load union
programs are opaque-producer admission fences, not union-value implementation.

Use `python3 tools/check_error_family_20261019.py /path/to/szc --zig /path/to/zig`.
`--reference-only` skips candidate discovery. Compiler discovery also supports
SZC/ZIG, local build paths and PATH. Existing compiler_process bounds execution
and cleanup; reference caches are private per run. JSON reports capture exact
sources/hashes, argv, compiler versions/hashes, statuses, complete ordered primary
lists, stdout/stderr and timing; process abnormalities are separate failures.

All sixty stay visible and strict. Manifest source hashes and case count are
verified; candidate/reference hashes are stable before/after each run. Expected
status and FULL primary list come from independent final-reference observations,
not candidate output. Notes/carets/coordinates are preserved raw for review but
are not claimed exact parity by this runner. NYI never earns language parity,
including reference-invalid C ABI and undefined-pointer concat fences. Later
durable policy suites must copy selected final source bytes unchanged and remain
separate from strict language parity; no new unique program budget is created.

Key actual semantics, not analogies:
- error.A has singleton error{A} type; E.A carries owner E type.
- Known error names compare across finite types, but coercion requires whole
  source-set subset compatibility; known-member narrowing is still rejected.
- Explicit discard and ignored expression have distinct diagnostic primaries.
- Empty error-set unions remain types distinct from their payload.
- void/noreturn/type/comptime_int/comptime_float and nested union payload TYPES
  are admitted; finite-error-set/ANYERROR and anyopaque payloads are rejected.
- Explicit ANYERROR!u8 TYPE identity is accepted, with all runtime frontiers closed.
- Parent-approved nonlocal break/continue and ordinary scalar/type underscore
  controls are included. Runtime error-valued joins are legal reference programs
  but are negative representation fences in this implementation batch.

Earlier draft sources/raw observations are retained in the independent analysis
bundle, including the intentional S02 member-type observation diagnostic and
parent-approved F11/F12/U01/U08 revisions. Final expected sources are frozen once;
no candidate-driven edits or guessed goldens are permitted.
