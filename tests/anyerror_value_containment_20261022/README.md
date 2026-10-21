# Fail-closed anyerror VALUE containment (27 frozen fixtures)

Standalone, compile-only safety appendix. No production or Makefile changes.
Original fixtures/manifests are not replaced, rewritten or reclassified.

## Portable default invocation

```sh
python3 tests/test_anyerror_value_containment_20261022.py
python3 tools/check_anyerror_value_containment_20261022.py build/szc --report build/anyerror-containment.json
```

Default **never discovers/invokes Zig**, including when `ZIG` is set. It uses
frozen observations and reports `reference_requested:false`, reference validation
count0 and per-row `reference_validated:null`. Exact reference revalidation is
opt-in only:

```sh
python3 tools/check_anyerror_value_containment_20261022.py build/szc --zig /data/toolchain/zig-reference/zig-x86_64-linux-0.17.0/zig --report build/anyerror-full.json
```

Explicit reference must report actual0.17.0. All27 unchanged sources compile via
`build-obj -fno-emit-bin` with private caches and exact ordered primary/status
checks. Notes, carets, locations and full renderer-byte parity are not claimed.
No Zig program is executed, no QBE executable is invoked, no backend ABI/layout
or native safety support is implied.

The checker compiles and runs a **native C frontend inspector** against this
checkout's compiler sources, using `CC` (default cc) and `CFLAGS` (default -O0 -g).
Compiler/probe subprocesses have finite execution+cleanup bounds through the
existing helper; `--build-timeout` defaults180s, per-process `--timeout`20s.
**The default C inspector is NOT sanitized**: plain CC/default -O0 -g does not
add sanitizer flags, even if the supplied candidate binary is sanitized. To
sanitize the inspector too, pass flags explicitly (and required runtime libs):

```sh
CC=gcc CFLAGS="-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all" python3 tools/check_anyerror_value_containment_20261022.py build/asan/szc --report build/anyerror-gcc-sanitized.json
# Repeat with CC=clang for the independent compiler gate.
```

Parent Makefile integration may forward `CC="$(CC)" CFLAGS="$(CFLAGS) $(SAN_FLAGS)"`;
this test patch does not change the Makefile. Sanitizing the candidate alone is
not evidence that the internal C inspector was sanitized. Manual fixed-snapshot
results in this handoff used a sanitized supplied candidate but a normal,
unsanitized C inspector. Full dual-compiler sanitized acceptance is the parent's
separate gate, not claimed here. Runtime-library environment remains caller-owned.
Probe/candidate/source/helper hashes are recorded and checked for stability.
Source/binary equivalence is NOT inferred merely from the candidate pathname.
An optional `--sema-overlay /immutable/sema.c` compiles only the inspector against
that read-only snapshot and records its hash; it never changes this checkout.
Normal integrated-tree runs do not need that option.

## Matrix and accounting

| Group | Cases | Purpose |
|---|---:|---|
| Local slots, const/global/temporary backing, global slot | 6 | No unsupported allocation/backing/global publication |
| Internal/extern/export parameter and result, indirect call | 7 | No unsupported TIR parameters/results/calls |
| Sole/first/last/local-const anyerror varargs | 4 | No sink call despite late argument failure |
| TypeOf of an undefined anyerror VALUE | 1 | No new scalar anyerror value admission |
| anyerror/explicit union/function/pointer type identity | 5 | Type metadata stays accepted |
| Dead anyerror-value branch | 1 | No lexical blanket rejection |
| Ordinary sink/slot/backing witnesses | 3 | Positive nonzero calibration of internal counters |

Zig0.17.0: **18 accepts /9 rejects**. Contract classifications are **9 ACCEPT**,
**9 REFERENCE-VALID-UNSUPPORTED**, **9 REFERENCE-INVALID-NYI-REFUSAL**. A passing
containment refusal earns **zero exact-parity credit**, whether reference-valid
or reference-invalid. Actual CABI/variadic reference primaries remain in the
manifest. s07's reference rejection concerns the exported pointer-to-auto-function
parameter; it is not mislabeled as a reference-valid indirect-call program.

Candidate rejection policy requires status1 and nonempty NYI primaries all
mentioning anyerror; it is deliberately **not** an exact diagnostic/reference
parity assertion. This includes the already-fenced global-anyerror case a06.
All accept controls require status0, no primary errors and nonempty TIR/QBE.
Any timeout/crash, reference drift, new unrelated refusal or control regression
fails. Exit0 means containment policy passes, exit1 contract failure, exit2
infrastructure failure. There is no gap-waiver switch or failure filtering.

## Internal proof, not empty-QBE-only proof

Every case is inspected independently after sema, even when sema rejects.
The inspector scans **all allocated TIR instructions** in all function records,
including failed/unreachable body instructions, not just printed/reachable IR.
It checks direct anyerror/finite/optional/union roots and immutable array-child
edges; pointer/struct graphs are not recursively walked. It records:

- Unsupported TIR result types and ALLOC pointees; all allocations/stores.
- TIR_CALL records, direct sink calls, missing/out-of-range argument references.
- Immutable backing object root value types and actual mutable globals.
- Active anyerror runtime signatures (not merely type declarations).

Refusal fixtures require **zero calls, sink calls, slots, stores, backing
objects, runtime globals and blocked publication counters**. Type-only controls
keep those counters zero. Ordinary controls demand exactly one real sink call,
one slot+store, or one immutable backing object, respectively, preventing a
vacuous all-zero observer. Empty QBE on refusal is an additional driver-output
check, never substituted for these internal assertions.

The inspector handles lazy object-table sentinel allocation: an uninitialized
object table has0 real objects, not unsigned underflow. Reserved global/object
sentinels are excluded. Interned comptime anyerror values are reported separately;
zero ephemeral value-table entries are not confused with runtime publication.
Original strict56 manifest and source hashes are verified before/after every run.

## Attribution

Input source snapshot is sema SHA-256
`611bbfd0b642c6a1317c23d1f27cb971509324b536ed33f12b0ea938060d20cc` over HEAD466f1d7.
It reproduces17 safety-contract failures, preserves all9 positive controls, and
already refuses the global-variable case. The hole is preexisting, not caused
by the finite undefined-carrier change. See the independent boundary audit and
the task handoff report for before/after candidate evidence. This suite neither
adds runtime anyerror support nor claims a full existing-suite acceptance run.
