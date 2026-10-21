# Undefined finite error-query regression (36 new fixtures)

Baseline candidate: `466f1d7`. Actual reference: Zig `0.17.0`.
Read `docs/ERROR_QUERY_STATUS.md` for the larger feature boundary. This is an
independent test-only appendix, not a replacement or expansion of strict56.
The original strict56 fixture bytes and manifest remain unchanged and are
hash-checked before and after this checker runs.

## Run

```sh
# Portable candidate-only gates: frozen observations, no Zig discovery.
python3 tools/check_error_query_undefined_20261022.py build/szc --report build/undefined-strict.json
python3 tools/check_error_query_undefined_20261022.py build/szc --allow-known-gaps --report build/undefined-policy.json
# Full reference revalidation requires explicit pinned Zig.
ZIG=/data/toolchain/zig-reference/zig-x86_64-linux-0.17.0/zig
python3 tools/check_error_query_undefined_20261022.py build/szc --zig "$ZIG" --report build/undefined-strict.json
python3 tools/check_error_query_undefined_20261022.py build/szc --zig "$ZIG" --allow-known-gaps --report build/undefined-policy.json
python3 tools/check_error_query_undefined_20261022.py --zig "$ZIG" --reference-only --report build/undefined-reference.json
python3 tests/test_error_query_undefined_checker_20261022.py
```

Without `--zig`, the checker never discovers or invokes Zig (even when `ZIG`
is set). It uses frozen manifest observations, reports `reference_requested:false`,
`reference_basis:"frozen-0.17.0-observations"`, a reference validation count of0,
and `reference_validated:null` per row. Exact accounting in this mode means
matching those frozen observations, not fresh reference validation.

`--reference-only` requires explicit `--zig`; missing it is an argument error
(exit2). Explicit reference validation requires an executable, checks its actual
version, and compiles all36 sources unchanged using `build-obj -fno-emit-bin`.
Candidate uses `tir` only. Nothing is executed natively. Caches are private,
compiler processes are bounded through the existing `compiler_process` helper,
and source/compiler hashes are checked before and after. Ordered primary error
strings and exit statuses are exact; note, caret, source-location, secondary
formatting and backend parity are **not** claimed.

The default checker is strict: every mismatch exits1; infrastructure errors
exit2. `--allow-known-gaps` permits only the exact recorded baseline status and
ordered-primary snapshot, or a genuine improvement to exact reference parity.
A changed NYI, new wrong diagnostic, timeout/crash, lost exact match, or explicitly revalidated reference
drift still fails. Known gaps retain their nonexact accounting labels even when
this opt-in policy returns0. It is a containment/regression gate, not all-green
language parity. It performs no source rewriting or diagnostic normalization.

## Findings and boundaries

- `n01..n04`: errorName demands undefined operands, including empty/singleton
  finite sets and untyped undefined. An empty finite error set is not a reason
  to accept a demanded undefined errorName.
- `t01..t06`: TypeOf does not rescue these errorName/errorCast expressions from
  demand or disjoint-set validation. `t07/t08` contrast TypeOf of the carrier
  itself: Zig accepts, while the baseline candidate reports a finite-value NYI.
  `t09` shows TypeOf does not supply a missing errorCast destination.
- `c01..c08`: intersecting finite errorCast with an undefined value rejects as
  illegal undefined use. Disjoint sets, empty source, empty destination, and
  empty-to-empty reject as having no common errors **before** undefined-value
  demand. Untyped undefined cast operand instead reports an expected-error-type
  diagnostic. Discarding the result does not erase those checks.
- `d01..d07`: false/dead or unselected if/switch arms avoid these demands;
  selecting an undefined errorName does not. `d06` consumes the selected known
  cast result via errorName, avoiding an unrelated discarded-error diagnostic.
- `p01..p08`: undefined type target beats the ordinary integer/undefined operand
  diagnostics in these cases. A non-error destination reports found `'int'`
  before demanding the undefined operand. The explicit compileError operand
  sentinel is different: its own primary wins even over an undefined target.
  These are observed expression diagnostics, not a universal phase-order claim.

No runtime error producers, anyerror, error unions, layout/ABI, storage/backing,
backend lowering, or native safety coverage is added. compileError sentinels
are deliberately recorded even though baseline szc does not implement that
builtin; their NYI is not parity credit.

## Frozen baseline accounting

Zig: **8 accepts /28 rejects**. Baseline candidate:

| Accounting | Count |
|---|---:|
| EXACT-ACCEPT | 6 |
| EXACT-REJECT | 7 |
| REFERENCE-INVALID-NYI | 19 |
| REFERENCE-VALID-UNSUPPORTED | 2 |
| PRIMARY-GAP | 2 |

Thus **13/36 exact**, **23 strict failures**. Unsupported-but-reference-valid
cases are reported separately from NYI refusals of invalid programs. This table
is not an expectation for another agent's modified compiler: exact improvements
are allowed and counted as such; remaining/new gaps must stay visible.
