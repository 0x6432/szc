#!/usr/bin/env python3
"""Check root offsets, writable PIE relocations and foreign global-array mutations."""
import os
from pathlib import Path
import re
import shlex
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
from run_array_storage import run, sections


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    assert cc
    passed = 0
    with tempfile.TemporaryDirectory(prefix="szc-global-array-storage-") as directory:
        root = Path(directory)
        helper = root / "foreign.c"
        helper.write_text("#include <stdint.h>\n#include <string.h>\n"
                          "uint8_t verify(void *base, void *element, void *table, uint64_t alignment) {\n"
                          " unsigned char *b = base; void *entries[2]; uint16_t n;\n"
                          " if ((uintptr_t)b % alignment || element != b + 8) return 1;\n"
                          " memcpy(entries, table, sizeof entries);\n"
                          " if (entries[0] != b + 8 || entries[1] != b) return 2;\n"
                          " memcpy(&n, element, 2); if (n != 5) return 3;\n"
                          " n = 31; memcpy(element, &n, 2); entries[1] = b + 10;\n"
                          " memcpy(table, entries, sizeof entries); return 0;\n}\n")
        wrapper = root / "cc-pie"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc)
                           + ' -fPIE -pie -Wl,-z,relro,-z,now "$@" ' + shlex.quote(str(helper)) + "\n")
        wrapper.chmod(0o755)
        env = dict(os.environ, SZC_CC=str(wrapper))
        for alignment in (1, 2, 16, 256):
            source = root / "test.zig"
            source.write_text(f"var a: [2][3]u16 align({alignment}) = .{{.{{1,2,3}},.{{4,5,6}}}};\n"
                              "const element: *align(1) u16 = &a[1][1];\n"
                              "const readonly: *align(1) const u16 = element;\n"
                              "var table: [2]*align(1) u16 = .{element,&a[0][0]};\n"
                              "extern fn verify(base: *align(1) [2][3]u16, elem_arg: *align(1) u16, "
                              "table_arg: *[2]*align(1) u16, alignment: u64) u8;\n"
                              "pub fn main() u8 { const saved = a; "
                              f"const n = verify(&a,element,&table,{alignment}); if (n != 0) return n; "
                              "if (readonly.* != 31 or a[1][1] != 31 or saved[1][1] != 5) return 4; "
                              "table[1].* = 47; if (a[1][2] != 47) return 5; return 0; }\n")
            executable = root / "native"
            run([compiler, "build-exe", str(source), "-o", str(executable)], env)
            assert run([str(executable)]) == b""
            assert "DYN" in run(["readelf", "-h", str(executable)]).decode()
            assert b"TEXTREL" not in run(["readelf", "-d", str(executable)])
            globals_found = re.findall(r"^global (\S+):", run([compiler, "tir", str(source)]).decode(), re.M)
            assert len(globals_found) == 2
            section_map = sections(executable)
            symbol_rows = [row.split() for row in run(["readelf", "-sW", str(executable)]).decode().splitlines()]
            for index, symbol in enumerate(globals_found):
                matches = [fields for fields in symbol_rows if fields and fields[-1] == symbol]
                assert len(matches) == 1
                fields = matches[0]
                assert fields[4] == "LOCAL" and "W" in section_map[int(fields[6])][4]
                assert section_map[int(fields[6])][0] != ".data.rel.ro"
                if index == 0:
                    assert int(fields[1], 16) % alignment == 0
            zig = os.environ.get("ZIG")
            if zig:
                reference = root / "reference"
                run([zig, "build-exe", str(source), str(helper), "-lc", "-j2", f"-femit-bin={reference}"], timeout=120)
                assert run([str(reference)]) == b""
            passed += 1
    print(f"global array storage: {passed} passed, 0 failed")


if __name__ == "__main__":
    main()
