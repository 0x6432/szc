#!/usr/bin/env python3
"""Compare typed integer bit counts and exact primary diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    for width in (0, 1, 2, 5, 8, 16, 29, 32, 33, 40, 63, 64, 65, 128, 257, 65535):
        for signed in (False, True):
            if signed and width == 0:
                continue
            ty = ('i' if signed else 'u') + str(width)
            values = (0, -1) if signed else (0, 1) if width else (0,)
            if width and width < 1000:
                values += (-(1 << (width-1)), (1 << (width-1))-1) if signed else ((1 << width)-1, 1 << (width-1))
            for value in dict.fromkeys(values):
                raw = value % (1 << width)
                counts = (width - raw.bit_length(), width if raw == 0 else (raw & -raw).bit_length()-1, raw.bit_count())
                for builtin, expected in zip(('clz', 'ctz', 'popCount'), counts):
                    yield (f'const n = @{builtin}(@as({ty},{value})); '
                           f'const check:u8=if(n=={expected})0 else 256; '
                           'pub fn main() void { _=check; }\n')
            for builtin in ('clz', 'ctz', 'popCount'):
                # Undefined must stay undefined, except Zig's canonical zero-bit value.
                yield (f'const n = @{builtin}(@as({ty},undefined)); '
                       'const check:u8=if(n==0)0 else 256; pub fn main() void { _=check; }\n')
    for width in (0, 1, 2, 5, 8, 16, 29, 32, 33, 40, 63, 64):
        for builtin in ('clz', 'ctz', 'popCount'):
            result_bits = width.bit_length()
            yield (f'fn f(n:u{width}) void {{ const count = @{builtin}(n); '
                   f'const p:*const u{result_bits}=&count; _=p; }} '
                   'pub fn main() void { f(0); }\n')
    for builtin in ('clz', 'ctz', 'popCount'):
        for operand in ('1', 'true', '@as(f32,undefined)', 'null', 'undefined', '"abc"', '@as(u8,1), @as(u8,2)', ''):
            yield f'pub fn main() void {{ _ = @{builtin}({operand}); }}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-bit-counts-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_bit_counts case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_bit_counts case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'bit counts: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
