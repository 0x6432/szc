#!/usr/bin/env python3
"""Compare resizing @ptrCast slices against actual Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    types = ('[]u8', '[]u16', '[]u0', '[][3]u8', '[][2]u8', '[]const u8', '[]allowzero u8')
    for source in types:
        for destination in types:
            for expression in ('@ptrCast(p)', '@alignCast(@ptrCast(p))',
                               '@constCast(@alignCast(@ptrCast(p)))'):
                yield (f'fn f(p: {source}) {destination} {{ return {expression}; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
    for source in ('u8', 'u16', '[3]u8'):
        for count in (0, 1, 2, 3):
            for destination in ('u8', 'u16', '[3]u8'):
                yield ('pub fn main() void { const p: [*]' + source + ' = @ptrFromInt(16); '
                       f'const s: []{source} = p[0..{count}]; '
                       f'const q: []{destination} = @alignCast(@ptrCast(s)); _ = q; }}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-resized-slice-casts-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_resized_slice_casts case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                             source, f'diff_resized_slice_casts case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            exact = error and (error.startswith(('@ptrCast ', 'cannot infer length',
                               'null pointer casted', 'pointer address'))
                               or 'does not divide exactly' in error)
            if a.returncode != b.returncode or (exact and primary_error(a.stderr) != error):
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += bool(exact)
        print(f'resized slice casts: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
