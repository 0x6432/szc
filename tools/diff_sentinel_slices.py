#!/usr/bin/env python3
"""Compare scalar sentinel slice descriptors, coercions, inference and bounds."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def cases():
    for child, sentinel in (('u8','0'),('u8','17'),('i16','-1'),('bool','true'),('u0','0'),('u8','256'),('u8','undefined')):
        yield (f'fn f(p: [:{sentinel}]const {child}) usize {{ return p.len; }} '
               'pub fn main() void { _ = f(undefined); }\n')
    for source in ('[]u8','[:0]u8','[:17]u8','[]const u8','[:0]const u8','[:17]const u8'):
        for dest in ('[]u8','[:0]u8','[:17]u8','[]const u8','[:0]const u8','[:17]const u8'):
            yield f'fn f(p: {source}) {dest} {{ return p; }} pub fn main() void {{ _ = f(undefined); }}\n'
            for qual in ('','const '):
                yield f'fn f(p: *{qual}{source}) *{qual}{dest} {{ return p; }} pub fn main() void {{ _ = f(undefined); }}\n'
    for sentinel in ('','0','17'):
        suffix = ':'+sentinel if sentinel else ''
        for count in (0,2):
            for dest in ('[]u8','[:0]u8','[:17]u8','[]const u8','[:0]const u8','[:17]const u8'):
                for qual in ('','const '):
                    yield (f'fn f(p: *{qual}[{count}{suffix}]u8) {dest} {{ return p; }} '
                           'pub fn main() void { _ = f(undefined); }\n')
    for first in ('','0','17'):
        for second in ('','0','17'):
            left = ':'+first if first else ''
            right = ':'+second if second else ''
            expected = ':'+first if first and first == second else ''
            yield (f'fn f(c: bool, a: *const [2{left}]u8, b: *const [3{right}]u8) [{expected}]const u8 '
                   '{ const s = if(c) a else b; return s; } pub fn main() void { _ = f(true,undefined,undefined); }\n')
            yield (f'fn f(c: bool, a: [{left}]const u8, b: *const [3{right}]u8) [{expected}]const u8 '
                   '{ const s = if(c) a else b; return s; } pub fn main() void { _ = f(true,undefined,undefined); }\n')
    for source in ('[:0]const u8','[:17]const u8'):
        for expression in ('p[0..]','p[i..]','p[0..2]','p[i..j]','p[0]','p[i]','&p[i]','p.ptr','&p.ptr','p.*','&p.*'):
            yield (f'fn f(p: {source}, i: usize, j: usize) void {{ '+
                   ('' if 'i' in expression else '_ = i; ')+('' if 'j' in expression else '_ = j; ')+
                   '_ = '+expression+'; } pub fn main() void { f(undefined,0,0); }\n')
    for sentinel in ('0','17'):
        for expression in ('s[0]','s[2]','s[3]','s[0..]','s[1..]','s[0..2]','s[0..3]','s[0..4]','s.*','&s.*'):
            yield (f'const s: [:{sentinel}]const u8 = &.{{7,9}}; '
                   'pub fn main() void { _ = '+expression+'; }\n')
    for typ,sentinel,values in (('u8','17','2,3'),('bool','true','false,true'),('u0','0','0,0')):
        yield (f'const s: [:{sentinel}]const {typ} = &.{{{values}}}; '
               'pub fn main() void { const n: u8 = if(s.len != 2 or s[2] != '+sentinel+') 256 else 0; _ = n; }\n')
        for expression in ('s[1..]','s[0..2]'):
            yield (f'const s: [:{sentinel}]const {typ} = &.{{{values}}}; '
                   'pub fn main() void { const a = '+expression+'; const n: u8 = if(a[a.len] != '+sentinel+') 256 else 0; _ = n; }\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc');parser.add_argument('zig');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-sentinel-slices-diff-') as directory:
        path=Path(directory)/'test.zig'
        for checked,source in enumerate(cases(),1):
            path.write_text(source)
            a=run_compiler([args.szc,'tir',str(path)],source,f'diff_sentinel_slices case {checked} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],source,f'diff_sentinel_slices case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'sentinel slices: {checked} matched Zig 0.17.0',flush=True)


if __name__=='__main__':
    main()
