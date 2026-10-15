#!/usr/bin/env python3
"""Check reference compiler failures cannot masquerade as expected program exits."""
import json
from pathlib import Path
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compiler_process import run


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    harness = Path(__file__).resolve().parent / "run_e2e.sh"
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-e2e-adapter-") as directory:
        root = Path(directory)
        source = root / "test source.zig"
        source.write_text("// exit: 7\npub fn main() u8 { return 7; }\n")
        reference = root / "fake reference"
        recorded = root / "arguments"
        for status in (0, 1, 7, 99):
            reference.write_text("#!/usr/bin/env python3\nimport sys,json\nfrom pathlib import Path\n"
                                 f"Path({str(recorded)!r}).write_text(json.dumps(sys.argv[1:]))\n"
                                 + ("out = next(a.split('=',1)[1] for a in sys.argv if a.startswith('-femit-bin='))\n"
                                    "Path(out).write_text('#!/bin/sh\\nexit 7\\n'); Path(out).chmod(0o755)\n" if status == 0 else "")
                                 + f"sys.exit({status})\n")
            reference.chmod(0o755)
            # The harness finds its Python runner from its own location, not cwd.
            result = run(["env", "-C", str(root), "ZIG=" + str(reference), str(harness), compiler, str(source)], timeout=30)
            args = json.loads(recorded.read_text())
            assert args[:3] == ["build-exe", str(source), "-j2"]
            assert len(args) == 4 and args[3].startswith("-femit-bin=")
            assert ("checking e2e: " + str(source)).encode() in result.stdout
            if status == 0:
                assert result.returncode == 0 and b"e2e: 1 passed, 0 failed" in result.stdout
            else:
                assert result.returncode == 1 and b"FAIL (reference compilation)" in result.stdout
                assert b"e2e: 0 passed, 1 failed" in result.stdout
            checked += 1
    print(f"e2e adapter: {checked} passed, 0 failed", flush=True)


if __name__ == "__main__":
    main()
