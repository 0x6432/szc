#!/usr/bin/env python3
"""Generate indirect integer calls and function-pointer results at every width."""
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
                f"const F_{name} = fn ({name}) {name};",
                f"fn first_{name}(n: {name}) {name} {{ return n; }}",
                f"fn second_{name}(n: {name}) {name} {{ return n +% {step}; }}",
                f"fn invoke_{name}(p: *const F_{name}, n: {name}) {name} {{ return p(n); }}",
                f"fn select_{name}(c: bool) *const F_{name} {{ return if (c) &first_{name} else &second_{name}; }}",
                f"fn check_{name}(n: {name}) u8 {{",
                f"    if (invoke_{name}(first_{name}, n) != n) return 1;",
                f"    if (invoke_{name}(select_{name}(false), n) != n +% {step}) return 1;",
                f"    if (select_{name}(true)(n) != n) return 1;",
                "    return 0;",
                "}",
            ]
            calls += [f"    errors += check_{name}({minimum});", f"    errors += check_{name}({maximum});"]
    return "\n".join(lines + ["pub fn main() u8 {", "    var errors: u16 = 0;"] + calls + ["    return @intCast(errors);", "}", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate())


if __name__ == "__main__":
    main()
