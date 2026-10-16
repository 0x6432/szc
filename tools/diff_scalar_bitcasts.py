#!/usr/bin/env python3
"""Compare scalar bit-cast acceptance and constant representations with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def cases():
    types = ('u0', 'u1', 'i1', 'bool', 'u5', 'i5', 'u8', 'i8', 'u33', 'i33',
             'u64', 'i64', 'u128', 'i128', 'void')
    for source in types:
        for destination in types:
            yield (f'fn f(n: {source}) {destination} {{ return @bitCast(n); }} '
                   'pub fn main() void { _ = f(undefined); }\n')
    for width in (1, 2, 5, 8, 16, 29, 32, 33, 40, 63, 64, 65, 128, 257):
        modulus = 1 << width
        for source_signed in (False, True):
            source = ('i' if source_signed else 'u') + str(width)
            values = (0, -1, -(modulus//2), modulus//2-1) if source_signed else (0, 1, modulus//2, modulus-1)
            for destination_signed in (False, True):
                destination = ('i' if destination_signed else 'u') + str(width)
                for value in values:
                    expected = value % modulus
                    if destination_signed and expected >= modulus//2:
                        expected -= modulus
                    yield (f'pub fn main() void {{ const x: {source} = {value}; '
                           f'const y: {destination} = @bitCast(x); '
                           f'const check: u8 = if(y == {expected}) 0 else 256; _ = check; }}\n')
    for source, value in (('bool','true'), ('bool','false'), ('i1','-1'), ('i1','0'), ('u1','0'), ('u1','1')):
        for destination in ('bool','u1','i1'):
            bit = value in ('true','-1','1')
            expected = ('true' if bit else 'false') if destination == 'bool' else str(-int(bit) if destination == 'i1' else int(bit))
            yield (f'pub fn main() void {{ const x: {source} = {value}; const y: {destination} = @bitCast(x); '
                   f'const check: u8 = if(y == {expected}) 0 else 256; _ = check; }}\n')
    for source in ('u8','i8','bool','void','u0'):
        for destination in ('u8','i8','bool','void','u0'):
            yield (f'pub fn main() void {{ const x: {source} = undefined; '
                   f'const y: {destination} = @bitCast(x); _ = y; }}\n')
    for body in (
        'const n: u8 = @bitCast(1); _ = n;',
        'const n: comptime_int = @bitCast(@as(u8, 1)); _ = n;',
        'const n: usize = @bitCast(@as(*u8, @ptrFromInt(16))); _ = n;',
        'const n: *u8 = @bitCast(@as(usize, 16)); _ = n;',
        '_ = @bitCast(@as(u8, 1));',
        'const n: u8 = @bitCast(@as(u8, 1), @as(u8, 2)); _ = n;',
        'const n: u8 = @bitCast(undefined); _ = n;',
    ):
        yield 'pub fn main() void { '+body+' }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc'); parser.add_argument('zig'); args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-scalar-bitcasts-') as directory:
        path = Path(directory)/'test.zig'
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)], source, f'diff_scalar_bitcasts case {checked} szc', timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'], source, f'diff_scalar_bitcasts case {checked} zig', timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'scalar bit casts: {checked} matched Zig 0.17.0', flush=True)


if __name__ == '__main__':
    main()
