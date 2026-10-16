#!/usr/bin/env python3
"""Compare scalar integer-address conversion, qualifiers, alignment and constants."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def cases():
    for ty in ('*u8', '*u32', '*align(1) u32', '*align(256) u8',
               '*allowzero u8', '*allowzero u32', '[*]u16', '[*:17]const u16',
               '*const fn () void', '*const type', '*[2]u8', '*u0', '[]u8', 'u64'):
        for address in ('0', '1', '2', '16', '256', '-1', '18446744073709551616',
                        'undefined', '@as(usize,undefined)'):
            yield f'pub fn main() void {{ const p: {ty} = @ptrFromInt({address}); _ = p; }}\n'
        for operand in ('u8','u64','u65','i64','bool','usize'):
            yield (f'fn f(n: {operand}) void {{ const p: {ty} = @ptrFromInt(n); _ = p; }} '
                   'pub fn main() void { f(undefined); }\n')
    yield 'fn f(n: usize) usize { const p: *const type = @ptrFromInt(n); return @intFromPtr(p); } pub fn main() void { _ = f(16); }\n'
    yield 'const p: [*]u16 = @ptrFromInt(16); const s: [:17]u16 = p[0..2:17]; pub fn main() void { _ = s; }\n'
    for body in (
        'const p: [*]u8 = @ptrFromInt(16); const x: u8 = if(@intFromPtr(p-1)!=15) 256 else 0; _ = x;',
        'const p: [*]u8 = @ptrFromInt(18446744073709551615); const x: u8 = if(@intFromPtr(p+1)!=0) 256 else 0; _ = x;',
        'const p: [*]allowzero u8 = @ptrFromInt(0); _ = p-1;',
        'const p: [*]u16 = @ptrFromInt(16); const q: [*]u16 = @ptrFromInt(8); const x: u8 = if(p-q!=4) 256 else 0; _ = x;',
        'const p: [*]u16 = @ptrFromInt(16); const q: [*]u16 = @ptrFromInt(20); const x: u8 = if(p+2!=q) 256 else 0; _ = x;',
        'const p: *[3]u16 = @ptrFromInt(16); const x: u8 = if(@intFromPtr(&p[1])!=18) 256 else 0; _ = x;',
        'const p: *u8 = @ptrFromInt(@intCast(16)); _ = p;',
        'const p: *allowzero u8 = @ptrFromInt(0); _ = p.*;',
        'const p: *allowzero u8 = @ptrFromInt(0); p.* = 7;',
        'const p: [*]u16 = @ptrFromInt(16); const x: u8 = if(@intFromPtr(p+18446744073709551615)!=14) 256 else 0; _ = x;',
        'const p: *[3]u16 = @ptrFromInt(16); const x: u8 = if(@intFromPtr(p[1..2])!=18) 256 else 0; _ = x;',
        'const p: [*]u16 = @ptrFromInt(16); const s: []u16 = p[0..2]; const x: u16 = s.*; _ = x;',
        'const p: [*]u16 = @ptrFromInt(16); const s: []u16 = p[0..2]; const x: *[2]u16 = &s.*; _ = x;',

    ):
        yield 'pub fn main() void { '+body+' }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc'); parser.add_argument('zig'); args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-ptr-from-int-diff-') as directory:
        path = Path(directory)/'test.zig'
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc,'tir',str(path)], source, f'diff_ptr_from_int case {checked} szc', timeout=120)
            b = run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'], source, f'diff_ptr_from_int case {checked} zig', timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'pointer from integer: {checked} matched Zig 0.17.0', flush=True)


if __name__ == '__main__':
    main()
