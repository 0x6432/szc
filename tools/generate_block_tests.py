#!/usr/bin/env python3
"""Generate runtime/comptime block-result regressions for all 1–64-bit integer types."""
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
                f"const constant_{name}: {name} = value: {{ break :value {minimum}; }};",
                f"fn select_{name}(c: bool, a: {name}, b: {name}) {name} {{",
                "    return value: { if (c) break :value a; break :value b; };",
                "}",
                f"fn check_{name}(a: {name}, b: {name}) u8 {{",
                f"    if (select_{name}(true, a, b) != a or select_{name}(false, a, b) != b) return 1;",
                "    const nested = outer: {",
                "        _ = inner: { if (a < b) break :outer a; break :inner 0; };",
                "        break :outer b;",
                "    };",
                "    if (nested != a) return 1;",
                f"    if (constant_{name} != a) return 1;",
                "    const single = value: { if (a == b) return 1; break :value 0; };",
                "    return single;",
                "}",
            ]
            calls.append(f"    errors += check_{name}({minimum}, {maximum});")
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
