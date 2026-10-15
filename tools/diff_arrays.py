#!/usr/bin/env python3
"""Compare array types, literals, mutation, copies, reads and concatenation with Zig."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def cases():
    for child, value in (("u8", "7"), ("i33", "-7"), ("bool", "true"), ("u0", "0"),
                         ("void", "{}"), ("type", "u8")):
        for index in ("0", "1", "2", "-1", "undefined"):
            yield (f"const a = [_]{child}{{{value},{value}}}; const p = &a[{index}]; "
                   "pub fn main() void { _ = p; }\n")
        zero_sized = child in ("u0", "void")
        equality = "true" if zero_sized else "false"
        yield (f"const a = [_]{child}{{{value},{value}}}; const same = &a[0] == &a[0]; "
               f"const equal = &a[0] == &a[1]; "
               f"const check: u8 = if (!same or equal != {equality}) 256 else 0; "
               "pub fn main() void { _ = check; }\n")
        yield (f"const a = [2][2]{child}{{.{{{value},{value}}},.{{{value},{value}}}}}; "
               "const row = &a[1]; const p = &a[1][1]; const q = &row[1]; "
               "const check: u8 = if (p != q) 256 else 0; "
               "pub fn main() void { _ = check; _ = p.*; }\n")
        yield (f"const a = [_]{child}{{{value},{value}}}; "
               f"const p: *const {child} = &a[1]; pub fn main() void {{ p.* = {value}; }}\n")
    for alignment, index, target in itertools.product((1, 8, 16), (0, 1, 2, 4), (1, 2, 8, 16)):
        yield (f"const a: [8]u8 align({alignment}) = .{{1,2,3,4,5,6,7,8}}; "
               f"const p: *align({target}) const u8 = &a[{index}]; "
               "pub fn main() void { _ = p; }\n")
    for index, target in itertools.product((0, 1), ("", "allowzero ")):
        yield (f"fn f(p: *allowzero const [2]u8) void {{ const q: *{target}const u8 = &p[{index}]; _ = q; }} "
               "pub fn main() void { f(undefined); }\n")
    for source in (
        'const p: *const [2]u8 = undefined; const q = &p[1]; _ = q;',
        'const a = "ab"; const p: *const [2]u8 = a; const q = &a[1]; const r = &p[1]; '
        'const check: u8 = if (q != r) 256 else 0; _ = check;',
        'const a = "ab"; const q = &a[2]; const check: u8 = if (q.* != 0) 256 else 0; _ = check;',
        'const a = "ab"; const p: *const [2]u8 = a; _ = &p[2];',
        'const a = [_]u8{undefined,7}; const p = &a[1]; const check: u8 = if (p.* != 7) 256 else 0; _ = check;',
    ):
        yield "pub fn main() void { " + source + " }\n"
    for child, value in (("u8", "7"), ("i33", "-7"), ("bool", "true"), ("u0", "0"), ("void", "{}")):
        for body in (
            f"return if (c) a else .{{{value}, {value}}};",
            f"return result: {{ if (c) break :result a; break :result .{{{value}, {value}}}; }};",
            f"return while (c) {{ break a; }} else .{{{value}, {value}}};",
            f"_ = c; a[0] = {value}; return a;",
            f"_ = a; _ = c; return [_]{child}{{{value}}};",
        ):
            yield (f"fn f(a: [2]{child}, c: bool) [2]{child} {{ {body} }}\n"
                   f"pub fn main() void {{ _ = f(.{{{value}, {value}}}, true); }}\n")
        yield (f"fn f(a: [2]{child}) [2]{child} {{ return a; }} "
               f"fn invoke(p: *const fn ([2]{child}) [2]{child}, a: [2]{child}) [2]{child} {{ return p(a); }}\n"
               f"pub fn main() void {{ _ = invoke(&f, .{{{value}, {value}}}); }}\n")
    for child, length in itertools.product(("u8", "u0"), (0, 2)):
        ty = f"[{length}]{child}"
        yield f"export fn f(a: {ty}) void {{ _ = a; }} pub fn main() void {{}}\n"
        yield f"export fn f() {ty} {{ return undefined; }} pub fn main() void {{}}\n"
        yield f"extern fn f(a: {ty}) void; pub fn main() void {{ f(undefined); }}\n"
        yield f"extern fn f() {ty}; pub fn main() void {{ _ = f(); }}\n"
        yield f"extern fn f(a: {ty}) void; pub fn main() void {{ _ = &f; }}\n"
        yield f"extern fn f() {ty}; pub fn main() void {{ _ = &f; }}\n"
    for child, value in (("u8", "7"), ("i33", "-7"), ("bool", "true"), ("u0", "0"), ("void", "{}")):
        for body in (
            "const b = a; a[0] = n; _ = b[1];",
            "const b = a; a = b;",
            "const p = &a[0]; p.* = n;",
            "const p: *const [2]T = &a; _ = p[0];",
            "const p: *const [2]T = &a; p[0] = n;",
            "a = .{n};",
            "a = .{n, n};",
        ):
            body = body.replace("T", child)
            yield (f"fn operation(n: {child}) void {{ var a = [_]{child}{{n, n}}; {body} }}\n"
                   f"pub fn main() void {{ operation({value}); }}\n")
    for target, source in itertools.product(("u8", "u16", "bool"), repeat=2):
        value = "true" if source == "bool" else "7"
        yield (f"fn operation(n: {source}) void {{ const a = [_]{source}{{n,n}}; "
               f"var b: [2]{target} = undefined; b = a; _ = &b; }}\n"
               f"pub fn main() void {{ operation({value}); }}\n")
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
    print(f"arrays: {checked} matched Zig 0.17.0", flush=True)


if __name__ == "__main__":
    main()
