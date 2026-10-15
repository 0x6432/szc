#!/usr/bin/env python3
"""Exercise unbounded borrowed pointers, foreign storage and root-relative relocations."""
import os
from pathlib import Path
import shlex
import resource
import signal
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
from run_array_storage import run, run_bounded


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    assert cc
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    passed = 0
    with tempfile.TemporaryDirectory(prefix="szc-many-storage-") as directory:
        root = Path(directory)
        helper = root / "foreign.c"
        helper.write_text("#include <stdint.h>\n#include <string.h>\n"
                          "static unsigned char data[32];\n"
                          "const void *odd(void) { return data+1; }\n"
                          "const void *even(void) { return data; }\n"
                          "const void *foreign(void) { uint16_t n = 43; memcpy(data+7,&n,2); return data+1; }\n"
                          "uint8_t verify(void *base, void *element, void *slots, uint64_t alignment) {\n"
                          " unsigned char *b = base; void *entries[2]; uint16_t n;\n"
                          " if ((uintptr_t)b % alignment || element != b + 8) return 1;\n"
                          " memcpy(entries,slots,sizeof entries);\n"
                          " if (entries[0] != b || entries[1] != b + 6) return 2;\n"
                          " memcpy(&n,b+8,2); if (n != 5) return 3;\n"
                          " n = 31; memcpy(b+8,&n,2); return 0; }\n")
        wrapper = root / "cc-pie"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc)
                           + ' -fPIE -pie -Wl,-z,relro,-z,now "$@" ' + shlex.quote(str(helper)) + "\n")
        wrapper.chmod(0o755)
        env = dict(os.environ, SZC_CC=str(wrapper))
        for alignment in (1, 2, 16, 256):
            source = root / "test.zig"
            source.write_text(f"var a: [2][3]u16 align({alignment}) = .{{.{{1,2,3}},.{{4,5,6}}}};\n"
                              "const rows: [*]align(1) [3]u16 = &a;\n"
                              "const second: [*]align(1) u16 = &a[1];\n"
                              "const element = &rows[1][1];\n"
                              "const table: [2][*]align(1) u16 = .{&a[0],second};\n"
                              "extern fn foreign() [*]align(1) const u16;\n"
                              "extern fn verify(base: [*]align(1) [3]u16, elem_arg: *align(1) u16, "
                              "slots: *const [2][*]align(1) u16, alignment: u64) u8;\n"
                              "fn read(p: [*]align(1) const u16, i: usize) u16 { return p[i]; }\n"
                              "fn modify(p: [*]align(1) volatile u16, i: usize) void { p[i] += 2; }\n"
                              "fn advance(p: [*]align(1) const u16, i: usize) [*]align(1) const u16 { return p+i; }\n"
                              "fn retreat(p: [*]align(1) const u16, i: usize) [*]align(1) const u16 { return p-i; }\n"
                              "fn delta(p: [*]align(1) const u16, q: [*]align(1) const u16) usize { return p-q; }\n"
                              "pub fn main() u8 { const saved = rows[1]; "
                              f"const n = verify(rows,element,&table,{alignment}); if (n != 0) return n; "
                              "if (rows[1][1] != 31 or saved[1] != 5) return 4; "
                              "modify(second,1); if (element.* != 33) return 5; "
                              "if (read(foreign(),3) != 43) return 6; "
                              # Same root contains six u16s, not a separate three-element allocation.
                              "if (read(table[0],4) != 33) return 7; "
                              "const fp = foreign(); const ep = advance(fp,3); "
                              "if (ep[0] != 43 or retreat(ep,3) != fp or delta(ep,fp) != 3) return 8; "
                              "if (delta(fp,ep) != 9223372036854775805) return 9; return 0; }\n")
            executable = root / "native"
            run([compiler, "build-exe", str(source), "-o", str(executable)], env)
            assert run([str(executable)]) == b""
            assert "DYN" in run(["readelf", "-h", str(executable)]).decode()
            assert b"TEXTREL" not in run(["readelf", "-d", str(executable)])
            zig = os.environ.get("ZIG")
            if zig:
                reference = root / "reference"
                run([zig, "build-exe", str(source), str(helper), "-lc", "-j2", f"-femit-bin={reference}"], timeout=120)
                assert run([str(reference)]) == b""
            passed += 1
        source = root / "inexact.zig"
        source.write_text("extern fn odd() [*]align(1) const u16;\n"
                          "extern fn even() [*]align(1) const u16;\n"
                          "fn delta(p: [*]align(1) const u16, q: [*]align(1) const u16) usize { return p-q; }\n"
                          "pub fn main() void { _ = delta(odd(),even()); }\n")
        executable = root / "inexact"
        run([compiler, "build-exe", str(source), "-o", str(executable)], env)
        result = run_bounded([str(executable)], timeout=15)
        assert result.returncode == -signal.SIGABRT and result.stdout == b""
        assert result.stderr == b"exact division produced remainder\n", result.stderr
        # Non-exact pointer differences are illegal behavior, but upstream's raw
        # AIR div_exact does not add a safety panic. This is an szc-only debug
        # guard regression, not a claim about reference output on an invalid run.
        passed += 1
    print(f"many pointer storage: {passed} passed, 0 failed")


if __name__ == "__main__":
    main()
