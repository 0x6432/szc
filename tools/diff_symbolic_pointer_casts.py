#!/usr/bin/env python3
"""Compare symbolic pointer address casts without claiming immutable load support."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    roots = (
        ('const n: u32 = 0x12345678;', '&n'),
        ('var n: u32 = 0x12345678;', '&n'),
        ('const a = [2]u16{0x1122,0x3344};', '&a[1]'),
        ('', '"ab"'),
        ('fn target() void {}', '&target'),
        ('const s: []const u8 = "ab";', '&s'),
        ('const n: u16 = 17; const p: *const u16 = &n;', '&p'),
    )
    types = ('*const u8', '*u8', '*align(1) const i16', '*const u64',
             '*align(1) const u64', '[*]const u8', '[*:0]const u8',
             '*const fn() void', '*const [8]u8')
    for declarations, operand in roots:
        for destination in types:
            for expression in ('@ptrCast(p)', '@alignCast(@ptrCast(p))',
                               '@constCast(@ptrCast(p))',
                               '@volatileCast(@constCast(@alignCast(@ptrCast(p))))'):
                yield (declarations + ' pub fn main() void { const p = ' + operand
                       + f'; const q: {destination} = {expression}; _ = q; }}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-symbolic-casts-') as directory:
        path = Path(directory) / 'test.zig'
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_symbolic_pointer_casts case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                             source, f'diff_symbolic_pointer_casts case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            exact = error and error.startswith('@ptrCast ')
            if a.returncode != b.returncode or (exact and primary_error(a.stderr) != error):
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
        print(f'symbolic pointer casts: {checked} matched Zig 0.17.0', flush=True)


if __name__ == '__main__':
    main()
