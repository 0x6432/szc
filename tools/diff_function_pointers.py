#!/usr/bin/env python3
"""Compare supported function-pointer signature/qualifier coercions with Zig."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


SIGNATURES = [
    (("u8",), "u8"),
    (("u16",), "u8"),
    (("u8",), "u16"),
    (("*u8",), "u8"),
    (("*const u8",), "u8"),
    (("noalias *u8",), "u8"),
    ((), "void"),
    ((), "noreturn"),
    ((), "u0"),
]


def signature(params, result):
    return "fn (" + ", ".join(params) + ") " + result


def definition(params, result):
    parameters = []
    body = []
    for i, param in enumerate(params):
        qualifier = "noalias " if param.startswith("noalias ") else ""
        param = param.removeprefix("noalias ")
        parameters.append(f"{qualifier}a{i}: {param}")
        body.append(f"_ = a{i};")
    if result == "noreturn":
        body.append("unreachable;")
    elif result != "void":
        body.append("return 0;")
    return "fn target(" + ", ".join(parameters) + ") " + result + " { " + " ".join(body) + " }\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    signatures = [signature(*item) for item in SIGNATURES]
    pointers = [prefix + fn for fn, prefix in itertools.product(signatures, ("*", "*const "))]
    sources = []
    for source, destination in itertools.product(pointers, repeat=2):
        sources.append(f"fn convert(p: {source}) {destination} {{ return p; }}\n"
                       "pub fn main() void { _ = convert(undefined); }\n")
    for item, destination in itertools.product(SIGNATURES, pointers):
        sources.append(definition(*item) + f"pub fn main() void {{ const p: {destination} = target; _ = p; }}\n")
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-function-pointers-") as directory:
        path = Path(directory) / "test.zig"
        for source in sources:
            path.write_text(source)
            szc = run_compiler([args.szc, "tir", str(path)], source,
                f"diff_function_pointers case {checked + 1} szc", timeout=120)
            zig = run_compiler([args.zig, "build-exe", str(path), "-fno-emit-bin", "-j2"], source,
                f"diff_function_pointers case {checked + 1} zig", timeout=120)
            if szc.returncode != zig.returncode:
                raise SystemExit("disagreement:\n" + source + szc.stderr.decode(errors="replace")
                                 + zig.stderr.decode(errors="replace"))
            checked += 1
    print(f"function pointers: {checked} matched Zig 0.17.0")


if __name__ == "__main__":
    main()
