#!/usr/bin/env python3
"""Check wide-integer pointer strides against real C storage; no wide loads."""
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
    with tempfile.TemporaryDirectory(prefix="szc-wide-layout-") as directory:
        root = Path(directory)
        helper = root / "foreign.c"
        helper.write_text("#include <stdint.h>\n"
                          "_Alignas(16) static unsigned char backing[32768];\n"
                          "void *wide_backing(void) { return backing; }\n"
                          "uintptr_t offset(void) { return 2; }\n"
                          "uint8_t verify(const void *p, uintptr_t n) {\n"
                          " return (uintptr_t)p == (uintptr_t)backing + n ? 0 : 1; }\n")
        wrapper = root / "cc-pie"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc)
                           + ' -fPIE -pie -Wl,-z,relro,-z,now "$@" ' + shlex.quote(str(helper)) + "\n")
        wrapper.chmod(0o755)
        env = dict(os.environ, SZC_CC=str(wrapper))
        for child, size in (("u129", 32), ("u257", 48), ("i513", 80), ("u65535", 8192)):
            source = root / "test.zig"
            source.write_text(f"extern fn wide_backing() [*]{child};\n"
                              "extern fn offset() usize;\n"
                              f"extern fn verify(p: [*]{child}, n: usize) u8;\n"
                              "pub fn main() u8 { const p = wide_backing(); const n = offset(); "
                              f"const q = p+n; if (@intFromPtr(q)-@intFromPtr(p) != n*{size}) return 2; "
                              "if (q-p != n or (q-n) != p) return 3; "
                              f"if (@sizeOf({child}) != {size}) return 4; return verify(q,n*{size}); }}\n")
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
    print(f"wide pointer layout: {passed} passed, 0 failed")


if __name__ == "__main__":
    main()
