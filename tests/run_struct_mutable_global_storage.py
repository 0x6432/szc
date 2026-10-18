#!/usr/bin/env python3
"""Check writable internal struct globals and immutable controls; two PIE compiles.

Physical padding/undefined zero fill is szc policy, not a Zig differential claim.
C uses raw pointers and offsets, never the ordinary-struct C argument/result ABI.
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
    assert re.search(r"Type:\s+DYN\b", header), "expected explicit PIE"
    assert b"TEXTREL" not in run(["readelf", "-d", str(executable)]).stdout
    sections = {}
    for index, name, address, size, flags in re.findall(
        r"\[\s*(\d+)\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+[0-9a-f]+\s+(\S+)",
        run(["readelf", "-SW", str(executable)]).stdout.decode()
    ):
        sections[index] = (name, int(address, 16), int(size, 16), flags)
    symbols = {}
    for line in run(["readelf", "-sW", str(executable)]).stdout.decode().splitlines():
        fields = line.split()
        if len(fields) == 8:
            symbols[fields[7]] = fields
    relro = []
    page = os.sysconf("SC_PAGE_SIZE")
    for line in run(["readelf", "-lW", str(executable)]).stdout.decode().splitlines():
        fields = line.split()
        if fields and fields[0] == "GNU_RELRO":
            base, size = int(fields[2], 16), int(fields[5], 16)
            relro.append((base // page * page, (base + size) // page * page))
    assert relro, "missing RELRO"
    # Identify the exact tested objects without assuming global-ID numbering.
    ssa = executable.with_suffix(".ssa").read_text()
    call = re.search(r"call \$check_storage\(([^)]*)\)", ssa)
    assert call, "missing physical-check call"
    names = re.findall(r"\bl \$(szc\.(?:g|const)\.\d+)", call[1])
    assert len(names) == 7, names
    for name, extent in zip(names[:4], (80, 16, 16, 1)):
        fields = symbols[name]
        assert fields[4] == "LOCAL", "internal global was exported"
        address = int(fields[1], 16)
        section, base, size, flags = sections[fields[6]]
        assert section in (".data", ".bss") and "A" in flags and "W" in flags
        assert base <= address and address + extent <= base + size
        assert address % alignment == 0
        assert all(address + extent <= lo or address >= hi for lo, hi in relro)
        if alignment == 128:
            assert fields[2:4] == [str(extent), "OBJECT"]
            assert section == ".data", "outlined global must use writable data"
    control = symbols[names[4]]
    address = int(control[1], 16)
    section, _, _, _ = sections[control[6]]
    assert section == ".data.rel.ro"
    assert any(lo <= address and address + 40 <= hi for lo, hi in relro)
    assembly = executable.with_suffix(".s").read_text()
    addends = re.findall(r"\.quad\s+szc\.g\.\d+\s*\+\s*([0-9]+)", assembly)
    assert len([n for n in addends if int(n) != 0]) >= 2, "missing global field relocations"


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    if cc is None:
        raise SystemExit("system C compiler not found")
    fixtures = Path(__file__).resolve().parent / "fixtures"
    template = (fixtures / "struct_mutable_global_storage.zig").read_text()
    helper = fixtures / "struct_mutable_global_storage.c"
    with tempfile.TemporaryDirectory(prefix="szc-struct-mutable-global-") as directory:
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
    print("struct mutable global storage: 2 passed, 0 failed")


if __name__ == "__main__":
    main()
