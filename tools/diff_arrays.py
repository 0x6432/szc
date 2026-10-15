#!/usr/bin/env python3
"""Compare compile-time array types, literals, reads and concatenation with Zig."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def cases():
    for child, length in itertools.product(
            ("u8", "i40", "bool", "void", "u0", "type", "noreturn", "anyopaque"),
            ("0", "1", "3", "4294967296", "18446744073709551615", "-1", "18446744073709551616")):
        yield f"pub fn main() void {{ const T = [{length}]{child}; _ = T; }}\n"
    for ty, element in (("u8", "7"), ("i8", "-7"), ("bool", "true"), ("void", "{}"), ("u0", "0")):
        for length, count in itertools.product((0, 1, 3), (0, 1, 3)):
            items = ", ".join([element] * count)
            yield f"pub fn main() void {{ const a = [{length}]{ty}{{{items}}}; _ = a.len; }}\n"
        for index in ("-1", "0", "1", "2", "true", "undefined"):
            yield f"pub fn main() void {{ const a = [_]{ty}{{{element}, {element}}}; _ = a[{index}]; }}\n"
        yield f"pub fn main() void {{ const a: [2]{ty} = .{{{element}, {element}}}; _ = a[1]; }}\n"
    for left, right in itertools.product((("u8", "7"), ("u16", "8"), ("i8", "-7"),
                                          ("i16", "-8"), ("bool", "true"), ("void", "{}")), repeat=2):
        yield (f"pub fn main() void {{ const a = ([_]{left[0]}{{{left[1]}}}) ++ "
               f"([_]{right[0]}{{{right[1]}}}); _ = a.len; _ = a[0]; _ = a[1]; }}\n")
    for expression, index in itertools.product(('"ab"', '"a" ++ "b"', '[_]u8{7} ++ "b"',
                                                '"b" ++ [_]u8{7}', '"" ++ ""'), (0, 1, 2, 3)):
        yield f"pub fn main() void {{ const a = {expression}; _ = a[{index}]; }}\n"
    for target, source in itertools.product(("u8", "u16", "bool"), repeat=2):
        yield (f"pub fn main() void {{ const a: [2]{target} = @as([2]{source}, undefined); "
               "_ = a.len; }\n")
    for child, length, index in itertools.product(
            ("u0", "void", "bool", "i7", "u33", "u64"), (0, 1, 3), ("n", "0", "3", "undefined")):
        yield (f"fn read(p: *const [{length}]{child}, n: usize) {child} {{ "
               + ("_ = n; " if index != "n" else "")
               + f"return p[{index}]; }} pub fn main() void {{ _ = read(undefined, 0); }}\n")
    for source in (
        'const a: [2]u8 = "hi".*; _ = a[1];',
        'const a: *const [2]u8 = "hi"; _ = a[1];',
        'const a: *const [2]u8 = undefined; _ = a.len;',
        'const a: [3]u8 = undefined; _ = a.len;',
        'const a: [3]u8 = @as([2]u8, undefined); _ = a.len;',
        'const a: [2][2]u8 = .{ .{1,2}, .{3,4} }; _ = a[1][0];',
        'const a = [_]type{u8, u16}; const T = a[1]; _ = @as(T, 7);',
        'const a: [2]u8 = .{ @intCast(@as(u16, 7)), 9 }; _ = a[0];',
        'const a = [_]u8{} ++ [_]u8{7}; _ = a[0];',
        'const a = [_]u8{undefined, 7} ++ "x"; _ = a[1];',
        'const a = [_]u8; _ = a;',
    ):
        yield "pub fn main() void { " + source + " }\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-arrays-diff-") as directory:
        path = Path(directory) / "test.zig"
        for source in cases():
            path.write_text(source)
            szc = run_compiler([args.szc, "tir", str(path)], source,
                              f"diff_arrays case {checked + 1} szc", timeout=120)
            zig = run_compiler([args.zig, "build-exe", str(path), "-fno-emit-bin", "-j2"], source,
                              f"diff_arrays case {checked + 1} zig", timeout=120)
            if szc.returncode != zig.returncode:
                raise SystemExit("disagreement:\n" + source + szc.stderr.decode(errors="replace")
                                 + zig.stderr.decode(errors="replace"))
            checked += 1
    print(f"constant arrays: {checked} matched Zig 0.17.0", flush=True)


if __name__ == "__main__":
    main()
