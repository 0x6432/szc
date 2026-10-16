#!/usr/bin/env python3
"""Compare integer bit/byte permutations and exact primary diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    for width in (0, 1, 2, 5, 8, 16, 24, 29, 32, 33, 40, 48, 56, 63, 64, 65, 128, 257, 512, 513):
        for signed in (False, True):
            if signed and width == 0:
                continue
            ty = ('i' if signed else 'u') + str(width)
            values = (0, -1, -(1 << (width-1)), (1 << (width-1))-1) if signed else (0, 1, (1 << width)-1) if width else (0,)
            for builtin in ('bitReverse', 'byteSwap'):
                if builtin == 'byteSwap' and width % 8:
                    # Width errors must take precedence over undefined handling too.
                    yield (f'pub fn main() void {{ _ = @{builtin}(@as({ty},undefined)); }}\n')
                    continue
                for value in dict.fromkeys(values):
                    raw = value % (1 << width)
                    if builtin == 'byteSwap':
                        expected = int.from_bytes(raw.to_bytes(width//8, 'little'), 'big')
                    else:
                        expected = int(f'{raw:0{width}b}'[::-1], 2) if width else 0
                    if signed and expected >= 1 << (width-1):
                        expected -= 1 << width
                    yield (f'const n = @{builtin}(@as({ty},{value})); '
                           f'const p:*const {ty}=&n; const check:u8=if(n=={expected})0 else 256; '
                           'pub fn main() void { _=check; _=p; }\n')
                yield (f'const n = @{builtin}(@as({ty},undefined)); '
                       'const check:u8=if(n==0)0 else 256; pub fn main() void { _=check; }\n')
    for builtin in ('bitReverse', 'byteSwap'):
        for operand in ('1', 'true', '@as(f32,undefined)', 'null', 'undefined', '"abc"', '@as(u8,1), @as(u8,2)', ''):
            yield f'pub fn main() void {{ _ = @{builtin}({operand}); }}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-bit-transforms-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_bit_transforms case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_bit_transforms case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'bit transforms: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
