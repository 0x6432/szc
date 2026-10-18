#!/usr/bin/env python3
"""Check szc immutable struct bytes, rooted relocations, alignment and PIE/RELRO.

Two compiles cover normal QBE data and the above-64 outlined assembly path.
Undefined zero fill is szc serializer policy, so this is not a Zig differential
check; logical/reference semantics remain covered by the separate fixture suite.
"""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def run(command, env=None, timeout=120):
    result = subprocess.run(command, capture_output=True, env=env, timeout=timeout)
    assert result.returncode == 0, (command, result.returncode,
                                    result.stdout.decode(errors="replace"),
                                    result.stderr.decode(errors="replace"))
    return result


def check_elf(executable, alignment):
    header = run(["readelf", "-h", str(executable)]).stdout.decode()
    assert re.search(r"Type:\s+DYN\b", header), "expected explicit PIE executable"
    dynamic = run(["readelf", "-d", str(executable)]).stdout
    assert b"TEXTREL" not in dynamic, "struct pointers caused a text relocation"
    sections = run(["readelf", "-SW", str(executable)]).stdout.decode()
    parsed = {}
    for index, name, address, size, flags in re.findall(
        r"\[\s*(\d+)\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+[0-9a-f]+\s+(\S+)", sections
    ):
        parsed[name] = (index, int(address, 16), int(size, 16), flags)
    assert "A" in parsed[".rodata"][3] and "W" not in parsed[".rodata"][3]
    section_index, address, size, flags = parsed[".data.rel.ro"]
    assert "A" in flags and "W" in flags and size >= 80
    protected = False
    for line in run(["readelf", "-lW", str(executable)]).stdout.decode().splitlines():
        fields = line.split()
        if fields and fields[0] == "GNU_RELRO":
            base, length = int(fields[2], 16), int(fields[5], 16)
            protected |= base <= address and address + size <= base + length
    assert protected, "struct pointer objects lack full GNU_RELRO coverage"
    symbols = run(["readelf", "-sW", str(executable)]).stdout.decode()
    root_objects = []
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 8 and fields[7].startswith("szc.const."):
            assert fields[4] == "LOCAL", "internal struct backing exported"
            if fields[2:4] == ["80", "OBJECT"] and fields[6] == section_index:
                root_objects.append(fields)
    if alignment == 128:
        assert len(root_objects) == 1, "expected one outlined Root OBJECT of size80"
        assert int(root_objects[0][1], 16) % 128 == 0, "outlined Root is misaligned"
    assembly = executable.with_suffix(".s").read_text()
    addends = re.findall(r"\.quad\s+szc\.const\.\d+\s*\+\s*([0-9]+)", assembly)
    assert len([n for n in addends if int(n) != 0]) >= 2, "missing stored field/nested pointer addends"


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    if cc is None:
        raise SystemExit("system C compiler not found")
    fixtures = Path(__file__).resolve().parent / "fixtures"
    template = (fixtures / "struct_static_storage.zig").read_text()
    helper = fixtures / "struct_static_storage.c"
    with tempfile.TemporaryDirectory(prefix="szc-struct-static-storage-") as directory:
        root = Path(directory)
        wrapper = root / "cc-pie"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc)
                           + ' -fPIE -pie -Wl,-z,relro,-z,now "$@" '
                           + shlex.quote(str(helper)) + "\n")
        wrapper.chmod(0o755)
        env = dict(os.environ, SZC_CC=str(wrapper))
        for alignment in (8, 128):
            source = root / f"struct-{alignment}.zig"
            source.write_text(template.replace("STORAGE_ALIGNMENT", str(alignment)))
            executable = root / f"struct-{alignment}"
            result = run([compiler, "build-exe", str(source), "-o", str(executable), "--keep-temps"], env)
            assert not result.stdout and not result.stderr
            result = run([str(executable)], timeout=15)
            assert not result.stdout and not result.stderr
            check_elf(executable, alignment)
    print("struct static storage: 2 passed, 0 failed")


if __name__ == "__main__":
    main()
