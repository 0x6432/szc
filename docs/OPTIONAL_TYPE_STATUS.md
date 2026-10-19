# Optional type identity — bounded V2 candidate

This WIP batch starts from `5e13779`; M6.4 remains partial. Optional TYPE identity
is not optional VALUE support. No native optional programs are executed by its
focused checks. Full comptime, diagnostic-stream fidelity and hosted CI success
are not claimed.

## Implemented surface

- Dedicated unary IR optional-type constructor; lazy child analysis follows the
  existing declaration evaluator. Unused bad declarations retain semantic laziness.
- Canonical kind/child hashing and equality, preserving aliases, nesting, pointer
  qualifiers/topology and nominal child identity. Results are ordinary metatype
  values naming types, never payload-bearing optional values.
- Definedness checked before extracting a child type; builtin anyopaque rejects
  with the observed opaque-child primary. Legal type children include void,
  noreturn, type and comptime numeric types; legality does not imply runtime
  representation. Existing namespace-cycle rejection is preserved.
- Internal diagnostic names prepend '?'. There is NO new @typeName builtin,
  @typeInfo or payload reflection operation.

## Fail-closed representation frontier

An iterative structural walk follows array children, including zero lengths,
refuses optional payloads and stops at pointer/function/nominal identity. Child
construction is acyclic; no new global depth/fuel cap is imposed. The baseline's
257-level nonoptional zero-array size query remains accepted with identical TIR.
Existing parser/layout/evaluator budgets are unchanged, not replaced by a claim
that all type formatting or compiler work is newly bounded.

Optional values, layouts, typed undefined/null/payload injection, loads/stores,
array initialization/OPV, constant backing, bitcasts, globals and live by-value
signatures remain explicit NYI. Indirect zero-argument optional-return calls are
refused before call setup/emission. Pointer-only identity and existing layout-free
opaque handles do not force nominal fields. Real alignment/stride/extent demands
must pass checked preconditions before old recovery helpers or TIR are used.
No guessed alignment1, zero-alignment sentinel, optional scalar temporary,
nullable-pointer optimization, new backend representation or optional ValueKind
is introduced. Shared-layout and nonlocal control-flow models are not rewritten.

## Review-found blocker and V2 correction

The first frozen prototype passed the initial contract but had a producer escape:
concatenating zero-length optional arrays through pointer handles could skip all
element coercions and mint forbidden array/ref backing. The targeted undefined
pointer witness also reference-rejects while V1 incorrectly accepted. V1 remains
rejected/frozen; passing its old tests did not waive this defect.

V2's only correction relative to V1 is the two existing frontier checks immediately
after concat operand shape validation, before peer/count/sentinel/value/backing
construction. It does NOT fix generic undefined nonoptional concatenation. The
optional witness now receives honest NYI, not the reference's illegal-behavior
primary; do not count it as diagnostic parity. The ordinary defined nonoptional
zero-array concat control stays accepted. Independent static review checked the
immutable four-file patch and exact three-line V2 delta.

## Reproduction and honest outcomes

```sh
# Strict main contract: intentionally nonzero while the listed gaps remain.
python3 tools/check_optional_types_20261019.py build/asan/szc \
  --zig /path/to/zig-0.17.0 --report optional-types.json

# Persistent refusal/control contract; no Zig dependency by default.
python3 tools/check_optional_type_boundaries_20261019.py build/asan/szc
# Add --zig /path/to/zig-0.17.0 to revalidate frozen reference observations.
```

The unchanged 30-case contract yields 17 exact PRIMARY/STATUS matches (14 accepts,
three rejects), up from one baseline match. Sixteen scoped cases improve and the
sole prior passing control is preserved. Thirteen strict failures remain visible:
three absent @typeName cases, nominal opaque construction, missing error-value
construction, seven optional value/layout/ABI/use cases, and inherited dependency
cycle wording ('dependency loop detected' versus the reference's length2 primary).
The frozen sources/reference expectations remain unchanged; scope reconciliations
are not runner filters or guessed diagnostic replacements. This is not complete
optional semantics or note/caret/location parity.

The separate eight-case durable suite copies the six authorized boundary sources
and two concat controls exactly. Its seven refusal checks require intended NYI,
nonzero status and no generated QBE; the ordinary concat requires acceptance.
Six ref-valid cases are valid-unsupported fences; the undefined concat is
reference-invalid with candidate NYI, NOT exact-primary parity. Empty generated
output proves driver output refusal, not by itself absence of internal TIR or call
instructions. Static/live-call review provides the separate attribution. These
checks run automatically in `make test`; no external QBE or native execution is
needed. Failed exploratory ABI/extern callback drafts remain separate evidence.

The original 84-case switch corpus is untouched and stays at 80 passes / four
known failures. Type identity alone does not admit the payload injection/private
argument representation needed by its optional switch fixture. Error unions,
float literals and forced-comptime context are separate work.

Parent adds four compiler-only seed programs to each irgen/sema fuzz corpus for
identity, invalid child, logical stride and the rejected concat path. Fresh
frozen GCC/Clang regression/fuzz records are separate under
/data/validation/optional-v2-final; focused evidence alone is not full-gate approval.

## Hosted CI limitation

The last observed base-commit run39269443752 reports77 successful jobs and two
failed build-and-test jobs, both annotated as runner communication loss:
https://github.com/0x6432/szc/actions/runs/39269443752
Neither annotations nor inconsistent step timestamps establish a timeout case,
exit143, actual image or root cause. Raw log requests returned401. Local passing
tests are not proof of hosted success; do not weaken execution budgets/assertions
or attribute the loss to infrastructure, core collection or this compiler patch
without further evidence.
