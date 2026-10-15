#!/usr/bin/env python3
"""Compare aligned global constants, copied bindings and comptime-only restrictions with Zig."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def pointer(ty, alignment, const):
    return "*" + (f"align({alignment}) " if alignment else "") + ("const " if const else "") + ty


def cases():
    for ty, storage, destination, const, inferred in itertools.product(
            ("u8", "u32", "u64"), (0, 1, 4, 16, 64), (0, 1, 4, 16, 64), (False, True), (False, True)):
        annotation = "" if inferred else f": {ty}"
        alignment = f" align({storage})" if storage else ""
        yield (f"const value{annotation}{alignment} = @as({ty}, 7);\n"
               f"pub fn main() void {{ const p: {pointer(ty, destination, const)} = &value; _ = p.*; }}\n")

    for storage, destination, typed, local in itertools.product((1, 64), (1, 4, 64), (False, True), (False, True)):
        annotation = ": u32" if typed else ""
        alias = f"const alias{annotation} = value; "
        yield (f"const value: u32 align({storage}) = 7;\n"
               + ("" if local else alias + "\n")
               + "pub fn main() void { " + (alias if local else "")
               + f"const p: {pointer('u32', destination, True)} = &alias; _ = p.*; }}\n")
    for declaration in ("const value align(64) = 7;", "const value align(64) = u8;",
                        "const value align(64) = target;", "const value align(64) = null;",
                        "const value align(64) = undefined;", "const value align(64) = .tag;",
                        "const value: comptime_int align(64) = 7;", "const value: type align(64) = u8;"):
        yield "fn target() u8 { return 7; }\n" + declaration + "\npub fn main() void { _ = value; }\n"



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-global-const-alignment-diff-") as directory:
        path = Path(directory) / "test.zig"
        for source in cases():
            path.write_text(source)
            szc = run_compiler([args.szc, "tir", str(path)], source,
                f"diff_global_const_alignment case {checked + 1} szc", timeout=120)
            zig = run_compiler([args.zig, "build-exe", str(path), "-fno-emit-bin", "-j2"], source,
                f"diff_global_const_alignment case {checked + 1} zig", timeout=120)
            if szc.returncode != zig.returncode:
                raise SystemExit("disagreement:\n" + source + szc.stderr.decode(errors="replace")
                                 + zig.stderr.decode(errors="replace"))
            checked += 1
    print(f"global constant alignment: {checked} matched Zig 0.17.0", flush=True)


if __name__ == "__main__":
    main()
