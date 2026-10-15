#!/usr/bin/env python3
"""Check numeric pointer addresses against C without assuming object-file layouts."""
import os
from pathlib import Path
import shlex
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
from run_array_storage import run


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    assert cc
    passed = 0
    with tempfile.TemporaryDirectory(prefix="szc-pointer-addresses-") as directory:
        root = Path(directory)
        helper = root / "foreign.c"
        helper.write_text("#include <stdint.h>\n#include <string.h>\n"
                          "extern uint8_t callback(void);\n"
                          "void *null_address(void) { return 0; }\n"
                          "void *null_many(void) { return 0; }\n"
                          "uintptr_t c_address(const void *p) { return (uintptr_t)p; }\n"
                          "uint8_t verify(const void *p, uintptr_t address, const void *tail, uintptr_t tail_address, "
                          "uintptr_t fn_address) {\n"
                          " uint8_t (*fn)(void) = callback; uintptr_t bits; _Static_assert(sizeof(bits) == sizeof(fn), \"function pointer size\");\n"
                          " memcpy(&bits,&fn,sizeof bits);\n"
                          " if (address != (uintptr_t)p || tail_address != (uintptr_t)tail) return 1;\n"
                          " if (tail_address != address + 4 || bits != fn_address || fn_address % 64) return 2;\n"
                          " return fn() == 17 ? 0 : 3; }\n")
        wrapper = root / "cc-pie"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc)
                           + ' -fPIE -pie -Wl,-z,relro,-z,now "$@" ' + shlex.quote(str(helper)) + "\n")
        wrapper.chmod(0o755)
        env = dict(os.environ, SZC_CC=str(wrapper))
        for alignment in (1, 2, 16, 256):
            source = root / "test.zig"
            source.write_text(f"var a: [3]u16 align({alignment}) = .{{7,9,11}};\n"
                              "export fn callback() align(64) u8 { return 17; }\n"
                              "extern fn null_address() *allowzero u8;\n"
                              "extern fn null_many() [*]allowzero u8;\n"
                              "extern fn c_address(p: [*]align(1) const u16) usize;\n"
                              "extern fn verify(p: [*]align(1) const u16, address: usize, tail: *align(1) const u16, "
                              "tail_address: usize, fn_address: usize) u8;\n"
                              "pub fn main() u8 { const p: [*]align(1) u16 = &a; "
                              "const n = verify(p,@intFromPtr(p),&p[2],@intFromPtr(&p[2]),@intFromPtr(&callback)); "
                              "if (n != 0) return n; if (c_address(p) != @intFromPtr(&a)) return 4; "
                              "if (@intFromPtr(null_address()) != 0 or @intFromPtr(null_many()) != 0) return 5; return 0; }\n")
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
    print(f"pointer addresses: {passed} passed, 0 failed")


if __name__ == "__main__":
    main()
