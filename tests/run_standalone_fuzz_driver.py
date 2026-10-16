#!/usr/bin/env python3
"""Check standalone corpus ordering, deterministic mutations, and input capture."""
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    with tempfile.TemporaryDirectory(prefix="szc-fuzz-driver-") as temporary:
        work = Path(temporary)
        stub = work / "stub.c"
        stub.write_text('''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
\tfor (size_t i = 0; i < size; i++)
\t\tprintf("%02x", (unsigned int)data[i]);
\tputchar('\\n');
\treturn 0;
}
''')
        executable = work / "driver"
        subprocess.run(shlex.split(sys.argv[1]) + [
            "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
            str(ROOT / "fuzz/standalone.c"), str(stub), "-o", str(executable)
        ], check=True)

        def run(*arguments, success=True):
            result = subprocess.run([str(executable), *map(str, arguments)],
                                    capture_output=True, text=True, timeout=15)
            assert (result.returncode == 0) == success, result.stderr
            return result

        first, second = work / "first", work / "second"
        entries = [("z", b"last"), ("a", b"middle"), ("B", b"first"),
                   ("c", b"third"), (".ignored", b"hidden")]
        for directory, ordered in [(first, entries), (second, entries[::-1])]:
            directory.mkdir()
            for name, data in ordered:
                (directory / name).write_bytes(data)
        expected = [b"first".hex(), b"middle".hex(), b"third".hex(), b"last".hex()]
        assert run("-iterations", 0, first).stdout.splitlines()[:-1] == expected
        assert run("-iterations", 200, first).stdout == run("-iterations", 200, second).stdout
        saved = work / "last.bin"
        replay = run("-save-input", saved, "-iterations", 200, first)
        assert saved.read_bytes().hex() == replay.stdout.splitlines()[-2]
        replay = run("-iterations", 0, "-save-input", saved, first / "a")
        assert saved.read_bytes() == b"middle"
        assert replay.stdout.splitlines()[0] == b"middle".hex()
        run("-save-input", work / "missing/last.bin", first, success=False)
        empty = work / "empty"
        empty.mkdir()
        assert len(run("-iterations", 3, empty).stdout.splitlines()) == 4
        assert len(run("-iterations", 0, empty).stdout.splitlines()) == 1
        capped = work / "capped"
        capped.mkdir()
        for i in reversed(range(257)):
            (capped / f"{i:03}").write_bytes(str(i).encode())
        output = run("-iterations", 0, capped).stdout.splitlines()
        assert output[:-1] == [str(i).encode().hex() for i in range(256)]
        assert "256 corpus files" in output[-1]
    print("standalone fuzz driver: ordering, mutations, cap, and checkpoint checks passed")


if __name__ == "__main__":
    main()
