#!/usr/bin/env python3
"""Verify ABI-valid garbage in upper return-register bits cannot poison callbacks."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


SOURCE = """extern fn byte_value() u8;
extern fn signed_value() i8;
extern fn false_value() bool;
extern fn true_value() bool;
var byte_pointer = &byte_value;
var signed_pointer = &signed_value;
var false_pointer = &false_value;
var true_pointer = &true_value;
pub fn main() u8 {
    if (byte_pointer() != 7 or byte_value() != 7) return 1;
    if (signed_pointer() != -7 or signed_value() != -7) return 2;
    if (false_pointer() or false_value()) return 3;
    if (!true_pointer() or !true_value()) return 4;
    return 0;
}
"""
ASSEMBLY = """.text
.globl byte_value
byte_value:
    mov $0x12345607, %eax
    ret
.globl signed_value
signed_value:
    mov $0x123456f9, %eax
    ret
.globl false_value
false_value:
    mov $0x12345600, %eax
    ret
.globl true_value
true_value:
    mov $0x12345601, %eax
    ret
.section .note.GNU-stack,"",@progbits
"""


def run(command, env=None, timeout=120):
    result = subprocess.run(command, capture_output=True, env=env, timeout=timeout)
    assert result.returncode == 0, (command, result.returncode, result.stderr.decode(errors="replace"))
    return result


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    assert cc is not None, "system C compiler not found"
    with tempfile.TemporaryDirectory(prefix="szc-function-abi-") as directory:
        root = Path(directory)
        source, assembly = root / "main.zig", root / "helpers.s"
        source.write_text(SOURCE)
        assembly.write_text(ASSEMBLY)
        wrapper = root / "cc-helpers"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc) + ' "$@" ' + shlex.quote(str(assembly)) + "\n")
        wrapper.chmod(0o755)
        executable = root / "native"
        run([compiler, "build-exe", str(source), "-o", str(executable)], dict(os.environ, SZC_CC=str(wrapper)))
        result = run([str(executable)], timeout=15)
        assert not result.stdout and not result.stderr
        zig = os.environ.get("ZIG")
        if zig:
            executable = root / "reference"
            run([zig, "build-exe", str(source), str(assembly), f"-femit-bin={executable}"])
            result = run([str(executable)], timeout=15)
            assert not result.stdout and not result.stderr
    print("function ABI: 4 passed, 0 failed")


if __name__ == "__main__":
    main()
