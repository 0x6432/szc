# Bounded switch candidate

This is an in-progress implementation based on `e44e6fe`, not a completed M6.3
milestone or a Zig bootstrap claim. No remote changes have been made.

## Implemented scope

- Fixed-width integer and supported ordinary exhaustive nominal-enum operands;
  known wide integers and known zero-width values where existing producers admit
  them.
- Inclusive integer ranges, multiple labels, full-domain exhaustiveness,
  duplicate/overlap checking, and validation of every label before known selection.
- Deferred semantic analysis of unselected known arms.
- Immutable scalar captures, preserving enum identity and operand snapshots.
  A discrete single label refines a capture to a known value; a runtime range,
  including a degenerate range, does not.
- A single N-way result join using existing aggregate snapshot, cleanup and TIR
  branch machinery. No new backend switch operation or jump tables.
- Cumulative switch-work limit 1,048,576; semantic nesting limit 128; conservative
  ancestor-plus-current runtime prong depth limit 256. Limit failures are explicit
  unsupported-feature diagnostics, not partial execution.

## Reference and test contract

The reference is the exact CI-distributed Zig 0.17.0 binary. Independent programs
exercise all 24 planned test families. The default strict harness preserves
failures instead of filtering unsupported producers or diagnostic mismatches.

```sh
python3 tools/diff_switch.py build/asan/szc \
  --zig /path/to/zig-0.17.0 --qbe /path/to/qbe --stages --report strict.json
python3 tools/check_switch_unsupported.py build/asan/szc \
  --zig /path/to/zig-0.17.0 --qbe /path/to/qbe --report scope.json
python3 tools/diff_switch.py build/asan/szc \
  --zig /path/to/zig-0.17.0 --qbe /path/to/qbe \
  --stress --expect-budget --select budget --timeout 60 --report budget.json
```

The two-positional `diff_switch.py COMPILER REFERENCE` CLI also implements the
repository's compile-only differential adapter protocol. It does not run native
programs or independently verify the reference version; use explicit `--zig`
mode for pinned-version and native byte/exit checking.

Contract-only source specifications live under `tests/switch_contract/`, not the
auto-discovered golden directories. Supported native fixtures remain under
`tests/run/`. The standalone frontend protocol checks have their own script at
`tests/frontend/switch_frontend_check.sh`.

## Current strict result

| Outcome | Cases |
|---|---:|
| Pass | 63 |
| Expected empty-enum NYI | 1 |
| Silently skipped global-comptime invalid programs | 12 |
| Earlier/incorrect unsupported-feature gates | 4 |
| Strict primary-diagnostic mismatches | 4 |
| Total | 84 |

The strict command exits nonzero. This is not full contract acceptance.
All 13 focused native programs match reference output bytes and exit status.
Nine valid-unsupported-form checks and two cumulative-budget/control checks
pass their explicit refusal/limit contracts, not ordinary language acceptance.

### Open failures

1. **Global comptime:** existing frontend/sema does not execute global comptime
   blocks, so twelve original invalid programs are accepted without reaching
   switch semantic analysis. Fourteen reached-body counterparts are retained
   separately and reject with reference-matching primary diagnostics. They do
   not erase the original failures.
2. **Earlier gates:** optional/error-union types and float literals reject before
   switch validation. Those operand forms are language-invalid in the reference;
   earlier NYI is not diagnostic parity. Bare `_` on an integer is also invalid
   but currently hits the frontend non-exhaustive-enum NYI gate.
3. **Diagnostic text:** enum multi-capture knownness, index capture, reached float
   operands and capture shadowing have primary-message differences.
4. **Empty enums:** the reference accepts a compile-only zero-prong switch, but
   existing enum dereference/signature admission prevents producing its operand.
   Uninhabited operand propagation is not broadened in this batch. Do not execute
   the deliberately invalid-address compile-only specification.

## Other deferred forms

Labeled redispatch, inline prongs, pointer captures, bool/pointer/type/tagged-union
operands, non-exhaustive enums and runtime integers wider than 64 bits remain
explicitly unsupported. This does not implement general comptime execution,
imports, generics, std integration or bootstrapping.

## Regression status

Corrected GCC and Clang runs pass warning-clean debug and optimized builds,
ASan/UBSan unit/golden tests, frontend checks, full native/storage/alignment/safety
tests, and standalone fuzzing. Each compiler reports 12,577 unit checks, 1,370
goldens, 29,247 frontend checks and 308 safety cases. Standalone fuzzing replays
256/256/177/233 lexer/parser/irgen/sema seeds and 20,000 mutations per stage.

Native gates initially stopped at the new tool's adapter API mismatch. The tool
was fixed, and the unchanged compiler-process check passes 67 checks. Existing
test assertions, timeouts and suite discovery were not weakened.

Four normal 60-second libFuzzer runs passed with the new switch seeds. The
harness-only correction does not change compiler/fuzzer source inputs; reuse of
that evidence requires an exact manifest match for `src/`, `fuzz/` and `Makefile`.

The validation runner copied GCC and Clang trees sequentially. Updated status
documents were picked up by the later Clang copy, so its original all-file
integrity check reports differences in README, HANDOFF and MILESTONES. That failed
audit is preserved, not overwritten. Independent reconciliation verifies unchanged
executable inputs against the manifest in both validation trees and the integration
candidate. A future runner should freeze both copies before executing either.

Local evidence is retained under `/data/validation/switch-v2/`, including
`results.json`, stage logs and `integrity-reconciliation.json`; the prior libFuzzer
evidence is under `/data/validation/switch/`. Integrated strict-contract records
are under `/data/analysis/integration-contract-v2/`. These are local artifact paths,
not committed reference compilers or portable tool defaults.

Do not infer hosted CI success from local results. The previously investigated
hosted failures remain unexplained without authenticated logs.