# Bounded empty-enum terminal dereference

This WIP follow-up is based on `0472f8b`, not completed M6.3, general uninhabited
types, full comptime, bootstrap, or hosted-CI acceptance.

## Admission and representation

Only direct value dereference (`IR_DEREF`) of an OPERAND_RUNTIME single pointer
with a direct exhaustive empty-enum child may terminate. Existing pointer checks
run first; existing enum-layout resolution must succeed with noreturn backing.
A real TIR builder is required, outside TypeOf/comptime. The pointer expression
and its preceding effects are retained. Emit the existing TIR trap; return an
explicit local terminal outcome before map insertion. No E value, scalar load,
volatile load, tag, comparison, storage or ABI argument is fabricated.

Known pointers (including globals, physical addresses and undefined values),
reference formation `&p.*`, shared loads, nonempty/singleton enums, type queries,
comptime and by-value ABI/storage retain their previous paths. Existing joins,
nonlocal break/continue target exits and cleanup machinery are unchanged. Defers
after a terminal producer are not proactively executed; surviving sibling exits
retain their cleanup. A terminal dereference reached inside cleanup stops it.
The backend uses its existing trap lowering; native panic/trap parity is not
claimed and these fixtures MUST NOT be executed.

Duplicate `else` is preflighted structurally before gated switch forms. It is
rejected at the second `else` even if the operand would terminate. Semantic label
and body checking remains lazy, while ordinary lexical checks stay eager. The
existing compound protocol fixture is preserved exactly and explicitly changes
from successful IR generation to this precise structural error; a separate
no-duplicate-else semantic-invalid control still requires successful IR generation.
No golden files or reference expectations were regenerated.

## Focused evidence and limitations

Run the compile-only contract with actual pinned Zig 0.17.0:

```sh
python3 tools/check_empty_enum_terminal_20261019.py build/asan/szc \
  --zig /path/to/zig-0.17.0 --report empty-enum.json
```

All 30 sources remain visible: 25 exact matches (18 accepts / seven rejects),
five strict failures. This improves all 14 scoped targets and preserves all 11
baseline matches. The five failures are earlier @compileError admission, TypeOf
wrapping, internal E signature, exported E ABI and E storage. They are not filtered
or counted as parity. The runner intentionally exits nonzero. Six separate
reference-gated controls cover structural precedence, effectful pointer evaluation
and a terminal dereference reached inside cleanup. Original, call-prefix and
cleanup programs produce TIR/QBE and assemble into objects; none is executed.

The original 84-case switch suite advances from 79/4/1-NYI to 80 passes / four
failures. Only the empty-enum fixture classification changes from expected NYI
to required stage acceptance, in both harness CLI modes; its bytes and the other
83 cases are preserved. Four failures remain: three earlier producer gates and
one direct enum forced-comptime primary mismatch. M6.3 stays partial.

Additional admission observations remain separate: singleton enum(u0) is not
terminal; invalid enum(u8){} retains its backing error; known absolute pointers
remain unsupported. The pinned reference crashes on a known undefined-pointer
probe, reproduced once unchanged. That is abnormal/unresolved process evidence,
not language acceptance/rejection or parity; preserve candidate validation and
do not emulate or rerun the crash. Candidate admission outputs are unchanged from
the published baseline for these excluded cases.

A separate forced-comptime prototype was rejected for breaking valid nonlocal
break/continue. None of its code, control barriers or IR-golden changes is included.

Parent focused evidence is under /data/analysis/empty-enum-parent. Fresh frozen
GCC/Clang regression and fuzz gate records are separate under
/data/validation/empty-enum; do not infer a hosted CI result from local tests.
