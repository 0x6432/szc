> Historical scope of the finite TYPE-only batch based on ea86b15. The grouped
> known-symbolic-error/algebra/explicit-union-TYPE follow-up is documented in
> `ERROR_FAMILY_STATUS.md`; its known scalar exception does not relax storage,
> runtime unions, numeric error IDs or ABI fences described below.

# Finite explicit error-set TYPE identity — bounded V2 candidate

Based on published ea86b15. M6.5 remains partial: finite error-set TYPE identity is
not error VALUE support, representation, ABI or error-union support. No focused
error-set program is executed natively. Hosted CI is not claimed green.

## Implemented surface

A dedicated unary-metadata IR constructor carries decoded member names and source
offsets, not operand references, runtime values or numeric error IDs. Finite sets
are canonical immutable vectors sorted by decoded byte strings. Hash/equality use
membership rather than declaration order, alias, namespace or source spelling.
The empty set remains its own type: never noreturn, an empty enum, u0 or an OPV.
Ordinary VALUE_TYPE metatype values name these types; no payload error ValueKind
or global error-number registry is introduced.

Duplicate names are checked eagerly, including unused declarations and differently
quoted/escaped spellings of the same decoded name. Existing identifier decoding
and empty/NUL checks remain in force. No parser or general diagnostic rewrite is
introduced. Aliases, forward/namespace dependencies, metatype, pointer qualifiers,
zero-array TYPE identity, optional wrapping and lazy pointer-only nominal use are
covered by pinned compile-only controls.

## Representation remains fail-closed

The published optional frontier is generalized to a reason-carrying structural
walk: CLEAR / OPTIONAL / FINITE_ERROR_SET. Array children are followed iteratively,
including length zero; pointer/function/nominal identities stop the walk. No new
fuel/depth cap or recursive nominal scan is introduced. Optional refusal primaries
and checked natural-alignment preflights remain preserved.

Every finite error-set value/layout demand is intentionally unsupported, including
empty sets: coerce/typed undefined, storage/backing, zero-array values/OPV,
concat before peer/count/constructors, strides, size/alignment/bit-size queries,
loads/stores, globals, live by-value signatures and indirect return transport.
Existing layout-free pointer handles stay available; numeric creation, casts or
natural-layout demands must pass checked preconditions before old alignment
recovery helpers or TIR. No zero alignment sentinel or guessed ABI size is used.

No error.Member value construction, finite value injection, anyerror expansion,
set merging, error unions, try/catch/errdefer, scalar code numbering, runtime
storage/ABI, reflection builtins or backend representation is added. The original
switch error-union fixture remains an earlier producer gate.

## Renderer review and V2 correction

V1 passed focused semantic checks but static audit found its diagnostic renderer
could emit raw high bytes from decoded escaped identifiers. V1 is preserved and
blocked. V2 changes only two renderer comparisons relative to V1: sizing and
emission now hex-escape every byte >=127 as well as control bytes. Valid UTF-8 is
also escaped bytewise. Quotes/backslashes use valid quoted-identifier escapes.
This preserves exact decoded byte identity and produces an ASCII structural
spelling such as error{@"A",@"B"}; it is NOT Zig pretty-printer, location/note
parity or a new claim about legal reference identifiers. No UTF-8 validator,
identifier-legality change or public @typeName builtin is added.

## Strict contract and persistent policy checks

The unchanged 30-case reference contract records actual Zig 0.17.0 outcomes:
20 accepts and ten rejects. Baseline ea86b15 yields two exact matches and 28 strict
failures. V2 focused outcomes are 19 exact primary/status matches: all 17 targeted
foundation cases improve and both original passing controls remain accepted.
Eleven strict failures remain visible: nine earlier representation/value/ABI/use
gates and two inherited parser-primary mismatches. A matching rejection status
without the reference primary list is NOT parity. No fixture is filtered, wrapped
or rewritten to manufacture acceptance.

The durable policy suite reuses twelve exact source copies: nine representation
cases, canonical identity, eager unused duplicate rejection and ordinary zero-array
concat. The nine NYI refusal cases are policy checks, not language parity (six
reference-valid unsupported and three reference-invalid/refused, including ABI
and undefined concat). Complete ordered primary lists are checked without dedup:
some programs contain several independently failing expressions/functions.
Empty generated QBE proves driver refusal, not by itself absence of internal TIR;
source/producer review is a separate gate. No Zig lookup occurs by default;
explicit --zig revalidates the frozen reference observations compile-only.

A separate C API renderer regression covers empty/plain/quoted/backslash/control/
DEL/raw-high-byte/UTF-8-byte metadata. It tests only the internal spelling policy,
not reference identifier legality or runtime error values.

## Preservation and validation boundaries

Preserve the existing optional strict contract (17 exact / 13 remaining failures),
its eight automatic policy checks, the 257-level nonoptional array control and the
original switch checkpoint (80/84, four known failures). Existing supported native
and safety suites are regression evidence only, not native execution of the new
error-set fixtures. New compiler-only seed copies exercise canonical identity,
eager duplicate names, zero-array concat and pointer alignment refusal.

Fresh immutable parent regression/fuzz records are kept separately under
/data/validation/error-set-v2-final; focused results and static review alone do not
establish full-gate or hosted approval. The published optional commit ea86b15 had
77 hosted jobs pass and two GCC/Clang jobs fail with runner-communication-loss
annotations. Specific application timeout, exit143, root cause and runner image
remain unproven; no stale log authentication retry or CI weakening is implied.

The initial parent gate preserved a Clang launch failure (missing executable), not
a compiler-source failure. Clang 15.0.7 and sanitizer resources were restored and
retained under the computer workspace before the fresh final snapshots. That
environment failure is not counted as a semantic test or suppressed from records.
