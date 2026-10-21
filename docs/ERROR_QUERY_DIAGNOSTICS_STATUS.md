# Finite error-query diagnostics and undefined-value demand

Baseline: `466f1d7428b749889e74986b4c91cd313bd1f6ac`.
This follow-up completes six bounded gaps, not runtime error-set or error-union support.

## Implemented behavior

- Finite error-set structural type names concatenate canonical decoded member bytes,
  matching the pinned Zig 0.17.0 reference. Diagnostic names are not source identifier
  syntax: keywords, quotes, controls, UTF-8 and invalid bytes are not re-escaped.
  Canonical ordering and type identity are unchanged.
- Rendering computes a checked exact allocation size. Synthetic internal metadata
  with an embedded NUL receives an explicit invalid-name placeholder; source NUL
  identifiers remain rejected. No truncated prefix is presented as the full name.
- Semantic diagnostics retain their entire formatted message beyond the old 512-byte
  stack buffer, with checked formatting/allocation and unchanged short messages.
  Source suites verify complete 600- and 4,096-byte names and diagnostic suffixes.
- A literal or finite-set undefined scalar may remain a destination-typed symbolic
  `VALUE_UNDEFINED`. It is not a known error member, error code, layout or storage
  permission. Implicit finite-set coercion still requires whole-source subset
  compatibility. Value-demanding queries reject undefined values through the normal
  definedness check, including queries inside `@TypeOf`.
- Undefined finite `@errorCast` operands validate destination/source kinds and set
  intersection before value demand, following reference diagnostic precedence.
  Undefined cast results are never published as known errors.
- Allocation rechecks representation admission after initializer coercion, preventing
  symbolic undefined transport from admitting an error-valued runtime slot.

## Frozen contracts and limitations

The original 56 sources, membership, reference observations and manifest remain
unchanged. Exact status and complete ordered primary parity improves **46/56 to
52/56**, preserving all 46 baseline matches. Four reference-valid runtime cases
remain strict failures: `c12_runtime_source`, `c13_return_argument_assignment`,
`n10_runtime_load`, and `n11_runtime_argument`. Their refusal diagnostics and
multiplicity remain unchanged. Strict56 still exits 1; no unsupported case is
filtered or counted as exact.

The independent 36-source undefined appendix has **33 exact matches / 3 strict
failures**, up from 13 exact. The three existing `@compileError` operand NYIs remain
visible. Its default runner is strict; `--allow-known-gaps` permits only byte-pinned
baseline snapshots of unresolved failures and never credits them as parity.
Candidate-only mode uses frozen reference expectations and never discovers Zig;
explicit `--zig` freshly verifies actual version 0.17.0 and all reference outcomes.

Runtime/anyerror values, memory/layout/ABI, error unions, inference, try/catch and
errdefer remain unsupported. Existing pointer, address/backing, zero-array,
variadic publication, aggregate and runtime-join fences remain required. No new
ValueKind, TIR operation, error numbering or backend lowering is introduced.

## Additional anyerror containment correction

Independent review found a preexisting unsupported-value escape: typed undefined
`anyerror` could publish local allocations, runtime signatures or a variadic call
before backend rejection. Common representation admission now explicitly rejects
`anyerror` values (including array-wrapped values), and checked layout refuses
`anyerror` rather than treating it as a zero-size scalar. Signature diagnostics
retain their own boundary. This is failure-atomic containment, not runtime
`anyerror` support or additional strict56 parity credit. The type itself, pointer
identity and explicit error-union type identity remain available. Regression
acceptance requires direct internal no-allocation/no-backing/no-call checks, not
merely an empty generated output.

## Regression and expectation accounting

- Byte-exact source renderer suite: 32 cases. Reference LF continuation indentation
  is represented only in the expected reference presentation; observed bytes are
  not broadly normalized. Candidate raw LF presentation is tested independently.
- Internal renderer suite: 14 vectors, retaining all eight original input vectors.
  Their obsolete ASCII-safe quoted output policy is explicitly replaced with
  reference-backed raw-byte output, with six additional stress/safety controls.
- Undefined suite: 36 immutable source fixtures, plus 19 checker/accounting tests.
- Anyerror containment: 27 immutable fixtures and 20 checker/accounting tests;
  9 accepts, 9 reference-valid unsupported fences and 9 reference-invalid NYI
  refusals. All 27 include direct internal publication checks; NYI earns no
  exact-parity credit. The default Make gate sanitizes the C inspector too.
- Six existing candidate diagnostic expectations change: three family26 and three
  policy31 cases. Exit status, count/order, sources, reference observations and
  diagnostic-operation ledgers are unchanged. Three policy31 labels improve from
  structural rejection to exact parity rejection after explicit reference proof.
- All old Zig source fixtures and old IR/TIR outputs remain unchanged. The
  anyerror correction changes exactly three existing unit expected substrings
  (not their programs) and one full memcpy diagnostic golden: two earlier
  checked-layout refusals replace the later memcpy-element refusal. No rejected
  source is rewritten to hide a diagnostic.

Full acceptance additionally requires independent immutable implementation review,
GCC/Clang sanitizer unit/golden/policy suites, unchanged native safety programs,
standalone fuzz replays, namespace/switch-owner checks, timed libFuzzer campaigns,
and complete source-integrity comparison. Hosted CI health is a separate gate.
