#!/usr/bin/env python3
"""Compare descriptor field addresses, qualifiers, writes and provenance with Zig."""
import argparse
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def cases():
    storages = ('*[]const u16', '*const []const u16', '*volatile []const u16',
                '*allowzero []const u16', '*align(1) []const u16',
                '*align(32) []const u16', '*align(1) volatile const []const u16')
    for source in storages:
        for field, child in (('len','usize'), ('ptr','[*]const u16')):
            for qualifiers in ('', 'const ', 'volatile ', 'allowzero ', 'align(1) ',
                               'align(32) ', 'align(1) volatile const '):
                dest = '*'+qualifiers+child
                yield (f'fn f(p: {source}) {dest} {{ return &p.{field}; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
            yield (f'fn f(p: {source}) void {{ p.{field} = undefined; }} '
                   'pub fn main() void { f(undefined); }\n')
            yield (f'fn f(p: {source}) void {{ _ = p.{field}; }} '
                   'pub fn main() void { f(undefined); }\n')
    for body in (
        'const s: []const u8 = "hello"; const n = &s.len; const v: u8 = if (n.* != 5) 256 else 0; _ = v;',
        'const s: []const u8 = "hello"; const p = &s.ptr; const v: u8 = if (p.*[1] != 101) 256 else 0; _ = v;',
        'const s: []const u8 = "hello"; const v: u8 = if (&s.ptr == &s.len) 256 else 0; _ = v;',
        'const s: []u8 = undefined; _ = &s.len;',
        'const s: []u8 = undefined; _ = &s.ptr;',
        'const p: *[]u8 = undefined; _ = &p.len;',
        'const p: *[]u8 = undefined; _ = &p.ptr;',
        'var s: []const u8 = "hello"; s.ptr += 1; s.len -= 1;',
        'const s: []const u8 = "hello"; s.len = 1;',
        'const s: []u8 = undefined; s.ptr = undefined;',
        'const a = [3]u8{1,2,3}; const n = &a.len; const v: u8 = if (n.* != 3) 256 else 0; _ = v;',
        'var a = [3]u8{1,2,3}; a.len = 2;',
        'const a = [2][]const u8{"hello","xy"}; const n = &a[1].len; const v: u8 = if (n.* != 2) 256 else 0; _ = v;',
        'const a = [2][]const u8{"hello","xy"}; const p = &a[1].ptr; const v: u8 = if (p.*[1] != 121) 256 else 0; _ = v;',
    ):
        yield 'pub fn main() void { '+body+' }\n'
    yield ('var s: []const u8 = "hello"; const n = &s.len; '
           'pub fn main() void { n.* = 2; if (s.len != 2) @panic("bad field"); }\n')
    yield ('const s: []const u8 = "hello"; const a = &s.len; const b = &s.len; '
           'const n: u8 = if (a != b) 256 else 0; pub fn main() void { _ = n; }\n')


    for child in ('u8','u0','void'):
        yield (f'fn f(s: []{child}) void {{ s.len = 0; }} '
               'pub fn main() void { f(undefined); }\n')
        yield (f'fn f(s: []{child}) *const usize {{ return &s.len; }} '
               'pub fn main() void { _ = f(undefined); }\n')
    for source in ('[]align(1) u16', '[]volatile u16', '[]allowzero u16', '[]const u16'):
        for dest in ('[*]u16','[*]align(1) u16','[*]volatile u16','[*]allowzero u16','[*]const u16'):
            yield (f'fn f(p: *{source}) *{dest} {{ return &p.ptr; }} '
                   'pub fn main() void { _ = f(undefined); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-slice-fields-diff-') as directory:
        path = Path(directory)/'test.zig'
        checked = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)], source, f'diff_slice_fields case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'], source, f'diff_slice_fields case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'slice fields: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
