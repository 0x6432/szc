#!/usr/bin/env python3
"""Compare scalar memset typing, zero ranges and diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    for ty, values in (('u0', ('0', 'undefined')), ('u8', ('0', '255', '256', '-1', 'undefined')),
                       ('i8', ('-128', '127', '128', 'undefined')),
                       ('u16', ('1234', '65536')), ('u33', ('8589934591', '8589934592')),
                       ('i40', ('-549755813888', '549755813888')), ('bool', ('true', 'false', '1'))):
        for value in values:
            for count in (0, 1, 3):
                yield (f'fn f(p:[] {ty}) void {{ @memset(p,{value}); }} '
                       f'pub fn main() void {{ var a:[{count}]{ty}=undefined; f(&a); }}\n')
                yield (f'pub fn main() void {{ var a:[{count}]{ty}=undefined; @memset(&a,{value}); }}\n')
    for ty, value in (('u8', '1'), ('u0', 'undefined'), ('void', '{}')):
        for count in (0, 1):
            for const in ('', 'const '):
                yield (f'pub fn main() void {{ @memset(@as(*{const}[{count}]{ty},undefined),{value}); }}\n')
                yield (f'pub fn main() void {{ @memset(@as([]{const}{ty},undefined),{value}); }}\n')
        for shape in ('[*]',):
            yield (f'fn f(p:{shape}{ty}) void {{ @memset(p,{value}); }} '
                   f'pub fn main() void {{ var a:[1]{ty}=undefined; f(&a); }}\n')
    for operand in ('1', 'true', 'undefined', 'null', '"abc"', '@as(*u8,undefined)'):
        yield f'pub fn main() void {{ @memset({operand},1); }}\n'
    for args in ('', 'undefined', 'undefined,undefined,undefined'):
        yield f'pub fn main() void {{ @memset({args}); }}\n'
    yield ('fn f(p:[]*u8,n:*u8) void { @memset(p,n); } '
           'pub fn main() void { var n:u8=0; var p:[2]*u8=.{&n,&n}; f(&p,&n); }\n')
    yield ('fn f(p:[]volatile u8) void { @memset(p,undefined); } '
           'pub fn main() void { var a:[1]u8=.{0}; f(&a); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-scalar-memset-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_scalar_memset case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_scalar_memset case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'scalar memset: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
