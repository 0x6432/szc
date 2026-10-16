#!/usr/bin/env python3
"""Compare explicit slice sentinel assertions, contextual types and constant memory."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def cases():
    for child, values, sentinels in (('u8','7,9,17',('0','17','255','256','undefined','@intCast(17)')),
                                    ('bool','false,true,true',('false','true','undefined')),
                                    ('u0','0,0,0',('0','1'))):
        for sentinel in sentinels:
            for first,last in ((0,0),(0,1),(0,2),(1,2),(0,3),(3,3)):
                yield (f'const a = [3]{child}{{{values}}}; '
                       'pub fn main() void { _ = a['+str(first)+'..'+str(last)+':'+sentinel+']; }\n')
    for source in ('*[3]u8','*[3:17]u8','[]u8','[:17]u8','[*]u8','[*:17]u8','*u8'):
        for bounds in ('0..2','1..2','i..j','i..','0..'):
            for sentinel in ('0','17','@intCast(17)'):
                discard = ('' if 'i' in bounds else '_ = i; ')+('' if 'j' in bounds else '_ = j; ')
                yield (f'fn f(p: {source}, i: usize, j: usize) void {{ '+discard+'_ = p['+bounds+':'+sentinel+']; } '
                       'pub fn main() void { f(undefined,0,2); }\n')
    for body in (
        'const a = [3]u8{7,9,17}; const s = a[0..2:17]; const x: u8 = if(s.len != 2 or s[2] != 17) 256 else 0; _ = x;',
        'const a = [3]u8{7,9,17}; const s: [:17]const u8 = a[0..2:17]; const x: u8 = if(s.len != 2 or s[2] != 17) 256 else 0; _ = x;',
        'const a = [3]u16{7,9,65535}; const s = a[0..2:65535]; const x: u8 = if(s[2] != 65535) 256 else 0; _ = x;',
        'const a = [3]i16{7,9,-17}; const s = a[0..2:-17]; const x: u8 = if(s[2] != -17) 256 else 0; _ = x;',
        'const a = [3]bool{false,true,true}; const s = a[0..1:true]; const x: u8 = if(s[0] or !s[1]) 256 else 0; _ = x;',
        'const a = [3]u0{0,0,0}; const s = a[0..2:0]; const x: u8 = if(s[2] != 0) 256 else 0; _ = x;',
    ):
        yield 'pub fn main() void { '+body+' }\n'
    yield 'fn f(p: []u8, n: u8) void { _ = p[0..2:n]; } pub fn main() void { f(undefined,17); }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc'); parser.add_argument('zig'); args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-sentinel-assertions-diff-') as directory:
        path = Path(directory)/'test.zig'
        for checked,source in enumerate(cases(),1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)],source,f'diff_sentinel_assertions case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],source,f'diff_sentinel_assertions case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'sentinel assertions: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
