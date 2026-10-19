# Empty-enum terminal-dereference contract (2026-10-19)

**Compile only. Never execute these sources.** Acceptance is not native evidence
about dereferencing an uninhabited type, invalid addresses, traps or volatility.

```sh
ZIG=/path/to/zig-0.17.0 python3 tools/check_empty_enum_terminal_20261019.py --reference-only --report reference.json
ZIG=/path/to/zig-0.17.0 SZC=/path/to/szc python3 tools/check_empty_enum_terminal_20261019.py --report candidate.json
```

Explicit candidate/`--zig` paths also work. Without an explicit candidate or SZC,
the runner discovers checkout build/szc, build/asan/szc, then PATH. Commands may
run from any directory. All compiler/version children use compiler_process.run;
positive finite execution timeouts and the published finite cleanup policy
apply. Reference caches are private per invocation. Reference actually must
report version 0.17.0. Reference compilation is build-obj -fno-emit-bin, candidate
compilation is tir. Neither command executes a fixture or emits a native program.

The final manifest covers exactly 30 independent sources and checks their frozen
source hashes. Primary lists are observed from actual Zig 0.17.0, including
architecture-specific ABI text on the pinned x86_64 Linux reference. Full notes,
carets, commands, source bytes, timing and hashes remain in JSON reports; only
primary-error lists/status are asserted. No source rewriting, forced wrappers,
skipping/filtering cases or NYI-as-parity credit is supported.

original_switch_empty_enum.zig is byte-identical to the existing
 tests/switch_contract/tir/switch_empty_enum.zig (verified on every run).

Scope includes value-dereference and address-only distinctions, zero-prong and
volatile controls, semantic-lazy arms/statements versus eager lexical and
multiple-else errors, execution-marker pairs, cleanup suppression/surviving
sibling cleanup, one/both terminal joins, known-pruned bodies and valid outward
break/continue with known-only defers. Ordinary nonempty enums, numeric u0 and
pointer/type controls must keep passing.

Five earlier gates remain strict failures on the baseline, not expected-NYI
passes: compileError builtin generation, TypeOf dereference query, internal enum
value storage/signature, exported enum ABI and local empty-value storage. The
TypeOf boundary deliberately wraps its result in a pointer: reference rejects
with 'pointer to noreturn not allowed'; this must not be described as accepting
that whole source or returning E. Address-only &p.* is separately reference-valid.

The internal f(E) call is reference-valid but does not prove a by-value empty-enum
ABI exists: its argument is terminal, and the baseline encounters an earlier
function-storage gate. Do not broaden value/storage/ABI handling to make this
case pass in a direct runtime pointer-dereference batch.

Initial nonlocal-defer attempts are retained in external evidence: discarding a
used function parameter is invalid, and demanding an extern runtime cleanup
inside the forced expression is also reference-invalid. The final valid controls
use known-only cleanup. No unrestricted runtime-cleanup crossing is claimed.

Supplementary design probes are external to this frozen 30 and earn no candidate
credit. One-member enum(u0), undefined pointer, invalid explicit empty backing,
known-address/global pointers and native backend behavior are not silently
claimed covered. They remain explicit design/implementation gates; no general
uninhabited, TypeOf, aggregate, ABI or storage expansion is authorized here.
