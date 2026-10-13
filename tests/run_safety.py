#!/usr/bin/env python3
"""Runtime panic regressions, with optional checks against the official Zig binary."""
import os
import resource
import signal
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command):
    return subprocess.run(command, capture_output=True, timeout=15)


def binary_case(name, integer_type, operator, lhs, rhs, message="integer overflow"):
    source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{\n"
              f"    return a {operator} b;\n}}\n"
              f"pub fn main() void {{ _ = operation({lhs}, {rhs}); }}\n")
    return name, source, message


def cases():
    for bits in (1, 5, 8, 32, 33, 64):
        unsigned = f"u{bits}"
        signed = f"i{bits}"
        maximum = (1 << bits) - 1
        minimum = -(1 << (bits - 1))
        signed_max = (1 << (bits - 1)) - 1
        yield binary_case(f"add_{unsigned}", unsigned, "+", maximum, 1)
        yield binary_case(f"sub_{unsigned}", unsigned, "-", 0, 1)
        if bits > 1:
            yield binary_case(f"mul_{unsigned}", unsigned, "*", maximum, 2)
            yield binary_case(f"add_positive_{signed}", signed, "+", signed_max, 1)
            yield binary_case(f"sub_negative_{signed}", signed, "-", minimum, 1)
            yield binary_case(f"mul_positive_{signed}", signed, "*", signed_max, 2)
            yield binary_case(f"mul_below_min_{signed}", signed, "*", minimum, 2)
        yield binary_case(f"add_negative_{signed}", signed, "+", minimum, -1)
        yield binary_case(f"sub_positive_{signed}", signed, "-", signed_max, -1)
        yield binary_case(f"mul_negative_{signed}", signed, "*", minimum, -1)
        source = (f"fn operation(a: {signed}) {signed} {{ return -a; }}\n"
                  f"pub fn main() void {{ _ = operation({minimum}); }}\n")
        yield f"negate_{signed}", source, "integer overflow"
    for name, from_type, to_type, value in (
        ("cast_negative", "i64", "u64", -1),
        ("cast_unsigned_narrow", "u64", "u8", 256),
        ("cast_signed_low", "i64", "i8", -129),
        ("cast_signed_high", "i64", "i8", 128),
        ("cast_sign_change", "u64", "i64", 1 << 63),
        ("cast_small_sign_change", "u8", "i8", 128),
        ("cast_unsigned_zero", "u8", "u0", 1),
        ("cast_signed_zero", "i1", "u0", -1),
    ):
        source = (f"fn operation(a: {from_type}) {to_type} {{ return @intCast(a); }}\n"
                  f"pub fn main() void {{ _ = operation({value}); }}\n")
        yield name, source, "integer does not fit in destination type"
    for integer_type in ("u32", "u64"):
        for op, label in (("/", "div"), ("%", "rem")):
            yield binary_case(f"{label}_zero_{integer_type}", integer_type, op, 123, 0, "division by zero")

    for builtin in ("divTrunc", "divFloor", "divCeil", "divExact", "rem", "mod"):
        for integer_type in ("i8", "i64", "u64"):
            source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{ return @{builtin}(a, b); }}\n"
                      "pub fn main() void { _ = operation(3, 0); }\n")
            yield f"{builtin}_zero_{integer_type}", source, "division by zero"
    for builtin in ("divTrunc", "divFloor", "divCeil", "divExact"):
        for bits in (1, 8, 33, 64):
            source = (f"fn operation(a: i{bits}, b: i{bits}) i{bits} {{ return @{builtin}(a, b); }}\n"
                      f"pub fn main() void {{ _ = operation({-(1 << (bits - 1))}, -1); }}\n")
            yield f"{builtin}_overflow_i{bits}", source, "integer overflow"
    for integer_type in ("u8", "u64", "i8", "i64"):
        source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{ return @divExact(a, b); }}\n"
                  "pub fn main() void { _ = operation(5, 2); }\n")
        yield f"exact_remainder_{integer_type}", source, "exact division produced remainder"
    for bits in (5, 33):
        for op, label in (("<<", "shl"), (">>", "shr")):
            source = (f"fn operation(a: u{bits}, b: u{(bits - 1).bit_length()}) u{bits} {{\n"
                      f"    return a {op} b;\n}}\n"
                      f"pub fn main() void {{ _ = operation(1, {bits}); }}\n")
            yield f"{label}_size_{bits}", source, "shift amount is greater than the type size"


def main():
    szc = sys.argv[1] if len(sys.argv) > 1 else "build/asan/szc"
    zig = os.environ.get("ZIG")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix="szc-safety-") as directory:
        root = Path(directory)
        for name, source, message in cases():
            src = root / f"{name}.zig"
            src.write_text(source)
            try:
                for compiler in ([szc, zig] if zig else [szc]):
                    exe = root / ("reference" if compiler == zig else "native")
                    command = ([compiler, "build-exe", str(src), f"-femit-bin={exe}"]
                               if compiler == zig else
                               [compiler, "build-exe", str(src), "-o", str(exe)])
                    compiled = run(command)
                    if compiled.returncode != 0:
                        raise AssertionError("compilation failed: " + compiled.stderr.decode(errors="replace"))
                    result = run([str(exe)])
                    if result.returncode != -signal.SIGABRT:
                        raise AssertionError(f"{compiler} exit {result.returncode}; expected SIGABRT")
                    if result.stdout:
                        raise AssertionError("unexpected stdout")
                    if compiler == zig:
                        if message.encode() not in result.stderr:
                            raise AssertionError("reference panic message mismatch: " + result.stderr.decode(errors="replace"))
                    elif result.stderr != (message + "\n").encode():
                        raise AssertionError("panic message mismatch: " + result.stderr.decode(errors="replace"))
                passed += 1
            except (AssertionError, subprocess.TimeoutExpired) as error:
                print(f"FAIL {name}: {error}")
                failed += 1
    print(f"safety: {passed} passed, {failed} failed")
    return int(failed != 0)


if __name__ == "__main__":
    sys.exit(main())
