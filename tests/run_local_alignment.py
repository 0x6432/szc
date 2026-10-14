#!/usr/bin/env python3
"""Verify real mutable-local addresses for weak, natural and over-aligned stack storage."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


ALIGNMENTS = (1, 2, 4, 8, 16, 32, 64, 256, 4096)
TYPES = ("u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "u3", "i40",
         "bool", "*u8", "*const fn () u8", "u0", "void")


def fixtures():
    declarations, locals_, checks, aliases = [], [], [], []
    for index, ty in enumerate(TYPES):
        name = f"check_{index}"
        declarations.append(f"extern fn {name}(p: *align(1) const {ty}, alignment: usize) bool;")
        aliases.extend([f".globl {name}", f".set {name}, check_alignment"])
        for alignment in ALIGNMENTS:
            variable = f"v_{index}_{alignment}"
            seed = alignment % 5 + 1
            initial = "false" if ty == "bool" else "&first" if ty == "*u8" else "one" if "fn" in ty else "{}" if ty == "void" else "0" if ty == "u0" else str(seed)
            replacement = "true" if ty == "bool" else "&second" if ty == "*u8" else "two" if "fn" in ty else "{}" if ty == "void" else "0" if ty == "u0" else str(seed + 1)
            locals_.append(f"var {variable}: {ty} align({alignment}) = {initial};")
            checks.extend([f"{variable} = {replacement};", f"if (!{name}(&{variable}, {alignment})) return 1;"])
            if ty != "void":
                expression = f"{variable}()" if "fn" in ty else variable
                expected = "2" if "fn" in ty else replacement
                checks.append(f"if ({expression} != {expected}) return 2;")
    source = ("\n".join(declarations) + "\nfn one() u8 { return 1; }\nfn two() u8 { return 2; }\n"
              "pub fn main() u8 {\nvar first: u8 = 1; first += 0;\nvar second: u8 = 2; second += 0;\n"
              + "\n".join(locals_ + checks) + "\nreturn 0;\n}\n")
    assembly = (".text\ncheck_alignment:\nmov %rsi, %rax\nsub $1, %rax\nand %rdi, %rax\n"
                "sete %al\nmovzbl %al, %eax\nret\n" + "\n".join(aliases)
                + '\n.section .note.GNU-stack,"",@progbits\n')
    return source, assembly


def run(command, env=None, timeout=120):
    result = subprocess.run(command, capture_output=True, env=env, timeout=timeout)
    assert result.returncode == 0, (command, result.returncode, result.stderr.decode(errors="replace"))
    return result


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    assert cc is not None, "system C compiler not found"
    with tempfile.TemporaryDirectory(prefix="szc-local-alignment-") as directory:
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
    print(f"local storage alignment: {len(TYPES) * len(ALIGNMENTS)} passed, 0 failed")


if __name__ == "__main__":
    main()
