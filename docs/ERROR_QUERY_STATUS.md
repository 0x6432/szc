# Error queries and general ignored-result checking

Baseline: published `e594a4ddd3fb2f05d24fe102fd12332b4b4c9d16`.
Three bounded submilestones are grouped; this is not runtime error/union support
or completion of M6.5.

## Implemented subset

- Ordinary ignored expressions use existing ENSURE_RESULT semantics. Complete
  if/switch/labeled-block results retain normal peer analysis before one outer
  consumer. Missing-else incompatible-type errors are not masked. Loop body,
  continuation and ignored loop-else results have their own consumers; grouped
  expressions are transparent. Consumed and nonlocal target contexts stay real.
- `@errorName` on a genuine known finite error returns decoded bytes in the
  existing `*const [N:0]u8` literal carrier, not a fabricated slice or numeric code.
- Context-driven `@errorCast` validates genuine source Value/type identity,
  finite source/destination intersection, then known-member admission, and
  interns a destination-typed Value. Implicit coercion still requires whole
  source-set subset compatibility. TypeOf does not bypass membership validation.

Runtime/anyerror values, error/union layout and ABI, global error numbering,
union injection/unwrapping, inferred errors, try/catch/errdefer remain unsupported.
All prior zero-array, concat, storage/address, pointer-layout, runtime-join and
variadic call-publication fences remain in force. No new ValueKind or backend
lowering was added. Legacy CHECK_UNUSED_ERROR remains defined but unproduced.

## Frozen contracts and strict accounting

The unchanged 56-source reference contract records actual Zig 0.17.0:
30 accepts, 26 rejects. Published baseline: 12 exact /44 strict failures.
Fresh independent restored candidate: **46 exact /10 strict failures**:
26 accepts,20 rejects,34 improvements and all12 baseline matches preserved.
Remaining: five structural-primary gaps (including existing quoted type-name
formatting), four runtime-producer fences and one undefined-source NYI.
No failing case is filtered or reclassified as exact parity.

The separate31 policy gate reuses30 of these exact sources plus one authorized
TypeOf-membership appendix:13 accepts,13 exact parity rejects,3 structural
rejects,2 reference-valid unsupported fences. It never discovers Zig by default;
explicit --zig revalidates0.17.0. It is not an all-green strict-reference claim.
Old strict60 remains35/60; original switch checkpoint remains80/84.
Optional8,finite12,family26,vararg2,renderer8 and deep-array controls are retained.
No new query/error fixture is executed natively.

## Reviewed expectation changes

All92 old IR source bytes remain unchanged. Fresh full-output proof covers
50 changed expectations and42 identical outputs. Strip only statement consumers
whose results are unreferenced, normalize IDs, and85 match; canonicalize exact
`as @type_void, @void_value` identity definitions plus their genuinely used
references and all92 match. Exactly14 old/1 new identity definitions occur;
three used-reference substitutions are explicit, not claimed unused. There are
no residual differences. Six namespace frontend assertion counts increase for
new ordinary checks; all28 source programs are unchanged. No positive fixture
was rewritten to hide an ignored-result error.

## Recovery and evidence

A workspace rollback destroyed the first candidate binary and raw results.
The original three-source patch was recovered hash-exact, not reimplemented:
`4094559002cf2c94de60ae98d3daea87b2efd0551429d21ea5be99ed19a3c63f`.
Strict56 manifest remains
`572edc99094feeb2ba67d9d195280e75693ecaf6777b8665c8825da2853f72ea`.
New builds/tests and the full92 proof are fresh validation, not recovered raw.
Complete immutable source review:
`ba6d6306c464111bcb2af880903446a0e89ae6f25cd93f31abbb93fa69109735`.

Fresh final GCC and Clang gates pass: each12577 unit checks,1370 goldens,
308 unchanged native safety cases, all prior policies, namespace58-case stress
contract,197 namespace structural checks and29967 switch-owner checks. Allfour
standalone targets retain20000 mutations each per compiler. Four libFuzzer
campaigns each ran61 seconds without failure (3062774/46843/68539/56483 runs).
Both compilers independently reproduce strict56=46/10 without filtering.
Every existing file was compared to baseline:6911 unchanged and58 explicitly
reviewed modifications; no original Zig fixture changed. Hosted
CI health is a separate gate; lost-runner annotations do not establish a
compiler timeout, exit143, cleanup defect or root cause.
