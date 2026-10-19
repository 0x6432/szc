# Optional type identity contract (20261019)

Thirty reference-driven compile-only cases pinned to actual Zig0.17.0. Never execute these fixtures. Candidate runs `tir`; reference runs `build-obj -j2 -fno-emit-bin`; all subprocesses have finite compiler_process deadlines and private reference caches. Explicit candidate/--zig, SZC/ZIG, local build paths and PATH support portable discovery. Expected status and complete ordered primary lists are immutable; notes/carets are preserved raw but not automated equality assertions.

Run from any directory:
```
python3 tools/check_optional_types_20261019.py /path/to/szc --zig /path/to/zig --report /path/to/report.json
```

The exact5e13779 baseline is deliberately nonzero:1 exact core control pass,29 strict failures (20 status mismatches,9 unexpected NYI). No allowlist, skipping, failure filtering, source rewriting or forced wrapper erases original diagnostics. A type-only implementation does NOT close optional values/coercions/storage/ABI/unwrap/switch support.

Final approved scope has18 target failures plus1 preserved core pass;11 outside failures remain visible. The three exact @typeName observations are earlier builtin NYI because baseline does not implement @typeName; no new builtin is authorized. Nominal opaque container is an earlier frontend gate. Seven other cases explicitly exercise optional value/layout/ABI/use boundaries. Coarse manifest category tags describe original retrieval grouping, not permission to implement outside features; the final scope table is in the independent report.

`?noreturn`, `?type`, `?comptime_int`, `?comptime_float` and `?void` are legal TYPE identities in0.17.0. Do not reject them just to simplify unsupported value/layout policy. Nominal opaque cannot be optional. The separately authorized builtin `?anyopaque` admission record lives outside this30-case corpus.

The original optional-switch source is copied byte-for-byte from tests/switch_contract/errors/switch_operand_optional.zig; runner verifies identity. Reference rejects switch-on-optional. Baseline instead stops at optional_type NYI; that is not matching-language-diagnostic credit.
