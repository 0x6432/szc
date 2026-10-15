#!/usr/bin/env python3
"""Compare every const-qualifier coercion of one-, two-, and three-level pointers."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def pointer_type(qualifiers):
    return "".join("*const " if const else "*" for const in qualifiers) + "u8"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-pointer-coercions-") as directory:
        path = Path(directory) / "test.zig"
        for depth in range(1, 4):
            types = [pointer_type(q) for q in itertools.product((False, True), repeat=depth)]
            for source, destination in itertools.product(types, repeat=2):
                sources = [
                    f"fn convert(p: {source}) {destination} {{ return p; }}\n"
                    "pub fn main() void { _ = convert(undefined); }\n",
                    f"pub fn main() void {{ const p: {source} = undefined; "
                    f"const q: {destination} = p; _ = q; }}\n",
                ]
                for text in sources:
                    path.write_text(text)
                    szc = run_compiler([args.szc, "tir", str(path)], text,
                        f"diff_pointer_coercions case {checked + 1} szc", timeout=120)
                    zig = run_compiler([args.zig, "build-exe", str(path), "-fno-emit-bin", "-j2"], text,
                        f"diff_pointer_coercions case {checked + 1} zig", timeout=120)
                    if szc.returncode != zig.returncode:
                        raise SystemExit(f"disagreement in case {checked + 1}: {source} -> {destination}\n"
                                         + text
                                         + szc.stderr.decode(errors="replace")
                                         + zig.stderr.decode(errors="replace"))
                    checked += 1
    print(f"pointer coercions: {checked} matched Zig 0.17.0")


if __name__ == "__main__":
    main()
