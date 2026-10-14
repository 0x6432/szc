#!/usr/bin/env python3
"""Generate pointer load/store, const coercion and cleanup checks at every integer width."""
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
                f"fn load_{name}(p: *const {name}) {name} {{ return p.*; }}",
                f"fn store_{name}(p: *{name}, x: {name}) void {{ p.* = x; }}",
                f"fn cleanup_{name}(p: *{name}) {name} {{ defer p.* +%= {step}; return p.*; }}",
                f"fn indirect_{name}(p: *const *const {name}) {name} {{ return p.*.*; }}",
                f"fn check_{name}(a: {name}, b: {name}) u8 {{",
                "    var value = a;",
                f"    if (load_{name}(&value) != a) return 1;",
                f"    store_{name}(&value, b);",
                f"    var p: *{name} = &value;",
                "    p = &value;",
                f"    if (indirect_{name}(&p) != b) return 1;",
                f"    const old = cleanup_{name}(&p.*);",
                f"    if (old != b or load_{name}(&value) != b +% {step}) return 1;",
                "    return 0;",
                "}",
            ]
            calls.append(f"    errors += check_{name}({minimum}, {maximum});")
            calls.append(f"    errors += check_{name}({maximum}, {minimum});")
    lines += ["pub fn main() u8 {", "    var errors: u16 = 0;"]
    return "\n".join(lines + calls + ["    return @intCast(errors);", "}", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate())


if __name__ == "__main__":
    main()
