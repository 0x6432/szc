#!/usr/bin/env python3
"""Compare equal-stride @ptrCast slices against actual Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    families = (
        ('[]u8', '[]i8', '[]const u8', '[]volatile u8', '[]allowzero u8', '[:0]u8'),
        ('[]u16', '[]i16', '[]align(1) u16', '[]align(1) allowzero u16'),
        ('[]u0',),
    )
    expressions = ('@ptrCast(p)', '@alignCast(@ptrCast(p))',
                   '@constCast(@ptrCast(p))',
                   '@volatileCast(@constCast(@alignCast(@ptrCast(p))))')
    for types in families:
        for source in types:
            for destination in types:
                for expression in expressions:
                    yield (f'fn f(p: {source}) {destination} {{ return {expression}; }} '
                           'pub fn main() void { _ = f(undefined); }\n')
    for address in (0, 16, 17):
        for count in (0, 1):
            for destination in ('[]align(1) allowzero i16', '[]i16'):
                yield ('pub fn main() void { const p: [*]align(1) allowzero u16 = @ptrFromInt('
                       f'{address}); const s: []align(1) allowzero u16 = p[0..{count}]; '
                       f'const q: {destination} = @alignCast(@ptrCast(s)); _ = q; }}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-equal-slice-casts-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_equal_slice_casts case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                             source, f'diff_equal_slice_casts case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            exact = error and (error.startswith(('@ptrCast ', 'cannot infer length',
                               'null pointer casted', 'pointer address'))
                               or 'does not divide exactly' in error)
            if a.returncode != b.returncode or (exact and primary_error(a.stderr) != error):
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += bool(exact)
        print(f'equal-stride slice casts: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
