"""Bounded compiler subprocesses with complete process-group timeout cleanup."""
import os
import shlex
import signal
import subprocess


def run(command, timeout=15):
    # Descendants can inherit output pipes even after the immediate driver exits.
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          start_new_session=True) as process:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            stdout, stderr = process.communicate()
            raise subprocess.TimeoutExpired(command, timeout, output=stdout, stderr=stderr)
        return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)


def run_compiler(command, source, label, timeout):
    print(f"checking {label}: {shlex.join(command)}", flush=True)
    try:
        result = run(command, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.output or b"", error.stderr or b""
        reason = f"timed out after {timeout} seconds"
    except OSError as error:
        stdout, stderr = b"", str(error).encode()
        reason = "could not start compiler"
    else:
        if result.returncode in (0, 1):
            return result
        stdout, stderr = result.stdout, result.stderr
        reason = f"unexpected compiler exit {result.returncode}"
    raise SystemExit(f"{label}: {reason}\ncommand: {shlex.join(command)}\n"
                     f"source:\n{source}\nstdout:\n{stdout.decode(errors='replace')}\n"
                     f"stderr:\n{stderr.decode(errors='replace')}")
