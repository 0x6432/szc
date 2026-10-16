#!/usr/bin/env python3
"""Compare contextual alignment casts and nested qualifier permissions with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def cases():
    sources = ('*u8','*const u8','*volatile u8','*allowzero u8','*align(1) u16',
               '*align(32) u8','[*]u8','[*:17]u8','[]u8','[:17]u8','*[3]u8',
               '*[3:17]u8','*const [0]u8','*allowzero [0]u8','*const *const u8')
    destinations = ('*align(16) u8','*u8','*align(16) const u8','*align(16) volatile u8',
                    '*align(16) allowzero u8','[*]align(16) u8','[*:17]align(16) u8',
                    '[]align(16) u8','[:17]align(16) u8','*[3]u8','**u8')
    for source in sources:
        for destination in destinations:
            for expression in ('@alignCast(p)', '@constCast((@alignCast(@volatileCast(p))))'):
                yield (f'fn f(p: {source}) {destination} {{ return {expression}; }} '
                       'pub fn main() void { _ = f(undefined); }\n')
    for source in ('*u8','*allowzero u8','[]u8','[]allowzero u8'):
        for destination in ('*u8','*align(16) u8','*align(16) allowzero u8',
                            '[]u8','[]align(16) u8','[]align(16) allowzero u8'):
            yield (f'pub fn main() void {{ const p: {source} = undefined; '
                   f'const q: {destination} = @alignCast(p); _ = q; }}\n')
    for address in (0,1,16,17,32,256):
        for alignment in (1,2,16,256):
            for allowzero in ('','allowzero '):
                yield (f'pub fn main() void {{ const p: *allowzero u8 = @ptrFromInt({address}); '
                       f'const q: *align({alignment}) {allowzero}u8 = @alignCast(p); _ = q; }}\n')
    for expression in ('@alignCast(@alignCast(p))', '@alignCast((@constCast(@alignCast(p))))',
                       '@constCast(@alignCast(@constCast(p)))', '@alignCast(p)'):
        yield (f'fn f(p: *u8) void {{ _ = {expression}; }} '
               'pub fn main() void { f(undefined); }\n')
    for source in ('u8','bool','[2]u8'):
        yield (f'fn f(p: {source}) *u8 {{ return @alignCast(p); }} '
               'pub fn main() void { _ = f(undefined); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc'); parser.add_argument('zig'); args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-alignment-casts-') as directory:
        path = Path(directory)/'test.zig'
        for checked,source in enumerate(cases(),1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)],source,f'diff_alignment_casts case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],source,f'diff_alignment_casts case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'alignment casts: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
