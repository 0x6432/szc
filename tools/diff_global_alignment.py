#!/usr/bin/env python3
"""Compare aligned mutable global declarations and address coercions with Zig 0.17.0."""
import argparse
import itertools
from pathlib import Path
import subprocess
import tempfile


def pointer(ty, alignment, const):
    return "*" + (f"align({alignment}) " if alignment else "") + ("const " if const else "") + ty


def cases():
    for ty, storage, destination, const, inferred in itertools.product(
            ("u8", "u32", "u64"), (0, 1, 4, 16, 64), (0, 1, 4, 16, 64), (False, True), (False, True)):
        annotation = "" if inferred else f": {ty}"
        alignment = f" align({storage})" if storage else ""
        yield (f"var value{annotation}{alignment} = @as({ty}, 7);\n"
               f"pub fn main() void {{ const p: {pointer(ty, destination, const)} = &value; _ = p.*; }}\n")



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-global-alignment-diff-") as directory:
        path = Path(directory) / "test.zig"
        for source in cases():
            path.write_text(source)
            szc = subprocess.run([args.szc, "tir", str(path)], capture_output=True, timeout=120)
            zig = subprocess.run([args.zig, "build-exe", str(path), "-fno-emit-bin"],
                                 capture_output=True, timeout=120)
            if szc.returncode not in (0, 1) or zig.returncode not in (0, 1):
                raise SystemExit(f"compiler failed unexpectedly: szc={szc.returncode}, zig={zig.returncode}")
            if szc.returncode != zig.returncode:
                raise SystemExit("disagreement:\n" + source + szc.stderr.decode(errors="replace")
                                 + zig.stderr.decode(errors="replace"))
            checked += 1
    print(f"global alignment coercions: {checked} matched Zig 0.17.0", flush=True)


if __name__ == "__main__":
    main()
