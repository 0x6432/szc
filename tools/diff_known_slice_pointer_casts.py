#!/usr/bin/env python3
"""Compare known numeric slice-to-thin casts, including zero-address constants."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    for address in (0, 16, 17):
        for count in (0, 1):
            for destination in ('*i8', '*allowzero i8', '*u16', '*allowzero u16'):
                for expression in ('@ptrCast(s)', '@alignCast(@ptrCast(s))'):
                    yield ('pub fn main() void { const p: [*]allowzero u8 = @ptrFromInt('
                           f'{address}); const s: []allowzero u8 = p[0..{count}]; '
                           f'const q: {destination} = {expression}; _ = q; }}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-known-slice-casts-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_known_slice_pointer_casts case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                             source, f'diff_known_slice_pointer_casts case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'known slice pointer casts: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
