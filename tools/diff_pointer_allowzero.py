#!/usr/bin/env python3
"""Compare allowzero/const pointer coercions and joins with Zig 0.17.0."""
import argparse
import itertools
from pathlib import Path
import subprocess
import tempfile


def pointer_type(qualifiers):
    return "".join("*" + ("allowzero " if zero else "") + ("const " if const else "")
                   for const, zero in qualifiers) + "u8"


def conversions(source, destination):
    yield (f"fn convert(p: {source}) {destination} {{ return p; }}\n"
           "pub fn main() void { _ = convert(undefined); }\n")
    yield (f"pub fn main() void {{ const p: {source} = undefined; "
           f"const q: {destination} = p; _ = q; }}\n")


def cases():
    qualifiers = list(itertools.product((False, True), repeat=2))
    for depth in (1, 2):
        types = [pointer_type(q) for q in itertools.product(qualifiers, repeat=depth)]
        for source, destination in itertools.product(types, repeat=2):
            yield from conversions(source, destination)
    # Three-level allowzero variance; existing const-only tests cover that axis.
    types = [pointer_type([(False, zero) for zero in q])
             for q in itertools.product((False, True), repeat=3)]
    for source, destination in itertools.product(types, repeat=2):
        yield from conversions(source, destination)
    types = [pointer_type([q]) for q in qualifiers]
    for left, right, destination in itertools.product(types, repeat=3):
        yield (f"fn choose(c: bool, a: {left}, b: {right}) {destination} {{ "
               "const p = if (c) a else b; return p; }\n"
               "pub fn main() void { _ = choose(true, undefined, undefined); }\n")
    functions = [f"{prefix}fn ({parameter}) {result}"
                 for prefix, parameter, result in itertools.product(
                     ("*const ", "*allowzero const "),
                     ("*u8", "*allowzero u8"), ("*u8", "*allowzero u8"))]
    for source, destination in itertools.product(functions, repeat=2):
        yield from conversions(source, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-pointer-allowzero-") as directory:
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
    print(f"allowzero pointers: {checked} matched Zig 0.17.0", flush=True)


if __name__ == "__main__":
    main()
