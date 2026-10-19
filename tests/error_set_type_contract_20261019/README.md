# Finite explicit error-set type contract — 20261019

Thirty independent final programs pinned to actual Zig0.17.0. Reference20accept/10reject. Published exactea86b15 baseline: **2 exact matches/28 strict failures, exit1**. No skipped/allowed-failure lanes, no NYI-as-language-parity, no primary relaxation, wrappers or source rewrites. Every source hash is checked before use; full ordered reference primary lists are frozen from actual observations.

```
python3 tools/check_error_set_types_20261019.py /path/to/szc --zig /path/to/zig --report /path/to/report.json
```

Explicit candidate, SZC, repository build paths and PATH support discovery. ZIG/--zig selects reference; its actual version must report0.17.0. --reference-only revalidates all30 without candidate. Existing compiler_process helper enforces finite positive subprocess deadlines (default20s); reference build-obj -j2 -fno-emit-bin uses private caches, candidate stage tir is compile-only. No native execution, external QBE, install or credentials required. Candidate hash must stay stable before/after run. Diagnostic notes/carets/source spans are preserved raw, but automated equality is status + complete ordered primary list, not byte-identical notes.

## Scope, not pass filtering

Final partition:17 potential finite-type foundation targets,2 preserved ordinary controls,11 outside strict failures. Nine outside cases demand representation/ABI/runtime use; two are inherited parser diagnostic mismatches, NOT a request to rewrite generic parser behavior. Coarse manifest existing-control grouping includes the parser controls; independent report gives final implementation scope. No promise that all17 targets close; every actual remaining mismatch stays visible.

Coverage: member-order/alias canonical identity, distinct sets/empty!=noreturn, decoded quoted spelling, unusual/emptyquoted names, usedANDunused duplicates, optional finite children, metatype/TypeOf and pointer qualifiers, zero-array TYPE identity, namespace forward dependency and pointer-only struct laziness. No anyerror, error unions, error.Member value producers or missing reflection builtin assumptions. Internal canonical rendering is not a new @typeName query.

Reference rejects duplicates even in the unused declaration: duplicate_unused_lazy filename does NOT imply lazy duplicate acceptance. All A,A / A,@"A" / A,@"\x41" used variants reject duplicate error set field 'A'. Empty quoted name rejects identifier cannot be empty. Empty error{} is a distinct type from noreturn; no empty-enum terminal/value/layout behavior is inferred.

Representation queries for empty/singleton/two-member sets and natural alignment are reference-legal, but numeric sizes/alignment, runtime numbering, comptime classification and native semantics are NOT inferred from compile acceptance. Optional wrapping of finite sets is type identity only. Runtime load/internal-call boundary sources compare loaded error-valued expressions instead of discarding them. Initial discarded-error drafts were reference-invalid and are retained outside this corpus; no silent substitution or language-parity credit. Their corrected final sources remain outside representation scope.

The undefined pointer [0]E concat reference-rejects illegal undefined behavior. Baseline error_set_decl NYI is not the same diagnostic and receives no parity credit; a future policy NYI refusal also must not be called reference-primary parity. The ordinary defined [0]u8 concat remains accepted as a paired nonerror control.
