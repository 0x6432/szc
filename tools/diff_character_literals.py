#!/usr/bin/env python3
"""Compare character code points, escape boundaries and decoding diagnostics."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    # Force the expected code point at comptime; acceptance alone is not enough.
    def check(literal, expected):
        return (f'const n = {literal}; const check: u8 = if (n == {expected}) 0 else 256; '
                'pub fn main() void { _ = check; }\n')
    for code in range(32, 127):
        if code not in (39, 92):
            yield check("'" + chr(code) + "'", code)
    for code in (0, 1, 9, 10, 13, 31, 39, 92, 127, 128, 254, 255):
        yield check("'\\x%02x'" % code, code)
    for code in (0, 127, 128, 255, 256, 2047, 2048, 55295, 55296,
                 57343, 57344, 65535, 65536, 128512, 1114111):
        yield check("'\\u{%x}'" % code, code)
        if code >= 128 and not 55296 <= code <= 57343:
            yield check("'" + chr(code) + "'", code)
    for literal in (r"'\n'", r"'\r'", r"'\t'", r"'\\'", r"'\''", r"'\"'"):
        # Explicit escape values are also covered by the native fixture.
        yield 'pub fn main() void { const n = ' + literal + '; _ = n; }\n'
    for literal in (r"'\u{110000}'", r"'\u{ffffff}'", r"'\u{}'", r"'\u{g}'",
                    r"'\xgg'", r"'\x0'", r"'\x000'", r"'\q'", r"'\u3041'",
                    "''", "'ab'", r"'\u{ffff}x'"):
        yield 'pub fn main() void { const n = ' + literal + '; _ = n; }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-character-literals-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_character_literals case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'],
                             source, f'diff_character_literals case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'character literals: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
