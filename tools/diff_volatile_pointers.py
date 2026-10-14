#!/usr/bin/env python3
"""Compare volatile qualification, nested invariance, joins and function variance."""
import argparse
import itertools
from pathlib import Path
import subprocess
import tempfile


def pointer(child='u8', const=False, volatile=False, allowzero=False, alignment=0):
    return ('*' + (f'align({alignment}) ' if alignment else '') + ('allowzero ' if allowzero else '')
            + ('const ' if const else '') + ('volatile ' if volatile else '') + child)


def conversion(source, destination):
    return (f'fn convert(p: {source}) {destination} {{ return p; }}\n'
            'pub fn main() void { _ = convert(undefined); }\n')


def cases():
    flags = tuple(itertools.product((False, True), repeat=3))
    for source, destination in itertools.product(flags, repeat=2):
        yield conversion(pointer(const=source[0], volatile=source[1], allowzero=source[2]),
                         pointer(const=destination[0], volatile=destination[1], allowzero=destination[2]))
    for sc, sv, dc, dv, sa, da in itertools.product((False, True), (False, True), (False, True), (False, True), (1, 16), (1, 16)):
        yield conversion(pointer(const=sc, volatile=sv, alignment=sa), pointer(const=dc, volatile=dv, alignment=da))
    for outer_const, sc, sv, dc, dv in itertools.product((False, True), repeat=5):
        yield conversion(pointer(pointer(const=sc, volatile=sv)),
                         pointer(pointer(const=dc, volatile=dv), const=outer_const))
    for lv, lc, rv, rc, dv, dc in itertools.product((False, True), repeat=6):
        left, right, dest = pointer(const=lc, volatile=lv), pointer(const=rc, volatile=rv), pointer(const=dc, volatile=dv)
        yield (f'fn choose(flag: bool, a: {left}, b: {right}) {dest} {{ return if (flag) a else b; }}\n'
               'pub fn main() void { _ = choose(true, undefined, undefined); }\n')
    for sc, sv, dc, dv in itertools.product((False, True), repeat=4):
        source, destination = pointer(const=sc, volatile=sv), pointer(const=dc, volatile=dv)
        yield (f'fn target(p: {source}) void {{ _ = p; }}\n'
               f'fn take(p: *const fn ({destination}) void) void {{ _ = p; }}\n'
               'pub fn main() void { take(target); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix='szc-volatile-pointer-diff-') as directory:
        path = Path(directory) / 'main.zig'
        for source in cases():
            path.write_text(source)
            actual = subprocess.run([args.szc, 'tir', str(path)], capture_output=True, timeout=120)
            expected = subprocess.run([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], capture_output=True, timeout=120)
            if actual.returncode not in (0, 1) or expected.returncode not in (0, 1) or actual.returncode != expected.returncode:
                raise SystemExit(source + actual.stderr.decode(errors='replace') + expected.stderr.decode(errors='replace'))
            checked += 1
    print(f'volatile pointers: {checked} matched Zig 0.17.0', flush=True)


if __name__ == '__main__':
    main()
