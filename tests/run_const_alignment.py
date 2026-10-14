#!/usr/bin/env python3
"""Verify known and runtime immutable-local addresses and their alignment."""
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
    declarations, functions, known, checks, aliases = [], [], [], [], []
    for index, ty in enumerate(TYPES):
        name = f"check_{index}"
        declarations.append(f"extern fn {name}(p: *align(1) const {ty}, alignment: usize) bool;")
        aliases.extend([f".globl {name}", f".set {name}, check_alignment"])
        body = []
        initial = "true" if ty == "bool" else "&storage" if ty == "*u8" else "one" if "fn" in ty else "{}" if ty == "void" else "0" if ty == "u0" else "3"
        for alignment in ALIGNMENTS:
            variable = f"v_{index}_{alignment}"
            known.append(f"const {variable}: {ty} align({alignment}) = {initial};")
            checks.append(f"if (!{name}(&{variable}, {alignment})) return 1;")
            body.extend([f"const {variable}: {ty} align({alignment}) = input;",
                         f"if (!{name}(&{variable}, {alignment})) return 1;"])
            if ty != "void":
                body.append(f"if ({variable} != input) return 2;")
                expected = "true" if ty == "bool" else "&storage" if ty == "*u8" else "&one" if "fn" in ty else initial
                checks.append(f"if ({variable} != {expected}) return 2;")
        functions.append(f"fn validate_{index}(input: {ty}) u8 {{\n" + "\n".join(body) + "\nreturn 0;\n}\n")
        checks.append(f"if (validate_{index}({initial}) != 0) return 3;")
    source = ("\n".join(declarations) + "\nvar storage: u8 = 1;\nfn one() u8 { return 1; }\n"
              + "\n".join(functions) + "\npub fn main() u8 {\n"
              + "\n".join(known + checks) + "\nreturn 0;\n}\n")
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
    with tempfile.TemporaryDirectory(prefix="szc-const-alignment-") as directory:
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
        dynamic = run(["readelf", "-d", str(executable)]).stdout
        segments = run(["readelf", "-l", str(executable)]).stdout
        assert b"TEXTREL" not in dynamic, "immutable relocations created a text relocation"
        assert b"GNU_RELRO" in segments, "immutable pointer storage lacks RELRO"
        result = run([str(executable)], timeout=15)
        assert not result.stdout and not result.stderr
        zig = os.environ.get("ZIG")
        if zig:
            executable = root / "reference"
            run([zig, "build-exe", str(source), str(assembly), f"-femit-bin={executable}"])
            result = run([str(executable)], timeout=15)
            assert not result.stdout and not result.stderr
    print(f"constant storage alignment: {2 * len(TYPES) * len(ALIGNMENTS)} passed, 0 failed")


if __name__ == "__main__":
    main()
