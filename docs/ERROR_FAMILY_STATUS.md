# Bounded error-family batch — candidate under validation

Baseline: published `8fc08706a68d1fa913dcedbb240eca75d38a7ead`.
This batch groups three compatible submilestones. M6.5 remains partial.
No new error-family program is executed natively.

## Implemented semantic subset

- Known symbolic `error.Name` constants and owner-typed finite `E.Name` members.
  Identity stores decoded names, not global numeric error codes.
- Finite set merge `||`, subset-based coercion and known error equality.
  A known member of a wide source set cannot narrow unless the entire source
  set is a subset of the destination. Metadata peers do not masquerade as Values.
- Explicit error-union TYPE identity `E!T`, including existing anyerror TYPE lhs.
  This does not enable anyerror values, inferred unions or runtime union payloads.
- Distinct general IR consumers for explicit discard and bare ignored errors.
  Ordinary nonerror behavior and stronger ENSURE_RESULT contexts are preserved.
  Explicit discard diagnostics are anchored at the RHS expression.

Union child rules are reference-pinned. Comptime-only payload types can name a
union type; finite/anyerror error-set payloads and opaque payloads reject. Empty
error sets never become noreturn, OPV, scalar storage or a fabricated error code.

## Representation and control boundaries

Known scalar operations are not storage permission. Runtime values, memory and
ABI for finite errors or unions remain unsupported. Structural array walks
include length zero and stop at pointer/function/nominal identity boundaries.
Checked layout demand remains mandatory before natural-alignment recovery values.
Loads/stores, immutable backing/address creation, globals, arrays/concat/OPV,
strides, function signatures and indirect returns retain explicit fences.
Runtime error-valued joins refuse before typed merge or branch publication;
single surviving known exits retain existing control bookkeeping. No generic
forced-comptime barrier, nonlocal-control rewrite or backend lowering was added.

The existing finite-type ASCII quoted structural renderer is unchanged. Its
spelling is deliberately not exact Zig pretty-printing; resulting diagnostic
primary mismatches remain visible, never special-cased or filtered.

## Frozen contract and review

The unchanged shared 60-source contract uses actual Zig 0.17.0: 36 accepts and
24 rejects. Published baseline: two exact matches and 58 strict failures.
Focused V1: 35 exact (24 accepts, 11 rejects), 25 strict failures: 17 outside
cases plus eight structural-formatting primary gaps. This is 33 improvements
and both original controls preserved, not completion of all 41 potential targets.
Two inherited lexical defects remain visible, including a dead-branch unused
local accepted by the baseline. No general lexical/formatter repair is included.

Whole immutable review blocked V1: a refused variadic error argument could still
publish TIR_CALL with a NONE argument. The paired reference rejects that error
argument as C-ABI-invalid; the ordinary c_int control accepts. Empty generated
QBE alone was not treated as internal safety proof. V1 is preserved; the narrow
V2 correction preflights extra arguments and aborts on new materialization
diagnostics before call payload/instruction publication, without
changing legitimate zero-bit or direct-call NONE conventions.

Acceptance requires immutable V2 audit, independent paired TIR/QBE controls,
unchanged reference/policy tests, and a fresh GCC/Clang sanitizer/native-existing-
safety/fuzz/source-integrity gate. Focused results are not publication approval.

## Deferred

Numeric/global error identities; runtime error or union representation, storage
and ABI; anyerror value coercion; union injection/unwrap; inferred error sets;
try/catch/errdefer; reflection builtins; full diagnostic/annotation parity.
The original switch checkpoint remains 80/84 with four known producer/diagnostic
failures. Hosted CI health is separate from local validation.

## Immutable V2 review boundary

The six-file V2 patch differs from V1 by seven added lines, only in call analysis:
existing representation admission for evaluated variadic extras, one diagnostic
checkpoint and two failure-atomic aborts across argument/indirect-callee
materialization. No TirCall payload or instruction is published after a newly
reported materialization error. Intentional direct-call NONE and zero-bit
conventions remain unchanged. Independent delta review found no further concrete
blocker in that correction; this is not a replacement for fresh full validation.

V1 full audit: 372c1309e8ce25061b839f3a48c50ac8a9ea2abe110c172feecad7efed00c449.
V2 delta audit: 97edaf0b01f4eaea3e1e0191a7b2d498cd31c4b3990f7dd60b074207ac5268c2.
Full implementation patch: 32bb866c24cd7a0593c54b9a7e9578ae72f4d951e8d273a4a321093ef5c2464d.

## Durable policies and independent reproduction

The default candidate-only 26-case policy runner performs no Zig lookup; explicit
--zig revalidates the pinned reference. All sources are exact copies: 25 from
strict60 plus one separately authorized store/address producer witness. Outcomes
are nine accepts, one exact parity reject, three structural-diagnostic rejects,
eleven reference-valid unsupported fences and two reference-invalid NYI refusals.
Structural diagnostic checks are not exact Zig primary parity. Complete ordered
primary lists retain independently diagnosed operations; nothing is deduplicated.

The separate two-source vararg suite checks both TIR and emitted QBE: the error
witness must refuse with no sink call and no invalid NONE reference, while the
ordinary c_int call remains valid. V1 fails this containment test; V2 passes.
These tests run in make test alongside optional8, finite12 and internal renderer8.
No new fixture is executed natively. Independent V2 reproduces strict35/25,
all26 policies, both vararg checks and all prior policy/deep-array controls.

Parent additionally verified 3,196 original test files. Only 58 explicitly reviewed
IR expectations change (new consumers and reference renumbering); all 92 original
instruction streams and current expectations match. No old Zig source was modified. Four non-IR goldens have separately reviewed
opaque ValueId/symbol substitutions only; complete outputs are identical after
those explicit substitutions, with no instruction/data/byte/control changes. Final immutable full-gate records are external evidence at
/data/validation/error-family-v2-final2; passing that gate is mandatory before WIP
publication and does not establish hosted CI health or full M6.5 completion.

The first frozen full gate is retained at /data/validation/error-family-v2-final.
It passed 12,577 unit checks but exposed four sentinel golden ID renumberings:
canonical void is interned earlier by the new consumers. Parent approved only
three TIR ValueId substitutions and the corresponding two QBE byte-symbol names
in both uses/definitions after full-output proof (330 QBE lines, 12 data objects,
identical bytes/order/control). No compiler change or blanket golden generation.
A fresh full gate from two new trees is mandatory; the failed run is not replaced.
