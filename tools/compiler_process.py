"""Bounded compiler subprocesses with byte-preserving process-group cleanup."""
import os
import shlex
import signal
import subprocess
import time


# Execution budgets remain unchanged. Cleanup has one separate, finite budget;
# reserve part of it for reaping even when an escaped descendant holds a pipe.
_CLEANUP_TIMEOUT = 0.5
_REAP_RESERVE = 0.1


def _cleanup_warning(error):
    details = []
    if getattr(error, "cleanup_capture_incomplete", False):
        details.append("output capture did not reach EOF")
    if getattr(error, "cleanup_reap_incomplete", False):
        details.append("direct child was not reaped")
    details.extend(getattr(error, "cleanup_errors", ()))
    return "cleanup incomplete: " + "; ".join(details) if details else ""


def _cleanup(process, error):
    """Best-effort bounded cleanup; keep the triggering exception primary.

    Never target escaped descendants: only the session/process group we created.
    TimeoutExpired byte fields are cumulative snapshots, not incremental chunks.
    An additional cancellation during cleanup is recorded rather than replacing
    the original exception. Popen context-manager exit is deliberately avoided.
    """
    deadline = time.monotonic() + _CLEANUP_TIMEOUT
    stdout = getattr(error, "output", None)
    stderr = getattr(error, "stderr", None)
    capture_incomplete = True
    reap_incomplete = True
    errors = []

    def record(operation, failure):
        errors.append(f"{operation}: {type(failure).__name__}: {failure}")

    # Do not gate group cleanup on leader liveness: its children may still live.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    except BaseException as failure:
        record("SIGKILL process group", failure)
    try:
        remaining = max(0.0, deadline - time.monotonic() - _REAP_RESERVE)
        if remaining:
            try:
                latest_stdout, latest_stderr = process.communicate(timeout=remaining)
                if latest_stdout is not None:
                    stdout = latest_stdout
                if latest_stderr is not None:
                    stderr = latest_stderr
                capture_incomplete = False
            except subprocess.TimeoutExpired as failure:
                if failure.output is not None:
                    stdout = failure.output
                if failure.stderr is not None:
                    stderr = failure.stderr
            except BaseException as failure:
                record("drain output", failure)
    finally:
        # Closing local readers bounds drainage; it cannot promise termination
        # of a descendant that changed sessions. Do not guess its PID/identity.
        for stream in (process.stdout, process.stderr):
            if stream is not None:
                try:
                    stream.close()
                except BaseException as failure:
                    record("close output pipe", failure)
        try:
            process.wait(timeout=max(0.0, deadline - time.monotonic()))
            reap_incomplete = False
        except subprocess.TimeoutExpired:
            pass
        except BaseException as failure:
            record("reap direct child", failure)

    # Separate metadata never changes the captured compiler bytes. Preserve
    # exception identity/traceback for cancellation, OSError and other failures.
    error.cleanup_stdout = stdout if stdout is not None else b""
    error.cleanup_stderr = stderr if stderr is not None else b""
    error.cleanup_capture_incomplete = capture_incomplete
    error.cleanup_reap_incomplete = reap_incomplete
    error.cleanup_errors = tuple(errors)
    if isinstance(error, subprocess.TimeoutExpired):
        error.output, error.stderr = error.cleanup_stdout, error.cleanup_stderr
    warning = _cleanup_warning(error)
    if warning and hasattr(error, "add_note"):
        error.add_note(warning)


def run(command, timeout=15):
    # Descendants can inherit output pipes even after the immediate driver exits.
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except BaseException as error:
        try:
            _cleanup(process, error)
        except BaseException as failure:
            # Even an unexpected cleanup interruption must not replace the
            # triggering exception. No unbounded context-manager exit follows.
            error.cleanup_capture_incomplete = True
            error.cleanup_reap_incomplete = True
            error.cleanup_errors = getattr(error, "cleanup_errors", ()) + (
                f"cleanup interrupted: {type(failure).__name__}: {failure}",)
            if hasattr(error, "add_note"):
                error.add_note(_cleanup_warning(error))
        raise
    return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)


def run_compiler(command, source, label, timeout):
    print(f"checking {label}: {shlex.join(command)}", flush=True)
    try:
        result = run(command, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.output or b"", error.stderr or b""
        reason = f"timed out after {timeout} seconds"
        warning = _cleanup_warning(error)
        if warning:
            reason += f"; {warning}"
    except OSError as error:
        stdout = getattr(error, "cleanup_stdout", b"")
        stderr = getattr(error, "cleanup_stderr", str(error).encode())
        reason = f"could not start compiler: {error}"
        warning = _cleanup_warning(error)
        if warning:
            reason += f"; {warning}"
    else:
        if result.returncode in (0, 1):
            return result
        stdout, stderr = result.stdout, result.stderr
        reason = f"unexpected compiler exit {result.returncode}"
    raise SystemExit(f"{label}: {reason}\ncommand: {shlex.join(command)}\n"
                     f"source:\n{source}\nstdout:\n{stdout.decode(errors='replace')}\n"
                     f"stderr:\n{stderr.decode(errors='replace')}")


def main():
    """Shell adapter sharing the same whole-process-group compilation budget."""
    import argparse
    import math
    import sys

    def positive_timeout(text):
        value = float(text)
        if not math.isfinite(value) or value <= 0:
            raise argparse.ArgumentTypeError("timeout must be finite and greater than zero")
        return value

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=positive_timeout, default=120)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a compiler command is required")
    try:
        result = run(command, timeout=args.timeout)
    except subprocess.TimeoutExpired as error:
        sys.stdout.buffer.write(error.output or b"")
        sys.stderr.buffer.write(error.stderr or b"")
        sys.stderr.buffer.flush()
        print(f"compiler command timed out after {args.timeout:g} seconds: {shlex.join(command)}", file=sys.stderr)
        warning = _cleanup_warning(error)
        if warning:
            print(warning, file=sys.stderr)
        return 124
    except OSError as error:
        sys.stdout.buffer.write(getattr(error, "cleanup_stdout", b""))
        sys.stderr.buffer.write(getattr(error, "cleanup_stderr", b""))
        print(f"could not start compiler command: {shlex.join(command)}: {error}", file=sys.stderr)
        warning = _cleanup_warning(error)
        if warning:
            print(warning, file=sys.stderr)
        return 127
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    return result.returncode if result.returncode >= 0 else 128 - result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
