#!/usr/bin/env python3
"""Check immutable reference backing objects under PIE and ELF RELRO."""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def run(command, env=None):
    result = subprocess.run(command, capture_output=True, env=env, timeout=30)
    if result.returncode != 0:
        raise AssertionError(result.stderr.decode(errors="replace"))
    return result


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    if cc is None:
        raise SystemExit("system C compiler not found")
    with tempfile.TemporaryDirectory(prefix="szc-reference-storage-") as directory:
        root = Path(directory)
        wrapper = root / "cc-pie"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc) + ' -pie "$@"\n')
        wrapper.chmod(0o755)
        env = dict(os.environ, SZC_CC=str(wrapper))
        executable = root / "program"
        result = run([compiler, "build-exe", "tests/run/const_addresses.zig", "-o", str(executable)], env)
        assert not result.stderr, result.stderr.decode(errors="replace")
        result = run([str(executable)])
        assert not result.stdout and not result.stderr
        header = run(["readelf", "-h", str(executable)]).stdout.decode()
        assert re.search(r"Type:\s+DYN\b", header), "expected PIE executable"
        dynamic = run(["readelf", "-d", str(executable)]).stdout
        assert b"TEXTREL" not in dynamic, "read-only constant pointer caused a text relocation"
        sections = run(["readelf", "-SW", str(executable)]).stdout.decode()
        parsed = {}
        for name, address, size, flags in re.findall(
            r"\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+[0-9a-f]+\s+(\S+)", sections
        ):
            parsed[name] = (int(address, 16), int(size, 16), flags)
        assert "A" in parsed[".rodata"][2] and "W" not in parsed[".rodata"][2]
        address, size, _ = parsed[".data.rel.ro"]
        segments = run(["readelf", "-lW", str(executable)]).stdout.decode()
        protected = False
        for line in segments.splitlines():
            fields = line.split()
            if fields and fields[0] == "GNU_RELRO":
                base, length = int(fields[2], 16), int(fields[5], 16)
                protected |= base <= address and address + size <= base + length
        assert protected, "constant pointer objects are not protected by GNU_RELRO"
    print("reference storage: 4 passed, 0 failed")


if __name__ == "__main__":
    main()
