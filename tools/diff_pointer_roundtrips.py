#!/usr/bin/env python3
"""Compare provenance-proven exact pointer round trips."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    roots = (
        ('const n: u16 = 17;', '&n', '*const u16', 'q.* == 17'),
        ('const n: u33 = 8589934591;', '&n', '*const u33', 'q.* == n'),
        ('const n: i40 = -17;', '&n', '*const i40', 'q.* == n'),
        ('const a = [2]u16{17,29};', '&a', '*const [2]u16', 'q.*[1] == 29'),
        ('const a = [2]u16{17,29};', '&a[1]', '*const u16', 'q.* == 29'),
        ('', '"ab"', '*const [2:0]u8', 'q.*[2] == 0'),
        ('const s: []const u8 = "ab";', '&s', '*const []const u8', 'q.*.len == 2'),
    )
    erased_types = ('*const u8', '[*]const u8', '*const anyopaque', '*align(1) const u64')
    for declarations, operand, destination, predicate in roots:
        for erased in erased_types:
            for expression in ('@alignCast(@ptrCast(p))',
                               '@constCast(@alignCast(@ptrCast(p)))',
                               '@volatileCast(@alignCast(@ptrCast(p)))'):
                yield (declarations + f' const p: {erased} = @ptrCast({operand}); '
                       f'const q: {destination} = {expression}; '
                       f'const result: u8 = if ({predicate}) 0 else 256; '
                       'pub fn main() u8 { return result; }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-pointer-roundtrips-') as directory:
        path = Path(directory) / 'test.zig'
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_pointer_roundtrips case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                             source, f'diff_pointer_roundtrips case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            exact = error and error.startswith('@ptrCast ')
            if a.returncode != b.returncode or (exact and primary_error(a.stderr) != error):
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
        print(f'exact pointer round trips: {checked} matched Zig 0.17.0', flush=True)


if __name__ == '__main__':
    main()
