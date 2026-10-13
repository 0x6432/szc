#!/usr/bin/env python3
"""Generate a deterministic Zig regression over all signed/unsigned 1–64-bit widths."""
import argparse
import random
from pathlib import Path


def generate(seed):
    rng = random.Random(seed)
    lines = ["// exit: 0"]
    calls = []
    for signed in (False, True):
        for bits in range(1, 65):
            name = ("i" if signed else "u") + str(bits)
            modulus = 1 << bits

            def wrap(value):
                value %= modulus
                if signed and value >= (1 << (bits - 1)):
                    value -= modulus
                return value

            a = wrap(rng.randrange(modulus))
            b = wrap(rng.randrange(modulus))
            shift = rng.randrange(bits)
            checks = []
            for op, value in (
                ("+%", wrap(a + b)), ("-%", wrap(a - b)),
                ("*%", wrap(a * b)), ("&", wrap(a & b)),
                ("|", wrap(a | b)), ("^", wrap(a ^ b)),
            ):
                checks.append(f"(a {op} b) == {value}")
            checks += [f"~a == {wrap(~a)}",
                       f"(a << shift) == {wrap(a << shift)}",
                       f"(a >> shift) == {a >> shift}"]
            for op, value in (
                ("==", a == b), ("!=", a != b), ("<", a < b),
                (">", a > b), ("<=", a <= b), (">=", a >= b),
            ):
                checks.append(f"(a {op} b) == {str(value).lower()}")
            if signed:
                checks.append(f"(-%a) == {wrap(-a)}")
            elif b:
                checks += [f"(a / b) == {a // b}", f"(a % b) == {a % b}"]
            shift_bits = (bits - 1).bit_length()
            condition = f"saved == {wrap(a + b)} and " + " and ".join(checks)
            lines += [
                f"fn check_{name}(a: {name}, b: {name}, shift: u{shift_bits}) u8 {{",
                f"    var saved: {name} = a;",
                "    saved = saved +% b;",
                f"    return @intFromBool(!({condition}));",
                "}",
            ]
            calls.append(f"    errors = errors + check_{name}({a}, {b}, {shift});")
    # Each of the 128 checks returns at most one, so the u8 sum cannot overflow.
    lines += ["pub fn main() u8 {", "    var errors: u8 = 0;"]
    lines += calls + ["    return errors;", "}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seed", type=int, default=34017)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate(args.seed))


if __name__ == "__main__":
    main()
