#!/usr/bin/env python3
"""Compare slice dereference, address cancellation and runtime-length errors with Zig."""
import argparse
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def cases():
    for child, value in (('u8','7'), ('i33','-7'), ('u0','0'), ('void','{}'), ('type','u16')):
        for count in (0,1,3):
            items = ','.join([value]*count)
            for expr in ('s.*', '&s.*', '(&s).*.*'):
                yield (f'pub fn main() void {{ const a = [{count}]{child}{{{items}}}; '
                       f'const s: []const {child} = &a; const r = {expr}; _ = r; }}\n')
    for child in ('u8','u0','void'):
        for expr in ('s.*', '&s.*'):
            yield (f'fn f(s: []{child}) void {{ _ = {expr}; }} '
                   'pub fn main() void { f(undefined); }\n')
            # Zig 0.17.0 crashes (-11) on &undefined_zero_bit_slice.*;
            # keep that invalid-address case in szc-only diagnostic fixtures.
            if child == 'u8' or expr == 's.*':
                yield f'pub fn main() void {{ const s: []{child} = undefined; _ = {expr}; }}\n'
    for body in (
        'const a = [2][0]u8{.{},.{}}; const s: []const [0]u8 = &a; const v: [0]u8 = s.*; _ = v;',
        'const a = [2][2]u0{.{0,0},.{0,0}}; const s: []const [2]u0 = &a; const v: [2]u0 = s.*; const n: u8 = if (v[0] != 0) 256 else 0; _ = n;',
        'const a = [3]u0{0,0,0}; const s: []const u0 = &a; const v: u0 = s.*; _ = v;',
        'const a = [3]void{{},{},{}}; const s: []const void = &a; const v: void = s.*; _ = v;',
        'const s: []const u8 = "hello"; const a = s.*; const n: u8 = if (a.len != 5 or a[1] != 101) 256 else 0; _ = n;',
        'const a = [3]u8{2,3,5}; const s: []const u8 = a[1..3]; const p = &s.*; const n: u8 = if (p[0] != 3 or &p[1] != &a[2]) 256 else 0; _ = n;',
        'const a = [3]type{u8,u16,u32}; const s: []const type = &a; const T = s.*[1]; const n: u8 = if (T != u16) 256 else 0; _ = n;',
        'const s: []const u8 = "hello"; const p: *const [5]u8 = &s.*; _ = p;',
        'const s: []const u8 = "hello"; const p: *[5]u8 = &s.*; _ = p;',
        'const s: []u8 = @as(*[0]u8,undefined); _ = s.*;',
        'const s: []u8 = @as(*[0]u8,undefined); _ = &s.*;',
    ):
        yield 'pub fn main() void { '+body+' }\n'
    for result in ('u16','[3]u16'):
        yield ('var a: [3]u16 = .{2,3,5}; const s: []u16 = &a; '
               f'fn f() {result} {{ return s.*; }} pub fn main() void {{ _ = f(); }}\n')
    for result in ('u16','[0]u16'):
        yield ('var a: [3]u16 = .{2,3,5}; const s: []u16 = a[1..1]; '
               f'fn f() {result} {{ return s.*; }} pub fn main() void {{ _ = f(); }}\n')
    yield ('var a: [3]u16 = .{2,3,5}; const s: []u16 = a[1..1]; const e = s.*; '
           'pub fn main() void { const p: [0]u16 = e; _ = p; }\n')
    for alignment in (1,2,8,256):
        for dest in (1,2,8,256):
            yield (f'var a: [3]u16 align({alignment}) = .{{2,3,5}}; const s: []align({alignment}) u16 = &a; '
                   f'pub fn main() void {{ const p: *align({dest}) [3]u16 = &s.*; _ = p; }}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-slice-deref-diff-') as directory:
        path = Path(directory)/'test.zig'
        checked = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)], source, f'diff_slice_deref case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'], source, f'diff_slice_deref case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'slice dereference: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
