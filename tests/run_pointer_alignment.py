#!/usr/bin/env python3
"""Exercise actual unaligned scalar/pointer loads and stores through external storage."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


SCALARS = [("u16", ".short", 2, 1234, 4321), ("i16", ".short", 2, -1234, -4321),
           ("u32", ".long", 4, 1234567, 7654321), ("i32", ".long", 4, -1234567, -7654321),
           ("u64", ".quad", 8, 123456789012345, 234567890123456),
           ("i64", ".quad", 8, -123456789012345, -234567890123456)]


def fixtures():
    declarations, checks, assembly = [], [], [".data"]
    guards = []
    for i, (ty, directive, size, initial, replacement) in enumerate(SCALARS, 1):
        declarations.append(f"extern fn get_{ty}() *align(1) {ty};")
        checks.extend([f"const p{i} = get_{ty}();", f"if (p{i}.* != {initial}) return {i};",
                       f"p{i}.* = {replacement};", f"if (p{i}.* != {replacement}) return {i};"])
        assembly.extend([".p2align 3", f"data_{ty}:", ".byte 90", f"{directive} {initial}", ".byte 165"])
        guards.extend([f"cmpb $90, data_{ty}(%rip)", "jne guards_failed",
                       f"cmpb $165, data_{ty}+{size + 1}(%rip)", "jne guards_failed"])
    assembly.extend([".p2align 3", "pointer_data:", ".byte 90", ".quad first_byte", ".byte 165",
                     "first_byte: .byte 45", "second_byte: .byte 67", ".text"])
    guards.extend(["cmpb $90, pointer_data(%rip)", "jne guards_failed",
                   "cmpb $165, pointer_data+9(%rip)", "jne guards_failed"])
    for ty, *_ in SCALARS:
        assembly.extend([f".globl get_{ty}", f"get_{ty}:", f"lea data_{ty}+1(%rip), %rax", "ret"])
    assembly.extend([".globl get_pointer", "get_pointer:", "lea pointer_data+1(%rip), %rax", "ret",
                     ".globl get_other", "get_other:", "lea second_byte(%rip), %rax", "ret",
                     ".globl guards_ok", "guards_ok:", *guards, "mov $1, %eax", "ret",
                     "guards_failed:", "xor %eax, %eax", "ret", '.section .note.GNU-stack,"",@progbits'])
    declarations.extend(["extern fn get_pointer() *align(1) *u8;", "extern fn get_other() *u8;",
                         "extern fn guards_ok() bool;"])
    checks.extend(["const slot = get_pointer();", "if (slot.*.* != 45) return 7;",
                   "slot.* = get_other();", "if (slot.*.* != 67 or !guards_ok()) return 7;", "return 0;"])
    source = "\n".join(declarations) + "\npub fn main() u8 {\n    " + "\n    ".join(checks) + "\n}\n"
    return source, "\n".join(assembly) + "\n"


def run(command, env=None, timeout=120):
    result = subprocess.run(command, capture_output=True, env=env, timeout=timeout)
    assert result.returncode == 0, (command, result.returncode, result.stderr.decode(errors="replace"))
    return result


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    assert cc is not None, "system C compiler not found"
    with tempfile.TemporaryDirectory(prefix="szc-pointer-alignment-") as directory:
        root = Path(directory)
        source, assembly = root / "main.zig", root / "helpers.s"
        text, asm = fixtures()
        source.write_text(text)
        assembly.write_text(asm)
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
    print("unaligned memory: 7 passed, 0 failed")


if __name__ == "__main__":
    main()
