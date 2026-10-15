#!/usr/bin/env python3
"""Compare scalar sentinel array types, literals, coercions and concatenation."""
import argparse
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def cases():
    for child, sentinel in (('u8','0'),('u8','255'),('u8','256'),('i8','-1'),
                            ('u16','99'),('i33','-17'),('u0','0'),('u0','1'),
                            ('bool','true'),('bool','false'),('u8','undefined')):
        for count in (0,1,3):
            yield (f'fn f(p: *const [{count}:{sentinel}]{child}) void {{ _ = p; }} '
                   'pub fn main() void { f(undefined); }\n')
    for child, sentinel, values in (('u8','17','2,3'),('bool','true','false'),('u16','65535','')):
        for const in ('const ', ''):
            yield (f'const p: [*:{sentinel}]{const}{child} = &.{{{values}}}; '
                   'pub fn main() void { _ = p; }\n')
    types = ('[2]u8','[2:0]u8','[2:1]u8','[2:255]u8')
    for source in types:
        for dest in types:
            yield (f'fn f(p: {source}) {dest} {{ return p; }} '
                   'pub fn main() void { _ = f(undefined); }\n')
            for qualifier in ('', 'const '):
                yield (f'fn f(p: *{qualifier}{source}) *{qualifier}{dest} {{ return p; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
        for dest in ('[*]const u8','[*:0]const u8','[*:1]const u8','[*:255]const u8'):
            yield (f'fn f(p: *const {source}) {dest} {{ return p; }} '
                   'pub fn main() void { _ = f(undefined); }\n')
    for child, sentinel, values in (('u8','0','7,9'),('u8','255','7,9'),('i16','-17','7,9'),
                                    ('bool','true','false,true'),('bool','false','true,false'),('u0','0','0,0')):
        for length in ('2','_'):
            for pointer in (False,True):
                value = ('&' if pointer else '')+f'[{length}:{sentinel}]{child}'+'{'+values+'}'
                target = ('*const ' if pointer else '')+f'[2:{sentinel}]{child}'
                yield (f'const a: {target} = {value}; '
                       'pub fn main() void { const x: u8 = if(a[2] != '+sentinel+') 256 else 0; _ = x; }\n')
    for left in ('', ':0', ':1', ':255'):
        for right in ('', ':0', ':1', ':255'):
            sentinel = left or right
            if left and right and left != right:
                sentinel = ''
            yield ('const a = [_'+left+']u8{7,9}; const b = [_'+right+']u8{11}; '
                   'const c: [3'+sentinel+']u8 = a ++ b; '
                   'pub fn main() void { const x: u8 = if(c[0] != 7 or c[2] != 11) 256 else 0; _ = x; }\n')
            yield ('const a = &[_'+left+']u8{7,9}; const b = &[_'+right+']u8{11}; '
                   'const c: *const [3'+sentinel+']u8 = a ++ b; '
                   'pub fn main() void { const x: u8 = if(c[0] != 7 or c[2] != 11) 256 else 0; _ = x; }\n')
    for source in types:
        for dest in types:
            for qualifiers in ('', 'const '):
                yield (f'fn f(p: *{qualifiers}*{source}) *{qualifiers}*{dest} {{ return p; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
    for source in types:
        for dest in types:
            for prefix in ('[*]const ', '[*]', '[]const ', '[]'):
                yield (f'fn f(p: {prefix}{source}) {prefix}{dest} {{ return p; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
            yield (f'fn f(p: *const fn () {source}) *const fn () {dest} {{ return p; }} '
                   'pub fn main() void { _ = f(undefined); }\n')
    for first in ('0','1','255'):
        for second in ('0','1','255'):
            for qualifier in ('', 'const '):
                yield (f'fn f(c: bool, a: *{qualifier}[2:{first}]u8, b: *{qualifier}[2:{second}]u8) *{qualifier}[2]u8 '
                       '{ const p = if(c) a else b; return p; } '
                       'pub fn main() void { _ = f(true,undefined,undefined); }\n')
            if first != second:
                yield (f'fn f(c: bool, a: *[2:{first}]u8, b: *[3:{second}]u8) []u8 '
                       '{ const p = if(c) a else b; return p; } '
                       'pub fn main() void { _ = f(true,undefined,undefined); }\n')
    yield ('const a: [1][0:17]u8 = .{.{}}; const s: []const [0:17]u8 = &a; '
           'pub fn main() void { const v = s.*; const n: u8 = if(v[0][0] != 17) 256 else 0; _ = n; }\n')
    for body in (
        'const P = [18446744073709551615:0]u0; const n: u8 = if(@bitSizeOf(P) != 0) 256 else 0; _ = n;',
    ):
        yield 'pub fn main() void { '+body+' }\n'
    for expression in ('p[0..]', 'p[0]', 'p[i]'):
        discard = '' if expression == 'p[i]' else '_ = i; '
        yield ('fn f(p: *const [18446744073709551615:0]u0, i: usize) void { '+discard+'_ = '+expression+'; } '
               'pub fn main() void { f(undefined,0); }\n')
    for end in ('0', '3', '18446744073709551615'):
        yield ('fn f(p: *const [18446744073709551615:0]u0) void { _ = p[0..'+end+']; } '
               'pub fn main() void { f(undefined); }\n')
    for body in (
        'const a = [_:17]u16{2,3}; const n: u8 = if(a[2] != 17 or a.len != 2) 256 else 0; _ = n;',
        'const a = [_:17]u16{2,3}; _ = a[3];',
        'var a: [2:17]u16 = .{2,3}; a = .{a[1],a[2]};',
        'var a: [2:true]bool = .{false,true}; a = .{a[1],a[2]};',
        'const P = [2:17]u16; const Q = [2:(1+16)]u16; const n: u8 = if(P != Q) 256 else 0; _ = n;',
        'const P = [0:true]bool; const n: u8 = if(@sizeOf(P) != 1 or @bitSizeOf(P) != 1) 256 else 0; _ = n;',
        'const P = [0:0]u0; const n: u8 = if(@sizeOf(P) != 0 or @bitSizeOf(P) != 0) 256 else 0; _ = n;',
        'const P = [18446744073709551615:0]u0; const n: u8 = if(@sizeOf(P) != 0) 256 else 0; _ = n;',
        'const a = [_:255]u8{7,9}; const p: [*:255]const u8 = &a; const n: u8 = if(p[2] != 255) 256 else 0; _ = n;',
        'const a = [_:255]u8{7,9}; const p = a[1..]; const n: u8 = if(p[1] != 255) 256 else 0; _ = n;',
    ):
        yield 'pub fn main() void { '+body+' }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-array-sentinels-diff-') as directory:
        path = Path(directory)/'test.zig'
        for checked, source in enumerate(cases(),1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)],source,f'diff_array_sentinels case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],source,f'diff_array_sentinels case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'array sentinels: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
