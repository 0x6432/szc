# Durable optional representation-boundary contract

Exactly eight **existing final source byte copies**, not new programs: six independently pinned supplemental boundaries plus the optional/ordinary concat regression pair. The original frozen30 corpus, runner and expectations are unchanged. Sources are SHA256-checked against this manifest before compilation. No optional fixture may be executed.

## Candidate-only default (no Zig dependency)

```
python3 tools/check_optional_type_boundaries_20261019.py /path/to/szc --report /path/to/report.json
```

`SZC`, repository build paths and PATH also support candidate discovery. Omitted `--zig` means no Zig lookup/version call/reference compilation—even if `ZIG` is set. Every compiler invocation uses existing `compiler_process.run` with finite positive deadlines (default20s), bounded cleanup and abnormal-process reporting. Sanitized binaries may require caller-supplied library environment; the runner does not install dependencies or hide infrastructure failures.

Candidate stage is **emit-qbe**, not tir: seven refusals must return exactly1, the complete exact intended primary list, and **empty generated stdout**. The ordinary nonoptional concat must return0, no error primaries, and nonempty generated output. No external QBE, assembly/linking or native execution is invoked. `tir` intentionally prints partial diagnostic output and the old30 runner is not altered.

## Optional reference revalidation

```
python3 tools/check_optional_type_boundaries_20261019.py /path/to/szc --zig /path/to/zig --report /path/to/report.json
```

This verifies actual Zig0.17.0 and rechecks frozen reference status/ordered-primary lists with build-obj -j2 -fno-emit-bin in private temporary caches. Six boundaries and the ordinary concat reference-accept; the undefined optional-pointer concat reference-rejects with undefined-value illegal-behavior primary. No reference expectation is inferred from candidate output.

## Policy labels, not optional language-parity credit

- **VALID-UNSUPPORTED-FENCE** (six): reference-valid programs intentionally refused at unsupported optional representation/signature demand.
- **REF-INVALID-NYI-REFUSAL** (one): optional concat regression must explicitly refuse with optional-layout NYI. This is NOT exact-primary parity with the reference undefined-value language error.
- **ACCEPT** (one): defined ordinary `[0]u8` pointer concat must remain supported.

Exact refusal primary is `not supported by szc yet: optional value or layout demand`, except the runtime-address zero-argument indirect call, which requires `not supported by szc yet: optional values in runtime function signatures`. Any unrelated NYI, duplicate/additional primary, abnormal process, changed source, accepted optional witness or leaked generated stdout fails. All eight cases remain visible; no failure filtering/wrapping/source rewrites.

Empty emit-qbe output proves **driver output refusal only**, not absence of internal TIR/comptime backing or attribution to a particular call guard. Signature diagnosis is checked precisely, but guard ordering and successful pointer creation require independent source/TIR review. This runner does not claim those facts from stdout alone.

## Negative controls and scope

Published exact5e13779 baseline must fail seven intended-primary checks (earlier optional_type NYI) while ordinary concat passes. Rejected v1 must fail optional concat acceptance while preserving the other seven policy outcomes. Reviewed v2 is expected to satisfy all eight policy outcomes; that does not mean eight optional language-parity passes or full compiler approval.

Parent owns the Makefile full-gate hook. This patch does not edit Makefile, existing tests, compiler source, frozen30 or prior evidence. Publication requires that integration hook and parent full regression/static review, not merely these eight observations.
