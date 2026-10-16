#!/usr/bin/env python3
"""Compare scalar memcpy types, inferred lengths and diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error


def cases():
    for ty, value in (('u0', '0'), ('u8', '7'), ('i8', '-7'), ('u16', '1234'),
                      ('u33', '8589934591'), ('i40', '-123456789'), ('bool', 'true'), ('void', '{}')):
        for count in (0, 1, 3):
            for dest, source in (('*[N]T', '*const [N]T'), ('[]T', '[]const T'),
                                 ('[*]T', '[]const T'), ('[]T', '[*]const T'),
                                 ('*[N]T', '[*]const T'), ('[*]T', '*const [N]T')):
                dest, source = (s.replace('N', str(count)).replace('T', ty) for s in (dest, source))
                init = ','.join([value]*count)
                yield (f'fn f(d:{dest},s:{source}) void {{ @memcpy(d,s); }} '
                       f'pub fn main() void {{ var a:[{count}]{ty}=undefined; '
                       f'const b:[{count}]{ty}=.{{{init}}}; f(&a,&b); }}\n')
        for d, s in ((0, 0), (0, 1), (1, 0), (1, 1), (1, 3)):
            for const in ('', 'const '):
                yield (f'pub fn main() void {{ @memcpy(@as(*{const}[{d}]{ty},undefined),'
                       f'@as(*const [{s}]{ty},undefined)); }}\n')
        for dest, source in ((f'[]{ty}', f'[]const {ty}'), (f'[*]{ty}', f'[*]const {ty}'),
                             (f'*[0]{ty}', f'[*]const {ty}'), (f'[*]{ty}', f'*const [0]{ty}')):
            yield f'pub fn main() void {{ @memcpy(@as({dest},undefined),@as({source},undefined)); }}\n'
    for dest, source in (('u8','i8'), ('u8','u16'), ('u0','void'), ('bool','u1'),
                         ('*u8','*const u8'), ('*const u8','*u8'),
                         ('*volatile u8','*u8'), ('*u8','*volatile u8'),
                         ('*allowzero u8','*u8'), ('*u8','*allowzero u8'),
                         ('[]const u8','[]u8'), ('[]u8','[]const u8')):
        for count in ((0,) if dest == '[]const u8' else (0, 1)):
            yield (f'pub fn main() void {{ var a:[{count}]{dest}=undefined; '
                   f'const b:[{count}]{source}=undefined; @memcpy(&a,&b); }}\n')
    for dest in ('[]u8', '[]volatile u8'):
        for source in ('[]const u8', '[]const volatile u8'):
            yield (f'fn f(d:{dest},s:{source}) void {{ @memcpy(d,s); }} '
                   'pub fn main() void { var a:[2]u8=undefined; const b:[2]u8=.{7,9}; f(&a,&b); }\n')
    for args in ('', 'undefined', 'undefined,undefined,undefined', '1,1', 'undefined,undefined',
                 'true,"abc"', '"abc","abc"', '@as(*u8,undefined),"abc"',
                 '@as(*[1]u8,undefined),true'):
        yield f'pub fn main() void {{ @memcpy({args}); }}\n'
    for lo, hi, n in ((0,0,0),(0,0,1),(0,1,1),(0,1,2),(1,0,2),(0,2,2),(2,0,2)):
        yield (f'var a:[4]u16=.{{1,2,3,4}}; pub fn main() void {{ '
               f'@memcpy(a[{lo}..][0..{n}],a[{hi}..][0..{n}]); }}\n')
    for d, s, n in ((16,16,0),(16,16,1),(16,17,1),(16,17,2),(17,16,2),(16,18,2)):
        yield (f'pub fn main() void {{ const d:[*]u8=@ptrFromInt({d}); '
               f'const s:[*]const u8=@ptrFromInt({s}); @memcpy(d[0..{n}],s[0..{n}]); }}\n')
    for d, s in ((0,0),(0,1),(1,0),(1,1),(1,2)):
        yield (f'pub fn main() void {{ var a:[{d}]u8=undefined; '
               f'const b:[{s}]u8=undefined; @memcpy(&a,&b); }}\n')
    yield 'pub fn main() void { var a:[3]u8=undefined; @memcpy(&a,"abc"); }\n'
    yield ('pub fn main() void { var a:[2:17]u16=.{1,2}; '
           'const b:[2:19]u16=.{7,9}; @memcpy(&a,&b); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-scalar-memcpy-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, 'tir', str(path)], source,
                             f'diff_scalar_memcpy case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_scalar_memcpy case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'scalar memcpy: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
