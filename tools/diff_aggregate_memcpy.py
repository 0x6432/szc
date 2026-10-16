#!/usr/bin/env python3
"""Compare nonvolatile aggregate memcpy typing and diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    for length in (0, 1):
        yield (f'pub fn main() void {{ @memcpy(@as(*[1][{length}]type,undefined),'
               f'@as(*const [1][{length}]type,undefined)); }}\n')
    for ty in ('[3]u8', '[2]u33', '[2][3]i40', '[2:17]u16', '[]u8', '[]const u8', '[:17]const u8'):
        for count in (0, 1, 3):
            for d, s in (('*[N]T','*const [N]T'), ('[]T','[]const T'),
                         ('[*]T','[]const T'), ('[]T','[*]const T')):
                d, s = (x.replace('N',str(count)).replace('T',ty) for x in (d,s))
                yield (f'fn f(d:{d},s:{s}) void {{ @memcpy(d,s); }} '
                       f'pub fn main() void {{ var a:[{count}]{ty}=undefined; '
                       f'const b:[{count}]{ty}=undefined; f(&a,&b); }}\n')
    for d, s in (('[2]u8','[3]u8'), ('[2:17]u8','[2:19]u8'), ('[2]u8','[2]i8'),
                 ('[]const u8','[]u8'), ('[]u8','[]const u8'), ('[2]*const u8','[2]*u8'),
                 ('[2]*u8','[2]*const u8')):
        for count in (0, 1):
            yield (f'pub fn main() void {{ var a:[{count}]{d}=undefined; '
                   f'const b:[{count}]{s}=undefined; @memcpy(&a,&b); }}\n')
    for ty in ('[0]u8','[2]u0','[2][0]u16','[2]u8'):
        for n, m in ((0,0),(1,1),(0,1)):
            yield (f'pub fn main() void {{ @memcpy(@as(*[{n}]{ty},undefined),'
                   f'@as(*const [{m}]{ty},undefined)); }}\n')
    for ty in ('[3]u8', '[2]u33', '[]const u8'):
        for a,b,n in ((0,0,0),(0,0,1),(0,1,2),(1,0,2),(0,2,2)):
            yield (f'var a:[4]{ty}=undefined; pub fn main() void {{ '
                   f'@memcpy(a[{a}..][0..{n}],a[{b}..][0..{n}]); }}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-aggregate-memcpy-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_aggregate_memcpy case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_aggregate_memcpy case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'aggregate memcpy: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
