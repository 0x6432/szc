#!/usr/bin/env python3
"""Compare thin @ptrCast acceptance and primary diagnostics against actual Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def cases():
    types = ('*u8', '*i8', '*u16', '*align(1) u16', '*const u8',
             '*volatile u8', '*allowzero u8', '[*]u8', '[*:0]u8',
             '*[2]u8', '*const fn() void')
    expressions = ('@ptrCast(p)', '@alignCast(@ptrCast(p))',
                   '@constCast(@ptrCast(p))',
                   '@volatileCast(@constCast(@alignCast(@ptrCast(p))))')
    for source in types:
        for destination in types:
            for expression in expressions:
                yield (f'fn f(p: {source}) {destination} {{ return {expression}; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
    for address in (0, 1, 16, 17, 256):
        for destination in ('*i8', '*allowzero i8', '[*]u8', '[*:17]u8',
                            '*align(16) u16', '*align(16) allowzero u16'):
            for expression in ('@ptrCast(p)', '@alignCast(@ptrCast(p))'):
                yield ('pub fn main() void { const p: *allowzero u8 = @ptrFromInt('
                       f'{address}); const q: {destination} = {expression}; _ = q; }}\n')
    for source in ('*u8', '*allowzero u8', '[*]u8', '*const fn() void'):
        for destination in ('*i8', '*allowzero i8', '[*:0]u8'):
            yield (f'pub fn main() void {{ const p: {source} = undefined; '
                   f'const q: {destination} = @ptrCast(p); _ = q; }}\n')
    for expression in ('@ptrCast(@ptrCast(p))', '@ptrCast((@alignCast(@ptrCast(p))))',
                       '@constCast(@ptrCast(@constCast(p)))', '@ptrCast(p, p)',
                       '@ptrCast()', '@ptrCast(p)'):
        yield (f'fn f(p: *const u8) void {{ _ = {expression}; }} '
               'pub fn main() void { f(undefined); }\n')
    for source in ('u8', 'bool', '[2]u8'):
        yield (f'fn f(p: {source}) *u8 {{ return @ptrCast(p); }} '
               'pub fn main() void { _ = f(undefined); }\n')
    for expression in ('@alignCast(@ptrCast(p))', '@constCast(@ptrCast(p))',
                       '@volatileCast(@alignCast(@ptrCast(p)))'):
        yield (f'fn f(p: *const volatile u8) *u16 {{ return {expression}; }} '
               'pub fn main() void { _ = f(undefined); }\n')


def primary_error(stderr):
    for line in stderr.decode(errors='replace').splitlines():
        if ': error: ' in line:
            return line.split(': error: ', 1)[1]
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-thin-casts-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_thin_pointer_casts case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                             source, f'diff_thin_pointer_casts case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            # These diagnostics are fully implemented; compare exact operation names,
            # especially when @ptrCast is nested inside another cast.
            exact = error and (error.startswith(('@ptrCast ', 'redundant @'))
                               or 'pointer address 0x' in error
                               or error.startswith('null pointer casted to type'))
            if a.returncode != b.returncode or (exact and primary_error(a.stderr) != error):
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += bool(exact)
        print(f'thin pointer casts: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
