#!/usr/bin/env python3
"""Check safety-harness output, exit status and complete process-group cleanup."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch

sys.dont_write_bytecode = True
from run_safety import run


def descendant_exited(pid):
    try:
        state = Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1]
    except (FileNotFoundError, ProcessLookupError):
        return True
    return state.startswith("Z ")


def main():
    result = run([sys.executable, "-c", "import sys; print('out'); print('err', file=sys.stderr); sys.exit(7)"])
    assert (result.returncode, result.stdout, result.stderr) == (7, b"out\n", b"err\n")
    result = run([sys.executable, "-c", "import os, signal; os.kill(os.getpid(), signal.SIGTERM)"])
    assert result.returncode == -signal.SIGTERM
    checked = 2
    # Reaping can race either opening procfs (ENOENT) or reading it (ESRCH).
    for error in (FileNotFoundError(), ProcessLookupError()):
        with patch.object(Path, "read_text", side_effect=error):
            assert descendant_exited(123)
        checked += 1
    for state, exited in (("Z", True), ("S", False)):
        with patch.object(Path, "read_text", return_value=f"123 (python3) {state} 1"):
            assert descendant_exited(123) == exited
        checked += 1
    # Cover a waiting driver and a driver that exits while its child keeps pipes open.
    for wait_for_child in (False, True):
        with tempfile.TemporaryDirectory(prefix="szc-process-timeout-") as directory:
            child_file = Path(directory) / "child"
            source = ("import subprocess, sys, time\n"
                      "from pathlib import Path\n"
                      "child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])\n"
                      f"Path({str(child_file)!r}).write_text(str(child.pid))\n"
                      "print('ready', flush=True)\n"
                      + ("time.sleep(60)\n" if wait_for_child else ""))
            started = time.monotonic()
            try:
                run([sys.executable, "-c", source], timeout=3)
                raise AssertionError("process with inherited pipes did not time out")
            except subprocess.TimeoutExpired as error:
                assert error.timeout == 3 and error.stdout == b"ready\n" and error.stderr == b""
                assert time.monotonic() - started < 8, "cleanup waited for a descendant"
            pid = int(child_file.read_text())
            # A killed orphan can briefly remain as a zombie until PID 1 reaps it.
            for _ in range(100):
                if descendant_exited(pid):
                    break
                time.sleep(0.01)
            else:
                os.kill(pid, signal.SIGKILL)
                raise AssertionError("descendant survived")
            checked += 1
    print(f"process timeout: {checked} passed, 0 failed", flush=True)


if __name__ == "__main__":
    main()