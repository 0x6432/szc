# Finite error-set durable boundary policies

These twelve files are byte-identical copies of existing frozen strict-30 cases,
not twelve additional unique programs. The source hashes in manifest.json are
checked before compilation. No fixture is executed.

Run `python3 tools/check_error_set_type_boundaries_20261019.py build/szc`.
The default never discovers or invokes Zig. An explicit `--zig /path/to/zig`
option additionally revalidates the pinned 0.17.0 reference status and complete
primary lists, using private caches. Portable candidate discovery, bounded
compiler_process subprocesses and before/after binary hashes are retained.

Candidate checks use emit-qbe, without requiring external QBE:
- Six VALID-UNSUPPORTED-FENCE programs are reference-valid but require explicit
  finite-error-set NYI refusal, NOT language parity credit.
- Three REF-INVALID-NYI-REFUSAL programs (C ABI parameter, C ABI return, undefined
  pointer concat) require the intended NYI refusal, NOT reference-primary parity.
- Two ACCEPT controls require nonempty generated QBE and no primary diagnostics.
- One PARITY-REJECT case pins eager duplicate rejection in an unused declaration.

All refusals require status 1, the entire exact ordered primary list (including
multiplicity) and empty generated stdout. No filtering, deduplication, generic
NYI matching or source rewrites are allowed. The manifest separately attributes
each expected diagnostic to a source operation/function. Independent actual
immutable-candidate output is evidence, not a mechanism for manufacturing
expectations. A new same-operation cascade must be investigated, not blessed.

Empty generated output proves only driver-output refusal. It does not establish
absence of internal Values/TIR, a particular call guard, runtime error values,
ABI correctness, or implementation readiness. Static review and the unfiltered
strict-30 contract remain separate gates.
