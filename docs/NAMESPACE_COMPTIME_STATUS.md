# Bounded anonymous namespace comptime

This batch schedules existing anonymous namespace `comptime` IR. It is not a
complete M7 interpreter, generic-function engine or bootstrap milestone.

## Execution and isolation

Root namespaces and actually created struct/enum namespaces enqueue anonymous
work exactly once. Unused named namespaces stay lazy; unrelated named members
and test bodies are not eagerly executed. Anonymous declarations execute in source
order within each namespace. A FIFO fixed point also visits namespaces created by
anonymous work, initializers, functions, defaults or type queries.

Creation only queues work. Execution occurs after the creating analysis returns
and named values have been published. Each anonymous body has a fresh operand map
and definition-namespace context, without caller TIR, control targets or switch
owners. Namespace and anonymous-declaration states are separate from named value,
layout, export and default states; errors and failed dependencies remain sticky.

Known-value arithmetic, known branches, supported switch behavior and existing
lexical defers use the existing evaluator. Anonymous namespace statements now
check ignored nonvoid results using existing IR_ENSURE_RESULT. Explicit discards
and consuming initializers/break values remain consuming contexts. Fresh function,
test and namespace-member initializer boundaries do not inherit that statement
policy in that original batch. The later error-query follow-up independently
adds general ordinary ignored-result checks, including fresh function/test/member
initializer statements, without inheriting namespace execution context. See
`ERROR_QUERY_STATUS.md`; original namespace activation and work limits remain.

## Explicit bounds and unsupported capabilities

- One cumulative namespace-work budget of 1,048,576 units charges namespace/member
  admission before allocation, anonymous execution, body/dependency entry and
  executed instructions. It is not a universal bound on all compiler work.
- Active evaluator body/dependency depth is limited to 128, including cached
  dependency entries. Depth/work exhaustion is explicit NYI, not language parity.
- Created namespaces consume admission budget even without anonymous work. Very
  large sources previously accepted can therefore reach the new explicit limit.
- Reached loops and mutation are explicitly unsupported. General user-function
  comptime calls, generic specialization and broader evaluator capabilities remain
  outside this batch. Known-unselected unsupported bodies are not executed.

## Focused contract

`tools/check_namespace_comptime.py` validates the actual reference version and
keeps parity, classification gates, valid unsupported forms and resource limits
separate. Sources live outside auto-discovered golden directories.

```sh
python3 tools/check_namespace_comptime.py build/asan/szc \
  --zig /path/to/zig-0.17.0 --stress --report namespace.json
sh tests/frontend/global_comptime_frontend_check_20261019.sh
python3 tests/frontend/global_comptime_frontend_results_20261019.py \
  --candidate build/asan/szc --zig /path/to/zig-0.17.0 --report results.json
```

The frozen focused contract passes 58 cases: 34 exact rejections, 18 accepts,
one visible type-classification gate, three valid unsupported cases and two
resource limits. All twelve original global-comptime switch failures remain
unwrapped and now reject with exact reference primary-error lists. Thirty
frontend-policy semantic controls pass (15 accepts, 15 exact rejections); 28
structural cases / 185 checks pass normally and under ASan/UBSan. Eight unrelated
frontend contexts preserve byte-exact IR.

Adjacent private boundary probes also verify dependency depth 62/63 and repeated
literal work 3591/3592; shared cached-dependency work remains cumulative. These
are implementation-cost boundaries, not Zig source-language limits. Independent
static review found no concrete new blocker in the frozen source snapshot.

Parent integration passes the full fresh GCC/Clang warning-clean debug/release,
ASan/UBSan unit/golden, frontend, namespace-contract, native/safety and standalone
fuzz gate. Each compiler reports 12,577 unit checks, 1,370 goldens, 308 safety
cases, the 58-case namespace contract and 30 frontend semantic controls. Existing
switch frontend protocols remain green (29,288 original / 29,974 extended).
Four fresh 60-second libFuzzer runs pass. Both upfront-frozen compiler snapshots
pass complete source-integrity checks with no changed files.

Local evidence: `/data/validation/namespace/results.json`, per-stage logs and
manifest; parent strict switch records: `/data/analysis/namespace-parent-switch.json`.
The original 84-case switch contract now has 79 passes, one expected empty-enum
NYI and four explicit failures (three earlier type gates and one direct enum
forced-comptime primary mismatch). These remain visible; no full switch, complete
comptime, exact diagnostic-stream parity or hosted-green claim is made.

A later, separate empty-enum terminal-producer follow-up advances the original
84-case switch checkpoint to 80 passes and four existing failures (no expected
empty-enum NYI). Namespace scheduling itself is unchanged. See
`EMPTY_ENUM_TERMINAL_STATUS.md`; this does not fix the forced-comptime diagnostic
or implement general comptime control propagation.
