#!/usr/bin/env python3
"""Compare bounded runtime aggregate bit-cast acceptance against official Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def cases():
    types = ('u0', '[2]u0', '[0]u8', 'u3', '[3]bool', 'u10', '[2]i5',
             '[5]u2', 'u20', '[2][2]u5', 'u24', '[2:17]u8', 'u64', 'i64',
             '[2]i32', '[8]u8')
    for source in types:
        for destination in types:
            yield (f'fn f(n:{source}) {destination} {{ return @bitCast(n); }} '
                   'pub fn main() void { _=f(undefined); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-runtime-aggregate-bitcasts-') as directory:
        path = Path(directory) / 'test.zig'
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            actual = run_compiler([args.szc, 'tir', str(path)], source,
                                  f'diff_runtime_aggregate_bitcasts case {checked} szc', timeout=120)
            reference = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                                     source, f'diff_runtime_aggregate_bitcasts case {checked} zig', timeout=120)
            if actual.returncode != reference.returncode:
                raise SystemExit('disagreement:\n' + source + actual.stderr.decode(errors='replace')
                                 + reference.stderr.decode(errors='replace'))
        print(f'runtime aggregate bit casts: {checked} matched Zig 0.17.0', flush=True)


if __name__ == '__main__':
    main()
