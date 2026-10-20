# Separate two-program vararg containment regression

Exactly TWO source copies of the separately parent-authorized appendix:
known symbolic error variadic argument and ordinary explicit c_int control.
Neither belongs to strict60 or durable26; no existing source/expectation changes.
No error fixture execution, native linking or external QBE occurs.

Run `python3 tools/check_error_family_varargs_20261019.py build/szc`.
Default never discovers/invokes Zig. Explicit `--zig /path/to/zig` revalidates
actual0.17.0 plus frozen status/full ordered primary lists in private caches.
Existing compiler_process bounds execution/cleanup. Sources/binaries are hashed.

The error witness is REFERENCE-CABI-INVALID: actual Zig rejects
`cannot pass 'error{A}' to variadic function`. Candidate deliberately refuses
with the approved finite-error value/layout NYI. This is honest
REF-INVALID-NYI-REFUSAL policy, NOT reference-primary/language parity.

Both candidate stages are inspected:
- tir: exact status/ordered primary list; refused witness MUST contain no sink
  call and no `%4294967295` invalid NONE reference; c_int MUST retain its valid
  sink call with no invalid NONE reference.
- emit-qbe: exact status/ordered primary list; refused witness emits no generated
  stdout, while ordinary control emits QBE with a retained sink call.
- Timeouts, abnormal processes and sanitizer findings are failures, never
  interpreted as semantic rejection or proof of containment.

Frozen V1 negative control publishes `call sink(%4294967295)` despite NYI,
while QBE output remains empty. Thus empty driver output alone CANNOT prove
safe internal TIR. V2 is required to preflight/refuse before publishing the call
and preserve the ordinary call. Separate immutable source audit/full validation
remain necessary beyond these bounded invariants.
