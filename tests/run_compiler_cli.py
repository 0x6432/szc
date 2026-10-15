#!/usr/bin/env python3
"""Check the shell compiler adapter's bytes, statuses and descendant cleanup."""
import os
from pathlib import Path
import signal
import sys
import tempfile
import time

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compiler_process import run
from run_process_timeout import descendant_exited


def main():
    runner = Path(__file__).resolve().parents[1] / "tools" / "compiler_process.py"
    prefix = [sys.executable, str(runner)]
    checked = 0
    for status in (0, 1, 7, 99):
        code = f"import os,sys; os.write(1,b'out\\x00\\xff'); os.write(2,b'err\\x00\\xff'); sys.exit({status})"
        result = run([*prefix, "--", sys.executable, "-c", code], timeout=10)
        assert (result.returncode, result.stdout, result.stderr) == (status, b"out\x00\xff", b"err\x00\xff")
        checked += 1
    result = run([*prefix, "--", sys.executable, "-c", "import os,signal; os.kill(os.getpid(),signal.SIGTERM)"], timeout=10)
    assert result.returncode == 128 + signal.SIGTERM
    checked += 1
    result = run([*prefix, "--", "/nonexistent/szc compiler"], timeout=10)
    assert result.returncode == 127 and b"could not start compiler command" in result.stderr
    assert b"/nonexistent/szc compiler" in result.stderr and b"Traceback" not in result.stderr
    checked += 1
    for args in ([], ["--timeout", "0"], ["--timeout", "-1"], ["--timeout", "nan"], ["--timeout", "inf"]):
        result = run([*prefix, *args], timeout=10)
        assert result.returncode == 2 and b"error:" in result.stderr and b"Traceback" not in result.stderr
        checked += 1
    for wait_for_child in (False, True):
        with tempfile.TemporaryDirectory(prefix="szc-cli-cleanup-") as directory:
            root = Path(directory)
            child_file = root / "child pid"
            source = ("import subprocess,sys,time\nfrom pathlib import Path\n"
                      "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)'])\n"
                      f"Path({str(child_file)!r}).write_text(str(child.pid))\n"
                      "print('ready',flush=True)\nprint('partial error',file=sys.stderr,flush=True)\n"
                      + ("time.sleep(60)\n" if wait_for_child else ""))
            started = time.monotonic()
            result = run([*prefix, "--timeout", "3", "--", sys.executable, "-c", source], timeout=10)
            assert result.returncode == 124 and result.stdout == b"ready\n"
            assert b"partial error\n" in result.stderr and b"timed out after 3 seconds" in result.stderr
            assert b"Traceback" not in result.stderr and time.monotonic() - started < 9
            pid = int(child_file.read_text())
            for _ in range(100):
                if descendant_exited(pid):
                    break
                time.sleep(0.01)
            else:
                os.kill(pid, signal.SIGKILL)
                raise AssertionError("compiler descendant survived timeout")
            checked += 1
    print(f"compiler CLI: {checked} passed, 0 failed", flush=True)


if __name__ == "__main__":
    main()
