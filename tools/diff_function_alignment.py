#!/usr/bin/env python3
"""Compare aligned function addresses, aliases and pointer joins with Zig 0.17."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def cases():
    for storage, destination, alias in itertools.product((0, 1, 16, 64, 256), (0, 1, 16, 64, 256), (False, True)):
        modifier = f' align({storage})' if storage else ''
        qualifier = f'align({destination}) ' if destination else ''
        name = 'alias' if alias else 'target'
        yield (f'fn target(){modifier} u8 {{ return 7; }}\nconst alias = target;\n'
               f'pub fn main() void {{ const p: *{qualifier}const fn () u8 = &{name}; _ = p; }}\n')
    for left, right, destination in itertools.product((1, 16, 64), repeat=3):
        yield (f'fn first() align({left}) u8 {{ return 1; }}\nfn second() align({right}) u8 {{ return 2; }}\n'
               f'fn choose(flag: bool) *align({destination}) const fn () u8 {{ return if (flag) &first else &second; }}\n'
               'pub fn main() void { _ = choose(true); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix='szc-function-align-diff-') as directory:
        path = Path(directory) / 'main.zig'
        for source in cases():
            path.write_text(source)
            actual = run_compiler([args.szc, 'tir', str(path)], source,
                f"diff_function_alignment case {checked + 1} szc", timeout=120)
            expected = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                f"diff_function_alignment case {checked + 1} zig", timeout=120)
            if actual.returncode != expected.returncode:
                raise SystemExit(source + actual.stderr.decode(errors='replace') + expected.stderr.decode(errors='replace'))
            checked += 1
    print(f'function alignment: {checked} matched Zig 0.17.0', flush=True)


if __name__ == '__main__':
    main()
