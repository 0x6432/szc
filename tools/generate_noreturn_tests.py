#!/usr/bin/env python3
"""Generate noreturn branch joins for all 1–64-bit integer result types."""
import argparse
from pathlib import Path


def generate():
    lines = ["// exit: 0", "fn stop() noreturn { unreachable; }"]
    calls = []
    for signed in (False, True):
        for bits in range(1, 65):
            name = ("i" if signed else "u") + str(bits)
            minimum = -(1 << (bits - 1)) if signed else 0
            maximum = (1 << (bits - int(signed))) - 1
            lines += [
                f"fn choose_{name}(c: bool, x: {name}) {name} {{ return if (c) x else stop(); }}",
                f"fn block_{name}(c: bool, x: {name}) {name} {{ return value: {{ if (c) break :value x; stop(); }}; }}",
                f"fn cleanup_{name}(c: bool, x: {name}) {name} {{ if (c) {{ defer stop(); return x; }} return x; }}",
                f"fn check_{name}(a: {name}, b: {name}) u8 {{",
                f"    if (choose_{name}(true, a) != a or choose_{name}(true, b) != b) return 1;",
                f"    if (block_{name}(true, a) != a or block_{name}(true, b) != b) return 1;",
                f"    if (cleanup_{name}(false, a) != a or cleanup_{name}(false, b) != b) return 1;",
                "    return 0;",
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
