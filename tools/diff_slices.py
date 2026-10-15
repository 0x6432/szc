#!/usr/bin/env python3
"""Compare slice descriptors, coercions, fields, indexing and ABI with Zig 0.17."""
import argparse
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def sequence_cases():
    for child, value in (("u8","7"),("u33","4294967297"),("i7","-7"),("u0","0"),("void","{}")):
        for operand in ("a", "&a", "s"):
            for capture, body in (("item", "_ = item;"), ("_", "")):
                yield (f'fn f(s: []const {child}) void {{ const a = [2]{child}{{{value},{value}}}; '
                       + ('_ = s; ' if operand != 's' else '_ = a; ')
                       + f'for ({operand}) |{capture}| {{ {body} }} }} '
                       + f'pub fn main() void {{ const a = [2]{child}{{{value},{value}}}; f(&a); }}\n')
    for expression in ('for (s,0..) |x,i| { _ = x; _ = i; }',
                       'for (s,s) |x,y| { _ = x; _ = y; }',
                       'for (s) |x| { _ = x; break; }',
                       'for (s) |_| {} else {}',
                       'for (s) {}', 'for (s) |a,b| { _ = a; _ = b; }',
                       'for (s) |*a| { _ = a; }', 'for (s) |*_| {}'):
        yield 'fn f(s: []const u8) void { '+expression+' } pub fn main() void { f("hi"); }\n'
    for operand in ('a','&a','s'):
        yield ('pub fn main() void { var a = [2]u8{7,9}; const s: []u8 = &a; '
               + ('_ = s; ' if operand != 's' else '')
               + f'for ({operand}) |*item| {{ item.* += 1; }} }}\n')
    for child, value in (("[2]u8", ".{7,9}"), ("[]const u8", '"hi"')):
        for operand in ("a", "&a", "s"):
            yield (f'pub fn main() void {{ const a = [2]{child}{{{value},{value}}}; '
                   f'const s: []const {child} = &a; '
                   + ('_ = s; ' if operand != 's' else '')
                   + f'for ({operand}) |row| {{ for (row) |item| {{ _ = item; }} }} }}\n')
    yield 'pub fn main() void { const a = [2]u8{1,2}; for (&a) |*item| { item.* = 3; } }\n'
    yield 'pub fn main() void { for (42) |_| {} }\n'
    yield 'pub fn main() void { for (true) |_| {} }\n'
    yield 'fn f(p: [*]u8) void { for (p) |_| {} } pub fn main() void { f(undefined); }\n'


def cases():
    yield from sequence_cases()
    for child, value in (("u8","7"),("u16","257"),("u0","0"),("void","{}"),("type","u8")):
        for length in (0, 1, 3):
            items = ','.join([value]*length)
            yield (f'const s: []const {child} = &[{length}]{child}{{{items}}}; '
                   f'const n: u8 = if (s.len != {length}) 256 else 0; '
                   'pub fn main() void { _ = n; }\n')
            for index in (0, length):
                yield (f'const s: []const {child} = &[{length}]{child}{{{items}}}; '
                       f'pub fn main() void {{ _ = s[{index}]; }}\n')
    for source in (
        'const s: []const u8 = &.{7,9}; const n: u8 = if (s[1] != 9) 256 else 0; _ = n;',
        'const s: []const u8 = "hello"; const n: u8 = if (s.len != 5 or s[1] != 101) 256 else 0; _ = n;',
        'const s: []const type = &.{u8,u16}; const T = s[1]; _ = @as(T,257);',
        'const s: []const u8 = undefined; _ = s.len;',
        'const p: *[3]u8 = undefined; const s: []u8 = p; const n: u8 = if (s.len != 3) 256 else 0; _ = n;',
        'const s: []const u8 = [2]u8{7,9}; _ = s;',
        'const s: []u8 = &[_]u8{}; _ = s;',
        'const s: []align(64) u8 = &[_]u8{}; _ = s;',
        'const s: []u8 = ""; _ = s;',
        'const s: []u8 = "hi"; _ = s;',
        'const s: []const u8 = "hi"; _ = @intFromPtr(s);',
        'const s: []const u8 = "hi"; _ = @bitSizeOf([]const u8); _ = s;',
    ):
        yield 'pub fn main() void { '+source+' }\n'
    for source in ('*const [3]u16','*[3]u16','*align(1) [3]u16','*volatile [3]u16',
                   '*allowzero [3]u16','[]const u16','[]u16','[]align(1) u16',
                   '[]volatile u16','[]allowzero u16','[*]u16'):
        for dest in ('[]u16','[]const u16','[]align(1) const u16','[]volatile allowzero const u16'):
            yield (f'fn f(p: {source}) {dest} {{ return p; }} '
                   'pub fn main() void { _ = f(undefined); }\n')
    for expr in ('s.len','s.ptr','s[0]','s[1]','s.*'):
        yield (f'fn f(s: []const u8) void {{ _ = {expr}; }} '
               'pub fn main() void { f("hi"); }\n')
    for target in ('*[]u8','*const []const u8'):
        yield f'fn f(p: {target}) usize {{ return p.len; }} pub fn main() void {{ _ = f(undefined); }}\n'
    for target in ('*[]u8', '*const []const u8'):
        yield (f'fn f(p: {target}) usize {{ var q = p; _ = &q; return q.len; }} '
               'pub fn main() void { _ = f(undefined); }\n')
    yield 'export fn bad(s: []const u8) void { _ = s; } pub fn main() void {}\n'
    yield 'export fn bad() []const u8 { return "hi"; } pub fn main() void {}\n'
    yield 'fn f(s: []u8) []const u8 { return if (s.len == 0) s else "hi"; } pub fn main() void { var a = [2]u8{7,9}; _ = f(&a); }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-slices-diff-') as directory:
        path = Path(directory)/'test.zig'
        checked = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)], source, f'diff_slices case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'], source, f'diff_slices case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'slices: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
