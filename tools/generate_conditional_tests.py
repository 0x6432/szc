#!/usr/bin/env python3
"""Generate branch/merge regressions for every 1–64-bit signed/unsigned type."""
import argparse
from pathlib import Path


def generate():
    lines = ["// exit: 0"]
    calls = []
    for signed in (False, True):
        for bits in range(1, 65):
            name = ("i" if signed else "u") + str(bits)
            minimum = -(1 << (bits - 1)) if signed else 0
            maximum = (1 << (bits - int(signed))) - 1
            lines += [
                f"fn select_{name}(c: bool, a: {name}, b: {name}) {name} {{",
                "    return if (c) a else b;",
                "}",
                f"fn check_{name}(a: {name}, b: {name}) u8 {{",
                f"    var result: {name} = select_{name}(true, a, b);",
                "    if (result != a) return 1;",
                f"    result = select_{name}(false, a, b);",
                "    if (result != b) return 1;",
                f"    const literal: {name} = if (a < b) {minimum} else {maximum};",
                f"    if (literal != {minimum}) return 1;",
                "    const early = if (a <= b) 0 else return 1;",
                "    return early;",
                "}",
            ]
            calls.append(f"    errors = errors + check_{name}({minimum}, {maximum});")
    lines += ["pub fn main() u8 {", "    var errors: u8 = 0;"]
    lines += calls + ["    return errors;", "}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate())


if __name__ == "__main__":
    main()
