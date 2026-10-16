#!/usr/bin/env python3
"""Compare outer const/volatile casts, nested invariance and contextual coercions."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def cases():
    types = ('*u8','*const u8','*volatile u8','*const volatile u8',
             '*align(1) const u32','*allowzero const u8','[*:17]const u16',
             '[:17]const u16','[:17]volatile u16','*const *const u8','*const *u8',
             '[]const u8','[]volatile u8','*const [2]u8')
    for builtin in ('constCast','volatileCast'):
        for source in types:
            for destination in ('*u8','*const u8','*volatile u8','[]u8','[:17]u16'):
                yield (f'fn f(p: {source}) {destination} {{ return @{builtin}(p); }} '
                       'pub fn main() void { _ = f(undefined); }\n')
            yield (f'fn f(p: {source}) void {{ const q = @{builtin}(p); _ = q; }} '
                   'pub fn main() void { f(undefined); }\n')
            yield f'pub fn main() void {{ const p: {source} = undefined; _ = @{builtin}(p); }}\n'
        for operand in ('1','true','undefined','@as(u8,1)','[2]u8{1,2}'):
            yield f'pub fn main() void {{ _ = @{builtin}({operand}); }}\n'
    for body in (
        'const p: *const u8 = @ptrFromInt(16); const q = @constCast(p); const x: u8 = if(@intFromPtr(q)!=16) 256 else 0; _ = x;',
        'var a: u8 = 7; const p: *const volatile u8 = &a; const q: *u8 = @constCast(@volatileCast(p)); q.* = 9;',
        'const p: *const *const u8 = undefined; const q: **u8 = @constCast(p); _ = q;',
    ):
        yield 'pub fn main() void { '+body+' }\n'
    yield 'fn seven() u8 { return 7; } pub fn main() void { _ = @constCast(&seven)(); }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc'); parser.add_argument('zig'); args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-qualifier-casts-') as directory:
        path = Path(directory)/'test.zig'
        for checked,source in enumerate(cases(),1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)],source,f'diff_qualifier_casts case {checked} szc',timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],source,f'diff_qualifier_casts case {checked} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'qualifier casts: {checked} matched Zig 0.17.0',flush=True)


if __name__ == '__main__':
    main()
