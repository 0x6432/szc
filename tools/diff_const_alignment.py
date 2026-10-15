#!/usr/bin/env python3
"""Compare aligned constant bindings, pointer coercions and comptime preservation with Zig."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def pointer(ty, alignment, const):
    return "*" + (f"align({alignment}) " if alignment else "") + ("const " if const else "") + ty


def cases():
    for ty, storage, destination, const, inferred, known in itertools.product(
            ("u8", "u32", "u64"), (0, 1, 4, 16, 64), (0, 1, 4, 16, 64), (False, True), (False, True), (False, True)):
        annotation = "" if inferred else f": {ty}"
        alignment = f" align({storage})" if storage else ""
        initial = f"@as({ty}, 7)" if known else "n"
        discard = "_ = n; " if known else ""
        yield (f"fn f(n: {ty}) {ty} {{ {discard}const value{annotation}{alignment} = {initial}; "
               f"const p: {pointer(ty, destination, const)} = &value; return p.*; }}\n"
               "pub fn main() void { _ = f(7); }\n")
    for storage, destination, known, typed in itertools.product((1, 64), (1, 4, 64), (False, True), (False, True)):
        initial = "@as(u32, 7)" if known else "n"
        discard = "_ = n; " if known else ""
        annotation = ": u32" if typed else ""
        yield (f"fn f(n: u32) u32 {{ {discard}const value: u32 align({storage}) = {initial}; "
               f"const alias{annotation} = value; const p: {pointer('u32', destination, True)} = &alias; return p.*; }}\n"
               "pub fn main() void { _ = f(7); }\n")
    yield "pub fn main() void { const n align(64) = 32; const P = *align((&n).*) u8; _ = P; }\n"
    yield "pub fn main() void { const T align(16) = u8; const n: (&T).* = 7; _ = n; }\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-const-alignment-diff-") as directory:
        path = Path(directory) / "test.zig"
        for source in cases():
            path.write_text(source)
            szc = run_compiler([args.szc, "tir", str(path)], source,
                f"diff_const_alignment case {checked + 1} szc", timeout=120)
            zig = run_compiler([args.zig, "build-exe", str(path), "-fno-emit-bin", "-j2"], source,
                f"diff_const_alignment case {checked + 1} zig", timeout=120)
            if szc.returncode != zig.returncode:
                raise SystemExit("disagreement:\n" + source + szc.stderr.decode(errors="replace")
                                 + zig.stderr.decode(errors="replace"))
            checked += 1
    print(f"constant alignment: {checked} matched Zig 0.17.0", flush=True)


if __name__ == "__main__":
    main()
