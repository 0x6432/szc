#!/usr/bin/env python3
"""Compare integer absolute values, unsigned result types and diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    for width in (0, 1, 2, 5, 8, 16, 29, 32, 33, 40, 63, 64, 65, 128, 257, 513):
        for signed in (False, True):
            if signed and width == 0:
                continue
            ty = ('i' if signed else 'u') + str(width)
            values = (0, -1, -(1 << (width-1)), (1 << (width-1))-1) if signed else (0, 1, (1 << width)-1) if width else (0,)
            for value in dict.fromkeys(values):
                yield (f'const n = @abs(@as({ty},{value})); const p:*const u{width}=&n; '
                       f'const check:u8=if(n=={abs(value)})0 else 256; '
                       'pub fn main() void { _=check; _=p; }\n')
            yield (f'const n = @abs(@as({ty},undefined)); const check:u8=if(n==0)0 else 256; '
                   'pub fn main() void { _=check; }\n')
            if width <= 64:
                yield (f'fn f(n:{ty}) void {{ const magnitude=@abs(n); '
                       f'const p:*const u{width}=&magnitude; _=p; }} '
                       'pub fn main() void { f(0); }\n')
    for value in (0, 1, -1, -(1 << 256), (1 << 512)-1):
        yield (f'const n = @abs({value}); const p:*const comptime_int=&n; '
               f'const check:u8=if(n=={abs(value)})0 else 256; '
               'pub fn main() void { _=check; _=p; }\n')
    for operand in ('true', 'null', 'undefined', '"abc"', '@as(u8,1), @as(u8,2)', ''):
        yield f'pub fn main() void {{ _ = @abs({operand}); }}\n'
    yield 'pub fn main() void { const n = @abs(@as(f32,undefined)); _=n; }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-integer-abs-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_integer_abs case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_integer_abs case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'integer absolute values: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
