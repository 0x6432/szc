#!/usr/bin/env python3
"""Compare nonvolatile aggregate memset typing and diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    for ty, values in (('[3]u8', ('.{1,2,3}', 'undefined', '.{1,2}', '.{1,2,256}')),
                       ('[2]u33', ('.{1,8589934591}', '.{1,8589934592}')),
                       ('[2][2]i40', ('.{.{1,-2},.{3,4}}', 'undefined')),
                       ('[2:17]u16', ('.{3,5}', 'undefined')),
                       ('[2]bool', ('.{true,false}', '.{true,1}')),
                       ('[]const u8', ('"abc"', 'undefined')),
                       ('[0]u8', ('.{}', 'undefined')),
                       ('[2]u0', ('.{0,0}', 'undefined')),
                       ('[0]u16', ('.{}', 'undefined'))):
        for value in values:
            for count in (0, 1, 3):
                yield (f'pub fn main() void {{ var a:[{count}]{ty}=undefined; '
                       f'@memset(&a,{value}); }}\n')
                yield (f'fn f(p:[]{ty}) void {{ @memset(p,{value}); }} '
                       f'pub fn main() void {{ var a:[{count}]{ty}=undefined; f(&a); }}\n')
    for ty, value in (('[2]u8','.{1,2}'), ('[2]u0','.{0,0}'), ('[0]type','undefined')):
        for count in (0, 1):
            for qualifier in ('', 'const '):
                yield (f'pub fn main() void {{ @memset(@as(*{qualifier}[{count}]{ty},undefined),'
                       f'{value}); }}\n')
                yield (f'pub fn main() void {{ @memset(@as([]{qualifier}{ty},undefined),'
                       f'{value}); }}\n')
        yield (f'pub fn main() void {{ @memset(@as([*]{ty},undefined),{value}); }}\n')
    for shape, value in (('*volatile [0][2]u8', '.{1,2}'), ('[]volatile [2]u0', 'undefined')):
        yield f'pub fn main() void {{ @memset(@as({shape},undefined),{value}); }}\n'
    for shape in ('*[1][0]type', '[][0]type'):
        yield f'pub fn main() void {{ @memset(@as({shape},undefined),.{{}}); }}\n'
    for ty, value in (('[2]u8','.{1,2}'), ('[1]type','.{u8}')):
        yield (f'pub fn main() void {{ @memset(@as(*[1]{ty},undefined),{value}); }}\n')
    yield ('fn f(d:[][]const u8,v:[]u8) void { @memset(d,v); } '
           'pub fn main() void { var a:[2][]const u8=undefined; var b:[2]u8=.{1,2}; f(&a,&b); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-aggregate-memset-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_aggregate_memset case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_aggregate_memset case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'aggregate memset: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
