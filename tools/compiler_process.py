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
        return 124
    except OSError as error:
        print(f"could not start compiler command: {shlex.join(command)}: {error}", file=sys.stderr)
        return 127
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    return result.returncode if result.returncode >= 0 else 128 - result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
