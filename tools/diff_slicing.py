#!/usr/bin/env python3
"""Compare slice expressions, result shapes, qualifiers and constant views with Zig."""
import argparse
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def cases():
    for child, value in (("u8", "7"), ("u16", "257"), ("i33", "-7"),
                         ("u0", "0"), ("void", "{}"), ("type", "u16")):
        for base in ('a', '(&a)', 's'):
            for lo, hi in ((0, 0), (0, 2), (1, 3), (3, 3), (2, 1), (0, 4), (1, None)):
                end = '' if hi is None else str(hi)
                yield (f'pub fn main() void {{ const a = [3]{child}{{{value},{value},{value}}}; '
                       f'const s: []const {child} = &a; '
                       + ('_ = s; ' if base != 's' else '')
                       + f'const r = {base}[{lo}..{end}]; _ = r.len; }}\n')
    for ty in ('*[4]u16', '[]u16', '[*]u16', '*u16'):
        for bounds in ('0..2', '1..3', '1..n', 'n..', 'n..n', '0..0', '0..1', '1..1', '2..2'):
            uses_n = 'n' in bounds
            yield (f'fn f(p: {ty}, n: usize) void {{ '
                   + ('' if uses_n else '_ = n; ')
                   + f'const r = p[{bounds}]; _ = r; }} '
                   'pub fn main() void { f(undefined,1); }\n')
    for ty, bounds, result in (
        ('[]u16','1..3','*[2]u16'), ('*[4]u16','1..','*[3]u16'),
        ('[*]u16','1..3','*[2]u16'), ('[]u16','n..n','*[0]u16'),
        ('[*]u16','n..','[*]u16'), ('[]u16','n..','[]u16'),
        ('*[4]u16','n..3','[]u16'), ('*u16','0..1','*[1]u16'),
    ):
        yield (f'fn f(p: {ty}, n: usize) {result} {{ '
               + ('' if 'n' in bounds else '_ = n; ')
               + f'return p[{bounds}]; }} pub fn main() void {{ _ = f(undefined,1); }}\n')
    for source, dest in (
        ('*align(1) [4]u16', '*align(1) [2]u16'),
        ('*align(1) [4]u16', '*[2]u16'),
        ('*align(8) [4]u16', '*align(2) [2]u16'),
        ('*align(8) [4]u16', '*align(8) [2]u16'),
        ('*const [4]u16', '*[2]u16'),
        ('*const [4]u16', '*const [2]u16'),
        ('*volatile [4]u16', '*[2]u16'),
        ('*volatile [4]u16', '*volatile [2]u16'),
        ('*allowzero [4]u16', '*[2]u16'),
    ):
        yield f'fn f(p: {source}) {dest} {{ return p[1..3]; }} pub fn main() void {{ _ = f(undefined); }}\n'
    for body in (
        'const a = [5]u16{2,3,5,7,11}; const v = a[1..4]; const c = v.*; const n: u8 = if (c[0] != 3 or c[2] != 7) 256 else 0; _ = n;',
        'const a = [5]u16{2,3,5,7,11}; const v = a[1..4][1..2]; const n: u8 = if (v[0] != 5) 256 else 0; _ = n;',
        'const a = [3]type{u8,u16,u32}; const T = a[1..3].*[0]; const n: u8 = if (T != u16) 256 else 0; _ = n;',
        'const a = "hello"[1..4].*; const n: u8 = if (a[0] != 101 or a[2] != 108) 256 else 0; _ = n;',
        'const a = "hello"[1..]; const n: u8 = if (a[4] != 0) 256 else 0; _ = n;',
        'const a = "hello"[5..6]; const n: u8 = if (a[0] != 0) 256 else 0; _ = n;',
        'const p: *[4]u8 = undefined; _ = p[0..0];',
        'const p: *[4]u8 = undefined; _ = p[0..1];',
        'const n: u8 = 4; _ = n[0..1];',
    ):
        yield 'pub fn main() void { '+body+' }\n'


    for body in (
        'const s: []u8 = undefined; _ = s[0..0];',
        'const s: []u8 = undefined; _ = s[0..1];',
        'const a = [2][2]u16{.{1,2},.{3,4}}; const v = a[1..2]; const n: u8 = if (v.*[0][1] != 4) 256 else 0; _ = n;',
        'const a = [3]u65{1,2,3}; const v = a[1..3]; const n: u8 = if (v.*[1] != 3) 256 else 0; _ = n;',
        'const a = [3]u8{1,2,3}; const s: []const u8 = &a; const v = s[1..2]; const n: u8 = if (&v[0] != &a[1]) 256 else 0; _ = n;',
        'const a = [3]u8{1,2,3}; const s: []const u8 = &a; _ = s[3..4];',
        'const a = [3]u8{1,2,3}; const p: [*]const u8 = &a; const v = p[1..3]; const n: u8 = if (v.*[1] != 3) 256 else 0; _ = n;',
        'const a = [3]u8{1,2,3}; const p: [*]const u8 = &a; const v = p[2..5]; _ = v.*;',
        'const p: *u8 = undefined; _ = p[1..1];',
        'const p: *u8 = undefined; _ = p[2..2];',
        'const a = "hello"[1..4][1..2].*; const n: u8 = if (a[0] != 108) 256 else 0; _ = n;',
        'const a = "hello"[6..6]; _ = a.*;',
    ):
        yield 'pub fn main() void { '+body+' }\n'
    for bounds in ('0..0', '0..1', '1..1', '0..2'):
        yield ('fn f(p: *[]u8) void { const v = p['+bounds+']; _ = v; } '
               'pub fn main() void { f(undefined); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-slicing-diff-') as directory:
        path = Path(directory)/'test.zig'
        checked = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)], source, f'diff_slicing case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'], source, f'diff_slicing case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'slicing: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
