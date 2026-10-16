#!/usr/bin/env python3
"""Compare fixed-extent @ptrCast slice inference against actual Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    sources = ('*u8', '*u16', '*u0', '*[0]u8', '*[3]u8', '*[2]u16',
               '*const [2:0]u8', '*align(1) [2]u16', '[*]u8')
    destinations = ('[]u8', '[]u16', '[]u0', '[]i8', '[]const u8',
                    '[:0]u8', '[]volatile u8', '[]allowzero u16')
    expressions = ('@ptrCast(p)', '@alignCast(@ptrCast(p))',
                   '@constCast(@ptrCast(p))',
                   '@volatileCast(@constCast(@alignCast(@ptrCast(p))))')
    for source in sources:
        for destination in destinations:
            for expression in expressions:
                yield (f'fn f(p: {source}) {destination} {{ return {expression}; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
    for address in (0, 1, 16, 17):
        for destination in ('[]allowzero u8', '[]u8', '[]u16', '[]allowzero u16'):
            yield ('pub fn main() void { const p: *allowzero [4]u8 = @ptrFromInt('
                   f'{address}); const q: {destination} = @alignCast(@ptrCast(p)); _ = q; }}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-fixed-slice-casts-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_fixed_slice_casts case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                             source, f'diff_fixed_slice_casts case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            exact = error and (error.startswith(('@ptrCast ', 'cannot infer length',
                               'null pointer casted', 'pointer address'))
                               or 'does not divide exactly' in error)
            if a.returncode != b.returncode or (exact and primary_error(a.stderr) != error):
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += bool(exact)
        print(f'fixed slice casts: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
