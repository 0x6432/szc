#!/usr/bin/env python3
"""Check bounded compiler-runner statuses and actionable failure context."""
from contextlib import redirect_stdout
from io import StringIO
import importlib
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compiler_process import run_compiler


def main():
    checked = 0
    source, label = "pub fn main() void {}", "matrix case 17 zig"
    for status in (0, 1, 99):
        command = [sys.executable, "-c", f"import sys; print('out'); print('err', file=sys.stderr); sys.exit({status})"]
        progress = StringIO()
        with redirect_stdout(progress):
            try:
                result = run_compiler(command, source, label, timeout=30)
            except SystemExit as error:
                assert status == 99
                message = str(error)
                assert "unexpected compiler exit 99" in message
                assert source in message and label in message and "out\n" in message and "err\n" in message
            else:
                assert status in (0, 1)
                assert (result.returncode, result.stdout, result.stderr) == (status, b"out\n", b"err\n")
        assert label in progress.getvalue() and command[0] in progress.getvalue()
        checked += 1
    command = ["compiler", "-fno-emit-bin"]
    for failure, expected in (
        (subprocess.TimeoutExpired(command, 30, output=b"partial out", stderr=b"partial err"), "timed out after 30 seconds"),
        (FileNotFoundError("missing compiler"), "could not start compiler"),
    ):
        with patch("compiler_process.run", side_effect=failure) as runner, redirect_stdout(StringIO()):
            try:
                run_compiler(command, source, label, timeout=30)
            except SystemExit as error:
                message = str(error)
                assert expected in message and source in message and label in message and "-fno-emit-bin" in message
                if isinstance(failure, subprocess.TimeoutExpired):
                    assert "partial out" in message and "partial err" in message
            else:
                raise AssertionError("runner hid a compiler failure")
            runner.assert_called_once_with(command, timeout=30)
        checked += 1
    # Exercise actual tool imports and source/actor context outside the repo cwd.
    tool = Path(__file__).resolve().parents[1] / "tools" / "diff_pointer_coercions.py"
    with tempfile.TemporaryDirectory(prefix="szc-runner-cli-") as directory:
        root = Path(directory)
        compiler = root / "fake compiler"
        reference = root / "fake reference"
        reference.write_text("#!/usr/bin/env python3\nimport sys\nprint('reference crash', file=sys.stderr)\nsys.exit(99)\n")
        reference.chmod(0o755)
        for status, actor in ((99, "szc"), (0, "zig")):
            compiler.write_text(f"#!/usr/bin/env python3\nimport sys\nsys.exit({status})\n")
            compiler.chmod(0o755)
            result = subprocess.run([sys.executable, str(tool), str(compiler), str(reference)],
                                    cwd=root, capture_output=True, timeout=10)
            assert result.returncode == 1
            assert f"case 1 {actor}".encode() in result.stderr
            assert b"source:\nfn convert(p: *u8) *u8 { return p; }" in result.stderr
            assert b"unexpected compiler exit 99" in result.stderr and b"Traceback" not in result.stderr
            if actor == "zig":
                assert b"-j2" in result.stderr and b"reference crash" in result.stderr
            checked += 1
    # Check every migrated CLI adapter without re-typechecking all corpora here.
    # These mocks test wiring, not Zig accept/reject behavior (real matrices do that).
    tools = tool.parent
    for script in sorted(tools.glob("diff_*.py")):
        if script.stem == "diff_parse_errors":
            continue
        module = importlib.import_module(script.stem)
        calls = 0

        def check_adapter(command, text, context, timeout):
            nonlocal calls
            actor = "szc" if calls % 2 == 0 else "zig"
            assert context == f"{script.stem} case {calls // 2 + 1} {actor}"
            assert timeout == 120
            assert Path(command[2]).read_text() == text
            if actor == "szc":
                assert command == ["compiler", "tir", command[2]]
            else:
                assert command == ["reference", "build-exe", command[2], "-fno-emit-bin", "-j2"]
            calls += 1
            return subprocess.CompletedProcess(command, 0, b"", b"")

        with patch.object(module, "run_compiler", side_effect=check_adapter), \
                patch.object(sys, "argv", [str(script), "compiler", "reference"]), \
                redirect_stdout(StringIO()):
            module.main()
        assert calls > 0 and calls % 2 == 0
        checked += 1
    print(f"compiler runner: {checked} passed, 0 failed", flush=True)


if __name__ == "__main__":
    main()
