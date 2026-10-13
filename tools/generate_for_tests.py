#!/usr/bin/env python3
"""Generate range-loop bound/capture regressions for all 1–64-bit integer types."""
import argparse
from pathlib import Path


def generate():
    lines = ["// exit: 0"]
    calls = []
    for signed in (False, True):
        for bits in range(1, 65):
            name = ("i" if signed else "u") + str(bits)
            maximum = (1 << (bits - int(signed))) - 1
            n = min(3, maximum)
            lines += [
                f"fn check_{name}(n: {name}) u8 {{",
                "    var total: usize = 0;",
                "    for (0..@intCast(n), 7..) |i, j| { total += i + j - 7; }",
                f"    if (total != {n * (n - 1)}) return 1;",
                "    total = 0;",
                "    for (0..@intCast(n), 2..2 + @as(usize, @intCast(n))) |i, j| { total += j - i; }",
                f"    if (total != {2 * n}) return 1;",
                "    const result: u8 = for (0..@intCast(n)) |i| { if (i == 1) break @intCast(i); } else 4;",
                f"    if (result != {1 if n > 1 else 4}) return 1;",
                "    return 0;",
                "}",
            ]
            calls.append(f"    errors += check_{name}({n});")
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
