# Error queries and ordinary ignored results — frozen 56-case contract

Exact baseline: `e594a4ddd3fb2f05d24fe102fd12332b4b4c9d16`.

24 ordinary ignored-result programs, 16 known error-name programs, and 16 context-driven error-cast programs share one cap. Sources and expectations are immutable; no fixture is executed. Initial drafts, discarded hypotheses and same-slot corrections remain in the external evidence report. No reflection or assertion builtin is assumed: false-branch typed-constant failures follow the existing contract convention.

Run `python3 tools/check_error_queries_20261019.py --zig /path/to/zig build/szc --report /tmp/error-query.json`. Use `--reference-only` for reference revalidation. The reference must actually report version 0.17.0. Discovery is explicit path, ZIG/SZC, local candidate build or PATH. Compiler_process bounds execution and cleanup; caches are private.

Every program remains visible. Candidate status and entire ordered primary list must match the reference; NYI, abnormal processes, sanitizer reports, timeouts and infrastructure errors never earn parity. Raw diagnostics preserve notes/carets, but this strict runner does not assert caret or note equality. Runtime producers and undefined-value demand may remain unsupported; declared scope is not a promise of closure. Existing quoted structural type spelling can remain an exact-primary mismatch.

Reference-pinned findings: known `@errorName(error.A)` has type `*const [1:0]u8`; decoded escaped bytes and sentinel access are checked. `@errorCast` needs an actual result type; it permits known member narrowing even where implicit subset coercion rejects, but rejects nonmembers. Ordinary bare expressions, singleton loop bodies/continuations and value-producing control expressions have ignored-result obligations; consumed values, explicit discards, void/noreturn and nonlocal transfers remain controls.

A combined rejecting program does not prove its positive function independently; expected diagnostic multiplicity is attributed to distinct operations and unselected assertion bodies are only type-checked if selected. No source wrapper, filtering, forced context or native execution is used. This test-only contract does not add runtime error storage, registry, ABI or error-union cast support.
