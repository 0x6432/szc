# Bounded compiler subprocess cleanup

The shared `tools/compiler_process.py` helper retains the existing `run` and
`run_compiler` APIs, raw stdout/stderr bytes and success return codes. The ordinary
execution defaults remain 15 seconds (`run`) and 120 seconds (CLI). A timeout is
still a failure; cleanup does not extend the command's execution budget.

## Cleanup contract

After timeout or a post-launch exception, the helper immediately sends SIGKILL to
the remembered process group, even if its leader already exited. Cleanup has a
separate **0.5-second total monotonic allowance**, including a 0.1-second reserved
reap window. Output drainage and direct-child reaping are bounded. No unbounded
`communicate`, `wait`, or `Popen` context-manager exit follows a failure.

Repeated TimeoutExpired byte fields are cumulative snapshots. The newest available
stdout/stderr replaces the previous snapshot; a missing newer field preserves the
previous bytes. Snapshots are never concatenated. Closing local readers at the
cleanup deadline cannot promise unseen or future output.

The triggering exception object, command and timeout remain primary. Separate
attributes report capture/reap completeness, cleanup errors and best captured
bytes. CLI and `run_compiler` warnings do not modify captured byte buffers. The
wrapper also retains the original OSError explanation alongside any captured
compiler output. Cancellation and cleanup errors do not replace the original
failure.

## Containment limits

A process group does not contain descendants that create another session. If an
escaped child holds a pipe open, the helper stops drainage at the cleanup deadline
and reports incomplete capture; it does **not** claim to have terminated that
child. No process-name search, guessed PID or arbitrary descendant kill is used.
A direct child stuck in kernel I/O can also remain unreaped; that condition is
reported rather than followed by an unlimited wait. These are POSIX/Linux
contracts, not universal termination guarantees.

The regression supervisor uses independently recorded process identities, pidfds
and subreaper containment to rescue/reap its own fixtures. That containment is
test-only and is not added to production compiler execution.

## Evidence and tests

`make test` now runs `tests/run_compiler_cleanup_20261019.py`. Its 25 focused tests
include 13 real supervised process invocations and mocked failure branches:
ordinary/nonzero completion, binary output, inherited pipes, exited group leaders,
SIGTERM resistance, escaped descendants, high-volume dual-stream output, cumulative
snapshots, missing fields, bounded reaping, kill/close errors, cancellation,
exception identity and wrapper diagnostics. The existing 67 adapter and ten
safety-observability tests are unchanged and pass independently.

The original helper's unbounded post-kill drainage was reproduced with an escaped
pipe holder. This is a proven helper defect, **not proof of the historical hosted
CI timeout cause**. Native deadlines, panic bytes, SIGABRT and CORE policy remain
unchanged. No hosted-green claim is made.

Parent full fresh GCC/Clang validation passed: warning-clean debug/release,
ASan/UBSan 12,577 unit checks and 1,370 goldens, all frontend protocols, 25 cleanup
checks, 67 adapter checks, ten observability checks, full native/storage/alignment
and 308 safety cases per compiler, plus standalone fuzzing. Four fresh 60-second
libFuzzer runs and both upfront-frozen source-integrity checks pass. Evidence is
retained locally under `/data/validation/cleanup-v2/` with a complete manifest.
The strict 84-case switch audit was independently rerun and retains the checkpoint
below. Local results do not establish hosted CI success. Compiler sources are
byte-for-byte the published a4c4dcf baseline. The rejected forced-comptime prototype
and its independent contract are not part of this cleanup batch; switch status
remains 79 passes, four explicit failures and one expected empty-enum NYI.
