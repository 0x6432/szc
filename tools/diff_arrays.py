#!/usr/bin/env python3
"""Compare array types, literals, mutation, copies, reads and concatenation with Zig."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def int_from_ptr_cases():
    for target in ("*u8", "[*]const u8", "*u0", "*void", "*u128", "*anyopaque",
                   "*const fn () void", "[*]align(1) volatile allowzero u16"):
        for result in ("usize", "u8", "u128"):
            yield (f'fn f(p: {target}) {result} {{ return @intFromPtr(p); }} '
                   'pub fn main() void { _ = f(undefined); }\n')
    for source in (
        'const p: *u8 = undefined; const n = @intFromPtr(p); _ = n;',
        'const p: [*]u8 = undefined; const n: u8 = @intFromPtr(p); _ = n;',
        '_ = @intFromPtr(7);', '_ = @intFromPtr(true);', '_ = @intFromPtr();',
        'const a = [2]u8{7,9}; const p: [*]const u8 = &a; _ = @intFromPtr(p+1);',
        'const a: u8 = 7; _ = @intFromPtr(&a);',
    ):
        yield 'pub fn main() void { ' + source + ' }\n'
    yield 'const a: u8 = 7; const n = @intFromPtr(&a); pub fn main() void { _ = n; }\n'


def arithmetic_qualifier_cases():
    for operator, offset, alignment, qualifier in itertools.product(
            ("+", "-"), ("0", "1", "n"), (1, 2, 8), ("", "allowzero ")):
        use = "_ = n; " if offset != "n" else ""
        yield (f"fn f(p: [*]align(8) allowzero u16, n: usize) [*]align({alignment}) {qualifier}u16 {{ "
               f"{use}return p {operator} {offset}; }} "
               "pub fn main() void { _ = f(undefined,1); }\n")


def cases():
    yield from int_from_ptr_cases()
    yield from arithmetic_qualifier_cases()
    for target, statement in (("[*]u8", "q += 1;"), ("[*]u8", "q -= 1;"),
                              ("[*]u16", "q += @intCast(@as(u8,1));"), ("*u8", "q += 1;")):
        yield (f'fn f(p: {target}) {target} {{ var q = p; {statement} return q; }} '
               'pub fn main() void { _ = f(undefined); }\n')
    for child, value in (("u8", "7"), ("i33", "-7"), ("u0", "0"), ("void", "{}"), ("type", "u8")):
        for expr in ("p+0", "p+1", "(p+1)-1", "p-1", "p-p", "(p+1)-p", "p-(p+1)",
                     "p+p", "p*2", "p+%1", "1+p", "p+undefined", "p + -1"):
            yield (f'const a = [2]{child}{{{value},{value}}}; const p: [*]const {child} = &a; '
                   f'pub fn main() void {{ _ = {expr}; }}\n')
    for expr in ("p+n", "p-n", "p-p", "p+n-p", "p+0", "p+undefined"):
        use = "_ = n; " if "n" not in expr or "undefined" in expr else ""
        yield (f'fn f(p: [*]const u16, n: usize) void {{ {use}_ = {expr}; }} '
               'pub fn main() void { f(undefined,1); }\n')
    for source in (
        'const a = [3]u8{7,9,11}; const p: [*]const u8 = &a; '
        'const n: u8 = if ((p+2)[0] != 11 or ((p+2)-1)[0] != 9 or (p+2)-&a != 2) 256 else 0; _ = n;',
        'const a = [2]type{u8,u16}; const p: [*]const type = &a; const q = (p+1)-1; const n: q[0] = 256; _ = n;',
        'const p: [*]const u8 = undefined; _ = p+1;',
        'const p: [*]const u8 = undefined; _ = &(p+1)[0];',
    ):
        yield 'pub fn main() void { ' + source + ' }\n'
    for expr in ('&.{7,9}', '&([2]u8{7,9})'):
        yield f'const p: [*]const u8 = {expr}; pub fn main() void {{ _ = p[1]; }}\n'
    yield 'const p: [*]const [2]u8 = &.{.{1,2},.{3,4}}; pub fn main() void { _ = p[1][1]; }\n'
    # Many pointers borrow actual roots; only dereferences, not addresses, need backing.
    for child, value in (("u8", "7"), ("i33", "-7"), ("bool", "true"),
                         ("u0", "0"), ("void", "{}"), ("type", "u8")):
        for index, operation in itertools.product(("0", "1", "2", "5", "undefined", "-1"),
                                                   ("_ = &p[I];", "_ = p[I];")):
            yield (f"const a = [2]{child}{{{value},{value}}}; const p: [*]const {child} = &a; "
                   "pub fn main() void { " + operation.replace("I", index) + " }\n")
    for source_const, target_const, source_align, target_align in itertools.product(
            ("", "const "), ("", "const "), (1, 8), (1, 8)):
        yield (f"fn f(p: *align({source_align}) {source_const}[2]u16) void {{ "
               f"const q: [*]align({target_align}) {target_const}u16 = p; _ = q; }} "
               "pub fn main() void { f(undefined); }\n")
    for source, target in itertools.product(("", "const ", "volatile ", "allowzero "), repeat=2):
        yield (f"fn f(p: [*]{source}u16) void {{ const q: [*]{target}u16 = p; _ = q; }} "
               "pub fn main() void { f(undefined); }\n")
    for expression in ("p.*", "p.len", "&p.*", "p[true]", "p[undefined]", "p[-1]", "p[0]"):
        yield ("fn f(p: [*]const u8) void { _ = " + expression + "; } "
               "pub fn main() void { f(undefined); }\n")
    for expression in ("p[0]", "p[1]", "p[2]", "p[3]", "&p[7]"):
        yield ("const p: [*]const u8 = \"ab\"; pub fn main() void { _ = " + expression + "; }\n")
    for source in (
        'const a = [2][2]u8{.{1,2},.{3,4}}; const p: [*]const [2]u8 = &a; '
        'const check: u8 = if (p[1][0] != 3 or &p[1][1] != &a[1][1]) 256 else 0; _ = check;',
        'const p: [*]const [2]u8 = &([2][2]u8{.{1,2},.{3,4}}); _ = p[1][1];',
        'const a = [2]type{u8,u16}; const p: [*]const type = &a; const n: p[1] = 65535; _ = n;',
        'const a = [2]u8{1,2}; const p: [*]const u8 = &a; const check: u8 = if (&p[1] != &a[1]) 256 else 0; _ = check;',
    ):
        yield 'pub fn main() void { ' + source + ' }\n'
    for child, value in (("u8", "7"), ("i33", "-7"), ("bool", "true"), ("u0", "0"), ("void", "{}")):
        for body in ("const b = a; a = b;", f"a[1] = {value};", "const p = &a[1]; _ = p.*;",
                     "const p: *const [2]T = &a; _ = p[1];", "const p = &a; p.* = a;",
                     "const n = a.len; _ = n;", "const p: *volatile [2]T = &a; _ = p[1];"):
            yield (f"var a: [2]{child} = .{{{value},{value}}}; "
                   f"pub fn main() void {{ {body.replace('T',child)} }}\n")
        yield (f"var a: [2]{child} = .{{{value},{value}}}; const n = a[0]; "
               "pub fn main() void { _ = n; }\n")
        yield (f"var a: [2]{child} = .{{{value},{value}}}; const p = &a[1]; const n = p.*; "
               "pub fn main() void { _ = n; }\n")
        yield (f"var a: [2]{child} = .{{{value},{value}}}; const n = a.len; "
               "pub fn main() void { _ = n; }\n")
    for source in (
        'var a: [2][2]u8 = .{.{1,2},.{3,4}}; const n = a[0].len; pub fn main() void { _ = n; }',
        'var p: *[2]u8 = undefined; const n = p.len; pub fn main() void { _ = n; }',
        'var a: [2]u8 = .{1,2}; var p: [2]*u8 = .{&a[0],&a[1]}; pub fn main() void { p[0].* = 7; }',
        'var a: [2]u8 = .{1,2}; var b: [2]u8 = a; pub fn main() void { _ = &b; }',
        'var a: [2]u8 = .{1,2}; pub fn main() void { a = [_]u16{1,2}; }',
    ):
        yield source + "\n"
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
