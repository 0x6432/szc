#!/usr/bin/env python3
"""Compare scalar-sentinel many-pointer coercions and transformations with Zig."""
import argparse
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def cases():
    types = ('[*]const u8', '[*:0]const u8', '[*:1]const u8', '[*:255]const u8',
             '[*:0]u8', '[*:0]volatile const u8', '[*:0]allowzero const u8')
    for source in types:
        for dest in types:
            yield (f'fn f(p: {source}) {dest} {{ return p; }} '
                   'pub fn main() void { _ = f(undefined); }\n')
    for source in types[:4]:
        for dest in types[:4]:
            for qualifier in ('', 'const '):
                yield (f'fn f(p: *{qualifier}{source}) *{qualifier}{dest} {{ return p; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
    for sentinel, child in (('0', 'u8'), ('255', 'u8'), ('-1', 'i8'), ('true', 'bool'),
                            ('false', 'bool'), ('0', 'u0'), ('256', 'u8'),
                            ('-1', 'u8'), ('false', 'u8'), ('undefined', 'u8')):
        yield (f'fn f(p: [*:{sentinel}]{child}) void {{ _ = p; }} '
               'pub fn main() void { f(undefined); }\n')
    for source in ('[*:0]const u8', '[*:17]u16', '[*:true]bool', '[*:0]u0',
                   '[*:0]align(32) const u8', '[*:0]allowzero const u8'):
        for expression in ('p + 1', 'p - 1', 'p + n', 'p - n', 'p[n..]'):
            yield (f'fn f(p: {source}, n: usize) {source} {{ _ = &n; return {expression}; }} '
                   'pub fn main() void { _ = f(undefined,1); }\n')
    for source, single, array in (
        ('[*:0]const u8', '*const u8', '*const [1]u8'),
        ('[*:17]u16', '*u16', '*[1]u16'),
        ('[*:true]bool', '*bool', '*[1]bool'),
        ('[*:0]u0', '*u0', '*[1]u0'),
        ('[*:0]align(32) const u8', '*align(32) const u8', '*align(32) const [1]u8'),
        ('[*:0]allowzero const u8', '*allowzero const u8', '*allowzero const [1]u8'),
    ):
        for expression, dest in (('&p[0]', single), ('p[0..1]', array)):
            yield (f'fn f(p: {source}) {dest} {{ return {expression}; }} '
                   'pub fn main() void { _ = f(undefined); }\n')
    for source in types[:4]:
        for dest in types[:4]:
            yield (f'fn f(c: bool, a: {source}, b: {dest}) {source} {{ return if(c) a else b; }} '
                   'pub fn main() void { _ = f(true,undefined,undefined); }\n')
    for source in types[:4]:
        for literal in ('"abc"', '&[3]u8{1,2,0}', '""'):
            yield (f'fn f() {source} {{ return {literal}; }} '
                   'pub fn main() void { _ = f(); }\n')
    for source in types[:4]:
        for dest in types[:4]:
            yield (f'fn f(c: bool, a: {source}, b: {dest}) {source} {{ const p = if(c) a else b; return p; }} '
                   'pub fn main() void { _ = f(true,undefined,undefined); }\n')
    for source in types[:4]:
        for expression in ('if(c) a else "abc"', 'if(c) "abc" else a'):
            for dest in ('[*]const u8', '[*:0]const u8'):
                yield (f'fn f(c: bool, a: {source}) {dest} {{ const p = {expression}; return p; }} '
                       'pub fn main() void { _ = f(true,undefined); }\n')
    for source in ('[*:0]align(1) const u16', '[*:0]align(2) const u16', '[*:0]align(32) const u16'):
        for dest in ('[*:0]align(1) const u16', '[*:0]align(2) const u16', '[*:0]align(32) const u16'):
            yield (f'fn f(p: {source}) {dest} {{ return p; }} '
                   'pub fn main() void { _ = f(undefined); }\n')
    for body in (
        'const p: [*:0]const u8 = "hello"; const x: u8 = if(p[5] != 0 or p[1] != 101) 256 else 0; _ = x;',
        'const p: [*:0]const u8 = "hello"; const q = p + 2; const x: u8 = if(q[3] != 0 or q[0] != 108) 256 else 0; _ = x;',
        'const p: [*:0]const u8 = "hello"; const q = (p + 3) - 2; const x: u8 = if(q[0] != 101) 256 else 0; _ = x;',
        'const p: [*:0]const u8 = "hello"; _ = p[6];',
        'var n: u8 = 0; _ = &n; const P = [*:n]u8; _ = P;',
        'const n: u8 = 1 + 1; const P = [*:n]u8; const Q = [*:2]u8; const x: u8 = if(P != Q) 256 else 0; _ = x;',
        'const P = [*:false]bool; const Q = [*:true]bool; const x: u8 = if(P == Q) 256 else 0; _ = x;',
    ):
        yield 'pub fn main() void { '+body+' }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-sentinel-pointers-diff-') as directory:
        path = Path(directory)/'test.zig'
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)], source, f'diff_sentinel_pointers case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'], source, f'diff_sentinel_pointers case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'sentinel pointers: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
