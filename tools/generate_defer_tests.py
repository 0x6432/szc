#!/usr/bin/env python3
"""Generate scope-exit snapshot regressions for all 1–64-bit integer types."""
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
            step = -1 if signed and bits == 1 else 1
            lines += [
                f"fn snapshot_{name}(x: {name}) {name} {{",
                f"    var value = x; defer value +%= {step}; return value;",
                "}",
                f"fn check_{name}(c: bool, a: {name}, b: {name}) u8 {{",
                f"    if (snapshot_{name}(a) != a or snapshot_{name}(b) != b) return 1;",
                "    var value = a;",
                f"    const saved: {name} = done: {{",
                f"        defer {{ const next = value +% {step}; value = next; }}",
                "        if (c) break :done value;",
                "        value = b; break :done value;",
                "    };",
                "    const expected = if (c) a else b;",
                f"    if (saved != expected or value != expected +% {step}) return 1;",
                "    return 0;",
                "}",
            ]
            calls.append(f"    errors += check_{name}(true, {minimum}, {maximum});")
            calls.append(f"    errors += check_{name}(false, {minimum}, {maximum});")
    lines += ["pub fn main() u8 {", "    var errors: u16 = 0;"]
    lines += calls + ["    return @intCast(errors);", "}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate())


if __name__ == "__main__":
    main()
